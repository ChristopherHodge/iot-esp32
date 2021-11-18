cmake_minimum_required(VERSION 3.5)

idf_component_register(SRCS "stdk_app.cpp"
                    INCLUDE_DIRS "include" "${PROJECT_DIR}"
                    REQUIRES iot-stdk iot-common)
