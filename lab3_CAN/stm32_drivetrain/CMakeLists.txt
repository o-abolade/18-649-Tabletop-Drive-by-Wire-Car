cmake_minimum_required(VERSION 3.20.0)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/zephyr/lab3_common.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(stm32_drivetrain)

lab3_add_common_libs()

# --- Lab 2 drivetrain modules, one library each (see CLAUDE.md conventions) ---
foreach(mod testpoints motor_control encoder pid_controller)
  add_library(${mod} STATIC src/${mod}.c)
  target_include_directories(${mod} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(${mod} PUBLIC zephyr_interface)
  add_dependencies(${mod} zephyr_generated_headers)
  target_link_libraries(app PRIVATE ${mod})
endforeach()
target_link_libraries(motor_control PUBLIC testpoints)

# --- main app: DRIVE, PID ---
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
