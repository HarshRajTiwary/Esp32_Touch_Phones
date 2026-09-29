# Phone2 - Bluetooth helper firmware (generic ESP32, Bluetooth Classic HFP AG).
#
#   powershell -File tools\build.ps1 -Target bt
cmake_minimum_required(VERSION 3.16)

set(PHONE2_FW ${CMAKE_CURRENT_LIST_DIR}/..)
set(EXTRA_COMPONENT_DIRS ${PHONE2_FW}/common)
# Build only what main needs (the common folder also holds S3-only bits in future).
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(phone2_bt)
