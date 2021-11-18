#ifndef _INFLUX_H_
#define _INFLUX_H_

#include "iot-config.h"
#include "devices.h"
#include "network.h"

/*!
    @file
    @brief Interface for writing time-series data to InfluxDB

 */

#ifndef INFLUX_HOST
 #define INFLUX_HOST          "influxdb.localdomain"
#endif

#ifndef INFLUX_DB_NAME
 #define INFLUX_DB_NAME       "sensors"
#endif

#ifndef INFLUX_PORT
 #define INFLUX_PORT          80
#endif

#define INFLUX_ENDPOINT       "/write"
#define INFLUX_PARAMS         "db=" INFLUX_DB_NAME
#define INFLUX_BASE_QUERY     "%s,device_id=%s,sensor_id=%hhu"

#define INFLUX_QUERY_SZ       (100)
#define INFLUX_HTTP_BUF_SZ    (150)
#define INFLUX_QUEUE_TIMEOUT  (60000)

#ifndef INFLUX_QUEUE_STACK_SZ
 #define INFLUX_QUEUE_STACK_SZ  DEFAULT_STACK_SZ
#endif

#define INFLUX_CLIENT_CONFIG(_local_buf)   \
	{                                              \
		.host = INFLUX_HOST,                       \
		.path = INFLUX_ENDPOINT,                   \
		.query = INFLUX_PARAMS,                    \
		.disable_auto_redirect = true,             \
		.event_handler = http_event_handler,       \
		.transport_type =  (esp_http_client_transport_t)\
					HTTP_TRANSPORT_OVER_TCP,       \
 		.user_data = _local_buf,                   \
		.skip_cert_common_name_check = false,      \
	}

/*!
    @brief Schedule async update to InfluxDB

    Parse the payload and schedule updates, allowing the payload heap allocation
    to be free'd immediately
    @param payload
 */
void	influx_queue_payload(device_data_t *payload);

/*!	
    @brief Initialize the InfluxDB update queue and management task

 */
void	influx_queue_init();

#endif  // _INFLUX_H_
