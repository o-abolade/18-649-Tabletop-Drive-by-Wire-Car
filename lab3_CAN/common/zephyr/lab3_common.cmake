# Shared Lab 3 build setup for the STM32 zones. Include from a zone's
# CMakeLists.txt in two steps:
#
#   include(../common/zephyr/lab3_common.cmake)   # BEFORE find_package(Zephyr)
#   find_package(Zephyr ...)
#   lab3_add_common_libs()                         # AFTER project()

set(LAB3_COMMON ${CMAKE_CURRENT_LIST_DIR}/..)

# Shared CAN overlay first, then the zone's own overlay so it can override
# (e.g. re-enable a peripheral the shared file disables).
set(DTC_OVERLAY_FILE
    "${LAB3_COMMON}/zephyr/dual_can.overlay;${CMAKE_CURRENT_SOURCE_DIR}/boards/nucleo_f401re.overlay")

# Every library follows the project's CMake convention, including the
# zephyr_generated_headers dependency that prevents the parallel-build race.
function(lab3_add_lib name)
  add_library(${name} STATIC ${ARGN})
  target_include_directories(${name} PUBLIC ${LAB3_COMMON} ${LAB3_COMMON}/zephyr)
  target_link_libraries(${name} PUBLIC zephyr_interface)
  add_dependencies(${name} zephyr_generated_headers)
  target_link_libraries(app PRIVATE ${name})
endfunction()

macro(lab3_add_common_libs)
  lab3_add_lib(can_proto ${LAB3_COMMON}/can_proto.c)
  lab3_add_lib(can_link  ${LAB3_COMMON}/zephyr/can_link.c)
  lab3_add_lib(zone_core ${LAB3_COMMON}/zephyr/zone_core.c)
  lab3_add_lib(wcet      ${LAB3_COMMON}/zephyr/wcet.c)
  lab3_add_lib(relay     ${LAB3_COMMON}/zephyr/relay.c)
  target_link_libraries(can_link PUBLIC can_proto wcet)
  target_link_libraries(zone_core PUBLIC can_link wcet)
  target_link_libraries(relay PUBLIC can_link wcet)
  target_include_directories(app PRIVATE ${LAB3_COMMON} ${LAB3_COMMON}/zephyr)
endmacro()
