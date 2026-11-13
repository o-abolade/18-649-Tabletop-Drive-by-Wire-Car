# Lab 5 step 3 bench: time sync + PPS on the two Nucleos, no Raft yet.
#   west build -b nucleo_f401re Lab5_Time_Sync/sync_bench -d build_n2 -- -DCONFIG_SYNC_BENCH_NODE=2
#   west build -b nucleo_f401re Lab5_Time_Sync/sync_bench -d build_n3 -- -DCONFIG_SYNC_BENCH_NODE=3
cmake_minimum_required(VERSION 3.20.0)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/zephyr/lab5_common.cmake)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lab5_sync_bench)

lab5_add_common_libs()
target_sources(app PRIVATE src/main.c)
