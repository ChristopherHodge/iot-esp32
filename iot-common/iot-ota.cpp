#ifndef DISABLE_ESP_OTA
#include "iot-ota.h"

static const char *TAG = "iot-ota";

const esp_http_client_config_t *ota_http_config;
char ota_fw_url[OTA_BUF_LEN];

const EventGroupHandle_t xOTAUpdate = xEventGroupCreate();

uint8_t ota_get_latest_version(char* src, char *buf, size_t len) {
    http_client_t m_client;
    http_client_t *client = http_client_init(&m_client);
    http_client_enable_ssl(client, CA_CRT);

    char ota_buf[OTA_BUF_LEN];
    strncpy(ota_buf, src, sizeof(ota_buf)-1);
    http_loc_t loc = parse_url(ota_buf);
    strcat(loc.path, OTA_ENDPOINT_LATEST);

    http_client_connect(client, loc.host, loc.port);
    http_client_get(client, loc.path);
    http_response_t resp = http_client_get_response(client);
    if(resp.status > 299 || resp.status < 200) {
        LOGE("invalid http response: %d", resp.status);
        return false;
    }
    http_client_read(client, buf, len);
    stripchrs(buf);
    http_client_close(client);
    return true;
}

const esp_http_client_config_t ota_get_config(char* src, char *vers, char *buf, size_t len) {
    snprintf(buf, len-1, "%s/%s.bin", src, vers);
    LOGI("set ota url: %s", buf);
    const esp_http_client_config_t http_config = {
        .url = buf,
        .cert_pem = CA_CRT,
        .timeout_ms = 50000,
        .keep_alive_enable = true,
    };
    return http_config;
}

void ota_update_task(void *ptx) {
    LOGI("installing ota update: %s", ota_http_config->url);
    esp_err_t ret = esp_https_ota(ota_http_config);
    if (ret == ESP_OK) {
        esp_restart();
    } else {
        LOGE("ota failed");
    }
    xEventGroupSetBits(xOTAUpdate, 1);
    vTaskDelete(NULL);
}

void do_ota_update(const esp_http_client_config_t *config) {
    ota_http_config = (esp_http_client_config_t*)calloc(1, sizeof(esp_http_client_config_t));
    memcpy((esp_http_client_config_t*)ota_http_config, config, sizeof(esp_http_client_config_t));

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    esp_wifi_set_ps(WIFI_PS_NONE);

    xTaskCreate(&ota_update_task, "ota_update_task", 10240, NULL, 5, NULL);
    xEventGroupWaitBits(xOTAUpdate, 1, true, true, portMAX_DELAY);

    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
}

void check_fw_update() {
    char latest[FW_VERS_LEN];
    if(strncmp(OTA_PREFIX, BUILD_VERSION, strlen(OTA_PREFIX)) != 0) {
        LOGI("skipping update: %s is not a release tag", BUILD_VERSION);
        return;
    }
    if(!ota_get_latest_version(ESP_OTA_URL, latest, sizeof(latest)-1)) {
        LOGE("failed to get latest fw version");
        return;
    }
    LOGI("fw: latest: %s / running: %s", latest, BUILD_VERSION);
    if(strncmp(latest, BUILD_VERSION, FW_VERS_LEN) == 0) {
        return;
    }
    const esp_http_client_config_t config = ota_get_config(
            ESP_OTA_URL, latest, ota_fw_url, sizeof(ota_fw_url)-1);
    do_ota_update(&config);
}

#endif // DISABLE_ESP_OTA
