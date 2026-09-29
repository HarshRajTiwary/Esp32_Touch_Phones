#pragma once
// Input rules for network settings and the data reconnect schedule.
// Pure C++ (host-tested).
#include <stddef.h>
#include <stdint.h>

namespace netrules {

// True when `s` can be placed inside a quoted AT command argument: printable
// ASCII only, no quote or backslash (they would end the argument early and let
// the rest be interpreted as a new command).
bool atSafe(const char* s);

// Hotspot name: 1..32 printable ASCII characters.
bool validSsid(const char* s, const char** why);
// WPA2 passphrase: 8..63 printable ASCII characters.
bool validWpaPass(const char* s, const char** why);
// APN: 1..63 characters, letters, digits, '.', '-', '_'.
bool validApn(const char* s, const char** why);

// Exponential reconnect delay: doubles from `minMs` up to `maxMs`.
uint32_t nextBackoff(uint32_t currentMs, uint32_t minMs, uint32_t maxMs);

}  // namespace netrules
