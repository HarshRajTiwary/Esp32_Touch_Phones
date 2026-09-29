#pragma once
// Board bring-up for the ESP32 Bluetooth helper.
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void board_init(void);
// Status LED (no-op when PIN_STATUS_LED is -1).
void board_led(bool on);
// Selects the analog audio path (no-op when PIN_AUDIO_SWITCH is -1):
// true = earbuds (ESP32 DAC drives the modem MIC input), false = wired handset.
void board_audio_switch(bool earbuds);

#ifdef __cplusplus
}
#endif
