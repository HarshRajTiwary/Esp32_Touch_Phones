#include "contacts.h"

#include <stdlib.h>
#include <string.h>

#include "plog.h"
#include "text_util.h"
#include "storage.h"

namespace contacts {

static const char* kPath = "/contacts.bin";
static const uint32_t kMagic = 0x43544331;  // "CTC1"

static Contact* s_list = nullptr;
static int s_count = 0;
static storage::Deferred s_save;

static void ensure() {
  if (!s_list) s_list = (Contact*)storage::allocLarge(sizeof(Contact) * MAX_CONTACTS);
}

static void sortList() {
  // insertion sort: the list is almost always already sorted
  for (int i = 1; i < s_count; ++i) {
    Contact c = s_list[i];
    int j = i - 1;
    while (j >= 0 && txt::icmp(s_list[j].name, c.name) > 0) {
      s_list[j + 1] = s_list[j];
      --j;
    }
    s_list[j + 1] = c;
  }
}

void load() {
  ensure();
  s_count = 0;
  storage::loadFile(kPath, [](storage::Source& in, size_t size, void*) -> bool {
    storage::FileHeader h;
    if (in.read(&h, sizeof h) != sizeof h || h.magic != kMagic || h.recSize != sizeof(Contact)) return false;
    int n = (int)h.count;
    if (n > MAX_CONTACTS) n = MAX_CONTACTS;
    if (in.read(s_list, sizeof(Contact) * n) != sizeof(Contact) * n) return false;
    for (int i = 0; i < n; ++i) {
      s_list[i].name[CONTACT_NAME_MAX - 1] = 0;
      s_list[i].number[NUMBER_MAX - 1] = 0;
    }
    s_count = n;
    return true;
  }, nullptr);
  sortList();
  LOG("DATA", "%d contacts", s_count);
}

static void save() {
  bool ok = storage::saveFile(kPath, [](storage::Sink& out, void*) -> bool {
    storage::FileHeader h{kMagic, 1, (uint16_t)sizeof(Contact), (uint32_t)s_count};
    return out.write(&h, sizeof h) && out.write(s_list, sizeof(Contact) * s_count);
  }, nullptr);
  if (ok) s_save.dirty = false;
}

void service(uint32_t nowMs, bool allowWrite) {
  if (s_save.due(nowMs, allowWrite)) save();
}

int count() { return s_count; }

const Contact& at(int i) {
  static Contact empty{};
  return (i >= 0 && i < s_count) ? s_list[i] : empty;
}

int indexOf(const char* name, const char* number) {
  for (int i = 0; i < s_count; ++i)
    if (strcmp(s_list[i].name, name) == 0 && strcmp(s_list[i].number, number) == 0) return i;
  return -1;
}

int add(const char* name, const char* number) {
  ensure();
  if (s_count >= MAX_CONTACTS || !number || !*number) return -1;
  Contact c;
  txt::copy(c.name, name && *name ? name : number, sizeof c.name);
  txt::copy(c.number, number, sizeof c.number);
  s_list[s_count++] = c;
  sortList();  // moves entries around, so look the new one up again
  s_save.touch();
  return indexOf(c.name, c.number);
}

int update(int i, const char* name, const char* number) {
  if (i < 0 || i >= s_count || !number || !*number) return -1;
  Contact c;
  txt::copy(c.name, name && *name ? name : number, sizeof c.name);
  txt::copy(c.number, number, sizeof c.number);
  s_list[i] = c;
  sortList();
  s_save.touch();
  return indexOf(c.name, c.number);
}

void remove(int i) {
  if (i < 0 || i >= s_count) return;
  memmove(&s_list[i], &s_list[i + 1], sizeof(Contact) * (s_count - i - 1));
  --s_count;
  s_save.touch();
}

void clearAll() {
  s_count = 0;
  s_save.touch();
}

int findByNumber(const char* number) {
  if (!number || !*number) return -1;
  for (int i = 0; i < s_count; ++i)
    if (txt::numbersMatch(s_list[i].number, number)) return i;
  return -1;
}

void displayName(const char* number, char* out, size_t cap) {
  int i = findByNumber(number);
  if (i >= 0) txt::copy(out, s_list[i].name, cap);
  else if (number && *number) txt::copy(out, number, cap);
  else txt::copy(out, "Unknown", cap);
}

int importList(const std::vector<Contact>& list) {
  int added = 0;
  for (const auto& c : list) {
    if (s_count >= MAX_CONTACTS) break;
    if (findByNumber(c.number) >= 0) continue;
    s_list[s_count++] = c;
    ++added;
  }
  if (added) {
    sortList();
    s_save.touch();
  }
  return added;
}

}  // namespace contacts
