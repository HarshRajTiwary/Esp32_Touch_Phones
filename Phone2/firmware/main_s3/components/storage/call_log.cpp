#include "call_log.h"

#include <stdlib.h>
#include <string.h>

#include "plog.h"
#include "text_util.h"
#include "storage.h"

namespace calllog {

static const char* kPath = "/calls.bin";
static const uint32_t kMagic = 0x434C4731;  // "CLG1"
static const uint8_t kUnseen = 0x01;        // stored in CallLogEntry::reserved

static CallLogEntry s_list[MAX_CALL_LOG];   // index 0 = newest
static int s_count = 0;
static storage::Deferred s_save;

void load() {
  s_count = 0;
  storage::loadFile(kPath, [](storage::Source& in, size_t, void*) -> bool {
    storage::FileHeader h;
    if (in.read(&h, sizeof h) != sizeof h || h.magic != kMagic || h.recSize != sizeof(CallLogEntry)) return false;
    int n = (int)h.count;
    if (n > MAX_CALL_LOG) n = MAX_CALL_LOG;
    if (in.read(s_list, sizeof(CallLogEntry) * n) != sizeof(CallLogEntry) * n) return false;
    for (int i = 0; i < n; ++i) s_list[i].number[NUMBER_MAX - 1] = 0;
    s_count = n;
    return true;
  }, nullptr);
  LOG("DATA", "%d call log entries", s_count);
}

static void save() {
  bool ok = storage::saveFile(kPath, [](storage::Sink& out, void*) -> bool {
    storage::FileHeader h{kMagic, 1, (uint16_t)sizeof(CallLogEntry), (uint32_t)s_count};
    return out.write(&h, sizeof h) && out.write(s_list, sizeof(CallLogEntry) * s_count);
  }, nullptr);
  if (ok) s_save.dirty = false;
}

void service(uint32_t nowMs, bool allowWrite) {
  if (s_save.due(nowMs, allowWrite)) save();
}

int count() { return s_count; }

const CallLogEntry& at(int i) {
  static CallLogEntry empty{};
  return (i >= 0 && i < s_count) ? s_list[i] : empty;
}

void add(const char* number, CallType type, uint32_t ts, uint16_t durationS) {
  if (s_count == MAX_CALL_LOG) --s_count;
  memmove(&s_list[1], &s_list[0], sizeof(CallLogEntry) * s_count);
  CallLogEntry& e = s_list[0];
  memset(&e, 0, sizeof e);
  txt::copy(e.number, number && *number ? number : "Unknown", sizeof e.number);
  e.ts = ts;
  e.duration = durationS;
  e.type = (uint8_t)type;
  e.reserved = type == CallType::Missed ? kUnseen : 0;
  ++s_count;
  s_save.touch();
}

void remove(int i) {
  if (i < 0 || i >= s_count) return;
  memmove(&s_list[i], &s_list[i + 1], sizeof(CallLogEntry) * (s_count - i - 1));
  --s_count;
  s_save.touch();
}

void clearAll() {
  s_count = 0;
  s_save.touch();
}

int missedUnseen() {
  int n = 0;
  for (int i = 0; i < s_count; ++i) n += (s_list[i].reserved & kUnseen) ? 1 : 0;
  return n;
}

void markSeen() {
  bool changed = false;
  for (int i = 0; i < s_count; ++i)
    if (s_list[i].reserved & kUnseen) {
      s_list[i].reserved &= ~kUnseen;
      changed = true;
    }
  if (changed) s_save.touch();
}

const char* lastDialed() {
  for (int i = 0; i < s_count; ++i)
    if (s_list[i].type == (uint8_t)CallType::Outgoing) return s_list[i].number;
  return s_count ? s_list[0].number : "";
}

}  // namespace calllog
