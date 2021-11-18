#include <HardwareSerial.h>
#include "Esp.h"
#include "sdkconfig.h"
#include "sensor.h"
#include "network.h"
#include "ble.h"
#include "smartthings.h"
#include "app.h"

static const PROGMEM BLEUUID serviceUUID("e54B1d11-67f4-479e-8711-b3b99191ce6c");
static const PROGMEM BLEUUID charUUID("e54B1d12-67f4-479e-8711-b3b99191ce6c");
static const PROGMEM BLEUUID configUUID("e54B1d13-67f4-479e-8711-b3b99191ce6c");
static const PROGMEM BLEUUID versionUUID("e54B1d14-67f4-479e-8711-b3b99191ce6c");

static volatile uint8_t m_ble_conn_id_head = 0;
static bt_queue_handler_t bt_queue_handler;

static TimerHandle_t      xBLEConnTimer;
static EventGroupHandle_t xBLEState;
static EventGroupHandle_t xBLEConn;
static QueueHandle_t      xBLEDevice;
static QueueHandle_t      xQueue;

device_t *DEVICES[MAX_DEVICES];

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
	if(pBLERemoteCharacteristic->getUUID().equals(charUUID)) {
		esp_bd_addr_t *addr = pBLERemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().getNative();
		queue_entry_t entry;
		memcpy(entry.device_id, addr, sizeof(esp_bd_addr_t));
		entry.data = *(uint32_t*)pData;
		xQueueSend(xQueue, &entry, 1000 / portTICK_PERIOD_MS);
	}
}

bool subscribeEvents(BLEClient *pClient, BLEUUID service, BLEUUID characteristic) {
	BLERemoteService* pRemoteService = pClient->getService(service);
	if (pRemoteService == nullptr) {
		Serial.print("Failed to find our service UUID: ");
		pClient->disconnect();
		return false;
	}

	BLERemoteCharacteristic *pRemoteCharacteristic = pRemoteService->getCharacteristic(characteristic);
	if (pRemoteCharacteristic == nullptr) {
		Serial.print("Failed to find our characteristic UUID: ");
		pClient->disconnect();
		return false;
	}
	
	if(pRemoteCharacteristic->canNotify())
	pRemoteCharacteristic->registerForNotify(notifyCallback);

	return true;
}


MySecurity::MySecurity(SecureClient *client) {
	this->client = client;
}

uint32_t MySecurity::onPassKeyRequest() {
	uint32_t passkey = 122481;
	return passkey;
}

void MySecurity::onPassKeyNotify(uint32_t pass_key) {
	return;
}

bool MySecurity::onConfirmPIN(uint32_t pass_key) {
	vTaskDelay(5000);
	return true;
}

bool MySecurity::onSecurityRequest() {
	return true;
}

void MySecurity::onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl) {
	const char *ptr =  BLEAddress(auth_cmpl.bd_addr).toString().c_str();
	if(!auth_cmpl.success) {
		this->client->authstate = AUTH_FAILED;
		Serial.printf("AUTH_FAILED: %s\n", ptr);
		return;
	}
	this->client->authstate = AUTH_SUCCESS;
	Serial.printf("AUTH_SUCCESS: %s\n", ptr);
	Serial.printf("evt_addr: %s / client_addr: %s\n", ptr, this->client->device->id);
}

class MyClientCallback : public BLEClientCallbacks {
	public:
	SecureClient *client = NULL;
	
	MyClientCallback(SecureClient *_client) {
		this->client = _client;
	}
	
	void onConnect(BLEClient* pClient) {
	}

	void onDisconnect(BLEClient* pClient) {
		this->client->give();
	}
};

uint8_t valid_ble_mutex_id(EventBits_t mutexId) {
	size_t nBits = (sizeof(EventBits_t)-1)*8;
	EventBits_t mask = 1;
	for(; nBits > 0; --nBits) {
		if(mutexId == (mask << (nBits-1))) {
			return true;
		}
	}
	return false;
}

uint8_t wait_for_ble_mutex(device_t *device, uint16_t timeout = 10000) {
	#ifdef DEBUG
	char id[sizeof((((device_t){}).id))];
	if(device) {
		strncpy(id, device->id, sizeof(id));
	} else {
		strncpy(id, "ble_scan_mgr()", sizeof(id));
	}
	Serial.printf("wait_for_ble_mutex(): %s: waiting for ble_mutex to be available\n", id);
	#endif
	EventBits_t bits = xEventGroupWaitBits(xBLEConn, BLE_CONN_READY, false, true, timeout / portTICK_PERIOD_MS);
	if(bits != BLE_CONN_READY) {
		bits = (~(bits)) & BLE_CONN_READY;
		if(device) {
			Serial.printf("wait_for_ble_mutex(): 0x%06x: timeout (existing lock: 0x%06x)\n", device->connection->mutexId, bits);
			#ifdef DEBUG
		} else {
			Serial.printf("wait_for_ble_mutex(): %s: timeout (existing lock: 0x%06x)\n", id, bits);
			#endif
		}
		return false;
	}
	return true;
}

void clear_ble_mutex() {
	#ifdef DEBUG
	Serial.printf("clear_ble_mutex(): clearing all ble mutex locks (0x%06x)\n", (~(xEventGroupGetBits(xBLEConn)) & 0xffffff));
	#endif
	xEventGroupSetBits(xBLEConn, BLE_CONN_READY);
}

EventBits_t get_ble_mutex_id() {
	EventBits_t mutexId = (1 << m_ble_conn_id_head++);
	#ifdef DEBUG
	Serial.printf("get_ble_mutex_id(): assigning new id %d: lock_bits 0x%06x\n", m_ble_conn_id_head, mutexId);
	#endif
	return mutexId;
}

uint8_t get_ble_mutex_status(EventBits_t mutexId) {
	EventBits_t lock = xEventGroupGetBits(xBLEConn) & BLE_CONN_READY;
	lock = (~(lock)) & BLE_CONN_READY;
	return (lock & mutexId);
}

SecureClient::SecureClient(device_t *device) {
	this->device = device;
	this->security.client = this;
	this->authstate = AUTH_NONE;
	this->mutexId = get_ble_mutex_id();
}

uint8_t SecureClient::take() {
	if(get_ble_mutex_status(this->mutexId)) {
		#ifdef DEBUG
		printf("take(): lock is already held (0x%06x)\n", this->mutexId);
		#endif
		return true;
	}
	if(valid_ble_mutex_id(this->mutexId) != true) {
		Serial.printf("take(): error: got an invalid mutex id! (0x%06x)\n", this->mutexId);
		return false;
	}
	#ifdef DEBUG
	Serial.printf("take(): attempting to aquire mutexId 0x%06x\n", this->mutexId);
	#endif
	if(xEventGroupWaitBits(xBLEConn, this->mutexId, true, true, 6000 / portTICK_PERIOD_MS)) {
		Serial.printf("acquired lock for id 0x%06x\n", this->mutexId);
		} else {
		Serial.printf("take(): failed to acquire lock: id 0x%06x\n", this->mutexId);
		return false;
	}
	return true;
}

uint8_t SecureClient::give() {
	if(!get_ble_mutex_status(this->mutexId)) {
		return true;
	}
	if(valid_ble_mutex_id(this->mutexId) != true){
		Serial.printf("give(): error: got an invalid mutex id! (0x%06x)\n", this->mutexId);
		return false;
	}
	Serial.printf("give(): returning lock for id 0x%06x\n", this->mutexId);
	if((xEventGroupSetBits(xBLEConn, this->mutexId) & this->mutexId) == 0) {
		#ifdef DEBUG
		Serial.printf("give(): failed to return lock: id 0x%06x\n", this->mutexId);
		#endif
		return false;
	}
	return true;
}

uint8_t SecureClient::close() {
	if(this->client && this->client->isConnected()) {
		if(get_ble_mutex_status(this->mutexId) || (wait_for_ble_mutex(this->device) && this->take())) {
			#ifdef DEBUG
			Serial.printf("close(): disconnect(): %s\n", this->device->id);
			#endif
			this->client->disconnect();
			if(!wait_for_ble_mutex(this->device, 8000)) {
				this->give();
			}
		} else {
			#ifdef DEBUG
			Serial.printf("close(): failed to get lock: 0x%06x\n", this->mutexId);
			#endif
			return false;
		}
	} else {
		this->give();
	}
	this->authstate = AUTH_NONE;
	return true;
}

void scanComplete(BLEScanResults) { return; };

uint8_t SecureClient::configure() {
	if(!this->client) return false;

	BLERemoteService *service = this->client->getService(serviceUUID);
	if(!service) {
		Serial.println("configure(): missing service - abort connect config");
		this->close();
		return false;
	}
	
	BLERemoteCharacteristic *characteristic = service->getCharacteristic(configUUID);
	if(!characteristic) {
		Serial.println("configure(): missing characteristic - abort connect config");
		this->close();
		return false;
	}

	if(!this->client->isConnected()) {
		Serial.println("configure(): not connected to client");
		this->close();
		return false;
	}
	
	if(!updateVersion(device)) {
		Serial.println("configure(): updateVersion(): unable to read version characteristic");
		this->close();
		return false;
	}
	
	uint32_t data = characteristic->readUInt32();
	
	if(((data >> 24) & 0xff) == 12 || ((data >> 8) & 0xff) == 12) {
		if(((data >> 16) & 0xff) != 0 || (data & 0xff) != 0) {
			data = 0x10C;
			characteristic->writeValue((uint8_t*)&data, sizeof(data));
			return this->close();
		}
	}

	if(!data) {
		Serial.println("configure(): empty config: sending config to device");
		for(int i=0; i<MAX_SENSORS; i++) {
			sensor_t *sensor = &this->device->sensors[i];
			if(!sensor->in_use)
			continue;
			data |= sensor->interface << i * MAX_SENSORS * 8;
			data |= sensor->id << ((i * MAX_SENSORS) + 1) * 8;
		}
		if(data) {
			Serial.printf("configure(): SEND_CONFIG: 0x%04x\n", data);
			characteristic->writeValue((uint8_t*)&data, sizeof(data));
			this->close();
			return true;
		}
	}

	if(!this->client->isConnected()) {
		Serial.println("configure(): not connected to client");
		this->close();
		return false;
	}
	
	if(!subscribeEvents(this->client, serviceUUID, charUUID)) {
		Serial.println("configure(): failed to subscribe to device - disconnecting");
		this->close();
		return false;
	}
	
	this->give();
	return true;
}

uint8_t SecureClient::connect() {
	if(!this->client) {
		BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
		//BLEDevice::setCustomGattcHandler((gattc_event_handler)&local_gattc_handler);
		this->client = BLEDevice::createClient();
		this->client->setClientCallbacks(new MyClientCallback(this));
	}
	Serial.printf("connect(): %s\n", this->device->id);
	if(!get_ble_mutex_status(this->mutexId) && \
	!(wait_for_ble_mutex(this->device) && this->take())) {
		return false;
	}
	if(this->device->sensors[0].in_use == false) {
		if(!configureDevice(this->device)) {
			Serial.printf("configureDevice(): error: unable to create device\n");
			this->close();
			return false;
		}
	}

	uint16_t conn_id = this->client->getConnId();
	std::map<uint16_t, conn_status_t> peers = BLEDevice::getPeerDevices(true);
	if(peers.find(conn_id) != peers.end()) {
		Serial.printf("connect(): have duplicate client conn_id: attempting to disconnect both\n");
		BLEClient *p_client = BLEDevice::getClientByGattIf(conn_id);
		p_client->disconnect();
		this->close();
		return false;
	}

	Serial.printf("ble_connect(): %s\n", this->device->id);
	this->authstate = AUTH_PENDING;
	BLEDevice::setSecurityCallbacks((BLESecurityCallbacks*)&this->security);
	if(!this->client->connect(BLEAddress(this->device->id), BLE_ADDR_TYPE_RANDOM)){
		Serial.println("ble_connect(): failed");
		this->close();
		return false;
	}
	return true;
}

device_t* getDevice(BLEAddress *deviceId) {
	device_t *device;
	uint16_t ret_code;
	
	for(uint8_t i=0; i<MAX_DEVICES; i++) {
		device = DEVICES[i];
		if(deviceId->equals(BLEAddress(device->id))) {
			return device;
		}
	}
	return nullptr;
}


device_t create_device(uint8_t device_id) {
	device_t device = { .in_use = false, .id = { '\0', }, .sensors = { INVALID_SENSOR, },
			.connection = NULL, .last_seen = 0, .version = 0, .device_id = device_id, };
	return device;
}

void deleteDevice(device_t *device) {
	if(!device->connection->close()) {
		Serial.printf("deleteDevice(): unable to delete conn: %s\n", device->id);
		return;
	}
	
	device_t new_device = create_device(device->device_id);
	new_device.connection = device->connection;
	*device = new_device;
}

uint8_t displayClients() {
	uint8_t i, count = 0;
	device_t *device;
	char *msg = (char*)malloc(2048);
	char *pos = msg;

	#ifdef UART_MASTER
	char type[] = "master";
	#elif defined(UART_SLAVE)
	char type[] = "slave";
	#endif

	pos += sprintf(pos, "-----%s-----\n", type);
	pos += sprintf(pos, "* HEAP FREE: %d\n", ESP.getFreeHeap());
	for(i=0; i<MAX_DEVICES; i++) {
		device = DEVICES[i];
		if(device->in_use) {
			count++;
			pos += sprintf(pos, "conn %d: %s", i, device->id);
			if(device->version) {
				pos += sprintf(pos, "  (v%d)\n", device->version);
				} else {
				pos += sprintf(pos, "\n");
			}
		}
	}
	sprintf(pos, "---------------\n");
	
	Serial.print(msg);
	free(msg);
	return count;
}

void pruneClients() {
	device_t *device;
	for(uint8_t i=0; i<MAX_DEVICES; i++) {
		device = DEVICES[i];
		if(device->in_use && device->last_seen) {
			if(millis() > (device->last_seen + DEVICE_TIMEOUT)) {
				Serial.printf("pruneClients(): DELETE: %s\n", device->id);
				deleteDevice(device);
				vTaskDelay(250 / portTICK_RATE_MS);
				break;
			}
		}
	}
}

uint8_t updateVersion(device_t *device) {
	BLEClient *client = device->connection->client;
	if(!client) {
		Serial.printf("updateVersion(): %s: error: no client connection\n", device->id);
		return false;
	}
	BLERemoteService *service = client->getService(serviceUUID);
	if(!service) {
		Serial.println(": error: no service getting version");
		return false;
	}
	BLERemoteCharacteristic *versionChar = service->getCharacteristic(versionUUID);
	if(!versionChar) {
		Serial.printf("updateVersion(): %s: error: missing version characteristic\n", device->id);
		return false;
	}
	device->version = versionChar->readUInt16();
	Serial.printf("updateVersion(): %s: got: %d\n", device->id, device->version);
	return true;
}

uint8_t client_count() {
	device_t *device;
	uint8_t count = 0;
	for(uint8_t i=0; i<MAX_DEVICES; i++) {
		device = DEVICES[i];
		if(device->in_use) count++;
	}
	return count;
}

uint8_t ble_scan_start(BLEScan *pScan) {
	uint16_t cnt = client_count();
	if(cnt >= MAX_DEVICES) {
		Serial.printf("ble_scan_mgr(): max clients reached (%d): skipping scan\n", MAX_DEVICES);
		return false;
	}
	if(!wait_for_ble_mutex(0)) {
		Serial.printf("ble_scan_mgr(): failed to get ble mutex\n");
		return false;
	}
	Serial.println("ble_scan_mgr(): resuming scan");
	vTaskDelay((cnt * 200) / portTICK_RATE_MS);
	return pScan->start(0, &scanComplete, false);
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
	void onResult(BLEAdvertisedDevice advertisedDevice) {
		if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
			xEventGroupSetBits(xBLEState, BLE_STOP);
			esp_bd_addr_t *device = advertisedDevice.getAddress().getNative();
			if(xQueueSend(xBLEDevice, device, 1000 / portTICK_RATE_MS) != pdTRUE) {
				xEventGroupClearBits(xBLEState, BLE_STOP);
			}
		}
	}
};

device_t* createDevice(BLEAddress *addr) {
	device_t *device;

	if((device = getDevice(addr)) != nullptr) {
		if(device->connection) {
			device->connection->close();
		}
		return device;
	}
	
	for(uint8_t i=0; i<MAX_DEVICES; i++) {
		device = DEVICES[i];
		if(!device->in_use) {
			memcpy(device->id, addr->toString().c_str(), DEVICE_ID_SZ);
			device->in_use = true;
			device->last_seen = millis();
			if(!device->connection) {
				device->connection = new SecureClient(device);
			}
			return device;
		}
	}
	return nullptr;
}

uint8_t configureDevice(device_t *device) {
	Serial.printf("configureDevice(): getting config for %s\n", device->id);
	uint16_t ret_code = getSensors(device);
	if(ret_code == 404) {
		Serial.println("not found: initializing new device");
		if(!st_init_device(device->id)) {
			Serial.println("unable to init node");
			return false;
		}
	} else if(ret_code != 200) {
		Serial.printf("received invalid response: %d\n", ret_code);
		return false;
	}
	device->last_seen = millis();
	return true;
}

void vConnTimerCB(TimerHandle_t xTimer) {
	uint8_t device_id = (uint32_t)pvTimerGetTimerID(xBLEConnTimer);
	device_t *device = DEVICES[device_id];
	if(device) {
		Serial.printf("vConnTimerCB(): %s: conn timer expired: closing\n", device->id);
		device->connection->close();
	} else {
		Serial.printf("vConnTmerCB(): id %d: timer expired but no device found!\n", device_id);
	}
}

uint8_t handleAuthState(device_t *device) {
	while(get_ble_mutex_status(device->connection->mutexId)) {
		vTaskDelay(100 / portTICK_RATE_MS);
		switch(device->connection->authstate) {
			case AUTH_PENDING: {
				continue;
			} break;
			
			case AUTH_FAILED: {
				Serial.printf("connect(): auth_failed: %s\n", device->id);
				device->connection->close();
				return false;
			} break;
			
			case AUTH_NONE: {
				Serial.printf("handleAuthState(): state reset to NONE: returning.");
				return false;
			} break;
			
			case AUTH_SUCCESS: {
				Serial.printf("handleAuthState(): %s: got SUCCESS: invoking clients configure()\n", device->id);
				return device->connection->configure();
			} break;
			
			default: {
				device->connection->close();
				return false;
			} break;
		}
	}
	return false;
}

uint8_t handleConnection(device_t *device) {
	if(!device) return false;

	vTimerSetTimerID(xBLEConnTimer, (void*)(uint32_t)device->device_id);
	
	if((uint32_t)pvTimerGetTimerID(xBLEConnTimer) != (uint32_t)device->device_id) {
		Serial.printf("handleConnection(): vTimerID: mismatched id\n");
		return false;
	}

	if(xTimerReset(xBLEConnTimer, 100) != pdTRUE) {
		Serial.println("handleConnection(): failed to start connection handler timer");
		return false;
	}

	uint8_t ret;
	if((ret = device->connection->connect())) {
		xTimerReset(xBLEConnTimer, 100);
		ret = handleAuthState(device);
	}

	xTimerStop(xBLEConnTimer, 250);
	return ret;
}


void init_devices(device_t *devices, uint8_t n_devices) {
	for(uint8_t id=0; id<n_devices; id++) {
		devices[id] = create_device(id);
		DEVICES[id] = &devices[id];
	}
}

void bt_scan_mgr(void *ptx) {
	//BLEDevice::getAdvertising()->setScanFilter(true,true);
	BLEDevice::getAdvertising()->setScanFilter(false,false);
	BLEScan *pScan = BLEDevice::getScan();
	pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
	pScan->setInterval(1349);
	pScan->setWindow(449);
	pScan->setActiveScan(true);
	char tag[] = "bt_scan_mgr()";

	EventBits_t evt;
	UBaseType_t new_wm, old_wm = uxTaskGetStackHighWaterMark(NULL);
	
	for(;;) {
		xEventGroupWaitBits(xBLEState, BLE_READY, false, false, 90000 / portTICK_RATE_MS);
		xEventGroupClearBits(xBLEState, BLE_ALL);
		vTaskDelay(250 / portTICK_RATE_MS);
		xEventGroupSetBits(xBLEState, BLE_SCANNING);
		if(!ble_scan_start(pScan)) {
			continue;
		}
	#ifdef DEBUG
		old_wm = new_wm;
		new_wm = uxTaskGetStackHighWaterMark(NULL);
		Serial.printf("bt_scan_mgr(): old_wm: %d / new_wm: %d\n", old_wm, new_wm);
	#endif
		evt = xEventGroupWaitBits(xBLEState, BLE_FINISHED | BLE_STOP, true, false, 90000 / portTICK_RATE_MS);
		if(evt & BLE_STOP) {
			pScan->stop();
			xEventGroupClearBits(xBLEState, BLE_ALL);
			Serial.printf("%s: scan stopped\n", tag);
		} else {
			pScan->stop();
			xEventGroupClearBits(xBLEState, BLE_ALL);
			Serial.printf("%s: scan timeout\n", tag);
			xEventGroupSetBits(xBLEState, BLE_READY);
		}
	}
}

void bt_device_mgr(void *ptx) {
	device_t m_devices[MAX_DEVICES];
	init_devices(m_devices, MAX_DEVICES);
	
	esp_bd_addr_t newDevice;
	uint8_t client_cnt = 0;
	BLEAddress m_ble_address(newDevice);
	BLEAddress *address = &m_ble_address;
	char *result[] = BLE_CONN_RESULT;
	EventBits_t evt;
	
	xBLEConnTimer = xTimerCreate("xBLEConnTimer", 24000 / portTICK_RATE_MS, pdFALSE, (void*)0, vConnTimerCB);

	for(;;) {
		if(xQueueReceive(xBLEDevice, &newDevice, 20000 / portTICK_RATE_MS) != pdTRUE) {
			client_cnt = displayClients();
			evt = xEventGroupWaitBits(xBLEState, BLE_SCANNING | BLE_READY, false, false, 1000 / portTICK_RATE_MS);
			if(evt & (BLE_SCANNING | BLE_READY)) pruneClients();
			continue;
		}

		address = new (&m_ble_address) BLEAddress(newDevice);
		Serial.printf("bt_device_mgr(): xQueueRecieve(xNewdevice): %s\n", address->toString().c_str());
		vTaskDelay((client_cnt * 150) / portTICK_RATE_MS);
	
		if(client_cnt >= MAX_DEVICES) {
			Serial.printf("mainTask(): max_clients exceeped: ignoring conn request\n");
		} else {
			if(device_t *device = createDevice(address)) {
				uint8_t res = handleConnection(device);
				Serial.printf("mainTask(): handleConnection(): %s\n", result[res]);
				if(!res) {
					vTaskDelay(250 / portTICK_RATE_MS);
				}
			}
		}
	
		xEventGroupClearBits(xBLEState, BLE_ALL);
		client_cnt = client_count();
	
		vTaskDelay(1000 / portTICK_RATE_MS);
		xEventGroupSetBits(xBLEState, BLE_READY);
	}
}

void bt_set_queue_handler(bt_queue_handler_t handler) {
	bt_queue_handler = handler;
}

void bt_queue_mgr(void *ptx) {
	queue_entry_t entry;
	BLEAddress m_ble_address(entry.device_id);
	BLEAddress *address = &m_ble_address;
	UBaseType_t old_wm, new_wm = uxTaskGetStackHighWaterMark(NULL);

	for(;;) {
		xQueueReceive(xQueue, &entry, portMAX_DELAY);
		xEventGroupWaitBits(xBLEState, BLE_SCANNING | BLE_READY, false, false, 30000 / portTICK_RATE_MS);
		address = new (&m_ble_address) BLEAddress(entry.device_id);
		Serial.printf("bt_queue_mgr(): processing payload: %s\n", address->toString().c_str());
		bt_queue_handler(address, &entry.data);
		vTaskDelay(200 / portMAX_DELAY);
	}
}

void bt_scan_start() {
	Serial.printf("bt_scan_start(): xBLEState->BLE_READY\n");
	xEventGroupSetBits(xBLEConn, BLE_CONN_READY);
	xEventGroupSetBits(xBLEState, BLE_READY);
}

void ble_init() {
	BLEDevice::init("Collector");
	//esp_ble_gap_set_prefer_conn_params((*(esp_bd_addr_t*)BLEDevice::getAddress().getNative()), 50, 500, 0, BLE_CONN_TIMEOUT);
	esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, BLE_POWER_LEVEL);
	esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, BLE_POWER_LEVEL);
	esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN ,BLE_POWER_LEVEL);
	esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

	BLESecurity mSecurity;
	BLESecurity *pSecurity = &mSecurity;
	
	pSecurity->setKeySize();
	pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_MITM);
	pSecurity->setCapability(ESP_IO_CAP_IN);
	pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
	pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

	xQueue = xQueueCreate(12, sizeof((queue_entry_t){{},{}}));
	xBLEDevice = xQueueCreate(1, sizeof(esp_bd_addr_t));
	xBLEState = xEventGroupCreate();
	xBLEConn = xEventGroupCreate();
	
	xEventGroupClearBits(xBLEState, BLE_ALL);
	
	xTaskCreatePinnedToCore(bt_scan_mgr, "bt_scan_mgr", BT_SCAN_STACK_SZ, NULL, 8, NULL, 1);
	xTaskCreatePinnedToCore(bt_device_mgr, "bt_device_mgr", BT_DEVICE_STACK_SZ, NULL, 7, NULL, 1);
	xTaskCreatePinnedToCore(bt_queue_mgr, "bt_queue_mgr", BT_QUEUE_STACK_SZ, NULL, 6, NULL, 1);
}
