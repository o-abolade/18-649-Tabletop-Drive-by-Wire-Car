cmake_minimum_required(VERSION 3.20.0)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/zephyr/lab3_common.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stm32_steering)

lab3_add_common_libs()

# --- steering module (Lab 2 servo driver) ---
add_library(steering STATIC src/steering.c)
target_include_directories(steering PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(steering PUBLIC zephyr_interface)
add_dependencies(steering zephyr_generated_headers)

# --- main app: STEER, FORCE_TX (RELAY is in common/zephyr/relay.c) ---
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(app PRIVATE steering)
