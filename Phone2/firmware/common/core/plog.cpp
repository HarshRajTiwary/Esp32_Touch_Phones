#include "plog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "text_util.h"

#ifdef ESP_PLATFORM
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static SemaphoreHandle_t s_lock;
static void lock() {
  if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}
static void unlock() {
  if (s_lock) xSemaphoreGive(s_lock);
}
uint32_t plogMillis() { return (uint32_t)(esp_timer_get_time() / 1000); }
#else
#include <chrono>
static void lock() {}
static void unlock() {}
uint32_t plogMillis() {
  static const auto t0 = std::chrono::steady_clock::now();
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}
#endif

namespace {
constexpr int kHistLines = 48;
constexpr int kHistLen = 120;
char s_hist[kHistLines][kHistLen];
int s_histHead = 0;   // next slot to write
int s_histCount = 0;
PlogSink s_sink = nullptr;
}  // namespace

void plogInit() {
#ifdef ESP_PLATFORM
  if (!s_lock) s_lock = xSemaphoreCreateMutex();
#endif
}

void plogSetSink(PlogSink sink) { s_sink = sink; }

void plogPrintf(const char* tag, const char* fmt, ...) {
  char buf[256];
  int n = snprintf(buf, sizeof buf, "%7lu [%s] ", (unsigned long)plogMillis(), tag);
  if (n < 0) return;
  if (n >= (int)sizeof buf) n = sizeof buf - 1;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + n, sizeof buf - n, fmt, ap);
  va_end(ap);
  lock();
  fputs(buf, stdout);
  fputc('\n', stdout);
  txt::copy(s_hist[s_histHead], buf, kHistLen);
  s_histHead = (s_histHead + 1) % kHistLines;
  if (s_histCount < kHistLines) s_histCount++;
  unlock();
  PlogSink sink = s_sink;
  if (sink) sink(buf);
}

int plogHistoryCount() { return s_histCount; }

bool plogHistory(int i, char* out, size_t cap) {
  lock();
  bool ok = i >= 0 && i < s_histCount;
  if (ok) {
    int start = (s_histHead - s_histCount + kHistLines) % kHistLines;
    txt::copy(out, s_hist[(start + i) % kHistLines], cap);
  }
  unlock();
  return ok;
}

const char* logNum(const char* number) {
#if PHONE_LOG_PRIVATE
  return number ? number : "";
#else
  static char ring[4][NUMBER_MAX];
  static uint8_t k;
  char* out = ring[k++ & 3];
  txt::maskNumber(number ? number : "", out, NUMBER_MAX);
  return out;
#endif
}

const char* logText(const char* text) {
#if PHONE_LOG_PRIVATE
  return text ? text : "";
#else
  static char ring[4][24];
  static uint8_t k;
  char* out = ring[k++ & 3];
  snprintf(out, 24, "<%u chars>", (unsigned)strlen(text ? text : ""));
  return out;
#endif
}
