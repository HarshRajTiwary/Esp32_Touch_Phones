#include "messages.h"

#include <stdlib.h>
#include <string.h>

#include "plog.h"
#include "text_util.h"
#include "storage.h"

namespace messages {

static const char* kPath = "/sms.bin";
static const uint32_t kMagic = 0x534D5331;  // "SMS1"

// On-disk record: fixed part followed by `len` bytes of UTF-8 text.
struct DiskRec {
  uint32_t id;
  uint32_t ts;
  char number[NUMBER_MAX];
  uint8_t flags;
  uint8_t reserved;
  uint16_t len;
};

static SmsRecord* s_list = nullptr;
static int s_cap = 0;
static int s_count = 0;
static uint32_t s_nextId = 1;
static storage::Deferred s_save;

static void ensure() {
  if (s_list) return;
  s_cap = storage::hasPsram() ? MAX_SMS : 60;  // without PSRAM keep only the newest 60 in RAM
  s_list = (SmsRecord*)storage::allocLarge(sizeof(SmsRecord) * s_cap);
  if (!s_list) s_cap = 0;
}

void load() {
  ensure();
  s_count = 0;
  storage::loadFile(kPath, [](storage::Source& in, size_t, void*) -> bool {
    storage::FileHeader h;
    if (in.read(&h, sizeof h) != sizeof h || h.magic != kMagic || h.recSize != sizeof(DiskRec)) return false;
    uint32_t n = h.count;
    // keep only the newest `s_cap` messages if the file holds more
    uint32_t skip = n > (uint32_t)s_cap ? n - s_cap : 0;
    for (uint32_t k = 0; k < n; ++k) {
      DiskRec d;
      if (in.read(&d, sizeof d) != sizeof d) return false;
      if (d.len >= SMS_TEXT_MAX) return false;
      SmsRecord tmp;
      if (in.read(tmp.text, d.len) != d.len) return false;
      if (k < skip) continue;
      SmsRecord& r = s_list[s_count++];
      r.id = d.id;
      r.ts = d.ts;
      memcpy(r.number, d.number, NUMBER_MAX);
      r.number[NUMBER_MAX - 1] = 0;
      r.flags = d.flags & ~SMS_SENDING;  // a send interrupted by a reboot failed
      if (d.flags & SMS_SENDING) r.flags |= SMS_FAILED;
      r.len = d.len;
      memcpy(r.text, tmp.text, d.len);
      r.text[d.len] = 0;
      if (r.id >= s_nextId) s_nextId = r.id + 1;
    }
    return true;
  }, nullptr);
  LOG("DATA", "%d messages (%d unread), capacity %d", s_count, unreadCount(), s_cap);
}

static void save() {
  bool ok = storage::saveFile(kPath, [](storage::Sink& out, void*) -> bool {
    storage::FileHeader h{kMagic, 1, (uint16_t)sizeof(DiskRec), (uint32_t)s_count};
    if (!out.write(&h, sizeof h)) return false;
    for (int i = 0; i < s_count; ++i) {
      const SmsRecord& r = s_list[i];
      DiskRec d;
      memset(&d, 0, sizeof d);
      d.id = r.id;
      d.ts = r.ts;
      memcpy(d.number, r.number, NUMBER_MAX);
      d.flags = r.flags;
      d.len = (uint16_t)strnlen(r.text, SMS_TEXT_MAX - 1);
      if (!out.write(&d, sizeof d) || !out.write(r.text, d.len)) return false;
    }
    return true;
  }, nullptr);
  if (ok) s_save.dirty = false;
}

void service(uint32_t nowMs, bool allowWrite) {
  if (s_save.due(nowMs, allowWrite)) save();
}

int count() { return s_count; }
int capacity() { return s_cap; }

SmsRecord& at(int i) {
  static SmsRecord empty{};
  return (i >= 0 && i < s_count) ? s_list[i] : empty;
}

uint32_t add(const SmsRecord& r) {
  ensure();
  if (!s_cap) return 0;
  if (s_count == s_cap) {  // full: drop the oldest message
    memmove(&s_list[0], &s_list[1], sizeof(SmsRecord) * (s_count - 1));
    --s_count;
  }
  SmsRecord& d = s_list[s_count++];
  d = r;
  d.id = s_nextId++;
  d.text[SMS_TEXT_MAX - 1] = 0;
  d.len = (uint16_t)strlen(d.text);
  s_save.touch();
  return d.id;
}

SmsRecord* byId(uint32_t id) {
  for (int i = s_count - 1; i >= 0; --i)
    if (s_list[i].id == id) return &s_list[i];
  return nullptr;
}

void setFlags(uint32_t id, uint8_t set, uint8_t clear) {
  SmsRecord* r = byId(id);
  if (!r) return;
  r->flags = (uint8_t)((r->flags & ~clear) | set);
  s_save.touch();
}

void remove(uint32_t id) {
  for (int i = 0; i < s_count; ++i)
    if (s_list[i].id == id) {
      memmove(&s_list[i], &s_list[i + 1], sizeof(SmsRecord) * (s_count - i - 1));
      --s_count;
      s_save.touch();
      return;
    }
}

void removeThread(const char* number) {
  int o = 0;
  for (int i = 0; i < s_count; ++i) {
    if (txt::numbersMatch(s_list[i].number, number)) continue;
    if (o != i) s_list[o] = s_list[i];
    ++o;
  }
  if (o != s_count) {
    s_count = o;
    s_save.touch();
  }
}

void markThreadRead(const char* number) {
  bool changed = false;
  for (int i = 0; i < s_count; ++i)
    if ((s_list[i].flags & SMS_UNREAD) && txt::numbersMatch(s_list[i].number, number)) {
      s_list[i].flags &= ~SMS_UNREAD;
      changed = true;
    }
  if (changed) s_save.touch();
}

int unreadCount() {
  int n = 0;
  for (int i = 0; i < s_count; ++i) n += (s_list[i].flags & SMS_UNREAD) ? 1 : 0;
  return n;
}

void clearAll() {
  s_count = 0;
  s_save.touch();
}

int buildThreads(Thread* out, int max) {
  int n = 0;
  for (int i = s_count - 1; i >= 0; --i) {
    const SmsRecord& r = s_list[i];
    int t = -1;
    for (int k = 0; k < n; ++k)
      if (txt::numbersMatch(out[k].number, r.number)) { t = k; break; }
    if (t < 0) {
      if (n >= max) continue;
      t = n++;
      txt::copy(out[t].number, r.number, sizeof out[t].number);
      out[t].lastIndex = i;
      out[t].unread = 0;
      out[t].total = 0;
    }
    out[t].total++;
    if (r.flags & SMS_UNREAD) out[t].unread++;
  }
  return n;
}

int threadIndices(const char* number, int* out, int max) {
  int n = 0;
  for (int i = 0; i < s_count && n < max; ++i)
    if (txt::numbersMatch(s_list[i].number, number)) out[n++] = i;
  // if the thread is longer than `max`, keep the newest ones
  if (n == max) {
    int total = 0;
    for (int i = 0; i < s_count; ++i) total += txt::numbersMatch(s_list[i].number, number) ? 1 : 0;
    if (total > max) {
      n = 0;
      int skip = total - max;
      for (int i = 0; i < s_count; ++i)
        if (txt::numbersMatch(s_list[i].number, number)) {
          if (skip > 0) { --skip; continue; }
          out[n++] = i;
        }
    }
  }
  return n;
}

}  // namespace messages
