#ifndef IOT_OTA_H
#define IOT_OTA_H

#include "iot-common.h"
#include "esp_system.h"
#include "esp_https_ota.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "network.h"

#ifndef CA_CRT
 #define CA_CRT APP_CA_CRT
#endif

#ifndef FW_RELEASE_PREFIX
 #define FW_RELEASE_PREFIX   "release"
#endif

#ifndef ESP_OTA_URL_BASE
 #define DISABLE_ESP_OTA
#endif

#define FW_VERS_LEN          24

#ifndef DISABLE_ESP_OTA

#define ESP_OTA_URL          ESP_OTA_URL_BASE "/" FW_BASE_NAME
#define OTA_ENDPOINT_LATEST  "/latest"
#define OTA_PREFIX           FW_RELEASE_PREFIX "-"
#define OTA_BUF_LEN          150

void check_fw_update();

#endif // DISABLE_ESP_OTA

#endif // IOT_OTA_H
