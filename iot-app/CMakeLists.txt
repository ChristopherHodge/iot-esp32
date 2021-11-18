cmake_minimum_required(VERSION 3.13)

idf_component_register(SRCS  "app.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES iot-common)
