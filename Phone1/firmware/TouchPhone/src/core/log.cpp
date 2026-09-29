#include "log.h"

#include <Arduino.h>
#include <stdarg.h>

#include "text_util.h"

static SemaphoreHandle_t s_logLock;

void logInit() {
  s_logLock = xSemaphoreCreateMutex();
}

void logPrintf(const char* tag, const char* fmt, ...) {
  char buf[256];
  int n = snprintf(buf, sizeof buf, "%7lu [%s] ", (unsigned long)millis(), tag);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf + n, sizeof buf - n, fmt, ap);
  va_end(ap);
  if (s_logLock) xSemaphoreTake(s_logLock, portMAX_DELAY);
  Serial.println(buf);
  if (s_logLock) xSemaphoreGive(s_logLock);
}

const char* logNum(const char* number) {
#if PHONE_LOG_PRIVATE
  return number;
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
  return text;
#else
  static char ring[4][24];
  static uint8_t k;
  char* out = ring[k++ & 3];
  snprintf(out, 24, "<%u chars>", (unsigned)strlen(text ? text : ""));
  return out;
#endif
}
