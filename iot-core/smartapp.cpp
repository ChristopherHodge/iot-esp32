#include "iot-common.h"
#include "smartapp.h"

static const char *TAG = "smartapp";

static QueueHandle_t xSTQueue;

uint8_t st_init_device(char *device_id) {
	uint8_t ret = false;
    http_client_t m_http_client;
	http_client_t *http_client = http_client_init(&m_http_client);

    char endpoint[50];
	sprintf(endpoint, "/devices/%s", device_id);
	
	http_response_t resp = st_api_request(http_client, HTTP_METHOD_PUT, endpoint);
	if(resp.status >= 200 && resp.status < 300) {
		char resp_body[resp.length];
		http_client_read(http_client, resp_body, sizeof(resp_body));
	    cJSON *root = cJSON_Parse(resp_body);
		cJSON *id = cJSON_GetObjectItem(root, "deviceId");
		ret = (id != NULL);
		cJSON_Delete(root);
	} else {
		LOGE("st_init_device(): http response code: %d", resp.status);
	}
	http_client_close(http_client);
	return ret;
}

uint8_t st_create_device(char *device_id, sensor_type_t sensor_type) {
	uint8_t ret = false;
    http_client_t m_http_client;
	http_client_t *http_client = http_client_init(&m_http_client);

	const sensor_t *sensor = sensor_get_by_type(sensor_type);
	attribute_t type_str;
	memcpy(type_str, sensor->type, sizeof(attribute_t));
	enum_to_str((char*)type_str);

    cJSON *new_device = cJSON_CreateObject();
    cJSON *id = cJSON_CreateString(device_id);
    cJSON *type = cJSON_CreateString(type_str);
	cJSON_AddItemToObject(new_device, "device_id", id);
	cJSON_AddItemToObject(new_device, "sensor_type", type);
    char *body = cJSON_PrintUnformatted(new_device);
    cJSON_Delete(new_device);

	LOGI("stapi(POST): %s", body);
    http_client_set_post_data(http_client, body, strlen(body));
	http_response_t resp = st_api_request(http_client, HTTP_METHOD_POST, "/devices");
	if(resp.status >= 200 && resp.status < 300) {
		char resp_body[resp.length];
		http_client_read(http_client, resp_body, sizeof(resp_body));
	    cJSON *root = cJSON_Parse(resp_body);
		cJSON *id = cJSON_GetObjectItem(root, "deviceId");
		ret = (id != NULL);
		cJSON_Delete(root);
	}
	http_client_close(http_client);
	return ret;
}

static req_status_t st_update_device(device_t *device) {
	char endpoint[50];
	sprintf(endpoint, "/config/%s", device->id);

	LOGI("getting device configuration from ST API");
	LOGD("GET %s", endpoint);

	http_client_t m_http_client;
	http_client_t *http_client = http_client_init(&m_http_client);

	http_response_t resp = st_api_request(http_client, HTTP_METHOD_GET, endpoint);
    if(resp.status != 200) {
	  LOGE("st_api_request: invalid response code: %d", resp.status);
	  http_client_close(http_client);
      return (req_status_t)resp.status;
	}
	int len;
	if(resp.length == -1) {
        len = HTTP_CLIENT_BUF_LEN;	
	} else {
		len = resp.length;
	}
    char body[len];
	http_client_read(http_client, body, len);
	http_client_close(http_client);

	LOGD("updating device config");
    cJSON *root = cJSON_Parse(body);
	cJSON *id;
	uint8_t idx = 0;
    cJSON_ArrayForEach(id, root) {
		attribute_t type_name;
		cJSON *t = cJSON_GetObjectItem(id, "type");
		if(t == nullptr) {
			continue;
		}
		strncpy((char*)type_name, cJSON_GetStringValue(t), sizeof(attribute_t)-1);
		const sensor_t *sensor = sensor_get_by_type_name(type_name);
		if(sensor != nullptr) {
			LOGI("update: sensor_id %d / type %s", idx, type_name);
			device->sensors[idx] = *sensor;
			device->sensors[idx++].in_use = true;
		}
	}
	cJSON_Delete(root);
	return (req_status_t)resp.status;
}

req_status_t st_configure_device(device_t *device) {
	LOGD("getting config for %s", device->id);
    req_status_t status = st_update_device(device);
    if(status == HttpStatus_Ok) {
	    device->last_seen = MILLIS;
    }
    return status;
}

static void st_queue_task(void *ptx) {
    xSTQueue = xQueueCreate(6, ST_PAYLOAD_SZ);
	st_payload_t payload;
	char url[100];
	char apikey[50];

	strcpy(apikey, "Bearer ");
    strcat(apikey, ST_CONFIG->apikey);

	char http_resp_buf[ST_HTTP_BUF_SZ];
	uint8_t err_cnt = 0;
	esp_err_t err;

	esp_http_client_config_t config = ST_CLIENT_CONFIG(http_resp_buf);
	esp_http_client_handle_t client = nullptr;
	uint8_t is_retry = false;

    for(;;) {
        STACK_STATS
		strncpy(url, ST_CONFIG->apiurl, sizeof(url));
		strcat(url, ST_DEVICE_ENDPOINT);
		memset(&payload, 0, ST_PAYLOAD_SZ);
		if(xQueueReceive(xSTQueue, (void*)&payload, ST_QUEUE_TIMEOUT) != pdTRUE) {
			if(client != nullptr) {
				esp_http_client_close(client);
			}
			continue;
		}

		if(client == nullptr) {
 			client = esp_http_client_init(&config);
    		esp_http_client_set_method(client, HTTP_METHOD_POST);
			esp_http_client_set_header(client, "Authorization", apikey);
    		esp_http_client_set_header(client, "Content-Type", "application/json");
		}
		strncat(url, payload.endpoint, DEVICE_ID_SZ+2);
		esp_http_client_set_url(client, url);
    	esp_http_client_set_post_field(client, payload.body, strlen(payload.body));
	    err = esp_http_client_perform(client);
    	if (err == ESP_OK) {
            esp_http_client_get_status_code(client);
            esp_http_client_get_content_length(client);
			is_retry = false;
		} else {
			LOGW("failed to write st data: resp_code: 0x%04x", err);
			esp_http_client_cleanup(client);
			client = nullptr;
			err_cnt++;
			if(is_retry) {
				is_retry = false;
				LOGE("max retries reached: giving up on this message");
			} else {
				is_retry = true;
				LOGI("will try again once more..");
				xQueueSendToFront(xSTQueue, (void*)&payload, DELAY_S4);
				vTaskDelay(DELAY_S5);
			}
		}
		wifi_err_check(err_cnt);
		err_cnt = 0;
	}
}

uint8_t st_queue_payload(device_data_t *payload) {
	st_payload_t st_payload;
	char buf[30];

    snprintf(buf, sizeof(buf), "%s-%d", payload->device_id, payload->data.sensor_id);
	memcpy(st_payload.endpoint, buf, sizeof(st_payload.endpoint));
    cJSON *app_event = cJSON_CreateObject();

	attribute_t type_name;
	sensor_type_get_name(payload->data.type, type_name);
    cJSON *type = cJSON_CreateString((char*)type_name);
    cJSON *v    = NULL;

    sensor_val_t *vals = payload->data.values;
    for(uint8_t i=0; i<payload->data.num_values; i++) {
	    switch(payload->data.type) {
		    case SENSOR_BATTERY: {
			    v = cJSON_CreateNumber(vals[i].u16);
			    cJSON *cap  = cJSON_CreateString("battery");
			    cJSON *attr  = cJSON_CreateString("battery");
  			    cJSON_AddItemToObject(app_event, "attribute", attr);
  			    cJSON_AddItemToObject(app_event, "capability", cap);
		    } break;

		    case SENSOR_MOTION: {
			    strncpy(buf, MOTION_STRING[vals[i].u16], sizeof(buf)-1);
                enum_to_str(buf);
			    v = cJSON_CreateString(buf);
		    } break;

		    case SENSOR_CONTACT: {
			    strncpy(buf, CONTACT_STRING[vals[i].u16], sizeof(buf)-1);
			    lowerchrs(buf);
			    v = cJSON_CreateString(buf);
		    } break;

		    case SENSOR_PRESENCE: {
			    strncpy(buf, PRESENCE_STRING[vals[i].u16], sizeof(buf)-1);
                enum_to_str(buf);
			    v = cJSON_CreateString(buf);
		    } break;

		    default: {
			    v = cJSON_CreateNumber(vals[i].u16);
		    } break;
        }
    }

    cJSON_AddItemToObject(app_event, "type", type);
    cJSON_AddItemToObject(app_event, "value", v);

    char *app_body = cJSON_PrintUnformatted(app_event);
	strncpy(st_payload.body, app_body, sizeof(st_payload.body));

    cJSON_Delete(app_event);
    cJSON_free(app_body);

    LOGI("stapi(POST): %s", st_payload.body);
	return xQueueSend(xSTQueue, &st_payload, ST_QUEUE_TIMEOUT);
}

uint8_t st_send_payload(device_data_t *payload) {
	return st_queue_payload(payload);
}

void st_app_init() {
	bt_set_device_config_cb((bt_device_config_cb_t)st_configure_device);
#ifndef DISABLE_DEVICE_CREATION
	bt_set_device_init_cb((bt_device_init_cb_t)st_init_device);
#endif
#ifdef CREATE_STATIC_DEVICES
	bt_set_device_create_cb((bt_device_create_cb_t)st_create_device);
#endif
	xTaskCreatePinnedToCore(st_queue_task, "st_app_queue", ST_QUEUE_STACK_SZ, NULL, NET_QUEUE_PRIO, NULL, 1);
}
