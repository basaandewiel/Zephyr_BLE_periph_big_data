# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

#if(BOARD MATCHES "qemu_x86")
#  list(APPEND QEMU_EXTRA_FLAGS -serial tcp:127.0.0.1:5678,server)
#endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(debug)

target_sources(app PRIVATE src/main.c)

#baswi added next line for BT
zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)
