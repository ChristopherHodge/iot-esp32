
#ifndef _DEVICES_H_
#define _DEVICES_H_

#include "iot-config.h"
#include "sensor.h"

/*!
    @file
    @brief Manage interactions with physical devices

 */

#ifndef PRESENCE_DEPART_DELAY_MS
 #define PRESENCE_DEPART_DELAY_MS  90000
#endif

#ifndef PRESENCE_ARRIVE_DELAY_MS
 #define PRESENCE_ARRIVE_DELAY_MS  0
#endif

#ifndef PRESENCE_STACK_SZ
 #define PRESENCE_STACK_SZ     (2 * 1024)
#endif

#ifndef PRESENCE_TASK_PRIO
 #define PRESENCE_TASK_PRIO     (DEFAULT_TASK_PRIO + 2)
#endif

#define PRESENCE_UPDATE_TIMEOUT    10000
#define PRESENCE_UPDATE_DELAY_MS  120000

#define DEVICE_MGMT_TASK_SZ     (3 * 1024)
#define DEVICE_MGMT_TASK_DELAY  40000

#ifndef DEVICE_TIMEOUT
 #define DEVICE_TIMEOUT          200000
#endif

#define DEVICE_ID_SZ	        18

#define SENSOR_SIZEOF   sizeof((sensor_t){ SENSOR_NONE, {}, {},  0, 0, 0, 0, })
#define DEVICE_SIZEOF   sizeof((device_t){ 0, {}, { {}, {}, }, NULL, 0, 0, 0, })
#define SENSORS_SIZE     (SENSOR_SIZEOF * MAX_SENSORS)
#define DEVICES_SIZE     (DEVICE_SIZEOF * MAX_DEVICES)

/*!
    @var typedef char[] device_id_t
    @brief Standard notation for PHY address

 */
typedef	char		device_id_t[DEVICE_ID_SZ];

class SecureClient;

#define ACTION_STATE(STATE) \
    STATE(NONE)             \
    STATE(ARRIVED)          \
    STATE(DEPARTED)         \
    STATE(MAX) 

#define ACTION_ENUM(Name)       ACTION_##Name,

/*!
    @enum action_t
	@brief Possible GATT characteristic value types

 */
typedef enum action { ACTION_STATE(ACTION_ENUM) } action_t;

#define NEW_DEVICE(__id)     \
{                                       \
    .in_use      = false,               \
    .id          = { '\x30' },          \
    .sensors     = { INVALID_SENSOR, }, \
    .connection  = NULL,                \
    .last_seen   = 0,                   \
    .version     = 0,                   \
    .hw_rev      = 0,                   \
    .device_id   = __id,                \
}

/*!
    @struct device_t
	@brief Representation of a device

 */
typedef struct device {
	bool           in_use;
	device_id_t    id;
	sensor_t       sensors[MAX_SENSORS];
	SecureClient   *connection;
	unsigned long  last_seen;
	uint16_t       version;
    uint16_t       hw_rev;
	uint8_t        device_id;
} device_t;

/*!
    @struct device_presence_t
	@brief Device presence information

 */
typedef struct device_presence {
    device_t *device;
    presence_t presence;
    volatile uint8_t arrived : 1;
    volatile uint8_t departed : 1;
    unsigned long int ts;
} device_presence_t;

/*!
    @struct devices_t
	@brief Handles for all devices definitions

 */
typedef struct devices {
    device_t **devices;
    uint8_t num_devices = MAX_DEVICES;
} devices_t;

/*!
    @struct devices_data_t
	@brief Top level of a device update

 */
typedef struct device_data {
  device_id_t			device_id;
  sensor_multi_data_t	data;
} device_data_t;

/*!
    @fn device_update_cb_t
    @brief Callback to provide result of update

    @param device_data contains scope list with status results
    @param errs a counter of number of errors encounterd in process

 */
typedef     void (*device_update_cb_t)(device_data_t, uint8_t);

/*!
    @fn device_presence_cb_t
    @brief Callback when device presence changes
    
    @param device_presence
    @returns uint8_t

 */
typedef     uint8_t (*device_presence_cb_t)(device_presence_t);

/*!
    @brief Set the callback handler from device updates

    Since updates are async in another task, provide a callback
    with a result
    @param handler device_update_cb_t
 */
void    set_device_update_cb(device_update_cb_t);

/*!
    @brief Set a callback from presence updates

    Provide a callback for notification on any presence status update
    @param handler device_presence_cb_t
 */
void    set_device_presence_cb(device_presence_cb_t);

/*!
    @brief Get list of pointer references to all of the devices

    @return devices_t
 */
devices_t   get_devices();

/*!
    @brief Get a pointer to a sigle device by the device_id

    @return device_id
 */
device_t*   get_device(device_id_t);

/*!
    @brief Define a new device

    Update the first device slot which is not currently in use this device_id
    and return the pointer to the record.  Returns null if no slots available.
    @return device_t* ptr to new device or null if not available
 */
device_t*   create_device(device_id_t);

/*!
    @brief Clear the device record for the given device ptr

    @param device* device ptr
 */
void    delete_device(device_t*);

/*!
    @brief Load the definition of static device id's

    Configures devices with id's from STATIC_DEVICE_LIST.  These are also
    added to the BLE whitelist, so `ble_init()` must be called first.  If
    no sensor type is provided, the device will have none configured.
    @param sensor_type a default sensor_type to configure, if desired
 */
void device_load_static_list(sensor_type_t sensor_type=SENSOR_NONE);

/*!
    @brief Allocate a new device payload from heap

    @param device_id the device_id for the payload
    @param num_entries number of sensor entries to allocate
    @return device_data_t* 
 */
device_data_t*  device_payload_init(const char*, uint8_t);

/*!
    @brief  Return a handle to a payload sensor entry by index

    Retruns a handle for use with the sensor_payload_entry_*() calls
    @param payload a device payload
    @param idx the requested sensor data index we plan to update
    @return sensor_data_entry_t
 */
sensor_data_entry_t device_payload_get_entry(device_data_t*, uint8_t);

/*!
    @brief Free heap memory allocation associated with a device payload

    @param payload
 */
void    device_payload_free(device_data_t*);

/*!
    @brief Schedule a device payload for async delivery

    Destination(s) will be assigned based on sensor rules which assign destination
    scopes to the payload prior to placing it in a queue to attempt to ensure
    they are all satisfied.
    @param payload
 */
void    device_process_payload(device_data_t*);

/*!
    @brief Add a sensor to an existing device

    Will add the specified type to the first unused slot for the device.
    Returns a null ptr if there are no available slots.
    @param device
    @param sensor_type
    @return sensor_t  ptr or null
 */
sensor_t*   device_add_sensor(device_t*, sensor_type_t);

/*!
    @brief attach the device presence handlers to BLE updates 

    Cause BLE connect/disconnect to schedule presence events with SmartThings handler
 */
void    device_enable_ble_presence();

/*!
    @brief Craft an update on behalf of a device and queue for delivery

    @param device_id 
    @param type sensor_type_t
    @param state  value
    @return uint8_t 
 */
uint8_t device_send_update(const char*, sensor_type_t, uint16_t, uint8_t device_idx=0);

/*!
    @brief set the BLE service to use on devices to BAS

    Configure a subscription to devices BLE BAS service with network handler
 */
void    device_enable_ble_bas();

/*!
    @brief Allocate devices per iot-config.h and start management task

 */
void    device_init();

uint8_t device_payload_alloc_entry(device_data_t*);
uint8_t device_update_needed(device_t*, uint16_t);

#endif  // _DEVICES_H_
