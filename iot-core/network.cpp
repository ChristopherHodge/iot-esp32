#include "iot-common.h"
#include "devices.h"
#include "influx.h"
#include "network.h"

static const char *TAG = "network";

static	volatile    uint8_t     wifi_err_cnt    = 0;

static  EventGroupHandle_t  xWifiState          = NULL;
static  QueueHandle_t       xNetUpdateQueue     = NULL;

void net_queue_mgr(void *ptx) {
    xNetUpdateQueue = xQueueCreate(6, sizeof(void*));
    device_data_t *payload = NULL;

	for(;;) {
        STACK_STATS
		if(xQueueReceive(xNetUpdateQueue, &(payload), portMAX_DELAY) != pdTRUE) {
			continue;
		}
        LOGD("payload received by net queue: %s", payload->device_id);
		device_process_payload(payload);
		vTaskDelay(NET_UPDATE_DELAY);
	}
}

uint8_t network_queue_payload(device_data_t *payload) {
    if(xNetUpdateQueue == NULL) {
        return true;
    }
    if(xQueueSend(xNetUpdateQueue, (void*)&payload, DELAY_S4) != pdTRUE) {
        LOGE("failed to queue payload: %s", payload->device_id);
        return false;
    }
    return true;
}

void wifi_err_check(uint8_t err) {
    if (err > 0) {
        wifi_err_cnt += err;
    } else if (wifi_err_cnt > 0) {
        wifi_err_cnt--;
    }

    if (wifi_err_cnt > WIFI_ERR_THRESH) {
        LOGE("wifi_err_cnt exceeds threshold: RESETTING DEVICE");
        vTaskDelay(DELAY_S4);
        rtc_reset();
    }
}

void mdns_start(mdns_config_t config) {
    if(mdns_init() != ESP_OK) {
        LOGE("failed to start mDNS");
        return;
    };
    mdns_hostname_set(config.hostname);
    mdns_service_add(config.instance, config.service, config.proto,
                        config.port, config.service_txt, config.num_entries);
}

void mdns_stop() {
    mdns_free();
}

http_client_t* http_client_init(http_client_t *client) {
    memset(&client->esp_config, 0, sizeof(esp_http_client_config_t));
    client->esp_config.transport_type = HTTP_TRANSPORT_OVER_TCP;
    client->esp_config.timeout_ms = TCP_CONN_TIMEOUT;
    client->esp_config.keep_alive_enable = false;

    memset(&client->headers, 0, sizeof((http_headers_t){{},0}));
    memset(client->buf, 0, HTTP_CLIENT_BUF_LEN);
    client->buf_ptr = (char*)client->buf;
    return client;
}

void http_client_enable_ssl(http_client_t *client, char *ca_pem) {
    client->esp_config.transport_type = HTTP_TRANSPORT_OVER_SSL; 
    client->esp_config.cert_pem = ca_pem;
}

void http_client_set_auth(http_client_t *client, char *user, char *pass) {
    client->esp_config.username = user;
    client->esp_config.password = pass;
}

void http_client_set_agent(http_client_t *client, char *agent) {
    client->esp_config.user_agent = agent;
}

void http_client_set_header(http_client_t *client, char *key, char *value) {
    http_header_t *header = &client->headers.entries[client->headers.idx++];
    header->key = key;
    header->value = value;
}

void http_client_connect(http_client_t *client, char *host, int port) {
    client->esp_config.host = client->buf_ptr;
    if(strncpy(client->buf_ptr, host, HTTP_CLIENT_BUF_LEN) != NULL) {
        client->buf_ptr += strlen(client->buf_ptr);
    }
    client->esp_config.port = port;
}

void http_client_set_query(http_client_t *client, char *params) {
    client->esp_config.query = params;
}

void http_client_set_post_data(http_client_t *client, char *body, size_t len) {
    client->body = body;
    client->body_len = len;
}

http_response_t http_client_get_response(http_client *client) {
    http_response_t resp;
    resp.status = esp_http_client_get_status_code(client->esp_handle);
    resp.length = esp_http_client_get_content_length(client->esp_handle);
    return resp;
}

void http_client_builder(http_client_t *client) {
   client->esp_handle = esp_http_client_init(&client->esp_config); 
   for(uint8_t i=0; i < client->headers.idx; i++) {
       http_header_t *header = &client->headers.entries[i];
       esp_http_client_set_header(client->esp_handle, header->key, header->value);
   }
}

http_response_t http_client_get(http_client_t *client, char *path) {
    http_response_t resp;
    client->esp_config.path = path;
    http_client_builder(client);
    if(esp_http_client_open(client->esp_handle, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client->esp_handle);
        resp = http_client_get_response(client);
    } else {
        LOGE("http_connect(): failed");
        resp.status = 1;
    }
    return resp;
}

http_response_t http_client_put(http_client_t *client, char *path) {
    http_response_t resp;
    client->esp_config.path = path;
    client->esp_config.method = HTTP_METHOD_PUT;
    http_client_builder(client);
    if(esp_http_client_open(client->esp_handle, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client->esp_handle);
        resp = http_client_get_response(client);
    } else {
        LOGE("http_connect(): failed");
        resp.status = 1;
    }
    return resp;
}

http_response_t http_client_post(http_client_t *client, char *path) {
    http_response_t resp;
    client->esp_config.path = path;
    client->esp_config.method = HTTP_METHOD_POST;
    http_client_builder(client);
    if(esp_http_client_open(client->esp_handle, client->body_len) == ESP_OK) {
        esp_http_client_write(client->esp_handle, client->body, client->body_len);
        esp_http_client_fetch_headers(client->esp_handle);
        resp = http_client_get_response(client);
    } else {
        LOGE("http_connect(): failed");
        resp.status = 1;
    }
    return resp;
}

size_t http_client_read(http_client_t *client, char *buf, size_t len)  {
    return esp_http_client_read_response(client->esp_handle, buf, len);
}

esp_err_t http_client_close(http_client_t *client) {
    return esp_http_client_cleanup(client->esp_handle);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    static int retries;
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                LOGI("start provisioning..");
                break;
            case WIFI_PROV_CRED_RECV: {
                LOGI("got credentials");
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                LOGE("unable to connect to network");
                retries++;
                if (retries >= MAX_PROV_RETRY) {
                    LOGI("max retries, restarting provisioning..");
                    wifi_prov_mgr_reset_sm_state_on_failure();
                    retries = 0;
                }
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                LOGI("success!");
                retries = 0;
                break;
            case WIFI_PROV_END:
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        LOGI("connected");
        xEventGroupSetBits(xWifiState, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        LOGI("disconnected..");
        esp_wifi_connect();
    }
}

static void wifi_init_sta(void) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static char *output_buffer;
    static int output_len;
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            LOGD("HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED: {
            LOGD("HTTP_EVENT_ON_CONNECTED");
            char buf[100];
            esp_http_client_get_url(evt->client, buf, sizeof(buf));
            strtok((char*)buf, "//");
            char *host = strtok(NULL, "/");
            LOGD("opened connection: %s", host);
        } break;
        case HTTP_EVENT_HEADER_SENT:
            LOGD("HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            LOGD("HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            LOGD("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            LOGE("Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            LOGD("HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED: {
            LOGD("HTTP_EVENT_DISCONNECTED");
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
             }
             char buf[100];
             esp_http_client_get_url(evt->client, buf, sizeof(buf));
             strtok((char*)buf, "//");
             char *host = strtok(NULL, "/");
             LOGD("closed connection: %s", host);
        } break;
    }
    return ESP_OK;
}

http_loc_t parse_url(char *url) {
	char *p = url;
    strtok(p, "//");

    char *host = strtok(NULL, ":") + 1;
    uint16_t port = atoi(strtok(NULL, "/"));

    char *path = strtok(NULL, "\0");
    p = path + strlen(path) + 1;

    strcpy(p, "/");
    strcat(p, path);
    path = p;

	return (http_loc_t){
		host = host,
		path = path,
		port = port,
	};
}

void wifi_connect() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    xWifiState = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_softap,
    	.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    	.app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned) {
        LOGI("starting provisioning");
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_0, NULL, WIFI_AP_NAME, NULL));
    } else {
        LOGI("connecting..");
        wifi_prov_mgr_deinit();
        wifi_init_sta();
    }

    xEventGroupWaitBits(xWifiState, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}

void network_queue_init() {
    switch(LOG_LEVEL) {
        case ESP_LOG_INFO:
            esp_log_level_set("HTTP_CLIENT", ESP_LOG_WARN);
            break;
        default:
            break;
    }
    char task[] = NET_QUEUE_TASK;
    for(uint8_t i=0; i<NET_QUEUE_NUM_TASKS; i++) {
        snprintf(task, strlen(task), NET_QUEUE_TASK, i);
        xTaskCreatePinnedToCore(net_queue_mgr, task, NET_QUEUE_STACK_SZ, NULL, NET_QUEUE_PRIO, NULL, 1);
    }
}
