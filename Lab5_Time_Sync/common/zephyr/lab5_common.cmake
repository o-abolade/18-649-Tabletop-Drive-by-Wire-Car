# Shared Lab 5 build setup for the STM32 apps. Include from an app's
# CMakeLists.txt in two steps:
#
#   include(../common/zephyr/lab5_common.cmake)   # BEFORE find_package(Zephyr)
#   find_package(Zephyr ...)
#   lab5_add_common_libs()                         # AFTER project()

set(LAB5_COMMON ${CMAKE_CURRENT_LIST_DIR}/..)

# Shared CAN + sync overlay first, then the app's own overlay (if any) so it
# can override.
set(DTC_OVERLAY_FILE "${LAB5_COMMON}/zephyr/dual_can.overlay")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/boards/nucleo_f401re.overlay)
  list(APPEND DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/boards/nucleo_f401re.overlay)
endif()

set(LAB5_INCLUDES ${LAB5_COMMON} ${LAB5_COMMON}/zephyr ${LAB5_COMMON}/raft)

# Same convention as Lab 3, including the zephyr_generated_headers
# dependency that prevents the parallel-build race.
function(lab5_add_lib name)
  add_library(${name} STATIC ${ARGN})
  target_include_directories(${name} PUBLIC ${LAB5_INCLUDES})
  target_link_libraries(${name} PUBLIC zephyr_interface)
  add_dependencies(${name} zephyr_generated_headers)
  target_link_libraries(app PRIVATE ${name})
endfunction()

macro(lab5_add_common_libs)
  # Portable core: identical source on the Pi
  lab5_add_lib(raft_core
    ${LAB5_COMMON}/raft/raft_proto.c
    ${LAB5_COMMON}/raft/netstate.c
    ${LAB5_COMMON}/raft/raft_log.c
    ${LAB5_COMMON}/raft/raft.c
    ${LAB5_COMMON}/raft/gtime.c
    ${LAB5_COMMON}/raft/refsync.c)
  lab5_add_lib(can_proto ${LAB5_COMMON}/can_proto.c)
  lab5_add_lib(wcet      ${LAB5_COMMON}/zephyr/wcet.c)
  lab5_add_lib(can_link  ${LAB5_COMMON}/zephyr/can_link.c)
  lab5_add_lib(gsync     ${LAB5_COMMON}/zephyr/gsync.c)
  target_link_libraries(can_link PUBLIC can_proto raft_core wcet)
  target_link_libraries(gsync PUBLIC can_link raft_core)
  target_include_directories(app PRIVATE ${LAB5_INCLUDES})
endmacro()

# The zone apps (N2, N3) add Lab 3's zone core and relay, and the Raft glue.
macro(lab5_add_zone_libs)
  lab5_add_lib(zone_core ${LAB5_COMMON}/zephyr/zone_core.c)
  lab5_add_lib(relay     ${LAB5_COMMON}/zephyr/relay.c)
  lab5_add_lib(raft_node ${LAB5_COMMON}/zephyr/raft_node.c)
  target_link_libraries(zone_core PUBLIC can_link raft_node wcet)
  target_link_libraries(relay PUBLIC can_link raft_node wcet)
  target_link_libraries(raft_node PUBLIC can_link gsync raft_core)
endmacro()
