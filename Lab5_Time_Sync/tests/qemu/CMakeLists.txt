# Runs the Lab 5 host tests on an emulated Cortex-M3, for machines without a
# host C compiler. From the zephyrproject workspace:
#   west build -b qemu_cortex_m3 <repo>/Lab5_Time_Sync/tests/qemu -d <build dir>
#   west build -d <build dir> -t run      (Ctrl+A, X to quit QEMU)
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lab5_tests)

set(TESTS ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(RAFT ${TESTS}/../common/raft)

target_include_directories(app PRIVATE ${TESTS} ${RAFT})
target_sources(app PRIVATE
  ${TESTS}/test_main.c
  ${TESTS}/test_netstate.c
  ${TESTS}/test_raft_proto.c
  ${RAFT}/netstate.c
  ${RAFT}/raft_proto.c
)
