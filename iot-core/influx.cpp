#include "iot-common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "network.h"
#include "influx.h"

static const char *TAG = "influx";

static QueueHandle_t xInfluxQueue = NULL;

static void influx_queue_task(void *ptx) {
    xInfluxQueue = xQueueCreate(6, INFLUX_QUERY_SZ);
	char query[INFLUX_QUERY_SZ];
	char *body = (char*)query;

	char http_resp_buf[INFLUX_HTTP_BUF_SZ];
	uint8_t err_cnt = 0;
	esp_err_t err;

	esp_http_client_config_t config = INFLUX_CLIENT_CONFIG(http_resp_buf);
	esp_http_client_handle_t client = nullptr;
	uint8_t is_retry = false;

#ifdef INFLUX_BATCH_BUF_SZ
	body = (char*)calloc(INFLUX_HTTP_BUF_SZ, INFLUX_BATCH_BUF_SZ);
#endif

    for(;;) {
        STACK_STATS
		memset((void*)query, 0, INFLUX_QUERY_SZ);
		if(xQueueReceive(xInfluxQueue, (void*)query, INFLUX_QUEUE_TIMEOUT) != pdTRUE) {
			if(client != nullptr) {
				esp_http_client_cleanup(client);
				client = nullptr;
			}
			continue;
		}

#ifdef INFLUX_BATCH_BUF_SZ
		LOGI("batching influx event to buffer");
		strncat(body, query, INFLUX_QUERY_SZ);
		strcat(body, "\n");
		if(strlen(body) < (INFLUX_BATCH_BUF_SZ-1) * INFLUX_QUERY_SZ) {
			continue;
		}
		LOGI("delivering batched influx payload (%d bytes):", strlen(body));
		LOGI("%s", body);
#endif

		if(client == nullptr) {
 			client = esp_http_client_init(&config);
    		esp_http_client_set_method(client, HTTP_METHOD_POST);
    		esp_http_client_set_header(client, "Content-Type", "application/json");
		}
    	esp_http_client_set_post_field(client, body, strlen(body));
	    err = esp_http_client_perform(client);
    	if (err == ESP_OK) {
            esp_http_client_get_status_code(client);
            esp_http_client_get_content_length(client);
			is_retry = false;
		} else {
			LOGW("failed to write influx data: resp_code: 0x%04x", err);
			esp_http_client_cleanup(client);
			client = nullptr;
			err_cnt++;
			if(is_retry) {
				is_retry = false;
				LOGE("max retries reached: giving up on this message");
			} else {
				is_retry = true;
				LOGI("will try again once more..");
				xQueueSendToFront(xInfluxQueue, (void*)query, DELAY_S4);
				vTaskDelay(DELAY_S5);
			}
		}
		wifi_err_check(err_cnt);
		err_cnt = 0;
#ifdef INFLUX_BATCH_BUF_SZ
		memset(body, 0, 1);
#endif
	}
}

void influx_queue_payload(device_data_t *payload) {
	if(!xInfluxQueue) {
		LOGW("xInfluxQueue: not initialized");
		return;
	}

	attribute_t sensor_name;
	sensor_type_get_name(payload->data.type, sensor_name);

	char query[INFLUX_QUERY_SZ];
	int pos = sprintf(query, INFLUX_BASE_QUERY, sensor_name,
												payload->device_id,
												payload->data.sensor_id);

	for(uint8_t i=0; i < payload->data.num_values; i++) {
		if(strlen(payload->data.tags[i].key)) {
			pos += sprintf(query + pos, ",%s=%s", payload->data.tags[i].key,
												payload->data.tags[i].val);
		}
	}

	query[pos++] = ' ';

	for(uint8_t i=0; i<payload->data.num_values; i++) {
    	sensor_val_t val = payload->data.values[i];
		pos += sprintf(query+pos, "%s=%d,", val.attribute, val.u16);
	}
	query[pos-1] = '\0';

	LOGI("influx(POST): %s", query);
	xQueueSend(xInfluxQueue, (void*)query, DELAY_S4);
}

void influx_queue_init() {
	xTaskCreatePinnedToCore(influx_queue_task, "influx_queue", INFLUX_QUEUE_STACK_SZ, NULL, NET_QUEUE_PRIO, NULL, 1);
}
