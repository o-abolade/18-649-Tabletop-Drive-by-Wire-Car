cmake_minimum_required(VERSION 3.28.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stm32)

# --- pi_stm32_uart module ---
add_library(pi_stm32_uart STATIC src/pi_stm32_uart.c)
target_include_directories(pi_stm32_uart PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pi_stm32_uart PUBLIC zephyr_interface)
add_dependencies(pi_stm32_uart zephyr_generated_headers)

# --- blinker module ---
add_library(blinker STATIC src/blinker.c)
target_include_directories(blinker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(blinker PUBLIC zephyr_interface)
add_dependencies(blinker zephyr_generated_headers)

# --- main app ---
target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE pi_stm32_uart)
target_link_libraries(app PRIVATE blinker)