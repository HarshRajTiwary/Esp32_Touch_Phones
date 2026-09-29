#include "modem_port.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

#include <initializer_list>

#include "cmux.h"
#include "plog.h"
#include "s3_config.h"

namespace mport {
namespace {

uart_port_t s_uart = UART_NUM_1;
uint32_t s_baud = 115200;
RxFn s_rx = nullptr;
volatile Mode s_mode = Mode::Raw;
SemaphoreHandle_t s_txLock;
EventGroupHandle_t s_ev;
cmux::Decoder s_dec;
volatile uint32_t s_rxBytes = 0, s_txBytes = 0;
volatile bool s_flowOff = false;
volatile bool s_restartSeen = false;
char s_garbage[24];
size_t s_garbageLen = 0;

// Event bits: UA / DM received for DLCI 0..3
constexpr EventBits_t kUaBit0 = 1 << 0;
constexpr EventBits_t kDmBit0 = 1 << 8;

uint8_t s_tx[cmux::kMaxInfo + cmux::kOverhead];  // guarded by s_txLock

bool uartWrite(const void* d, size_t n) {
  int w = uart_write_bytes(s_uart, d, n);
  if (w > 0) s_txBytes = s_txBytes + (uint32_t)w;
  return w == (int)n;
}

bool sendFrame(uint8_t dlci, uint8_t ctrl, const uint8_t* data, size_t len) {
  xSemaphoreTake(s_txLock, portMAX_DELAY);
  size_t n = cmux::build(dlci, ctrl, data, len, s_tx, sizeof s_tx);
  bool ok = n && uartWrite(s_tx, n);
  xSemaphoreGive(s_txLock);
  return ok;
}

void sendControl(uint8_t type, bool command, const uint8_t* v, size_t n) {
  uint8_t info[32];
  size_t len = cmux::buildControl(type, command, v, n, info, sizeof info);
  if (len) sendFrame(0, cmux::UIH, info, len);
}

// Control channel messages from the module (DLCI 0, UIH).
void onControl(const uint8_t* d, size_t len) {
  if (len < 2) return;
  uint8_t type = d[0];
  bool command = (type & 0x02) != 0;
  uint8_t base = (uint8_t)(type | 0x02);   // command form
  size_t n = d[1] >> 1;
  const uint8_t* v = d + 2;
  if (n > len - 2) n = len - 2;
  if (!command) return;  // responses to our own commands (MSC etc.) need no action
  switch (base) {
    case cmux::MSG_MSC:
    case cmux::MSG_PN:
    case cmux::MSG_TEST:
    case cmux::MSG_RPN:
    case cmux::MSG_RLS:
      sendControl(base, false, v, n);  // acknowledge by echoing the values
      break;
    case cmux::MSG_FCOFF:
      s_flowOff = true;
      sendControl(base, false, nullptr, 0);
      break;
    case cmux::MSG_FCON:
      s_flowOff = false;
      sendControl(base, false, nullptr, 0);
      break;
    case cmux::MSG_CLD:
      sendControl(base, false, nullptr, 0);
      LOG("MUX", "module closed the multiplexer");
      s_mode = Mode::Raw;
      break;
    default: {
      uint8_t t = type;
      sendControl(cmux::MSG_NSC, false, &t, 1);
      break;
    }
  }
}

void onFrame(void*, const cmux::Frame& f) {
  switch (f.ctrl) {
    case cmux::UA:
      if (f.dlci < 8) xEventGroupSetBits(s_ev, kUaBit0 << f.dlci);
      break;
    case cmux::DM:
      if (f.dlci < 8) xEventGroupSetBits(s_ev, kDmBit0 << f.dlci);
      break;
    case cmux::DISC:
      sendFrame(f.dlci, cmux::UA | cmux::kPF, nullptr, 0);
      LOG("MUX", "module closed channel %u", f.dlci);
      break;
    case cmux::SABM:
      sendFrame(f.dlci, cmux::UA | cmux::kPF, nullptr, 0);
      break;
    case cmux::UIH:
    case cmux::UI:
      if (f.dlci == 0) onControl(f.data, f.len);
      else if (s_rx && f.len) s_rx(f.dlci, f.data, f.len);
      break;
    default:
      break;
  }
}

// Plain text outside frames while in mux mode: remember the tail and look for
// the module's boot banner ("RDY", "*ATREADY: 1").
void onGarbage(void*, uint8_t b) {
  if (b == '\r' || b == '\n') {
    s_garbage[s_garbageLen] = 0;
    if (strstr(s_garbage, "RDY") || strstr(s_garbage, "ATREADY")) {
      if (!s_restartSeen) LOG("MUX", "module printed its boot banner - it restarted");
      s_restartSeen = true;
    }
    s_garbageLen = 0;
    return;
  }
  if (s_garbageLen < sizeof s_garbage - 1) s_garbage[s_garbageLen++] = (char)b;
}

void rxTask(void*) {
  static uint8_t buf[1024];
  for (;;) {
    int n = uart_read_bytes(s_uart, buf, sizeof buf, pdMS_TO_TICKS(10));
    if (n <= 0) continue;
    s_rxBytes = s_rxBytes + (uint32_t)n;
    if (s_mode == Mode::Mux) s_dec.feed(buf, (size_t)n, onFrame, onGarbage, nullptr);
    else if (s_rx) s_rx(kChanRaw, buf, (size_t)n);
  }
}

bool openDlci(uint8_t dlci, uint32_t timeoutMs) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    xEventGroupClearBits(s_ev, (kUaBit0 | kDmBit0) << dlci);
    sendFrame(dlci, cmux::SABM | cmux::kPF, nullptr, 0);
    EventBits_t b = xEventGroupWaitBits(s_ev, (kUaBit0 | kDmBit0) << dlci, pdTRUE, pdFALSE, pdMS_TO_TICKS(timeoutMs));
    if (b & (kUaBit0 << dlci)) return true;
    if (b & (kDmBit0 << dlci)) {
      LOG("MUX", "channel %u refused (DM)", dlci);
      return false;
    }
  }
  LOG("MUX", "no answer when opening channel %u", dlci);
  return false;
}

}  // namespace

bool begin(const Config& cfg, RxFn rx) {
  s_uart = (uart_port_t)cfg.uart;
  s_rx = rx;
  s_baud = cfg.baud;
  s_txLock = xSemaphoreCreateMutex();
  s_ev = xEventGroupCreate();
  uart_config_t uc = {};
  uc.baud_rate = (int)cfg.baud;
  uc.data_bits = UART_DATA_8_BITS;
  uc.parity = UART_PARITY_DISABLE;
  uc.stop_bits = UART_STOP_BITS_1;
  bool hwFlow = cfg.rts >= 0 && cfg.cts >= 0;
  uc.flow_ctrl = hwFlow ? UART_HW_FLOWCTRL_CTS_RTS : UART_HW_FLOWCTRL_DISABLE;
  uc.rx_flow_ctrl_thresh = 100;
  uc.source_clk = UART_SCLK_DEFAULT;
  // 16 KB receive ring: a full-speed PPP burst while the RX task is busy must not overflow.
  if (uart_driver_install(s_uart, 16384, 4096, 0, nullptr, 0) != ESP_OK || uart_param_config(s_uart, &uc) != ESP_OK ||
      uart_set_pin(s_uart, cfg.tx, cfg.rx, hwFlow ? cfg.rts : UART_PIN_NO_CHANGE,
                   hwFlow ? cfg.cts : UART_PIN_NO_CHANGE) != ESP_OK) {
    LOG("MODEM", "UART%d init failed", cfg.uart);
    return false;
  }
  gpio_pullup_en((gpio_num_t)cfg.rx);  // idle-high while the modem is unpowered
  xTaskCreatePinnedToCore(rxTask, "modem_rx", 4096, nullptr, 18, nullptr, 0);
  LOG("MODEM", "UART%d TX=GPIO%d RX=GPIO%d @ %lu baud%s", cfg.uart, cfg.tx, cfg.rx, (unsigned long)cfg.baud,
      hwFlow ? ", RTS/CTS" : "");
  return true;
}

bool setBaud(uint32_t b) {
  if (uart_set_baudrate(s_uart, b) != ESP_OK) return false;
  s_baud = b;
  return true;
}

uint32_t baud() { return s_baud; }
Mode mode() { return s_mode; }

bool write(int chan, const void* data, size_t len) {
  if (s_mode == Mode::Raw) {
    if (chan != kChanRaw && chan != kChanAt) return false;
    xSemaphoreTake(s_txLock, portMAX_DELAY);
    bool ok = uartWrite(data, len);
    xSemaphoreGive(s_txLock);
    return ok;
  }
  if (chan == kChanRaw) chan = kChanAt;
  // Respect the module's flow control on the data channel (a short wait, then drop:
  // PPP retransmits, and AT must never block for long).
  if (chan == kChanData) {
    for (int i = 0; i < 20 && s_flowOff; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    if (s_flowOff) return false;
  }
  const uint8_t* p = (const uint8_t*)data;
  while (len) {
    size_t chunk = len > MODEM_CMUX_N1 ? MODEM_CMUX_N1 : len;
    if (!sendFrame((uint8_t)chan, cmux::UIH, p, chunk)) return false;
    p += chunk;
    len -= chunk;
  }
  return true;
}

bool writeStr(int chan, const char* s) { return write(chan, s, strlen(s)); }

bool muxStart(uint32_t timeoutMs) {
  s_dec.reset();
  s_dec.setMaxDlci(kChanData);  // we only ever open channels 0..2
  s_restartSeen = false;
  s_flowOff = false;
  s_garbageLen = 0;
  s_mode = Mode::Mux;
  if (!openDlci(0, timeoutMs) || !openDlci(kChanAt, timeoutMs) || !openDlci(kChanData, timeoutMs)) {
    s_mode = Mode::Raw;
    return false;
  }
  // Tell the module our side of both channels is ready (DTR/RTS on).
  for (uint8_t dlci : {(uint8_t)kChanAt, (uint8_t)kChanData}) {
    uint8_t v[2] = {(uint8_t)((dlci << 2) | 0x02 | 0x01), cmux::kV24Ready};
    sendControl(cmux::MSG_MSC, true, v, 2);
  }
  LOG("MUX", "multiplexer up: channel 1 = AT, channel 2 = data");
  return true;
}

void muxStop() {
  if (s_mode != Mode::Mux) return;
  sendControl(cmux::MSG_CLD, true, nullptr, 0);
  vTaskDelay(pdMS_TO_TICKS(200));
  s_mode = Mode::Raw;
  LOG("MUX", "multiplexer closed");
}

bool restartSeen() { return s_restartSeen; }
void clearRestartSeen() { s_restartSeen = false; }

void flushInput() { uart_flush_input(s_uart); }

Stats stats() {
  Stats s;
  s.rxBytes = s_rxBytes;
  s.txBytes = s_txBytes;
  s.frames = s_dec.frames();
  s.fcsErrors = s_dec.fcsErrors();
  s.garbage = s_dec.garbage();
  s.peerFlowOff = s_flowOff;
  return s;
}

}  // namespace mport
