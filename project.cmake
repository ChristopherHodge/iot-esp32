cmake_minimum_required(VERSION 3.5)

set(git_describe_cmd git -C "${CMAKE_SOURCE_DIR}" describe --long --always)

execute_process(
	COMMAND ${git_describe_cmd} --tags --exact
	OUTPUT_VARIABLE git_describe
)

if(git_describe)
	string(REGEX REPLACE "([^\-]+)\-[0-9]+\-([^$]+)" "\\1-\\2" BUILD_VERSION ${git_describe})
else()
	execute_process(
		COMMAND ${git_describe_cmd} --all
		OUTPUT_VARIABLE git_describe
	)
	string(REGEX REPLACE "[^/]+/([^\-]+)\-([0-9]+)\-([^$]+)" "development-\\3" BUILD_VERSION ${git_describe})
endif()

string(STRIP ${BUILD_VERSION} BUILD_VERSION)

message("BUILD_VERSION: ${BUILD_VERSION}")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DBUILD_VERSION=\\\"${BUILD_VERSION}\\\"")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBUILD_VERSION=\\\"${BUILD_VERSION}\\\"")

set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS}
      iot-esp32/iot-common
      main
  )

if(IOT_ESP32_USE_CORE)
	set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS}
      		iot-esp32/iot-core
      		iot-esp32/iot-libs/esp-nimble-cpp
	)
else()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DDISABLE_ESP_OTA")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDISABLE_ESP_OTA")
endif()

if(IOT_ESP32_USE_STDK)
	set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS}
	                      iot-esp32/iot-app-stdk
			      iot-esp32/iot-stdk)
	set(STDK_IOT_CORE_USE_DEFINED_CONFIG "y")
	configure_file(device_info.json config/device_info.json COPYONLY)
	configure_file(onboarding_config.json config/onboarding_config.json COPYONLY)
else()    
    set(EXTRA_COMPONENT_DIRS ${EXTRA_COMPONENT_DIRS} iot-esp32/iot-app)
endif()

configure_file(iot-config.h config/iot-config.h COPYONLY)
