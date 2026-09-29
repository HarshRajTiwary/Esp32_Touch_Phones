#pragma once
// Parsers for SIM800L AT responses and unsolicited result codes (URCs).
// Pure C++ - covered by tests/host.
#include <stddef.h>
#include <stdint.h>

#include "../core/phone_types.h"

enum class AtFinal : uint8_t { None, Ok, Error, CmeError, CmsError, NoCarrier, Busy, NoAnswer, NoDialtone };

// Classifies a response line. `code` receives the +CME/+CMS error number.
AtFinal atClassifyFinal(const char* line, int* code);
const char* atErrorText(AtFinal f, int code);

// Comma separated argument list after "+XXX: ", quotes stripped.
struct AtArgs {
  static constexpr int kMax = 10;
  static constexpr int kCap = 80;
  int n = 0;
  char f[kMax][kCap];
  bool quoted[kMax];
  int i(int k, int def = -1) const;       // integer value of field k
  const char* s(int k) const { return k < n ? f[k] : ""; }
};
// Parses the part after the first ':' of `line` (or the whole line if none).
bool atParseArgs(const char* line, AtArgs* out);

struct ClccInfo {
  int idx = 0, dir = 0, stat = 0, mode = 0, mpty = 0, type = 0;
  char number[NUMBER_MAX] = "";
};
bool parseClcc(const char* line, ClccInfo* out);
bool parseClip(const char* line, char* number, size_t cap);
bool parseCmti(const char* line, int* index);
bool parseCsq(const char* line, int* rssi);
bool parseCreg(const char* line, RegState* reg);
bool parseCops(const char* line, char* op, size_t cap);
bool parseCbc(const char* line, int* pct, int* mv);
bool parseCclk(const char* line, uint32_t* epoch, int* tzQuarters);
bool parseCpin(const char* line, SimState* sim);
bool parseCusd(const char* line, int* m, char* text, size_t cap, int* dcs);
bool parseCmgrHeader(const char* line, int* stat, int* len);
bool parseCmglHeader(const char* line, int* index, int* stat, int* len);
bool parseCpbr(const char* line, int* idx, char* number, size_t ncap, char* name, size_t namecap);
bool parseCnum(const char* line, char* number, size_t cap);

// Signal quality helpers
int csqToDbm(int csq);        // -113..-51, 0 if unknown
int csqToBars(int csq);       // 0..4

// USSD text decoding (UCS2 hex or plain), output UTF-8.
void decodeUssd(const char* raw, int dcs, char* out, size_t cap);

// True if the line starts a quoted string that is not closed on the same line
// (SIM800 prints multi-line USSD menus with raw newlines inside the quotes).
bool atHasOpenQuote(const char* line);
