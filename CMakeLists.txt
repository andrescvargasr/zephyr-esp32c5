# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Add wifi-credentials snippet
if(NOT wifi-credentials IN_LIST SNIPPET)
  set(SNIPPET wifi-credentials ${SNIPPET} CACHE STRING "wifi-credentials snippet" FORCE)
endif()

if(NOT espressif-psram-8M IN_LIST SNIPPET)
  set(SNIPPET espressif-psram-8M ${SNIPPET} CACHE STRING "espressif-psram-8M snippet" FORCE)
endif()

if(NOT espressif-psram-wifi IN_LIST SNIPPET)
  set(SNIPPET espressif-psram-wifi ${SNIPPET} CACHE STRING "espressif-psram-wifi snippet" FORCE)
endif()

set(mcuboot_key_file ${CMAKE_CURRENT_SOURCE_DIR}/key.pem)
set(mcuboot_CONFIG_BOOT_SIGNATURE_KEY_FILE "\"${mcuboot_key_file}\"")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# From MQTT publisher sample
include(${ZEPHYR_BASE}/samples/net/common/common.cmake)
