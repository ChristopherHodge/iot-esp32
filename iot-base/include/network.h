

#ifndef NETWORK_H_
#define NETWORK_H_

#include <mbedtls/esp_config.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include "mbedtls/esp_debug.h"

#define TCP_CONN_TIMEOUT      5000
#define WIFI_CONN_TIMEOUT     15000

extern WiFiManager *wifiManager;
extern WiFiClient *client;
extern WiFiClientSecure *ssl_client;

void network_init();
#endif /* NETWORK_H_ */
