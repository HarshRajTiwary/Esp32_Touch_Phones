#pragma once
// SMS store (LittleFS /sms.bin). Messages are kept in arrival order in PSRAM
// and grouped into conversation threads by phone number for the UI.
#include <stddef.h>

#include "phone_types.h"

namespace messages {

void load();
int count();
int capacity();
SmsRecord& at(int i);                       // 0 = oldest
uint32_t add(const SmsRecord& r);           // assigns and returns the id; drops the oldest when full
SmsRecord* byId(uint32_t id);
void setFlags(uint32_t id, uint8_t set, uint8_t clear);
void remove(uint32_t id);
void removeThread(const char* number);
void markThreadRead(const char* number);
int unreadCount();
void clearAll();

struct Thread {
  char number[NUMBER_MAX];
  int lastIndex;    // index of the newest message
  int unread;
  int total;
};
// Newest conversation first. Returns the number of threads written.
int buildThreads(Thread* out, int max);
// Indices of the messages in one conversation, oldest first.
int threadIndices(const char* number, int* out, int max);

void service(uint32_t nowMs, bool allowWrite);

}  // namespace messages
