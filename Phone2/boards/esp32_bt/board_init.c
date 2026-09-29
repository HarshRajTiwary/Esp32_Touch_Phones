#include "board_init.h"

#include <driver/gpio.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <sdkconfig.h>

#include "board_config.h"

static const char* TAG = "BOARD";

static void out_level(int pin, int level) {
  if (pin < 0) return;
  gpio_config_t c = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
  gpio_set_level((gpio_num_t)pin, level);
  gpio_config(&c);
  gpio_set_level((gpio_num_t)pin, level);
}

void board_led(bool on) {
  if (PIN_STATUS_LED >= 0) gpio_set_level((gpio_num_t)PIN_STATUS_LED, on ? 1 : 0);
}

void board_audio_switch(bool earbuds) {
  if (PIN_AUDIO_SWITCH >= 0)
    gpio_set_level((gpio_num_t)PIN_AUDIO_SWITCH, earbuds ? AUDIO_SWITCH_EARBUDS_LEVEL : !AUDIO_SWITCH_EARBUDS_LEVEL);
}

void board_init(void) {
  out_level(PIN_STATUS_LED, 0);
  out_level(PIN_AUDIO_SWITCH, !AUDIO_SWITCH_EARBUDS_LEVEL);
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  uint32_t fs = 0;
  esp_flash_get_size(NULL, &fs);
  ESP_LOGI(TAG, "%s: %s rev v%d.%d, %d cores, flash %lu MB, Bluetooth Classic: %s", BOARD_NAME, CONFIG_IDF_TARGET,
           ci.revision / 100, ci.revision % 100, ci.cores, (unsigned long)(fs >> 20),
           (ci.features & CHIP_FEATURE_BT) ? "yes" : "NO - this chip cannot run the helper");
}
