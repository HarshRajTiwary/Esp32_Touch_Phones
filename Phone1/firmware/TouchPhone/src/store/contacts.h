#pragma once
// Local contact list (LittleFS /contacts.bin), kept sorted by name.
#include <stddef.h>

#include <vector>

#include "../core/phone_types.h"

namespace contacts {

void load();
int count();
const Contact& at(int i);
int add(const char* name, const char* number);          // returns new index or -1
int update(int i, const char* name, const char* number);  // returns the new index or -1
void remove(int i);
void clearAll();
int findByNumber(const char* number);                    // -1 if unknown
int indexOf(const char* name, const char* number);
// Writes the contact name for a number, or the number itself.
void displayName(const char* number, char* out, size_t cap);
int importList(const std::vector<Contact>& list);        // returns number added
void service(uint32_t nowMs, bool allowWrite);           // deferred save

}  // namespace contacts
