
#ifndef SMARTTHINGS_H_
#define SMARTTHINGS_H_

#include "iot-config.h"
#include "iot-common.h"
#include "network.h"

#ifndef ST_MDNS_SVC
 #define ST_MDNS_SVC           "smartthings"
#endif

#ifndef ST_HTTP_PORT
 #define ST_HTTP_PORT          80
#endif

#ifndef ST_CONFIG_PORT
 #define ST_CONFIG_PORT        88
#endif

#ifndef CA_CRT
 #define CA_CRT ST_CA_CRT
#endif

#define ST_CONFIG_STACK_SZ      (3*1024)
#define ST_CONFIG_TASK_PRIO     (DEFAULT_TASK_PRIO-1)

#define ST_CONFIG_NVS            "stconfig"
#define ST_CONFIG_UPDATE_SECS    30

#define ST_STACK_SZ              DEFAULT_STACK_SZ
#define ST_API_BUF_LEN           300
#define ST_API_URL_LEN           200
#define ST_API_KEY_LEN           80
#define ST_SEND_PAYLOAD_RETRIES  1

typedef struct stconfig {
	char apiurl[ST_API_URL_LEN];
	char apikey[ST_API_KEY_LEN];
    uint8_t updated;
} stconfig_t;

extern stconfig_t *ST_CONFIG;

/*!
    @file
    @brief Provides SmarThings and coordinates SmartApp/device discovery

 */

/*!
    @brief Initialize SmartThings capabilities

    This will start the tasks required to peer with a SmartApp via
    the configure mDNS service name, to provide us with the necessary
    details for consuing the API
 */
void		smartthings_init();

/*!
    @brief Perform a REST command to our SmartApp API

    Basic requests only need to provide a blank client instance.
    Alternatively provide some additional configuration to perform more
    involed requests.
    @return http_response_t 
 */
http_response_t st_api_request(http_client_t*, esp_http_client_method_t, char*);

#endif /* SMARTTHINGS_H_ */
