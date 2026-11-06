cmake_minimum_required(VERSION 3.22.1)
project(sensors_compat C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O2 -fPIC")

add_library(sensors_compat SHARED
    src/sensors_compat.c
)

target_link_libraries(sensors_compat
    log
    dl
    pthread
)

set_target_properties(sensors_compat PROPERTIES
    OUTPUT_NAME "sensors.compat"
    PREFIX ""
)
