// AT engine: line assembly for the AT and data channels, command execution
// with URC dispatch, SMS prompt handling.
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdlib.h>

#include "a7672s_internal.h"
#include "modem_port.h"
#include "plog.h"
#include "text_util.h"

namespace a7672s {
namespace {

constexpr size_t kLineMax = 1100;   // a CMGL/CMGR PDU line is up to ~350 hex chars; USSD up to ~400

// Splits a byte stream into lines and pushes them (heap copies) into a queue.
struct LineAssembler {
  QueueHandle_t q = nullptr;
  char buf[kLineMax];
  size_t len = 0;
  uint32_t dropped = 0;

  void init(int depth) { q = xQueueCreate(depth, sizeof(char*)); }

  void emit() {
    buf[len] = 0;
    char* line = strdup(buf);
    len = 0;
    if (!line) return;
    if (xQueueSend(q, &line, 0) != pdTRUE) {
      free(line);
      dropped++;
    }
  }

  void feed(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      char c = (char)d[i];
      if (c == '\r') continue;
      if (c == '\n') {
        if (len) emit();
        continue;
      }
      // The SMS prompt "> " is not followed by a newline: emit it on its own.
      if (len == 0 && c == '>') {
        buf[len++] = '>';
        emit();
        continue;
      }
      if (len == 0 && c == ' ') continue;
      if (len < kLineMax - 1) buf[len++] = c;
    }
  }

  bool pop(char* out, size_t cap, uint32_t waitMs) {
    char* line = nullptr;
    if (xQueueReceive(q, &line, pdMS_TO_TICKS(waitMs)) != pdTRUE) return false;
    txt::copy(out, line, cap);
    free(line);
    return true;
  }

  void flush() {
    char* line;
    while (xQueueReceive(q, &line, 0) == pdTRUE) free(line);
    len = 0;
  }
};

LineAssembler s_at;
LineAssembler s_data;
volatile bool s_dataPpp = false;
PppInputFn s_pppInput = nullptr;
int s_timeouts = 0;

}  // namespace

void atInit() {
  s_at.init(64);
  s_data.init(16);
}

void onPortBytes(int chan, const uint8_t* data, size_t len) {
  if (chan == mport::kChanData) {
    PppInputFn fn = s_pppInput;
    if (s_dataPpp && fn) fn(data, len);
    else s_data.feed(data, len);
    return;
  }
  s_at.feed(data, len);  // kChanRaw or kChanAt
}

bool readLine(char* out, size_t cap, uint32_t waitMs) { return s_at.pop(out, cap, waitMs); }

void flushLines() { s_at.flush(); }

void drainUrcs() {
  static char line[kLineMax];
  while (s_at.pop(line, sizeof line, 0)) handleLine(line);
}

void sendRaw(const void* data, size_t len) { mport::write(mport::kChanAt, data, len); }

int consecutiveTimeouts() { return s_timeouts; }
void resetTimeouts() { s_timeouts = 0; }

AtFinal exec(const char* cmd, uint32_t timeoutMs, const LineFn& fn, bool callFinals, int* errCode) {
  static char line[kLineMax];
  drainUrcs();  // anything that arrived before the command belongs to nobody
  mport::writeStr(mport::kChanAt, cmd);
  mport::writeStr(mport::kChanAt, "\r");
  uint32_t start = nowMs();
  for (;;) {
    uint32_t elapsed = nowMs() - start;
    if (elapsed >= timeoutMs) break;
    if (!s_at.pop(line, sizeof line, timeoutMs - elapsed)) continue;
    if (strcmp(line, cmd) == 0) continue;  // command echo (before ATE0 took effect)
    int code = 0;
    AtFinal f = atClassifyFinal(line, &code);
    bool callResult = f == AtFinal::NoCarrier || f == AtFinal::Busy || f == AtFinal::NoAnswer || f == AtFinal::NoDialtone;
    if (f != AtFinal::None && (!callResult || callFinals)) {
      if (callResult) handleLine(line);  // the call layer must see how the call ended
      if (errCode) *errCode = code;
      s_timeouts = 0;
      if (f != AtFinal::Ok) LOG("MODEM", "%s -> %s", redactCmd(cmd), line);
      return f;
    }
    handleLine(line);
    if (fn) fn(line);
  }
  s_timeouts++;
  LOG("MODEM", "timeout: %s", redactCmd(cmd));
  return AtFinal::None;
}

bool ok(const char* cmd, uint32_t timeoutMs) { return exec(cmd, timeoutMs) == AtFinal::Ok; }

const char* redactCmd(const char* cmd) {
  static const char* const kSecret[] = {"AT+CPIN=", "AT+CPWD=", "AT+CLCK=", "AT+CGAUTH="};
  for (const char* p : kSecret)
    if (txt::startsWith(cmd, p)) return p;  // prefix only, e.g. "AT+CPIN="
  return cmd;
}

bool waitPrompt(uint32_t timeoutMs) {
  static char line[kLineMax];
  uint32_t start = nowMs();
  for (;;) {
    uint32_t elapsed = nowMs() - start;
    if (elapsed >= timeoutMs) return false;
    if (!s_at.pop(line, sizeof line, timeoutMs - elapsed)) continue;
    if (line[0] == '>') return true;
    int code;
    AtFinal f = atClassifyFinal(line, &code);
    if (f != AtFinal::None && f != AtFinal::NoCarrier) return false;
    handleLine(line);
  }
}

// ---- data channel ----------------------------------------------------------------
bool dataReadLine(char* out, size_t cap, uint32_t waitMs) { return s_data.pop(out, cap, waitMs); }

void dataSend(const char* s) { mport::writeStr(mport::kChanData, s); }

void dataFlushLines() { s_data.flush(); }

void setDataMode(bool ppp, PppInputFn input) {
  s_pppInput = input;
  s_dataPpp = ppp;
  if (!ppp) s_data.flush();
}

}  // namespace a7672s
