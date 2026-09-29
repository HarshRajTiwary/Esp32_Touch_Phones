#pragma once
// Helpers shared by the screen implementation files.
#include <stddef.h>

namespace screens {
void registerMain();
void registerPhone();
void registerSms();
void registerContacts();
void registerSettings();

// Contact name for a number (or the number), converted for our fonts.
void displayName(const char* number, char* out, size_t cap);
// Finds the first phone number (>= 10 digits) inside a message text.
bool findPhoneNumber(const char* text, char* out, size_t cap);
// Label + value row used by the info screens.
void infoRow(int y, const char* label, const char* value);
}  // namespace screens
