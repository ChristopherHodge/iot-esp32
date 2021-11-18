#ifndef IOT_CONFIG_H_
#define IOT_CONFIG_H_

// maximum number of devices.  when using 'STATIC' devices, must be at
// least the number we will configure.  for 'DYNAMIC' this is the maximum
// concurrent devices we can support, not the maximum we've 'seen'
#define MAX_DEVICES     2

// maximum number of 'sensor' types a device can have associated
#define MAX_SENSORS     1

// BLE pre-shared key
#define BLE_PASSKEY     123456

// for the purpose of presnce, we likely only care about a few known
// devices.  using 'NO_DYNAMIC_DEVICES' will prevent new device discovery
// which is preferable in most other scenarios
#define NO_DYNAMIC_DEVICES

// change the default time a device must be away before being 
// considered 'NOT_PRESENT'
#define PRESENCE_DEPART_DELAY_MS  20000

// without DYNAMIC_DEVICES, we need to define the devices here
#define STATIC_DEVICE_LIST  \
  "00:00:00:00:00",         \
  "11:11:11:11:11"

// trusted CA for your SmartApp endpoints
#define CA_CRT \
  "-----BEGIN CERTIFICATE-----\n" \
  "xxxxxxxxxxxxxxxxxxxxxxxxxxx\n" \
  "-----END CERTIFICATE-----\n"

// Settings for InfluxDB
#define INFLUX_DB_NAME     "sensors"
#define INFLUX_HOST        "influxdb.localdomain"

// SmartThings App discovery
#define ST_MDNS_SVC        "SxNET"     // the mDNS service name

// OTA
#define ESP_OTA_URL_BASE     "https://ota.mycompany.com/"
#define FW_BASE_NAME         "presence"


#endif

