#include "btlink_uart.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "plog.h"

namespace btl {
namespace {

uart_port_t s_port = UART_NUM_MAX;
FrameHandler s_handler = nullptr;
SemaphoreHandle_t s_txLock;
Decoder s_dec;
uint8_t s_seq = 0;
volatile uint32_t s_rx = 0, s_tx = 0, s_lastRx = 0;

void rxTask(void*) {
  static uint8_t buf[256];
  static Frame f;
  for (;;) {
    int n = uart_read_bytes(s_port, buf, sizeof buf, pdMS_TO_TICKS(50));
    for (int i = 0; i < n; ++i) {
      if (s_dec.push(buf[i], &f)) {
        s_rx = s_rx + 1;
        s_lastRx = plogMillis();
        if (s_handler) s_handler(f);
      }
    }
  }
}

}  // namespace

bool start(const UartConfig& cfg, FrameHandler handler) {
  s_port = (uart_port_t)cfg.port;
  s_handler = handler;
  s_txLock = xSemaphoreCreateMutex();
  uart_config_t uc = {};
  uc.baud_rate = (int)cfg.baud;
  uc.data_bits = UART_DATA_8_BITS;
  uc.parity = UART_PARITY_DISABLE;
  uc.stop_bits = UART_STOP_BITS_1;
  uc.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uc.source_clk = UART_SCLK_DEFAULT;
  if (uart_driver_install(s_port, 2048, 2048, 0, nullptr, 0) != ESP_OK ||
      uart_param_config(s_port, &uc) != ESP_OK ||
      uart_set_pin(s_port, cfg.txPin, cfg.rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    LOG("LINK", "UART%d init failed", cfg.port);
    return false;
  }
  // A weak pull-up keeps RX idle-high while the other board is unpowered or rebooting.
  gpio_pullup_en((gpio_num_t)cfg.rxPin);
  xTaskCreate(rxTask, "btlink_rx", 4096, nullptr, 9, nullptr);
  LOG("LINK", "BT link on UART%d TX=%d RX=%d @ %lu baud", cfg.port, cfg.txPin, cfg.rxPin, (unsigned long)cfg.baud);
  return true;
}

bool send(Msg type, const void* payload, size_t len) {
  if (s_port == UART_NUM_MAX) return false;
  uint8_t wire[kMaxWire];
  xSemaphoreTake(s_txLock, portMAX_DELAY);
  size_t n = encode(type, s_seq++, payload, len, wire, sizeof wire);
  bool ok = n > 0 && uart_write_bytes(s_port, wire, n) == (int)n;
  xSemaphoreGive(s_txLock);
  if (ok) s_tx = s_tx + 1;
  return ok;
}

uint32_t rxFrames() { return s_rx; }
uint32_t txFrames() { return s_tx; }
uint32_t rxErrors() { return s_dec.crcErrors() + s_dec.framingErrors(); }
uint32_t lastRxMs() { return s_lastRx; }

}  // namespace btl
