# Phone2 - ESP32-S3 phone firmware (UI, A7672S modem, SMS, contacts, 4G data, hotspot).
#
#   powershell -File tools\build.ps1 -Target main
cmake_minimum_required(VERSION 3.16)

set(PHONE2_FW ${CMAKE_CURRENT_LIST_DIR}/..)
# Third-party components fetched by tools\setup.ps1 (kept outside OneDrive)
if(DEFINED ENV{PHONE2_DEPS})
  set(PHONE2_DEPS $ENV{PHONE2_DEPS})
else()
  set(PHONE2_DEPS $ENV{LOCALAPPDATA}/Phone2/deps)
endif()
if(NOT EXISTS ${PHONE2_DEPS}/LovyanGFX OR NOT EXISTS ${PHONE2_DEPS}/esp_littlefs)
  message(FATAL_ERROR "Missing LovyanGFX / esp_littlefs in ${PHONE2_DEPS} - run tools\\setup.ps1")
endif()
set(EXTRA_COMPONENT_DIRS ${PHONE2_FW}/common ${PHONE2_DEPS}/LovyanGFX ${PHONE2_DEPS}/esp_littlefs)
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(phone2_main)
