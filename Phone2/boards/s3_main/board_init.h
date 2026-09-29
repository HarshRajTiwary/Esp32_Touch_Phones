#pragma once
// Board bring-up for the ESP32-S3 main board.
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char* chip;         // "ESP32-S3"
  int cores;
  int revision;             // chip revision x100 (e.g. 2 = v0.2)
  uint32_t flash_bytes;
  uint32_t psram_bytes;     // 0 = no PSRAM found
  int cpu_mhz;
  uint8_t mac[6];
} board_info_t;

// Puts every board GPIO in a safe idle state (modem control lines released,
// chip selects high, backlight off) and prints the board summary.
void board_init(void);
void board_get_info(board_info_t* out);

// Home/BOOT button (true while pressed).
bool board_home_pressed(void);

// Optional on-board RGB LED (no-op when PIN_RGB_LED is -1). r/g/b 0..255.
void board_led(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif
