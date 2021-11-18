#include "network.h"
#include "sensor.h"
#include "ble.h"
#include "smartthings.h"
#include "app.h"

void app_start() {
  // create the device structs using params from iot-config.h
  device_init();

  // connect wifi if configured, or start wifi-provisioning
  wifi_connect();

  // check for OTA update
  check_fw_update();

  // configure the modem and start the needed tasks, the default
  ble_init();

  vTaskDelay(200 / portTICK_RATE_MS);

  // create device entries for the id's in STATIC_DEVICE_LIST and
  // configure them with the PRESENCE capability
  device_load_static_list(SENSOR_PRESENCE);

  // connect to smartthings, waits for SmartApp info from the mDNS
  // using service name configured in iot-config.h
  smartthings_init();

  vTaskDelay(200 / portTICK_RATE_MS);

  // create queue and task for handling REST device updates
  network_queue_init();

  // connect the devices presence handlers as callbacks from the
  // BLE service connect / disconnect routines
  device_enable_ble_presence();

  // use GATT BAS.  set the UUID's and the correct callback handler
  // to the subscribed notifications 
  device_enable_ble_bas();

  vTaskDelay(200 / portTICK_RATE_MS);

  // unblock the BLE scanning task
  bt_scan_enable();
}

