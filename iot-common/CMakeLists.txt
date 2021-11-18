cmake_minimum_required(VERSION 3.13)

idf_component_register(SRCS "iot_common.cpp" "iot-ota.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES "iot-core" "app_update" "esp_https_ota")
