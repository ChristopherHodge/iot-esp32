
#ifndef BLE_H_
#define BLE_H_

#include "pgmspace.h"
#include "BLEDevice.h"
#include "BLESecurity.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"

#define BT_DEVICE_STACK_SZ     16384
#define BT_QUEUE_STACK_SZ      8192
#define BT_SCAN_STACK_SZ       4096

#define MAX_DEVICES            CONFIG_BTDM_CTRL_BLE_MAX_CONN
#define BLE_POWER_LEVEL        ESP_PWR_LVL_P9
#define DEVICE_ID_SZ		   20
#define BLE_CONN_TIMEOUT       8000
#define DEVICE_TIMEOUT         4 * 60 * 1000

#define BLE_CONN_RESULT       { "failed", "success", }
#define BLE_CONN_READY        (0xffffffUL)

typedef enum AUTHSTATE {
	AUTH_NONE,
	AUTH_PENDING,
	AUTH_SUCCESS,
	AUTH_FAILED,
} authstate_t;

typedef enum BLE_STATE {
	BLE_NONE     = 0,
	BLE_SCANNING = (1 << 0),
	BLE_FINISHED = (1 << 1),
	BLE_READY    = (1 << 2),
	BLE_STOP     = (1 << 3),
	BLE_CONNECT  = (1 << 4),
	BLE_ALL      = 0xFF,
} ble_state_t;

class SecureClient;

class MySecurity: public BLESecurityCallbacks {
	public:
	SecureClient *client;
	public:
	MySecurity() {};
	MySecurity(SecureClient*);
	uint32_t onPassKeyRequest();
	void onPassKeyNotify(uint32_t);
	bool onConfirmPIN(uint32_t);
	bool onSecurityRequest();
	void onAuthenticationComplete(esp_ble_auth_cmpl_t);
};

typedef struct device device_t;

class SecureClient {
	public:
	device_t *device = NULL;
	BLEClient *client = NULL;
	MySecurity security;
	EventBits_t mutexId = -1;
	volatile authstate_t authstate;
	public:
	SecureClient(device_t *);
	uint8_t take();
	uint8_t give();
	uint8_t connect();
	uint8_t configure();
	uint8_t close();
};

typedef char device_id_t[20];

typedef struct device {
	bool in_use;
	device_id_t id;
	sensor_t sensors[MAX_SENSORS];
	SecureClient *connection;
	unsigned long last_seen;
	uint16_t version;
	uint8_t device_id;
} device_t;

typedef struct queue_entry {
	esp_bd_addr_t device_id;
	uint32_t data;
} queue_entry_t;

typedef void (*bt_queue_handler_t)(BLEAddress*, uint32_t*);
void bt_set_queue_handler(bt_queue_handler_t);

void ble_init();
uint8_t updateVersion(device_t *);
uint8_t configureDevice(device_t*);
device_t* getDevice(BLEAddress*);
void bt_scan_start();

#endif /* BLE_H_ */
