#include "board_init.h"

#include <driver/gpio.h>
#include <driver/rmt_tx.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_psram.h>
#include <sdkconfig.h>
#include <string.h>

#include "board_config.h"

static const char* TAG = "BOARD";

static rmt_channel_handle_t s_ledChan;
static rmt_encoder_handle_t s_ledEnc;

static void out_level(int pin, int level) {
  if (pin < 0) return;
  gpio_config_t c = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT};
  gpio_set_level((gpio_num_t)pin, level);
  gpio_config(&c);
  gpio_set_level((gpio_num_t)pin, level);
}

// A released key must not be driven: open-drain high (= floating) for a raw
// module PWRKEY/RESET, or driven low for a transistor-buffered one.
static void release_key(int pin, int active_high) {
  if (pin < 0) return;
  gpio_config_t c = {.pin_bit_mask = 1ULL << pin,
                     .mode = active_high ? GPIO_MODE_OUTPUT : GPIO_MODE_OUTPUT_OD};
  gpio_set_level((gpio_num_t)pin, active_high ? 0 : 1);
  gpio_config(&c);
  gpio_set_level((gpio_num_t)pin, active_high ? 0 : 1);
}

static void in_pullup(int pin) {
  if (pin < 0) return;
  gpio_config_t c = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
  gpio_config(&c);
}

static void led_init(void) {
#if PIN_RGB_LED >= 0
  rmt_tx_channel_config_t tc = {
      .gpio_num = PIN_RGB_LED,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 10000000,  // 0.1 us ticks
      .mem_block_symbols = 64,
      .trans_queue_depth = 2,
  };
  if (rmt_new_tx_channel(&tc, &s_ledChan) != ESP_OK) return;
  // WS2812: 0 = 0.3 us high + 0.9 us low, 1 = 0.9 us high + 0.3 us low
  rmt_bytes_encoder_config_t ec = {
      .bit0 = {.level0 = 1, .duration0 = 3, .level1 = 0, .duration1 = 9},
      .bit1 = {.level0 = 1, .duration0 = 9, .level1 = 0, .duration1 = 3},
      .flags.msb_first = 1,
  };
  if (rmt_new_bytes_encoder(&ec, &s_ledEnc) != ESP_OK) return;
  rmt_enable(s_ledChan);
  board_led(0, 0, 0);
#endif
}

void board_led(uint8_t r, uint8_t g, uint8_t b) {
  if (!s_ledChan || !s_ledEnc) return;
  uint8_t grb[3] = {g, r, b};
  rmt_transmit_config_t t = {.loop_count = 0};
  rmt_transmit(s_ledChan, s_ledEnc, grb, sizeof grb, &t);
}

bool board_home_pressed(void) {
#if PIN_BUTTON_HOME >= 0
  return gpio_get_level((gpio_num_t)PIN_BUTTON_HOME) == 0;
#else
  return false;
#endif
}

void board_get_info(board_info_t* out) {
  memset(out, 0, sizeof *out);
  esp_chip_info_t ci;
  esp_chip_info(&ci);
  out->chip = CONFIG_IDF_TARGET;
  out->cores = ci.cores;
  out->revision = ci.revision;
  uint32_t fs = 0;
  if (esp_flash_get_size(NULL, &fs) == ESP_OK) out->flash_bytes = fs;
#if CONFIG_SPIRAM
  if (esp_psram_is_initialized()) out->psram_bytes = (uint32_t)esp_psram_get_size();
#endif
  out->cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  esp_read_mac(out->mac, ESP_MAC_WIFI_STA);
}

void board_init(void) {
  // SPI chip selects idle high so nothing talks on the bus before its driver starts.
  out_level(PIN_TFT_CS, 1);
  out_level(PIN_TOUCH_CS, 1);
  out_level(PIN_SD_CS, 1);
  out_level(PIN_TFT_BL, 0);          // backlight off until the first frame is drawn
  release_key(PIN_MODEM_PWRKEY, MODEM_PWRKEY_ACTIVE_HIGH);
  release_key(PIN_MODEM_RESET, MODEM_RESET_ACTIVE_HIGH);
  release_key(PIN_LINK_RESET, 0);    // ESP32 EN: open-drain, released
  in_pullup(PIN_BUTTON_HOME);
  in_pullup(PIN_TOUCH_IRQ);
  if (PIN_MODEM_STATUS >= 0) in_pullup(PIN_MODEM_STATUS);
  if (PIN_MODEM_RI >= 0) in_pullup(PIN_MODEM_RI);
  out_level(PIN_MODEM_DTR, 0);       // DTR low = modem may not sleep
  led_init();

  board_info_t bi;
  board_get_info(&bi);
  ESP_LOGI(TAG, "%s: %s rev v%d.%d, %d cores @ %d MHz, flash %lu MB, PSRAM %lu MB, MAC %02X:%02X:%02X:%02X:%02X:%02X",
           BOARD_NAME, bi.chip, bi.revision / 100, bi.revision % 100, bi.cores, bi.cpu_mhz,
           (unsigned long)(bi.flash_bytes >> 20), (unsigned long)(bi.psram_bytes >> 20),
           bi.mac[0], bi.mac[1], bi.mac[2], bi.mac[3], bi.mac[4], bi.mac[5]);
  if (!bi.psram_bytes) ESP_LOGW(TAG, "no PSRAM found - the UI and message store will use less memory");
}
