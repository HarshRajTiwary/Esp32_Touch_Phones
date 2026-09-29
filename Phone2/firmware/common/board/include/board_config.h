#pragma once
// Single entry point for pin definitions. The board is chosen from the build
// target (see ../CMakeLists.txt); the real tables live in Phone2/boards/<board>/.
#if defined(PHONE_BOARD_S3_MAIN)
#include "../../../../boards/s3_main/board_config.h"
#elif defined(PHONE_BOARD_ESP32_BT)
#include "../../../../boards/esp32_bt/board_config.h"
#else
#error "No Phone2 board selected (PHONE_BOARD_S3_MAIN / PHONE_BOARD_ESP32_BT)"
#endif
