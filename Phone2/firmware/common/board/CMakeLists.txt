# Board layer: picks Phone2/boards/<board> from the build target.
#   esp32s3 -> boards/s3_main   (the phone)
#   esp32   -> boards/esp32_bt  (Bluetooth helper)
# Override with the environment variable PHONE2_BOARD=<dir name>.
if(DEFINED ENV{PHONE2_BOARD})
  set(board $ENV{PHONE2_BOARD})
elseif(IDF_TARGET STREQUAL "esp32s3")
  set(board s3_main)
elseif(IDF_TARGET STREQUAL "esp32")
  set(board esp32_bt)
else()
  message(FATAL_ERROR "Phone2 has no board for target ${IDF_TARGET}")
endif()
set(board_dir ${CMAKE_CURRENT_LIST_DIR}/../../../boards/${board})
string(TOUPPER ${board} board_upper)

set(reqs esp_driver_gpio spi_flash esp_hw_support log)
if(board STREQUAL "s3_main")
  list(APPEND reqs esp_driver_rmt esp_psram)
endif()

idf_component_register(
  SRCS "${board_dir}/board_init.c"
  INCLUDE_DIRS "include" "${board_dir}"
  REQUIRES ${reqs}
)
target_compile_definitions(${COMPONENT_LIB} PUBLIC PHONE_BOARD_${board_upper}=1)
