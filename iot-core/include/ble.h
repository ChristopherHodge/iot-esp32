#ifndef BLE_H_
#define BLE_H_

#include "iot-config.h"
#include "esp_blufi_api.h"
#include "NimBLEDevice.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "devices.h"
#include "network.h"

/*!
    @file
	@brief Interfaces for BLE communication

 */

#define BLE_PASSKEY            (122481UL)

#define BLE_UPDATE_QUEUE_SZ    (6)

#define AUTH_INPUT_DELAY       (3500)

#define BLE_CONNECT_TIMEOUT_SECS         7
#define SECURE_CONN_ATTEMPTS_TO_BACKOFF  3
#define SECURE_CONN_FAIL_BACKOFF_MS      1000
#define BLE_SCAN_FAIL_THRESH             3

#ifndef SECURE_CONN_TIMEOUT_MS
 #define SECURE_CONN_TIMEOUT_MS          9000
#endif

#ifndef BT_DEVICE_STACK_SZ
 #define BT_DEVICE_STACK_SZ     DEFAULT_STACK_SZ
#endif

#ifndef BT_QUEUE_STACK_SZ
 #define BT_QUEUE_STACK_SZ      (3 * 1024) 
#endif

#ifndef BT_SCAN_STACK_SZ
 #define BT_SCAN_STACK_SZ       (3 * 1024)
#endif

#ifndef BT_TASK_PRIO
 #define BT_TASK_PRIO           DEFAULT_TASK_PRIO
#endif

#ifndef BLE_LOG_LEVEL
 #define BLE_LOG_LEVEL          ESP_LOG_WARN
#endif

#ifndef BT_DEVICE_PRIO
 #define BT_DEVICE_PRIO   	    (BT_TASK_PRIO + 1)
#endif

#ifndef BT_SCAN_PRIO
 #define BT_SCAN_PRIO           (BT_TASK_PRIO + 1)
#endif

#ifndef BT_QUEUE_PRIO
 #define BT_QUEUE_PRIO          (BT_TASK_PRIO - 1)
#endif

#define BLE_MAX_DEVICES         CONFIG_BTDM_CTRL_BLE_MAX_CONN

#ifndef BLE_POWER_LEVEL
 #define BLE_POWER_LEVEL        ESP_PWR_LVL_P9
#endif

#define BLE_CONN_RESULT       { "failed", "success", }
#define BLE_CONN_READY        ((1 << 24) - 1)

#ifdef DISABLE_DYNAMIC_DEVICES 
 #define DISABLE_DEVICE_CREATION
 #define DISABLE_DEVICE_PRUNING
 #define DISABLE_REMOTE_CONFIG
 #define DEFAULT_BLE_SCAN_MODE   BLE_DEVICE_STATIC
#else
 #define DEFAULT_BLE_SCAN_MODE   BLE_DEVICE_DYNAMIC
#endif

#define BLE_SVC_UUID_LONG      128
#define BLE_SVC_UUID_SHORT     16

#ifndef BLE_SVC_UUID_LEN
 #define BLE_SVC_UUID_LEN  BLE_SVC_UUID_LONG
#endif

#define BLE_MS_UNIT_1_25MS(x)       ((x * 1000) / 1250)
#define BLE_MS_UNIT_10MS(x)         ((x * 1000) / 10000)

#define BLE_INIT_PARAM_MIN_INT      BLE_MS_UNIT_1_25MS(50)
#define BLE_INIT_PARAM_MAX_INT      BLE_MS_UNIT_1_25MS(50)
#define BLE_INIT_PARAM_TIMEOUT      BLE_MS_UNIT_10MS(400)

#define BLE_CONN_PARAM_MIN_INT      BLE_MS_UNIT_1_25MS(650)
#define BLE_CONN_PARAM_MAX_INT      BLE_MS_UNIT_1_25MS(650)
#define BLE_CONN_PARAM_TIMEOUT      BLE_MS_UNIT_10MS(2600)

#define BLE_PARAM_UPDATE_INTVL      1000
#define BLE_PARAM_VERIFY_INTVL      100
#define BLE_PARAM_UPDATE_MAX_TRIES  6

#ifndef BLE_SVC_UUIDS
 #define BLE_SVC_UUIDS(SVC_UUID)   \
     SVC_UUID(SERVICE,  (0u))      \
     SVC_UUID(CHAR,     (0u))      \
     SVC_UUID(CONFIG,   (0u))      \
     SVC_UUID(VERSION,  (0u))      \
     SVC_UUID(PRESENCE, (0u))
#endif

#if BLE_SVC_UUID_LEN == BLE_SVC_UUID_SHORT
 #define ALLOC_UUID(name, val)       NimBLEUUID((uint16_t) val )
#elif BLE_SVC_UUID_LEN == BLE_SVC_UUID_LONG
 #define ALLOC_UUID(name, val)       NimBLEUUID( val ).to128()
#endif

#define ALLOC_UUIDS(...)     ALLOC_UUID(__VA_ARGS__),

#define BLE_UUID_NONE        ALLOC_UUID(NULL,0x0u)

#define BLE_BAS_SVC_UUID     (0x180fu)
#define BLE_BAS_CHR_UUID     (0x2a19u)

#define BLE_BAS_SVC     ALLOC_UUID(NULL, BLE_BAS_SVC_UUID)
#define BLE_BAS_CHR     ALLOC_UUID(NULL, BLE_BAS_CHR_UUID)

#define BLE_SVC_STRING(Name, val)   BUILD_STRING(Name)
#define UUID_ENUM(Name, val)        UUID_##Name,
typedef enum uuid { BLE_SVC_UUIDS(UUID_ENUM) } ble_svc_uuid_t;

/*!
    @enum ble_char_len_t
	@brief Possible GATT characteristic value types

 */
typedef enum BLE_CHAR_LEN {
	BLE_CHAR_LEN_NONE,
	BLE_CHAR_LEN_8BIT,
	BLE_CHAR_LEN_16BIT,
	BLE_CHAR_LEN_24BIT,
	BLE_CHAR_LEN_32BIT,
	BLE_CHAR_LEN_MAX,
} ble_char_len_t;

/*!
    @enum authstate_t
	@brief States associated with BLE authorization of a connection
 */
typedef enum AUTHSTATE {
	AUTH_NONE,
	AUTH_PENDING,
	AUTH_SUCCESS,
	AUTH_FAILED,
} authstate_t;

/*!
    @enum ble_state_t
	@brief Flags representing the current/desired state of our BLE tasks

 */
typedef enum BLE_STATE {
	BLE_NONE     = 0,
	BLE_SCANNING = (1 << 0),
	BLE_FINISHED = (1 << 1),
	BLE_READY    = (1 << 2),
	BLE_STOP     = (1 << 3),
	BLE_CONNECT  = (1 << 4),
	BLE_ALL      = 0xFF,
} ble_state_t;

/*!
    @enum scan_filter_t
	@brief Setting to specify HCI scan type
	
	Type of BLE scanning to use, which generally will correspond
	to DEVICE_DYNAMIC vs DEVICE_STATIC operation
 */
typedef enum SCAN_FILTER_MODE {
  SCAN_FILTER_NO_FILTER = BLE_HCI_SCAN_FILT_NO_WL,
  SCAN_FILTER_WHITELIST = BLE_HCI_SCAN_FILT_USE_WL,
  SCAN_FILTER_MODE_MAX,
} scan_filter_t;

/*!
    @enum ble_device_config_t 
	@brief The mode for managing devices

 */
typedef enum BLE_DEVICE_CONFIG {
  BLE_DEVICE_DYNAMIC = SCAN_FILTER_NO_FILTER,
  BLE_DEVICE_STATIC  = SCAN_FILTER_WHITELIST,
  BLE_DEVICE_CONFIG_MAX,
} ble_device_config_t;

/*!
    @enum ble_device_status_t 
	@brief BLE controller shared actions

	Flags representing the current state of gloabl resources
	as they pertain to device management tasks
 */
typedef enum BLE_DEVICE_STATE {
  DEVICE_NONE = 0,
  DEVICE_BLE  = (1 << 0),
  DEVICE_HTTP = (1 << 1),
  DEVICE_ALL  = 0xFF,
} ble_device_state_t;

class SecureClient;

/*!
    @struct ble_config_t 
	@brief Configuration of BLE operation

 */
typedef struct ble_config {
  ble_device_config_t device_mode;
} ble_config_t;

/*!
    @class SecureClient
	@brief Handling of a BLE connection

	This class represents and provides everything regarding
J	the BLE connection lifecycle of a defined device.
 */
class SecureClient {
	public:
	device_t				*device		= NULL;
	BLEClient				*client		= NULL;
	EventBits_t				mutexId		= -1;
    volatile authstate_t	authstate 	= AUTH_NONE;
	uint8_t					retries 	= 0; 
	unsigned long int		attempted   = 0;
	public:
	SecureClient(device_t *);
	void updateConnParams();
	uint8_t isConnected();
	int getRssi();

/*!
    @brief Take connection semaphore

	Exclusively operate on this connection handle
    @return uint8_t 
 */
	uint8_t		take();

/*!
    @brief Give back connection semaphore

	Release exclusive lock of this connection handle
    @return uint8_t 
 */
	uint8_t		give();

/*!
    @brief Initiate a connection with device

	Begins the GAP procedure for a BLE connection
    @return uint8_t 
 */
	uint8_t		connect();

/*!
    @brief Configure the device using BLE GATT 

	Use our configuration GATT characteristic to send the
	details of the  device configuration to specify the behavior
	of the connected device.
    @return uint8_t 
 */
	uint8_t		configure();

/*!
    @brief Close the BLE client connection

	Closes the client connecion and ensures all resource states
	are cleared
    @return uint8_t 
 */
	uint8_t		close();
};

/*!
    @struct queue_entry_t 

	Item representing a BLE notification callback, which we send
	to a queue from the callback handler.
 */
typedef struct queue_entry {
	esp_bd_addr_t  device_id;
	uint32_t       data;
} queue_entry_t;

extern const char *UUID_STRING[];

typedef	void		(*bt_queue_handler_t)(BLEAddress*, uint32_t*);
typedef	void		(*bt_conn_handler_t)(device_t*);
typedef	void		(*bt_device_mgmt_init_t)();
typedef void		(*ble_notify_cb_t)(BLERemoteCharacteristic*, uint8_t*, size_t, bool);
typedef	uint8_t		(*bt_device_init_cb_t)(char*);
typedef	uint8_t		(*bt_device_create_cb_t)(char*, sensor_type_t);
typedef	req_status_t	(*bt_device_config_cb_t)(device_t*);

/*!
	@brief Parse value from GATT and populate a payload entry

	@param entry ptr to payload entry to populate
	@param ble_data ptr to GATT value
*/
void	ble_parse_sensor_payload(sensor_data_entry_t*,  uint8_t*);

/*!
	@brief Register the task to process the GATT payload queue

	If there's a BLE service to accept incoming GATT payloads, there
	must be a consumer of the queue to process them
	@param handler bt_queue_handler_t
*/
void	bt_set_queue_handler(bt_queue_handler_t);

/*!
	@brief Register callback for GAP 'on_connect' event

	@param handler bt_conn_handler_t
*/
void	bt_set_connect_handler(bt_conn_handler_t);

/*!
	@brief Register callback for GAP 'on_disconnect' event

	@param handler bt_connt_handler_t
*/
void	bt_set_disconnect_handler(bt_conn_handler_t);

/*!
	@brief Register callback for GAP 'on_auth' event

	@param handler bt_connt_handler_t
*/
void	bt_set_auth_handler(bt_conn_handler_t);

/*!
	@brief Register callback for using external service for device information

	@param handler bt_device_config_cb_t
*/
void	bt_set_device_config_cb(bt_device_config_cb_t);

/*!
	@brief Register callback for initializing devices in another service

	@param handler bt_device_init_cb_t
*/
void	bt_set_device_init_cb(bt_device_init_cb_t);

/*!
	@brief Register callback for creating devices in another service

	@param handler bt_device_create_cb_t
*/
void	bt_set_device_create_cb(bt_device_create_cb_t);

/*!
	@brief Register callback on BLE scan mgr task init
    
	@param handler (bt_device_mgmt_init_t)
*/
void	bt_set_scan_mgr_init(bt_device_mgmt_init_t);

/*!
	@brief Register callback on BLE device mgmt task init

	@param handler (bt_device_mgmt_init_t)
*/
void	bt_set_device_mgmt_init(bt_device_mgmt_init_t);

/*!
	@brief Unlock BLE scanning task

	Used for task synchronization across peripherals
*/
void    bt_scan_enable();

/*!
	@brief Initialize BLE services

	@param device_mod ble_device_config_t
	Either SCAN_FILTER_NO_FILTER or SCAN_FILTER_WHITELIST
	to control scanning behavior of new advertisements.
	If not specified, the default is determined by the value
	of NO_DYNAMIC_DEVICES and will most likely be what you
	want.
*/
void      ble_init(ble_device_config_t config_mode=DEFAULT_BLE_SCAN_MODE);

/*!
	@brief Used to pre-define a device in STATIC config

	Chances are you'll only use this via `device_load_static_lic()`
	@param device_id The device to create
*/
void    ble_add_static_device(device_id_t);

/*!
    @brief Request the BLE device to provide update immediately
  
    @param device 
    @param sensor_index The devices index of the requested sensor type
    @return uint8_t 
*/
uint8_t   ble_request_attr_update(device_t*, uint8_t);

/*!
	@brief Callback handler (bt_queue_handler_t)

	Handles incoming BLE payloads in their native format and
	delivers them to the outgoing network after construct the
	appropriate device_data_t structure
	@param *addr BLEAddress from GATT payload
	@param *data uint32_t value from GATT
 */
void	  ble_sensor_network_queue(BLEAddress*, uint32_t*);

/*!
    @brief Update the BLE UUID's we expect from a device

	@param svc_uuid The type of UUID
	@param uuid The new value
 */
void ble_set_svc_uuid(ble_svc_uuid_t, NimBLEUUID);

uint8_t ble_request_enter_dfu(device_t*);
uint8_t bt_conn_check(device_t*);

#endif /* BLE_H_ */
