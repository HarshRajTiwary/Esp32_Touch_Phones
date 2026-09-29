#pragma once
// Tagged debug logging on the USB serial port:  [MODEM] SIM READY
#include "../config/app_config.h"

void logInit();
void logPrintf(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// Returns a masked copy of a phone number unless PHONE_LOG_PRIVATE is enabled.
// Uses a small rotating buffer, valid until 4 more calls.
const char* logNum(const char* number);
// Returns "<N chars>" instead of message text unless PHONE_LOG_PRIVATE is enabled.
const char* logText(const char* text);

#if PHONE_DEBUG
#define LOG(tag, ...) logPrintf(tag, __VA_ARGS__)
#else
#define LOG(tag, ...) do {} while (0)
#endif
