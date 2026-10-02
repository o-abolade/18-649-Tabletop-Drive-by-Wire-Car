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

# --- blinker control module ---
add_library(blinker_ctrl STATIC src/blinker_ctrl.c)
target_include_directories(blinker_ctrl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(blinker_ctrl PUBLIC zephyr_interface)
add_dependencies(blinker_ctrl zephyr_generated_headers)

# --- current module ---
add_library(current STATIC src/current.c)
target_include_directories(current PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(current PUBLIC zephyr_interface)
add_dependencies(current zephyr_generated_headers)

# --- steering module ---
add_library(steering STATIC src/steering.c)
target_include_directories(steering PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(steering PUBLIC zephyr_interface)
add_dependencies(steering zephyr_generated_headers)

# --- motor control module ---
add_library(motor_control STATIC src/motor_control.c)
target_include_directories(motor_control PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(motor_control PUBLIC zephyr_interface)
add_dependencies(motor_control zephyr_generated_headers)

# --- encoder module ---
add_library(encoder STATIC src/encoder.c)
target_include_directories(encoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(encoder PUBLIC zephyr_interface)
add_dependencies(encoder zephyr_generated_headers)

# --- pid module ---
add_library(pid_controller STATIC src/pid_controller.c)
target_include_directories(pid_controller PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(pid_controller PUBLIC zephyr_interface)
add_dependencies(pid_controller zephyr_generated_headers)

# --- testpoints module ---
add_library(testpoints STATIC src/testpoints.c)
target_include_directories(testpoints PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(testpoints PUBLIC zephyr_interface)
add_dependencies(testpoints zephyr_generated_headers)

# --- main app ---
target_sources(app PRIVATE src/main.c src/bench_commands.c)
target_link_libraries(app PRIVATE pi_stm32_uart)
target_link_libraries(app PRIVATE blinker)
target_link_libraries(app PRIVATE blinker_ctrl)
target_link_libraries(app PRIVATE current)
target_link_libraries(app PRIVATE steering)
target_link_libraries(app PRIVATE motor_control)
target_link_libraries(app PRIVATE encoder)
target_link_libraries(app PRIVATE pid_controller)
target_link_libraries(app PRIVATE testpoints)