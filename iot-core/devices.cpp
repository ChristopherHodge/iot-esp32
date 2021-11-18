#include "iot-common.h"
#include "ble.h"
#include "network.h"
#include "smartapp.h"
#include "devices.h"
#include "influx.h"

static const char *TAG = "devices";

static	device_update_cb_t		device_update_cb	= NULL;
static	device_presence_cb_t	device_presence_cb	= NULL;

static	device_t	DEVICES[MAX_DEVICES];
static	device_t	**pDEVICE = (device_t**)calloc(MAX_DEVICES, sizeof(device_t*));

static	device_presence_t	DEVICE_PRESENCE[MAX_DEVICES];

static EventGroupHandle_t xPresenceEvent;

static  void    send_scope_updates(device_data_t *);
static  uint8_t display_devices();
static  void    prune_devices();
static  void    device_presence_set_action(device_t *, action_t );
static  void    device_presence_update_action(device_t *);
static  void    device_set_presence(device_t *, presence_t );
static  void    vPresenceTask(void *);
static  void    vDeviceTask(void *);


device_t* get_device(device_id_t device_id) {
	device_t *device;
	
	for(uint8_t i=0; i<MAX_DEVICES; i++) {
		device = pDEVICE[i];
		if(strncmp(device_id, device->id, DEVICE_ID_SZ) == 0) {
			return device;
		}
	}
	return nullptr;
}

devices_t get_devices() {
    devices_t devices;
	devices.devices = pDEVICE;
    return devices;
}

void delete_device(device_t *device) {
	if(device->connection->client) {
		if(!device->connection->close()) {
			LOGW("delete_device(): unable to delete conn: %s", device->id);
			return;
		}
	}

	device_t new_device = NEW_DEVICE(device->device_id);
	new_device.connection = device->connection;
	*device = new_device;
}

device_t* create_device(device_id_t device_id) {
	device_t *device;

	for(uint8_t i=0; i<MAX_DEVICES; i++) {
		device = pDEVICE[i];
		if(!device->in_use) {
	        device_t new_device = NEW_DEVICE(i);
		    *device = new_device;
			LOGI("create_device(): %s", device_id);
			memcpy(device->id, device_id, DEVICE_ID_SZ);
			device->in_use = true;
			device->last_seen = MILLIS;
			if(!device->connection) {
				device->connection = new SecureClient(device);
			}
			DEVICE_PRESENCE[i].device = device;
			DEVICE_PRESENCE[i].presence = PRESENCE_NOT_PRESENT;
			return device;
		}
	}
	return nullptr;
}

#ifdef STATIC_DEVICE_LIST
void device_load_static_list(sensor_type_t sensor_type) {
	device_id_t devices[] = { STATIC_DEVICE_LIST };
	for(uint8_t i=0; i<(sizeof(devices) / sizeof(device_id_t)); i++) {
		if(strlen(devices[i]) == DEVICE_ID_SZ-1) {
			device_t *device = create_device(devices[i]);
			device_add_sensor(device, sensor_type);
		}
	}
}
#endif

void device_payload_all_sensors(device_data_t *payload) {
	sensor_data_entry_t entry = device_payload_get_entry(payload, 0);
	device_t *device = get_device(payload->device_id);
	for(uint8_t idx=0 ; idx<MAX_SENSORS; idx++) {
		if(device->sensors[idx].in_use) {
			device_send_update(payload->device_id, *entry.type, (*(uint16_t*)entry.value->value), idx);
		}
	}
}

void device_process_payload(device_data_t *payload) {
	device_t *device;

	if(!(device = get_device(payload->device_id))) {
		LOGE("no such device: %s", payload->device_id);
		return;
	}
	device->last_seen = MILLIS;
	sensor_t *sensor = nullptr;
	switch(payload->data.type) {

		case SENSOR_BATTERY:
			LOGD("sensor payload type is 'BATTERY'");
			if(payload->data.sensor_id == MAX_SENSORS) {
				LOGD("update is device level: submitting for all device sensors");
				device_payload_all_sensors(payload);
				goto cleanup;
			} break;

		default: {
			sensor = &device->sensors[payload->data.sensor_id];
			if(sensor->id != payload->data.type) {
				const sensor_t *sensor2 = sensor_get_by_type(payload->data.type);
				LOGE("payload sensor type / device index mismatch!  skipping.");
				LOGE("got: sensor = %s / payload = %s", sensor->type, sensor2->type);
				goto cleanup;
			} break;
		}
	}

	LOGD("sensor payload type is '%s'", sensor->type);
	if(sensor_process_payload(sensor, &payload->data)) {
		LOGD("sensor payload processed: scopes set: 0x%02hx", payload->data.scopes);
		send_scope_updates(payload);
	}

cleanup:
	device_payload_free(payload);
}

device_data_t* device_payload_init(const char *device_id, uint8_t num_entries) {
	LOGD("payload_init: using did: %s: alloc %d bytes", device_id, VAL_TRANSPORT_SZ);
	device_data_t *payload = (device_data_t*)calloc(1, VAL_TRANSPORT_SZ);
	memcpy(payload->device_id, device_id, sizeof(device_id_t));
	LOGD("payload device at offset %d bytes is set to: %s", 
			PTR_OFFSET(payload, payload->device_id), payload->device_id);

	while(num_entries--) {
		device_payload_alloc_entry(payload);
	}
	return payload;
}

uint8_t device_payload_alloc_entry(device_data_t *payload) {
	return sensor_payload_alloc_entry(&payload->data);
}

sensor_data_entry_t device_payload_get_entry(device_data_t *payload, uint8_t idx) {
	return sensor_payload_get_entry(&payload->data, idx);
}

void device_enable_ble_bas() {
	ble_set_svc_uuid(UUID_PRESENCE, BLE_BAS_SVC);
}

void set_device_update_cb(device_update_cb_t callback) {
	device_update_cb = callback;
}

void set_device_presence_cb(device_presence_cb_t callback) {
	device_presence_cb = callback;
}

void update_on_connect(device_t *device) {
  LOGI("on_auth(): proximity");
  device_presence_set_action(device, ACTION_ARRIVED);
}

void update_on_disconnect(device_t *device) {
  LOGI("on_disconnect(): proximity");
  device_presence_set_action(device, ACTION_DEPARTED);
}

void device_presence_update_cb(device_data_t ret_payload, uint8_t err) {
   if(ret_payload.data.type == SENSOR_PRESENCE && !err) {
     device_t *device = get_device((char*)ret_payload.device_id);
     device_presence_set_action(device, ACTION_NONE);
   } 
}

void device_enable_ble_presence() {
	bt_set_auth_handler((bt_conn_handler_t)update_on_connect);
	bt_set_disconnect_handler((bt_conn_handler_t)update_on_disconnect);
    set_device_update_cb((device_update_cb_t)device_presence_update_cb);
	xTaskCreatePinnedToCore(vPresenceTask, "presence_task", PRESENCE_STACK_SZ, NULL, PRESENCE_TASK_PRIO, NULL, 1);
}

void device_payload_free(device_data_t *payload) {
	LOGD("device_payload_free(): 0x%08x", (uint32_t)payload);
	free(payload->data.tags);
	free(payload->data.values);
	free(payload);
	payload = NULL;
}

sensor_t* device_add_sensor(device_t *device, sensor_type_t type) {
	for(uint8_t i=0; i<MAX_SENSORS; i++) {
		sensor_t *sensor = &device->sensors[i];
		if(!sensor->in_use) {
			if(type == SENSOR_PRESENCE) {
				device->last_seen = 0;
			}
			*sensor = *sensor_get_by_type(type);
			sensor->in_use = true;
			sensor->device_idx = i;
			return sensor;
		}	
		if(sensor->id == type) {
			sensor->device_idx = i;
			return sensor; 
		}
	}
	return nullptr;
}

uint8_t device_send_update(const char *device_id, sensor_type_t type, uint16_t state, uint8_t device_idx) {
	device_data_t  *payload = device_payload_init(device_id, 1);
	sensor_data_entry_t entry = device_payload_get_entry(payload, 0);

	attribute_t type_name;
	sensor_type_get_name(type, type_name);

	sensor_payload_entry_id(entry, device_idx, type);
	sensor_payload_entry_attr(entry, type_name, VAL_U16, (void*)&state);
	return network_queue_payload(payload);
}

uint8_t device_update_needed(device_t *device, uint16_t vers) {
#ifdef DFU_REQUIRE_HW_REV
	if(device->hw_rev != DFU_REQUIRE_HW_REV) {
		return false;
	}
#endif
	return (vers > device->version); 
}

void device_init() {
	memset(DEVICES, 0, DEVICES_SIZE);
	for(uint8_t i=0; i<MAX_DEVICES; i++) {
		pDEVICE[i] = &DEVICES[i];
	}
	xTaskCreatePinnedToCore(vDeviceTask, "device_mgmt_task", DEVICE_MGMT_TASK_SZ, NULL, DEFAULT_TASK_PRIO-1, NULL, 1);
}

static void send_scope_updates(device_data_t *payload) {
    uint8_t err = 0;

    if(payload->data.scopes & SCOPE_INFLUX) {
		influx_queue_payload(payload);
    }

    if(payload->data.scopes & SCOPE_SMARTTHINGS) {
	    if(!st_send_payload(payload)) {
	        LOGE("failed to update st device");
	        err++;
	    }
    }

	device_data_t ret_payload = *payload;

	if(device_update_cb != NULL) {
        device_update_cb(ret_payload, err);
	}
    wifi_err_check(err);
}

static uint8_t display_devices() {
	uint8_t i, count = 0;
	char *msg = (char*)malloc(1024);
	char *pos = msg;

  #ifdef UART_MASTER
   #define HEADER_LINE "\n-------master-------\n"
	#elif defined(UART_SLAVE)
   #define HEADER_LINE "\n-------slave--------\n"
	#else
   #define HEADER_LINE "\n---------------------\n"
  #endif
  #define FOOTER_LINE "---------------------"

	pos += sprintf(pos, "(%s)", BUILD_VERSION);
	pos += sprintf(pos, HEADER_LINE);
	pos += sprintf(pos, "* HEAP FREE: RTOS: %d / malloc: %d\n", \
			xPortGetFreeHeapSize(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
	devices_t devices = get_devices();
	for(i=0; i<devices.num_devices; i++) {
		device_t *device = devices.devices[i];
		if(device->in_use && device->connection) {
			count++;
			pos += sprintf(pos, "conn %d: %s", i, device->id);
			if(device->connection->isConnected()) {
				pos += sprintf(pos, " * ");
			} else {
				pos += sprintf(pos, "   ");
			}
			if(device->version) {
				pos += sprintf(pos, "(fw: v%d) (hw_rev: %d)", device->version, device->hw_rev);
			}
			if(device->sensors[0].id == SENSOR_PRESENCE) {
				int rssi = device->connection->getRssi();
				if(rssi) {
					pos += sprintf(pos, "(rssi: %d)", rssi);
				}
			}
			pos += sprintf(pos, "\n");
		}
	}
	sprintf(pos, FOOTER_LINE);
	
	LOGI("%s", msg);
	free(msg);
#ifndef DISABLE_DEVICE_CREATION
    return count;
#else
	return 0;
#endif
}

static void prune_devices() {
#ifndef DISABLE_DEVICE_PRUNING
	devices_t devices = get_devices();
	for(uint8_t i=0; i<devices.num_devices; i++) {
		device_t *device = devices.devices[i];
		if(device->in_use && device->last_seen) {
			if(device->sensors && device->sensors[0].id == SENSOR_PRESENCE) {
				continue;
			}
			if(MILLIS > (device->last_seen + DEVICE_TIMEOUT)) {
				LOGI("DELETE: %s", device->id);
				delete_device(device);
				break;
			}
		}
	}
#endif
}

static void device_presence_set_action(device_t *device, action_t evt) {
    if(device->sensors && (device->sensors[0].id != SENSOR_PRESENCE &&
			 			   device->sensors[0].id != SENSOR_FORCE)) {
	    return;
    }
	device_presence_t *state = &DEVICE_PRESENCE[device->device_id];
	LOGI("state_evt: %s -> %s", device->id, ACTION_STRING[evt]);

	state->arrived = false;
	state->departed = false;
	state->ts = MILLIS;

	switch(evt) {
		case ACTION_ARRIVED: {
			state->arrived = true;
		} break;

		case ACTION_DEPARTED: {
			state->departed = true;
		} break;

		default:
			return;
	}

    xEventGroupSetBits(xPresenceEvent, evt);
}

static void device_presence_update_action(device_t *device) {
  action_t action = ACTION_DEPARTED;
  if(device->connection->client && device->connection->client->isConnected()) {
    action = ACTION_ARRIVED;
  }
  device_presence_set_action(device, action);
}

static void device_set_presence(device_t *device, presence_t mode) {
  char *device_id = device->id;
  device_presence_t *presence = &DEVICE_PRESENCE[device->device_id];
  presence->presence = mode;
  const sensor_t *sensor = sensor_get_by_type(SENSOR_PRESENCE);
  device_send_update(device_id, sensor->id, mode);
  if(device_presence_cb != NULL) {
	if(device_presence_cb(*presence)) {
		device_presence_set_action(device, ACTION_NONE);
	}
  }
}

static void vPresenceTask(void *ptx) {
  xPresenceEvent = xEventGroupCreate();
  device_presence_t *state = nullptr;

  for(uint8_t i=0; i<MAX_DEVICES; i++) {
      state = &DEVICE_PRESENCE[i];
	  if(state->device == nullptr) {
		  continue;
	  }
	  device_presence_update_action(state->device);
	  vTaskDelay(250);
  }

  for(;;) {
    STACK_STATS
    xEventGroupWaitBits(xPresenceEvent, ACTION_MAX, true, false, PRESENCE_UPDATE_TIMEOUT);

    for(uint8_t i=0; i<MAX_DEVICES; i++) {
      state = &DEVICE_PRESENCE[i];

      if(state->device == nullptr) {
        continue;
      }

      if(state->arrived) {
	    if((MILLIS - state->ts) > (PRESENCE_ARRIVE_DELAY_MS-1)) {
          device_set_presence(state->device, PRESENCE_PRESENT);
		}
        continue;
      }

      if(state->departed) {
        if((MILLIS - state->ts) > (PRESENCE_DEPART_DELAY_MS-1)) {
          device_set_presence(state->device, PRESENCE_NOT_PRESENT);
        }
        continue;
      }

	  if(state->presence == PRESENCE_PRESENT) {
		  if((MILLIS - state->ts) > PRESENCE_UPDATE_DELAY_MS) {
			  state->ts = MILLIS;
			  device_set_presence(state->device, PRESENCE_PRESENT);
		  }
		  continue;
	  }
    }
  }
}

static void vDeviceTask(void *ptx) {
    for(;;) {
		vTaskDelay(DEVICE_MGMT_TASK_DELAY);
        STACK_STATS
        display_devices();
        prune_devices();
    }
}
