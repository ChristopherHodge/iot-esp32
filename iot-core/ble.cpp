#include "sdkconfig.h"
#include "iot-common.h"
#include "sensor.h"
#include "smartthings.h"
#include "ble.h"

static const char *TAG = "ble";

NimBLEUUID	UUIDS[]       = { BLE_SVC_UUIDS(ALLOC_UUIDS) };
const char *UUID_STRING[] = { BLE_SVC_UUIDS(BLE_SVC_STRING) };

static BLEUUID *serviceUUID  = &UUIDS[UUID_SERVICE];
static BLEUUID *charUUID     = &UUIDS[UUID_CHAR];
static BLEUUID *configUUID   = &UUIDS[UUID_CONFIG];
static BLEUUID *versionUUID  = &UUIDS[UUID_VERSION];
static BLEUUID *presenceUUID = &UUIDS[UUID_PRESENCE];

static const scan_filter_t SCAN_FILTER_MODES[SCAN_FILTER_MODE_MAX] = {
  SCAN_FILTER_NO_FILTER, SCAN_FILTER_WHITELIST,
};

static volatile uint8_t                m_ble_conn_id_head    = 0;
static          bt_queue_handler_t     bt_queue_handler      = NULL;
static          bt_conn_handler_t      bt_connect_handler    = NULL;
static          bt_conn_handler_t      bt_disconnect_handler = NULL;
static			bt_conn_handler_t	   bt_auth_handler       = NULL;

static			bt_device_config_cb_t	bt_device_config_cb	= NULL;
static			bt_device_init_cb_t		bt_device_init_cb	= NULL;
static			bt_device_create_cb_t	bt_device_create_cb	= NULL;
static			bt_device_mgmt_init_t	bt_device_mgmt_init	= NULL;

static EventGroupHandle_t xDeviceState;
static EventGroupHandle_t xBLEState;
static EventGroupHandle_t xBLEConn;
static QueueHandle_t      xBLEDevice;
static QueueHandle_t      xUpdateQueue;
static TimerHandle_t      xBLEConnTimer;

static ble_config_t runtime;

static void nim_to_bd_addr(const uint8_t *nim_addr, esp_bd_addr_t *buf) {
	uint8_t len = sizeof(esp_bd_addr_t);
	uint8_t *ptr = (uint8_t*)buf;
	for(uint8_t i=0; i < len; i++) {
		ptr[(len-1)-i] = nim_addr[i];
	}
}

static void generic_ble_notify_cb(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  LOGD("notifyCB: %s: %d bytes", pChar->getUUID().toString().c_str(), length);
  if(pChar->getUUID().equals(*charUUID)) {
		esp_bd_addr_t addr;
		nim_to_bd_addr(pChar->getRemoteService()->getClient()->getPeerAddress().getNative(), &addr);

		queue_entry_t entry;
		memcpy(entry.device_id, &addr, sizeof(esp_bd_addr_t));

		switch(length) {
			case BLE_CHAR_LEN_8BIT: {
				entry.data = *pData;
			} break;

			case BLE_CHAR_LEN_16BIT: {
				entry.data = *(uint16_t*)pData;
			} break;

			case BLE_CHAR_LEN_32BIT: {
				entry.data = *(uint32_t*)pData;
			} break;
		}
		xQueueSend(xUpdateQueue, &entry, 1000 / portTICK_PERIOD_MS);
	}
}

static void ble_bas_notify_cb(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    const sensor_t *sensor = sensor_get_by_type(SENSOR_BATTERY);
	BLEAddress addr = pChar->getRemoteService()->getClient()->getPeerAddress();
	device_id_t device_id;
	memcpy(device_id, addr.toString().c_str(), sizeof(device_id_t));
	uint8_t level = *pData;
	LOGD("ble_bas_update: %s: batt_level = %d", device_id, level);
    device_send_update(device_id, sensor->id, level);
}

static bool ble_subscribe(BLEClient *pClient, BLEUUID service, BLEUUID characteristic, ble_notify_cb_t ble_notify_cb) {
	BLERemoteService *pService = pClient->getService(service);
	if (pService == nullptr) {
		LOGE("missing service UUID: %s", service.toString().c_str());
		return false;
	}

	BLERemoteCharacteristic *pChar = pService->getCharacteristic(characteristic);
	if (pChar == nullptr) {
		LOGE("missing characteristic UUID: %s", characteristic.toString().c_str());
		return false;
	}
	
	if(!pChar->canNotify()) {
		LOGE("unable to subscribe: %s: does not support notify", characteristic.toString().c_str());
		return false;
	}
	return pChar->subscribe(true, ble_notify_cb, false);
}

class MyClientCallback : public BLEClientCallbacks {
	public:
	SecureClient *client = NULL;
	
	MyClientCallback(SecureClient *_client) {
		this->client = _client;
	}
	
	void onConnect(BLEClient* pClient) {
		if(bt_connect_handler != NULL) {
			bt_connect_handler(this->client->device);
		}
	}

	void onDisconnect(BLEClient* pClient) {
		this->client->give();
		this->client->authstate = AUTH_NONE;
		LOGW("on_disconnect: %s: last_err %d", this->client->device->id, pClient->getLastError());
		if(bt_disconnect_handler != NULL) {
			bt_disconnect_handler(this->client->device);
		}
	}

	uint32_t onPassKeyRequest() {
		return (uint32_t)BLE_PASSKEY;
	}

	void onPassKeyNotify(uint32_t pass_key) {
		return;
	}

	bool onConfirmPIN(uint32_t pass_key) {
		vTaskDelay(AUTH_INPUT_DELAY);
		return BLE_PASSKEY == pass_key;
	}

	bool onSecurityRequest() {
		return true;
	}

	void onAuthenticationComplete(ble_gap_conn_desc *auth_cmpl) {
		const char *ptr =  BLEAddress(auth_cmpl->peer_id_addr).toString().c_str();
		if(!auth_cmpl->sec_state.authenticated) {
			this->client->authstate = AUTH_FAILED;
			LOGE("AUTH_FAILED: %s", ptr);
			return;
		}
		this->client->authstate = AUTH_SUCCESS;
		LOGD("AUTH_SUCCESS: %s", ptr);
		if(bt_auth_handler != nullptr) {
		    bt_auth_handler(this->client->device);
		}
	}
};

static uint8_t valid_ble_mutex_id(EventBits_t mutexId) {
	size_t nBits = (sizeof(EventBits_t)-1)*8;
	EventBits_t mask = 1;
	for(; nBits > 0; --nBits) {
		if(mutexId == (mask << (nBits-1))) {
			return true;
		}
	}
	return false;
}

static uint8_t wait_for_ble_mutex(device_t *device, uint16_t timeout = 9000) {
	char id[DEVICE_ID_SZ];
	if(device) {
		strncpy(id, device->id, sizeof(id));
	} else {
		strncpy(id, "ble_scan_mgr()", sizeof(id));
	}
	LOGD("%s: waiting for ble_mutex to be available", id);
	EventBits_t bits = xEventGroupWaitBits(xBLEConn, BLE_CONN_READY, false, true, timeout / portTICK_PERIOD_MS);
	if(bits != BLE_CONN_READY) {
		bits = (~(bits)) & BLE_CONN_READY;
		if(device) {
			LOGI("0x%06x: timeout (existing lock: 0x%06x)", device->connection->mutexId, bits);
		} else {
			LOGW("%s: timeout (existing lock: 0x%06x)", id, bits);
		}
		return false;
	}
	return true;
}

static void clear_ble_mutex() {
	LOGD("clearing all ble mutex locks (0x%06x)", (~(xEventGroupGetBits(xBLEConn)) & 0xffffff));
	xEventGroupSetBits(xBLEConn, BLE_CONN_READY);
}

static EventBits_t get_ble_mutex_id() {
	EventBits_t mutexId = (1 << m_ble_conn_id_head++);
	LOGD("assigning new id %d: lock_bits 0x%06x", m_ble_conn_id_head, mutexId);
	return mutexId;
}

static uint8_t get_ble_mutex_status(EventBits_t mutexId) {
	EventBits_t lock = xEventGroupGetBits(xBLEConn) & BLE_CONN_READY;
	lock = (~(lock)) & BLE_CONN_READY;
	return (!(lock & mutexId));
}

static uint8_t update_version(device_t *device) {
	BLEClient *client = device->connection->client;
	if(!client) {
		LOGW("n%s: error: no client connection", device->id);
		return false;
	}
	BLERemoteService *service = client->getService(*serviceUUID);
	if(!service) {
		LOGW("error: no service getting version");
		return false;
	}
	BLERemoteCharacteristic *versionChar = service->getCharacteristic(*versionUUID);
	if(!versionChar) {
		LOGW("%s: error: missing version characteristic", device->id);
		return false;
	}
	uint32_t vers = versionChar->readValue<uint32_t>();
	device->hw_rev = (uint16_t)((vers >> 16) & 0xffff);
	device->version = (uint16_t)((vers) & 0xffff);
	LOGD("%s: got: %d", device->id, device->version);
	return true;
}

SecureClient::SecureClient(device_t *device) {
	this->device = device;
	this->authstate = AUTH_NONE;
	this->mutexId = get_ble_mutex_id();
}

uint8_t SecureClient::take() {
	if(get_ble_mutex_status(this->mutexId)) {
		LOGD("lock is already held (0x%06x)", this->mutexId);
		return true;
	}
	if(valid_ble_mutex_id(this->mutexId) != true) {
		LOGE("take(): error: got an invalid mutex id! (0x%06x)", this->mutexId);
		return false;
	}
	LOGD("attempting to aquire mutexId 0x%06x", this->mutexId);
	if(xEventGroupWaitBits(xBLEConn, this->mutexId, true, true, 6000 / portTICK_PERIOD_MS)) {
		LOGD("acquired lock for id 0x%06x", this->mutexId);
	} else {
		LOGI("failed to acquire lock: id 0x%06x", this->mutexId);
		return false;
	}
	return true;
}

uint8_t SecureClient::give() {
	if(!get_ble_mutex_status(this->mutexId)) {
		return true;
	}
	if(valid_ble_mutex_id(this->mutexId) != true){
		LOGD("error: got an invalid mutex id! (0x%06x)", this->mutexId);
		return false;
	}
	LOGD("returning lock for id 0x%06x", this->mutexId);
	if((xEventGroupSetBits(xBLEConn, this->mutexId) & this->mutexId) == 0) {
		LOGD("failed to return lock: id 0x%06x", this->mutexId);
		return false;
	}
	return true;
}

uint8_t SecureClient::close() {
	if(this->client) { // && this->client->isConnected()) {
		if(get_ble_mutex_status(this->mutexId) || (wait_for_ble_mutex(this->device) && this->take())) {
			LOGD("disconnect(): %s", this->device->id);
			this->client->disconnect();
			this->client->deleteServices();
			if(!wait_for_ble_mutex(this->device, 8000)) {
				this->give();
			}
		} else {
			LOGD("failed to get lock: 0x%06x", this->mutexId);
			return false;
		}
	} else {
		this->give();
	}
	this->authstate = AUTH_NONE;
	return true;
}

uint8_t SecureClient::configure() {
	char cmd[4];
	uint32_t data;
	BLERemoteCharacteristic *characteristic = nullptr;
	BLERemoteService *service = nullptr;

	if(!this->client) return false;

	if(serviceUUID->equals(BLE_UUID_NONE)) {
		return true;
	}

	service = this->client->getService(*presenceUUID);
	if(service) {
		if(this->device->sensors && this->device->sensors[0].id == SENSOR_PRESENCE) {
		    if(ble_subscribe(this->client, BLE_BAS_SVC, BLE_BAS_CHR, ble_bas_notify_cb)) {
				return true;
			}
			LOGE("configure(): failed to subscribe to BAS service");
		} else {
			LOGE("configure(): device config is not a presence sensor");
		}
		return false;
	}

	service = this->client->getService(*serviceUUID);
	if(!service) {
		LOGE("configure(): missing service: %s", serviceUUID->toString().c_str());
		return false;
	}
	
#ifndef DISABLE_REMOTE_CONFIG

	characteristic = service->getCharacteristic(*configUUID);
	if(!characteristic) {
		LOGE("configure(): missing characteristic: %s", configUUID->toString().c_str());
		return false;
	}

	if(!this->client->isConnected()) {
		LOGE("configure(): not connected to client");
		return false;
	}
	
	if(!update_version(device)) {
		LOGE("configure(): updateVersion(): unable to read version characteristic");
		return false;
	}

#ifdef DFU_START_BEFORE
	if(device_update_needed(device, DFU_START_BEFORE)) {
		if(ble_request_enter_dfu(device)) {
			LOGI("ble_request_enter_dfu(): %s", device->id);
			return false;
		}
		LOGE("ble_request_enter_dfu(): failed to send command");
	}
#endif
	
	data = characteristic->readValue<uint32_t>();

	if(!data) {
		for(int i=0; i<MAX_SENSORS; i++) {
			sensor_t *sensor = &this->device->sensors[i];
			if(!sensor->in_use) {
			  continue;
            }
			data |= sensor->interface << i * MAX_SENSORS * 8;
			data |= sensor->id << ((i * MAX_SENSORS) + 1) * 8;
		}
		LOGI("sending config to device: 0x%08x", data);
		if(data) {
			memcpy(cmd, &data, sizeof(cmd));
            LOGD("sending: 0x%02x-0x%02x-0x%02x-0x%02x", cmd[0], cmd[1], cmd[2], cmd[3]);
			if(!characteristic->writeValue(cmd, false)) {
				LOGE("failed to write config characteristic");
				return false;
			}
		}
	} else {
#ifdef ALLOW_CONFIGURED_UNKNOWN_DEVICES
		memcpy(cmd, &data, sizeof(cmd));
		LOGD("device has existing config: 0x%02x-0x%02x-0x%02x-0x%02x", cmd[0], cmd[1], cmd[2], cmd[3]);
		for(uint8_t i=1; i<sizeof(cmd); i+=2) {
			if(cmd[i] && !this->device->sensors[i/2].in_use) {
				LOGI("updating sensor type to %d on device %s", cmd[i], this->device->id);
				device_add_sensor(this->device, (sensor_type_t)cmd[i]);
			}
		}
#endif
	}

#endif  // DISABLE_REMOTE_CONFIG

	if(charUUID->equals(BLE_UUID_NONE)) {
		return true;
	}

	if(!this->client->isConnected()) {
		LOGE("configure(): not connected to client");
		return false;
	}

	if(!ble_subscribe(this->client, *serviceUUID, *charUUID, generic_ble_notify_cb)) {
		LOGE("configure(): failed to subscribe to device - disconnecting");
		return false;
	} else {
		LOGD("successfully subscribed to characteristic updates: ");
		LOGD("service: %s / characteristic: %s", serviceUUID->toString().c_str(), charUUID->toString().c_str());
		return true;
	}

	return true;
}

uint8_t SecureClient::connect() {
	uint8_t ret = true;
	if(!this->client) {
		LOGD("adding new BLEClient to SecureClient instance");
		this->client = BLEDevice::createClient();
		this->client->setClientCallbacks(new MyClientCallback(this));
		this->client->setConnectTimeout(BLE_CONNECT_TIMEOUT_SECS);
	}
	LOGD("connect(): %s", this->device->id);
	if(!get_ble_mutex_status(this->mutexId)) {
		LOGE("you must hold the device mutex prior to connect()");
		return false;
	}

	if(bt_device_config_cb && this->device->sensors[0].in_use != true) {
		xEventGroupClearBits(xDeviceState, DEVICE_HTTP);
		switch(bt_device_config_cb(this->device)) {

			case HttpStatus_Ok: {
				ret = true;
			} break;

			case HttpStatus_NotFound: {
#ifndef DISABLE_DEVICE_CREATION
				if(this->device->sensors[0].id == SENSOR_NONE && bt_device_init_cb != NULL) {
					LOGI("attempting to initialize as a new device");
					ret = bt_device_init_cb(this->device->id);
				}
#endif
#ifdef CREATE_STATIC_DEVICES
				if(this->device->sensors[0].id != SENSOR_NONE && bt_device_create_cb != NULL) {
					LOGI("attempting to create device");
					ret = bt_device_create_cb(this->device->id, this->device->sensors[0].id);
				}
#endif
			} break;

			default:
				ret = false;
				break;
		}
		xEventGroupSetBits(xDeviceState, DEVICE_HTTP);
		if(!ret) {
			LOGW("bt_device_config_cb:(): error: unable to configure device");
			this->close();
			return ret;
		}
	}

#ifdef BLE_USE_CONN_PARAMS
		this->client->setConnectionParams(
			BLE_INIT_PARAM_MIN_INT, BLE_INIT_PARAM_MAX_INT, 0, BLE_INIT_PARAM_TIMEOUT
		);
#endif
	LOGD("ble_connect(): %s", this->device->id);
	this->authstate = AUTH_PENDING;
	xEventGroupClearBits(xDeviceState, DEVICE_BLE);
	ret = this->client->connect(BLEAddress(this->device->id, BLE_ADDR_RANDOM));
	xEventGroupSetBits(xDeviceState, DEVICE_BLE);
	if(!ret) {
		LOGW("ble_connect(): failed");
		this->close();
		return ret;
	}
	return true;
}

void SecureClient::updateConnParams() {
#ifdef BLE_USE_CONN_PARAMS
	LOGI("attempt to update connection params");
	uint8_t attempt = 0;
	while(this->client->isConnected() && (attempt++ <= BLE_PARAM_UPDATE_MAX_TRIES)) {
		this->client->updateConnParams(
			BLE_CONN_PARAM_MIN_INT, BLE_CONN_PARAM_MAX_INT, 0, BLE_CONN_PARAM_TIMEOUT
		);
		for(uint8_t i=0; i<=(BLE_PARAM_UPDATE_INTVL/BLE_PARAM_VERIFY_INTVL); i++) {
			vTaskDelay(BLE_PARAM_VERIFY_INTVL);
			if(this->client->getConnInfo().getConnTimeout() == BLE_CONN_PARAM_TIMEOUT) {
				LOGI("updateConnParams(): success");
				return;
			}
		}
		LOGI("updateConnParams(): retrying..");
	}
	LOGE("failed to update parameters: disconnecting.");
	this->close();
#endif
}

uint8_t SecureClient::isConnected() {
	if(!this->client) {
		return false;
	}
	return this->client->isConnected();
}

int SecureClient::getRssi() {
	if(!this->isConnected()) {
		return false;
	}
	return this->client->getRssi();
}

uint8_t bt_conn_check(device_t *device) {
	if(!(device->connection->retries && device->connection->attempted)) {
		return true;
	}
	if(device->connection->retries < SECURE_CONN_ATTEMPTS_TO_BACKOFF) {
		return true;
	}
	uint32_t elapsed = (MILLIS - device->connection->attempted);
	return (elapsed > (device->connection->retries * SECURE_CONN_FAIL_BACKOFF_MS));
}

static uint8_t client_count() {
#ifndef DISABLE_DEVICE_CREATION
	uint8_t count = 0;
    devices_t devices = get_devices();
	for(uint8_t i=0; i<MAX_DEVICES; i++) {
		if(devices.devices[i]->in_use) count++;
	}
	return count;
#else
  return 1;
#endif
}

static uint8_t ble_scan_start(BLEScan* pScan) {
	uint16_t cnt = client_count();
	if(cnt >= BLE_MAX_DEVICES) {
		LOGI("max clients reached (%d): skipping scan", BLE_MAX_DEVICES);
		return 99;
	}
	if(!wait_for_ble_mutex(0)) {
		LOGW("failed to get ble mutex");
		return false;
	}
	LOGD("resuming scan");
#ifndef DISABLE_CLIENT_BALANCING
	vTaskDelay(cnt * DELAY_S1);
#endif // DISABLE_CLIENT_BALANCING
    return pScan->start(0, nullptr, false);
}

static uint8_t bt_adv_check(NimBLEAddress *addr) {
	devices_t devices = get_devices();
	for(uint8_t i=0; i<MAX_DEVICES; i++) {
		if(devices.devices[i]->connection &&
				devices.devices[i]->connection->retries) {
			if(addr->equals(NimBLEAddress(devices.devices[i]->id))) {
				return bt_conn_check(devices.devices[i]);
			}
		}
	}
	return true;
}

class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
	void onResult(BLEAdvertisedDevice *advertisedDevice) {
		NimBLEAddress addr = advertisedDevice->getAddress();
		if(!bt_adv_check(&addr)) {
			LOGW("%s: ignored advertisement due to backoff", addr.toString().c_str());
			return;
		}
		device_id_t device_id;
		memcpy(device_id, addr.toString().c_str(), sizeof(device_id));
#ifdef BLE_ADV_DEBUG
		LOGD("scan: %02x / %s", advertisedDevice->getAddressType(), device_id);
		for(uint8_t i=0; i<advertisedDevice->getServiceUUIDCount(); i++) {
			LOGD("remote: %s", advertisedDevice->getServiceUUID(i).toString().c_str());
			LOGD("ours: %s", serviceUUID->toString().c_str());
		}
#endif // BLE_ADV_DEBUG
		if (advertisedDevice->haveServiceUUID() &&
				(advertisedDevice->isAdvertisingService(*serviceUUID) ||
				 advertisedDevice->isAdvertisingService(*presenceUUID))) {
#ifdef DISABLE_DEVICE_CREATION
            if(get_device(device_id) == nullptr) {
                LOGI("%s: unknown device, ignoring", device_id);
			    vTaskDelay(DELAY_S3);
                return;
            } 
#endif // DISABLE_DEVICE_CREATION
			xEventGroupSetBits(xBLEState, BLE_STOP);
			esp_bd_addr_t device;
			nim_to_bd_addr(advertisedDevice->getAddress().getNative(), &device);
			if(xQueueSend(xBLEDevice, &device, DELAY_S4) != pdTRUE) {
				xEventGroupClearBits(xBLEState, BLE_STOP);
			}
		}
	}
};

static void vConnTimerCB(TimerHandle_t xTimer) {
	device_t *device = (device_t*)pvTimerGetTimerID(xTimer);
	if(device) {
		LOGW("%s: conn timer expired: closing", device->id);
		if(xEventGroupGetBits(xDeviceState) == BLE_ALL) {
			LOGD("conn timer: waiting for device resources to free");
			xEventGroupWaitBits(xDeviceState, BLE_ALL, false, true, 4000);
		}
		device->connection->close();
	} else {
		LOGW("%s: timer expired but no device found!", device->id);
	}
}

static uint8_t handleAuthState(device_t *device) {
	while(get_ble_mutex_status(device->connection->mutexId)) {
		vTaskDelay(DELAY_S1);
		switch(device->connection->authstate) {
			case AUTH_PENDING: {
				continue;
			} break;
			
			case AUTH_FAILED: {
				LOGW("connect(): auth_failed: %s", device->id);
				device->connection->close();
				return false;
			} break;
			
			case AUTH_NONE: {
				LOGD("state reset to NONE: returning.");
				return false;
			} break;
			
			case AUTH_SUCCESS: {
				LOGD("%s: got SUCCESS", device->id);
				return true;
			} break;
			
			default: {
				device->connection->close();
				return false;
			} break;
		}
	}
	return false;
}

static uint8_t handleConnection(device_t *device) {
	if(!device) return false;

	if(!(wait_for_ble_mutex(device) && device->connection->take())) {
		LOGE("handleConnection(): %s: failed to get device mutex", device->id);
		return false;
	}

	vTimerSetTimerID(xBLEConnTimer, (void*)device);
	if(((device_t*)pvTimerGetTimerID(xBLEConnTimer))->device_id != device->device_id) {
		LOGW("vTimerID: mismatched id");
		return false;
	}

	if(xTimerStart(xBLEConnTimer, DELAY_S4) != pdTRUE) {
		LOGW("failed to start connection handler timer");
		return false;
	}

	LOGD("xBLEConnTimer: started / ticks remaining: %d",
			(xTimerGetExpiryTime(xBLEConnTimer) - xTaskGetTickCount()));

    esp_err_t ret;
	if((ret = device->connection->connect())) {
		LOGD("connect(): success");
		if(xTimerReset(xBLEConnTimer, DELAY_S4) != pdTRUE) {
			LOGW("xBLEConnTimer: failed to reset on connection.  it will likely expire");
		}
		LOGD("xBLEConnTimer: reset / new ticks remaining: %d",
				(xTimerGetExpiryTime(xBLEConnTimer) - xTaskGetTickCount()));

		if((ret = handleAuthState(device))) {
			LOGD("auth(): success");
			if(xTimerReset(xBLEConnTimer, DELAY_S4) != pdTRUE) {
				LOGW("xBLEConnTimer: failed to reset on connection.  it will likely expire");
			}
			LOGD("xBLEConnTimer: reset / new ticks remaining: %d",
					(xTimerGetExpiryTime(xBLEConnTimer) - xTaskGetTickCount()));

            if((ret = device->connection->configure())) {
				LOGD("configure(): success");
			} else {
				LOGE("configure(): failed");
			}
		} else {
			LOGE("auth(): failed");
		}
	} else {
		LOGE("connect(): failed");
	}

	LOGD("xBLEConnTimer: stopping / ticks remaining: %d",
			(xTimerGetExpiryTime(xBLEConnTimer) - xTaskGetTickCount()));

	xTimerStop(xBLEConnTimer, DELAY_S4);
	if(ret) {
		device->connection->give();
	} else {
		device->connection->close();
	}
	return ret;
}

static void ble_configure_scan(BLEScan *pScan, BLEAdvertisedDeviceCallbacks *pAdvCB) {
	esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, BLE_POWER_LEVEL);
	esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, BLE_POWER_LEVEL);
	esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN ,BLE_POWER_LEVEL);

	pScan->setAdvertisedDeviceCallbacks(pAdvCB);
	pScan->setInterval(1349);
	pScan->setWindow(449);
	pScan->setActiveScan(true);
}

static void ble_configure_security(BLESecurity *pSecurity) {
  BLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_MITM);
  BLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
  BLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
}

static void bt_scan_mgr(void *ptx) {
    EventBits_t evt;
	uint8_t scan_fail_cnt = 0;
	uint8_t err;

    BLEScan *pScan = BLEDevice::getScan();
	BLEAdvertisedDeviceCallbacks *pAdvCB = new MyAdvertisedDeviceCallbacks();
    BLESecurity mSecurity;
    BLESecurity *pSecurity = &mSecurity;

	esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
    ble_configure_scan(pScan, pAdvCB);
    ble_configure_security(pSecurity);

    scan_filter_t scan_filter = \
        (scan_filter_t)runtime.device_mode;

    switch(scan_filter) {
        case SCAN_FILTER_WHITELIST: {
            LOGI("setting whitelist only scanning");
	        BLEDevice::getAdvertising()->setScanFilter(true,true);
	        //pScan->setFilterPolicy(scan_filter);
        } break;

        case SCAN_FILTER_NO_FILTER: {
            LOGI("setting promiscuous scanning");
	        BLEDevice::getAdvertising()->setScanFilter(false,false);
        } break;

        default:
            break;
    }

	for (;;) {
		STACK_STATS
		LOGD("xBLEState: waiting for READY");
		evt = xEventGroupWaitBits(xBLEState, BLE_READY, false, false, 60000);
		if (evt & BLE_READY) {
			LOGD("xBLEState->READY");
		} else {
			LOGW("xBLEState->TIMEOUT: timeout waiting for READY, going ahead anyway");
			delete(pAdvCB);
			pAdvCB = new MyAdvertisedDeviceCallbacks();
			ble_configure_scan(pScan, pAdvCB);
		}
		xEventGroupClearBits(xBLEState, BLE_ALL);
		xEventGroupSetBits(xBLEState, BLE_SCANNING);
		vTaskDelay(DELAY_S2);
		LOGI("xBLEState->SCANNING");
		if(!(err = ble_scan_start(pScan))) {
			LOGE("failed to start scan, we'll try again..");
			if(err != 99 && scan_fail_cnt++ > BLE_SCAN_FAIL_THRESH) {
				rtc_reset();
			}
			continue;
		}
		scan_fail_cnt = 0;
		evt = xEventGroupWaitBits(xBLEState, BLE_FINISHED | BLE_STOP, true, false, 60000);
		pScan->stop();
		if (evt & BLE_STOP) {
			LOGI("xBLEState->STOP: scan stopped");
		    	vTaskDelay(DELAY_S0);
			xEventGroupClearBits(xBLEState, BLE_ALL);
		} else {
			LOGI("xBLEState->FINISHED: scan timeout");
		    	vTaskDelay(DELAY_S0);
			xEventGroupSetBits(xBLEState, BLE_READY);
		}
		vTaskDelay(DELAY_S0);
	}
}

static void bt_device_mgr(void *ptx) {
    xBLEDevice = xQueueCreate(1, sizeof(esp_bd_addr_t));
	xDeviceState = xEventGroupCreate();

	esp_bd_addr_t newDevice;
	uint8_t client_cnt = 0;
	BLEAddress m_ble_address(newDevice);
	BLEAddress *address = &m_ble_address;
	char *result[] = BLE_CONN_RESULT;
	EventBits_t evt;

	if(bt_device_mgmt_init != nullptr) {
		bt_device_mgmt_init();
	}
	
	xBLEConnTimer = xTimerCreate("xBLEConnTimer", SECURE_CONN_TIMEOUT_MS, pdFALSE, (void*)0, vConnTimerCB);

	for(;;) {
    STACK_STATS
		if(xQueueReceive(xBLEDevice, &newDevice, portMAX_DELAY) != pdTRUE) {
			xEventGroupWaitBits(xBLEState, BLE_SCANNING | BLE_READY, false, false, DELAY_S4);
			continue;
		}
	HEAP_BEGIN(new_device);
		address = new (&m_ble_address) BLEAddress(newDevice);
    	device_id_t device_id;
    	device_t *device;  
    	strncpy(device_id, address->toString().c_str(), DEVICE_ID_SZ-1);
		LOGI("xQueueRecieve(xNewDevice): %s", device_id);

		vTaskDelay(client_cnt * DELAY_S1);

		if(client_cnt >= MAX_DEVICES) {
			LOGI("max_clients exceeded: ignoring conn request");
		} else {
			xEventGroupSetBits(xDeviceState, DEVICE_ALL);
			if((device = get_device(device_id)) == nullptr) {
				if((device = create_device(device_id)) == nullptr) {
          			LOGE("failed to find/create a device for %s", device_id);
					xEventGroupClearBits(xBLEState, BLE_ALL);
					xEventGroupSetBits(xBLEState, BLE_READY);
          			continue;
        		}
        	LOGD("new device created for %s", device_id);
      		} else {
        		LOGD("found existing device for %s", device_id);
        		if(device->connection) {
          			device->connection->close();
        		}
      		} 
			if(!bt_conn_check(device)) {
				LOGI("device too soon: backing off");
				vTaskDelay(DELAY_S1);
			} else {
				LOGI("passing connection to handleConnection()");
				uint8_t res = handleConnection(device);
				LOGI("handleConnection(): %s", result[res]);
				if(!res) {
					LOGW("cleanup failed client connection");
					device->connection->close();
		    		vTaskDelay(DELAY_S5);
					device->connection->retries++;
					device->connection->attempted = MILLIS;
				} else {
					device->connection->retries = 0;
					device->connection->attempted = 0;
					device->connection->updateConnParams();
				}
			}
		}
		xEventGroupClearBits(xBLEState, BLE_ALL);
		client_cnt = client_count();

	HEAP_END(new_device);
	
		vTaskDelay(DELAY_S0);
		xEventGroupSetBits(xBLEState, BLE_READY);
	}
}

static void bt_queue_mgr(void *ptx) {
	size_t len = sizeof((queue_entry_t){{},0,});
	LOGD("xUpdateQueue size: %d", len);
    xUpdateQueue = xQueueCreate(BLE_UPDATE_QUEUE_SZ, len);

	queue_entry_t entry;
	BLEAddress m_ble_address(entry.device_id);
	BLEAddress *address = &m_ble_address;

	for(;;) {
    STACK_STATS
		if(xQueueReceive(xUpdateQueue, &entry, portMAX_DELAY) != pdTRUE) {
			continue;
		}
	HEAP_BEGIN(send_update);
		xEventGroupWaitBits(xBLEState, BLE_SCANNING | BLE_READY, false, false, 10000);
		address = new (&m_ble_address) BLEAddress(entry.device_id);
		LOGD("processing ble payload: %s", address->toString().c_str());
		bt_queue_handler(address, &entry.data);
	HEAP_END(send_update);
		vTaskDelay(DELAY_S0);
	}
}

void ble_parse_sensor_payload(sensor_data_entry_t *entry,  uint8_t *ble_data) {
	uint8_t idx = 0;
	uint8_t sensor_id = ble_data[idx++];
	sensor_type_t sensor_type = (sensor_type_t)ble_data[idx++];

	sensor_payload_entry_id(*entry, sensor_id, sensor_type);
	attribute_t type_name;
	sensor_type_get_name(sensor_type, type_name);

	uint16_t val = (ble_data[idx] << 8) | ble_data[idx+1];

	sensor_payload_entry_attr(*entry, type_name, VAL_U16, (void*)&val);
}

uint8_t ble_request_attr_update(device_t *device, uint8_t sensor_index) {
	if(!device->connection ||
			!device->connection->client ||
			!device->connection->client->isConnected()) {
		LOGE("%s: device does not map to client", device->id);
		return false;
	}

	LOGI("sending request for BLE sensor update");
	if(!wait_for_ble_mutex(device)) {
         LOGE("timed out waiting for mutex");
		 return false;
	}

	BLERemoteService *p_svc = device->connection->client->getService(*serviceUUID);
	BLERemoteCharacteristic *p_char = p_svc->getCharacteristic(*configUUID);

	if(p_char == nullptr) {
		LOGE("%s: unable to obtain service characteristic for configUUID", device->id);
		return false;
	}

	uint8_t cmd[4] { 0xff, 0xff, 0x01, sensor_index, };
	p_char->writeValue(cmd, sizeof(cmd), false);
	LOGD("sends: 0x%02x-0x%02x-0x%02x-0x%02x", cmd[0], cmd[1], cmd[2], cmd[3]);
	return true;
}

uint8_t ble_request_enter_dfu(device_t *device) {
	if(!device->connection ||
			!device->connection->client ||
			!device->connection->client->isConnected()) {
		LOGE("%s: device does not map to client", device->id);
		return false;
	}

	LOGI("sending request for BLE sensor update");
	if(!wait_for_ble_mutex(device)) {
         LOGE("timed out waiting for mutex");
		 return false;
	}

	BLERemoteService *p_svc = device->connection->client->getService(*serviceUUID);
	BLERemoteCharacteristic *p_char = p_svc->getCharacteristic(*configUUID);

	if(p_char == nullptr) {
		LOGE("%s: unable to obtain service characteristic for configUUID", device->id);
		return false;
	}

	uint8_t cmd[4] { 0xff, 0xff, 0xfe, 0x0, };
	p_char->writeValue(cmd, sizeof(cmd), false);
	LOGD("sends: 0x%02x-0x%02x-0x%02x-0x%02x", cmd[0], cmd[1], cmd[2], cmd[3]);
	return true;
}

void bt_scan_enable() {
	LOGI("Enabling scanning: xBLEState->BLE_READY");
	xEventGroupSetBits(xBLEConn, BLE_CONN_READY);
	xEventGroupSetBits(xBLEState, BLE_READY);
}

void ble_add_static_device(device_id_t device_id) {
	char *p = device_id;
    LOGI("adding %s to ble_scan whitelist", p);
    if(!BLEDevice::whiteListAdd(BLEAddress(p))) {
        LOGE("failed to update whitelist");
    }
}

void ble_init(ble_device_config_t device_mode) {
    runtime.device_mode = device_mode;

    esp_log_level_set("NimBLE",                     BLE_LOG_LEVEL);
    esp_log_level_set("NimBLEScan",                 BLE_LOG_LEVEL);
    esp_log_level_set("NimBLERemoteCharacteristic", BLE_LOG_LEVEL);

    xBLEState  = xEventGroupCreate();
    xBLEConn   = xEventGroupCreate();

    BLEDevice::init("NimBLE");
    esp_bt_sleep_disable();

    xTaskCreatePinnedToCore(bt_device_mgr, "bt_device_mgr", \
        BT_DEVICE_STACK_SZ, NULL, BT_DEVICE_PRIO, NULL, 1);
    xTaskCreatePinnedToCore(bt_queue_mgr, "bt_queue_mgr", \
        BT_QUEUE_STACK_SZ, NULL, BT_QUEUE_PRIO, NULL, 1);
    xTaskCreatePinnedToCore(bt_scan_mgr, "bt_scan_mgr", \
        BT_SCAN_STACK_SZ, NULL, BT_SCAN_PRIO, NULL, 1);

	vTaskDelay(500);
}

void bt_set_queue_handler(bt_queue_handler_t handler) {
	bt_queue_handler = handler;
}

void bt_set_connect_handler(bt_conn_handler_t handler) {
    bt_connect_handler = handler;
}

void bt_set_disconnect_handler(bt_conn_handler_t handler) {
    bt_disconnect_handler = handler;
}

void bt_set_auth_handler(bt_conn_handler_t handler) {
	bt_auth_handler = handler;
}

void bt_set_device_config_cb(bt_device_config_cb_t handler) {
	bt_device_config_cb = handler;
}

void bt_set_device_init_cb(bt_device_init_cb_t handler) {
	bt_device_init_cb = handler;
}

void bt_set_device_create_cb(bt_device_create_cb_t handler) {
	bt_device_create_cb = handler;
}

void bt_set_device_mgmt_init(bt_device_mgmt_init_t handler) {
    bt_device_mgmt_init = handler;
}

void bt_set_scan_mgr_init(bt_device_mgmt_init_t handler) {
    bt_device_mgmt_init = handler;
}

void ble_set_svc_uuid(ble_svc_uuid_t svc_uuid, NimBLEUUID uuid) {
	memcpy(&UUIDS[svc_uuid], &uuid, sizeof(UUIDS[0]));
}

void ble_sensor_network_queue(BLEAddress *addr, uint32_t *data) {
	uint8_t ble_data[4];;
	device_id_t device_id;

	memcpy(device_id, addr->toString().c_str(), sizeof(device_id_t));
	memcpy(ble_data, data, sizeof(uint32_t));

	const uint8_t num_entries = 1;
	device_data_t  *payload = device_payload_init((char*)device_id, num_entries);

	for(uint8_t i=0; i<num_entries; i++) {
		sensor_data_entry_t entry = device_payload_get_entry(payload, i);
		LOGD("fetched entry %d from payload", i);
		ble_parse_sensor_payload(&entry, ble_data);
		LOGD("parsed ble data into entry: value attribute is %s", entry.value->attribute);
	}

	if(!network_queue_payload(payload)) {
		LOGE("failed to queue payload: free() allocation");
		device_payload_free(payload);
	}
}
