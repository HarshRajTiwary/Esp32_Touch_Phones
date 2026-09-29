#include "net_rules.h"

#include <string.h>

namespace netrules {

static bool printable(const char* s) {
  for (const char* p = s; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c < 32 || c > 126) return false;
  }
  return true;
}

bool atSafe(const char* s) {
  if (!s || !printable(s)) return false;
  return !strchr(s, '"') && !strchr(s, '\\');
}

static bool fail(const char** why, const char* text) {
  if (why) *why = text;
  return false;
}

bool validSsid(const char* s, const char** why) {
  size_t n = s ? strlen(s) : 0;
  if (n == 0) return fail(why, "Enter a name");
  if (n > 32) return fail(why, "At most 32 characters");
  if (!printable(s)) return fail(why, "Only plain letters, digits and symbols");
  return true;
}

bool validWpaPass(const char* s, const char** why) {
  size_t n = s ? strlen(s) : 0;
  if (n < 8 || n > 63) return fail(why, "Use 8 to 63 characters");
  if (!printable(s)) return fail(why, "Only plain letters, digits and symbols");
  return true;
}

bool validApn(const char* s, const char** why) {
  size_t n = s ? strlen(s) : 0;
  if (n == 0) return fail(why, "The APN cannot be empty");
  if (n > 63) return fail(why, "At most 63 characters");
  for (size_t k = 0; k < n; ++k) {
    char c = s[k];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    if (!ok) return fail(why, "Only letters, digits, '.', '-' and '_'");
  }
  return true;
}

uint32_t nextBackoff(uint32_t cur, uint32_t minMs, uint32_t maxMs) {
  if (cur < minMs) return minMs;
  uint32_t next = cur > maxMs / 2 ? maxMs : cur * 2;
  return next > maxMs ? maxMs : next;
}

}  // namespace netrules
