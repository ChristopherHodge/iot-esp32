cmake_minimum_required(VERSION 3.5)

idf_component_register(
      SRCS "ble_smartthings_presence.cpp"
      REQUIRES iot-core iot-app bt esp-nimble-cpp bootloader
  )

