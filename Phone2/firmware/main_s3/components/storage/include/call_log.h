#pragma once
// Recent calls (LittleFS /calls.bin), newest first.
#include "phone_types.h"

namespace calllog {

void load();
int count();
const CallLogEntry& at(int i);        // 0 = newest
void add(const char* number, CallType type, uint32_t ts, uint16_t durationS);
void remove(int i);
void clearAll();
int missedUnseen();
void markSeen();
const char* lastDialed();             // for redial, "" if none
void service(uint32_t nowMs, bool allowWrite);

}  // namespace calllog
