#pragma once
// Tagged logging on the serial console, e.g.
//     12345 [MODEM] SIM READY
//
// Also keeps the most recent lines in memory for the on-screen log viewer
// (Settings > Diagnostics > Log) and can forward lines to an extra sink
// (the Bluetooth helper forwards its log to the S3 over the link).
#include <stddef.h>
#include <stdint.h>

#include "app_limits.h"

void plogInit();
void plogPrintf(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Extra output (called outside the log lock, one complete line without newline).
typedef void (*PlogSink)(const char* line);
void plogSetSink(PlogSink sink);

// Recent lines, newest last. Returns false when `i` is past the end.
bool plogHistory(int i, char* out, size_t cap);
int plogHistoryCount();

// Masked copy of a phone number (+91******3210) unless PHONE_LOG_PRIVATE is set.
// Uses a small rotating buffer, valid until 4 more calls.
const char* logNum(const char* number);
// "<N chars>" instead of message text unless PHONE_LOG_PRIVATE is set.
const char* logText(const char* text);

// Milliseconds since boot (esp_timer on the target, a monotonic clock on the host).
uint32_t plogMillis();

#define LOG(tag, ...) plogPrintf(tag, __VA_ARGS__)
