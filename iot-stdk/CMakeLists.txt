cmake_minimum_required(VERSION 3.5)

idf_component_register(
		INCLUDE_DIRS stdk/src/include
		             stdk/src/include/bsp
		             stdk/src/include/os
			     stdk/src/port/net/mbedtls
		REQUIRES iot-common libsodium
	)

set(STDK_IOT_CORE_USE_DEFINED_CONFIG "y")

set(STDK_LINK_LIBRARY
        __idf_libsodium
        __idf_json
   )

set(STDK_INCLUDE_PATH
        "$ENV{IDF_PATH}/components/freertos/include/freertos"
        "$ENV{IDF_PATH}/components/nvs_flash/include"
        "$ENV{IDF_PATH}/components/spi_flash/include"
        "$ENV{IDF_PATH}/components/bootloader_support/include"
    )

add_subdirectory(stdk)

target_link_libraries(${COMPONENT_LIB} INTERFACE iotcore) 
