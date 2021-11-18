#ifndef SMARTAPP_H_
#define SMARTAPP_H_

#include "iot-config.h"
#include "iot-common.h"
#include "network.h"
#include "devices.h"
#include "cJSON.h"
#include "smartthings.h"
#include "ble.h"

#define ST_BODY_SZ          100
#define ST_HTTP_BUF_SZ      150
#define ST_QUEUE_TIMEOUT    30000
#define ST_DEVICE_ENDPOINT  "/devices/"

#ifndef ST_QUEUE_STACK_SZ
 #define ST_QUEUE_STACK_SZ  DEFAULT_STACK_SZ
#endif

#define ST_PAYLOAD_SZ     (DEVICE_ID_SZ+2 + ST_BODY_SZ)

typedef struct st_payload {
    char endpoint[DEVICE_ID_SZ+2];
    char body[ST_BODY_SZ];
} st_payload_t;

typedef esp_http_client_method_t http_method_d;

#define ST_CLIENT_CONFIG(_local_buf)   \
	{                                              \
		.url = ST_CONFIG->apiurl,                  \
        .cert_pem = CA_CRT,                        \
		.disable_auto_redirect = true,             \
		.event_handler = http_event_handler,       \
		.transport_type =  (esp_http_client_transport_t)\
					HTTP_TRANSPORT_OVER_SSL,       \
 		.user_data = _local_buf,                   \
		.skip_cert_common_name_check = false,      \
	}

/*!
    @brief Schedule async update to InfluxDB

    Parse the payload and schedule updates, allowing the payload heap allocation
    to be free'd immediately
    @param payload
 */

/*!
    @file
    @brief Communicate with SmartThings via our SmartApp

 */

/*!
    @brief  Initialize a new unconfigured device in SmartThings

    `PUT /devices/:device_id`

    This informs the service of a new device, and any additioanl
    functionality for defining configuration will be done by the
    smartapp and notify us.
    @return uint8_t  evaluates boolean 
 */
uint8_t st_init_device(char*);

/*!
    @brief  Create a device in smartthings of specific type

    `POST /devices {body}`

    Creates a new device while declaring its type and will be
    created using that devie type, and immediately usable.
    @return uint8_t  evaluates boolean
 */
uint8_t st_create_device(char*, sensor_type_t);

/*!
    @brief Update a local device config from SmartThings

    `GET /configs/:device_id`
    
    Given a new/unconfigured device, query smartthings for the
    type and configure our record
    @return uint8_t  evaluates boolean
 */
req_status_t	st_configure_device(device_t*);

/*!
    @brief Send device payload as SmartThings Device Event

    `POST /devices/:device_id {body}`

    Parse the entries of a device payload and perform the REST
    calls to sent the Device Events to SmartThings
    @return uint8_t  evaluates boolean
 */
uint8_t st_send_payload(device_data_t*);

/*!
    @brief Register SmartApp callbacks

 */
void st_app_init();

/* deprecated */
http_response_t	send_notify_event(char*);

#endif // SMARTAPP_H_
