# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Add wifi-credentials snippet
if(NOT wifi-credentials IN_LIST SNIPPET)
  set(SNIPPET wifi-credentials ${SNIPPET} CACHE STRING "wifi-credentials snippet" FORCE)
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# From MQTT publisher sample
include(${ZEPHYR_BASE}/samples/net/common/common.cmake)