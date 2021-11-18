# IOT-ESP32
### Framework for ESP32 IOT devices with WiFi and BLE
Integrates with a combination of [SmartThings](https://smartthings.com/) 
and [InfluxDB](https://influxdata.com) sources to allow for a wide range of uses.

## Documentation
Documentation is available [here](https://github.hodge-labs.us/iot-esp32/docs).

## TOC
```
iot-esp32
│
├── iot-config.h     // config for all C components
│
├── project.cmake    // include this in your 'main' CMakeLists.txt
│
├── iot-app          // entrypoint to bootstrap non-stdk apps. 
│   └── include      // yields to 'app_start()' in 'main' 
│
├── iot-app-stdk     // entrypoint containing our hooks for stdk apps.
│   └── include      // starts the stdk tasks and yields to `app_start()`
│
├── iot-common       // common helpers, defaults, etc
│   └── include
│
├── iot-core         // services to handle devices and sensor management,
│   └── include      // BLE/WiFi tasks, API notification queues, etc.
│
├── iot-edge         // SmartThings Edge components
│   └── smartapp-discovery 
│       └── src      // Edge driver for discovering our SmartApp API details via mDNS
│
├── iot-libs         // home for upstream repos to be included as submodules
│   └── esp-nimble-cpp
│       └── src
│
└── iot-stdk         // our build integration with the STDK framework
    │
    └── stdk         // upstream STDK repo
        ├── src
        ├── examples
        └── tools
```
