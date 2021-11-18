#include <stdlib.h>
#include "iot-common.h"
#include "tcpip_adapter.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "network.h"
#include "sensor.h"
#include "ble.h"
#include "influx.h"
#include "smartthings.h"
#include "public-ca.h"

static const char* TAG = "smartthings";

static stconfig_t m_stconfig = { {}, {}, false };
stconfig_t *ST_CONFIG = &m_stconfig;

uint8_t st_config_loaded() {
	return (strlen(ST_CONFIG->apiurl) && strlen(ST_CONFIG->apikey));
}

void configure_mdns() {
	char svc[] = ST_MDNS_SVC;
	char hostname[20];
	char rand_id[4];
	char addr[12];
	char port[4];

    uint8_t val;
	esp_fill_random(&val, sizeof(val));
    itoa(val, rand_id, 10);

    strcat(hostname, svc);
	strcat(hostname, "_");
	strcat(hostname, rand_id);

    tcpip_adapter_ip_info_t ipInfo; 
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
    sprintf(addr, "%x", ipInfo.ip.addr);
    sprintf(port, "%x", ST_CONFIG_PORT);
    mdns_txt_item_t txt[2] = {
        {"deviceAddress", port},
        {"networkAddress", addr},
	};

	uint8_t id = esp_random() & 0xff;
	sprintf(hostname, "espressif%d", id);
    mdns_config_t mdns_config = {
		.hostname = (char*)hostname,
		.service = "_smartthings",
		.instance = svc,
		.proto = "_tcp",
		.port = ST_CONFIG_PORT,
		.service_txt = txt,
		.num_entries = 2,
	};
	mdns_start(mdns_config);
}

void store_st_config() {
	nvs_handle_t nv_data;
	if(nvs_open(ST_CONFIG_NVS, NVS_READWRITE, &nv_data) == ESP_OK) {
       nvs_set_str(nv_data, "apikey", ST_CONFIG->apikey);
       nvs_set_str(nv_data, "apiurl", ST_CONFIG->apiurl);
	   nvs_commit(nv_data);
	   nvs_close(nv_data);
	}
	ST_CONFIG->updated = false;
}

esp_err_t register_handler(httpd_req_t *req) {
	size_t len = httpd_req_get_url_query_len(req);
	LOGI("got /register callback with %d bytes of parameters", len);
	char q[len+1];
	httpd_req_get_url_query_str(req, q, sizeof(q));
	if(httpd_query_key_value(q, "apikey", ST_CONFIG->apikey, sizeof(m_stconfig.apikey)) == ESP_OK) {
	    LOGI("updated apikey: %s", ST_CONFIG->apikey);
	}
	if(httpd_query_key_value(q, "apiurl", ST_CONFIG->apiurl, sizeof(m_stconfig.apiurl)) == ESP_OK) {
	    LOGI("updated apiurl: %s", ST_CONFIG->apiurl);
	}
	httpd_resp_send(req, "OK", 2);
	vTaskDelay(DELAY_S1);
	ST_CONFIG->updated = true;
	return ESP_OK;
}

httpd_handle_t start_webserver() {
	httpd_uri_t uri_register {
		.uri     = "/register",
		.method  = HTTP_PUT,
		.handler = register_handler,
		.user_ctx = NULL,
	};

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.server_port = ST_CONFIG_PORT;
	config.ctrl_port = ST_CONFIG_PORT + 10;
    httpd_handle_t server;
    if(httpd_start(&server, &config) != ESP_OK) {
		return nullptr;
	}
    httpd_register_uri_handler(server, &uri_register);
    return server;
}

void st_config_task(void *pvx) {
	uint16_t duration = *(uint16_t*)pvx;
	httpd_handle_t server;
	if((server = start_webserver()) == nullptr) {
		LOGE("httpd server failed to start");
		goto cleanup;
	}
	configure_mdns();
	while(--duration) {
		vTaskDelay(1000 / portTICK_RATE_MS);
		if(ST_CONFIG->updated) {
			store_st_config();
			break;
		}
	}
cleanup:
	mdns_stop();
	httpd_stop(server);
	LOGI("st_config_task: complete");
	vTaskDelete(NULL);
}

void st_config_update(uint16_t duration) {
	LOGI("st_config_task: will listen for st_config updates for %d secs", duration);
	xTaskCreatePinnedToCore(st_config_task, "st_config_listen", ST_CONFIG_STACK_SZ, (void*)&duration, ST_CONFIG_TASK_PRIO-1, NULL, 1);
	vTaskDelay(DELAY_S4);
}

void wait_for_config() {
	httpd_handle_t server = start_webserver();
	configure_mdns();
	LOGI("waiting for mDNS broadcast..");
	uint16_t counter = 60;
	while(!st_config_loaded()) {
		if(!(--counter)) {
			counter = 60;
			LOGI("still waiting for mDNS broadcast..");
		}
		vTaskDelay(DELAY_S4);
	}
	mdns_stop();
	httpd_stop(server);
}

void st_config_load() {
	nvs_handle_t nv_data;
	size_t len;
    if(nvs_open(ST_CONFIG_NVS, NVS_READONLY, &nv_data) == ESP_OK) {
	   len = sizeof(ST_CONFIG->apikey);
       nvs_get_str(nv_data, "apikey", ST_CONFIG->apikey, &len);
	   len = sizeof(ST_CONFIG->apiurl);
       nvs_get_str(nv_data, "apiurl", ST_CONFIG->apiurl, &len);
	   nvs_close(nv_data);
	} else {
		LOGE("failed to open nvs partition");
	}

	if(st_config_loaded()) {
		LOGI("stconfig loaded.");
#ifdef ST_CONFIG_UPDATE_SECS
		st_config_update(ST_CONFIG_UPDATE_SECS);
#endif
	} else {
		LOGI("waiting to receive a smartthings config..");
		wait_for_config();
		store_st_config();
		LOGI("stconfig loaded.");
	}
}

void smartthings_init() {
	st_config_load();
}

http_response_t st_api_request(http_client_t *http_client,
                               esp_http_client_method_t verb,
                               char *endpoint) {
    char buf[ST_API_BUF_LEN];
    strncpy(buf, ST_CONFIG->apiurl, sizeof(buf) - 1);
	http_loc_t loc = parse_url(buf);

    strcat(loc.path, endpoint);
    char *apikey = loc.path + strlen(loc.path) + 1;
    strcpy(apikey, "Bearer ");
    strcat(apikey, ST_CONFIG->apikey);

    LOGD("connecting to host: %s", loc.host);

    http_client_enable_ssl(http_client, CA_CRT);
    http_client_set_agent(http_client, "custom-network/0.1");
    http_client_set_header(http_client, "Accept", "*/*");
    http_client_set_header(http_client, "Content-type", "application/json");
    http_client_set_header(http_client, "Authorization", apikey);
    http_client_connect(http_client, loc.host, loc.port);

	switch(verb) {
		case HTTP_METHOD_GET:
			return http_client_get(http_client, loc.path);
		case HTTP_METHOD_POST:
			return http_client_post(http_client, loc.path);
		case HTTP_METHOD_PUT:
			return http_client_put(http_client, loc.path);
		default:
			LOGE("invalid HTTP method requested");
			return (http_response_t){0, (size_t)-1};
	}

}