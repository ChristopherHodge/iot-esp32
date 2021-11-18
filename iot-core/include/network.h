#ifndef NETWORK_H_
#define NETWORK_H_

#include "iot-config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "tcpip_adapter.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include "mdns.h"
#include "esp_http_client.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "public-ca.h"

/*!
    @file
    @brief Interfaces for WiFi network communication

 */

#define TCP_CONN_TIMEOUT         5000
#define WIFI_CONN_TIMEOUT        15000
#define HTTP_CLIENT_BUF_LEN      100
#define MAX_PROV_RETRY           4

#define DEFAULT_HTTP_PORT        80
#define DEFAULT_HTTPS_PORT       443

#define WIFI_CONNECTED_BIT      (1 << 0)

#define PROV_TRANSPORT_SOFTAP   "softap"
#define WIFI_AP_NAME            "ConnectThing"

#ifndef NET_QUEUE_STACK_SZ
 #define NET_QUEUE_STACK_SZ     (6 * 1024)
#endif

#ifndef NET_TASK_PRIO
 #define NET_TASK_PRIO           DEFAULT_TASK_PRIO
#endif

#define NET_UPDATE_QUEUE_SZ      8 
#define NET_UPDATE_DELAY         50
#define NET_QUEUE_NUM_TASKS      1
#define NET_QUEUE_PRIO           NET_TASK_PRIO
#define NET_QUEUE_TASK           "net_queue_task_%d"

typedef struct device_data device_data_t;

typedef struct http_header {
	char *key;
	char *value;
} http_header_t;

typedef struct http_headers {
	http_header_t	entries[5];
	uint8_t			idx;
} http_headers_t;

typedef struct http_loc {
	char *host;
	char *path;
	uint16_t port;
} http_loc_t;

typedef struct http_client {
    esp_http_client_handle_t esp_handle;
	esp_http_client_config_t esp_config;
	char *body;
	size_t body_len;
	char buf[HTTP_CLIENT_BUF_LEN];
	char *buf_ptr;
	http_headers_t headers;
} http_client_t;

typedef struct http_response {
	uint16_t	status;
	size_t		length;
} http_response_t;

typedef struct mdns_config {
	char			*hostname;
    char			*service;
	char			*instance;
	char			*proto;
	uint16_t		port;
	mdns_txt_item_t *service_txt;
	uint8_t 		num_entries;
} mdns_config_t;

typedef HttpStatus_Code req_status_t;

/*!
    @brief connect wi-fi using stored creds or start wifi-provisioning

 */
void		wifi_connect();

/*!
    @brief track errors for wifi clients to compare against threshold

    Excessive errors will trigger a reset with powerdown of all domains
    @param N_of_Err
 */
void		wifi_err_check(uint8_t);

/*!
    @brief Allocate new http client handles to a struct

    @param client ptr to an http_client_t
    @return http_client_t*  ptr on succes or null
 */
http_client_t*	http_client_init(http_client_t*);

/*!
    @brief set http_client to use ssl

    @param http_client
    @param ca_pem PEM ASCII of the trusted CA
 */
void		http_client_enable_ssl(http_client_t*, char*);

/*!
    @brief set basic auth credentials for http client

    @param http_client
    @param username
    @param password
 */
void		http_client_set_auth(http_client_t*, char*, char*);

/*!
    @brief set custom headers, such as auth token

    @param http_client
    @param key
    @param value
 */
void		http_client_set_header(http_client_t*, char*, char*);

/*!
    @brief set http_client connection info (host/port)

    @param http_client
    @param host
    @param port
 */
void		http_client_connect(http_client_t*, char*, int);

/*!
    @brief set http_client pointer to buffer for request body

    @param http_client
    @param body ptr to body buffer
    @param len length of buffer
 */
void		http_client_set_post_data(http_client_t*, char*, size_t);

/*!
    @brief set http_client user-agent

    @param http_client
    @param useragent
 */
void		http_client_set_agent(http_client_t*, char*);

/*!
    @brief set http_client query params as string

    @param http_client
    @param params
 */
void		http_client_set_query(http_client_t*, char*);

/*!
    @brief read http_client response after executing request

    @param http_client
    @return http_response_t 
 */
http_response_t	http_client_get_response(http_client*);

/*!
    @brief execute an HTTP GET to path using client settings

    @param http_client
    @param uri_path
    @return http_response_t 
 */
http_response_t	http_client_get(http_client_t*, char*);

/*!
    @brief execute an HTTP PUT to path using client settings

    @param http_client
    @param uri_path
    @return http_response_t 
 */
http_response_t	http_client_put(http_client_t*, char*);

/*!
    @brief execute an HTTP POST to path using client settings

    @param http_client
    @param uri_path
    @return http_response_t 
 */
http_response_t	http_client_post(http_client_t*, char*);

/*!
    @brief read the body of the http_client response

    @param buf buf to write response
    @param len length of buf
    @return size_t  lenth returned to buffer
 */
size_t		http_client_read(http_client_t*, char*, size_t);

/*!
    @brief close all network resources associated with client

    @param http_client
    @return esp_err_t  ESP_OK on succes or applicable ESP error code
 */
esp_err_t	http_client_close(http_client_t*);

/*!
    @brief start tasks associated with the network queues

    Tasks associated with the network services, such as sending updates using
    transport protocols.  This is not related to the lower level wifi services
    start by wifi_connect() which handles PHY and IP layers.
 */
void		network_queue_init();

/*!
    @brief Schedule a payload to the network delivery queue

    The preferred route to process a payload.  When submitting to this queue,
    the delivery will happen async based on the rules i.e. scopes, retries,
    failures.  The payload allocation on the heap should not be free'd, all
    cleanup will be handled within the delivery task.

    @param payload
    @return uint8_t boolean eval
 */
uint8_t		network_queue_payload(device_data_t*);

/*!
    @brief start mDNS services using config

    Used by smartthings edge, see smartthings.h
    @param config
 */
void		mdns_start(mdns_config_t);

/*!
    @brief stop mDNS service


 */
void		mdns_stop();

/*!
    @brief parse string as URL

    @return http_loc_t
 */
http_loc_t parse_url(char*);

esp_err_t http_event_handler(esp_http_client_event_t*);

#endif /* NETWORK_H_ */
