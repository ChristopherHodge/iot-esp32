cmake_minimum_required(VERSION 3.13)

idf_component_register(SRCS "devices.cpp"
                            "influx.cpp"
                            "ble.cpp"
                            "network.cpp"
                            "sensor.cpp"
                            "smartapp.cpp"
                            "smartthings.cpp"
                    INCLUDE_DIRS
                            "include"
                    REQUIRES
                            bt 
                            mdns 
                            json 
                            iot-common
                            esp-nimble-cpp 
                            esp_http_client 
                            esp_http_server 
                            wifi_provisioning 
                            bootloader_support
    )
