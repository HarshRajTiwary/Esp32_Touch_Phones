#pragma once
// Parsers for SIMCom A76XX (A7672S) AT responses and unsolicited result codes
// (URCs). Mostly 3GPP TS 27.007/27.005 syntax plus SIMCom extensions.
// Pure C++ - covered by tests/host.
#include <stddef.h>
#include <stdint.h>

#include "phone_types.h"

enum class AtFinal : uint8_t { None, Ok, Error, CmeError, CmsError, NoCarrier, Busy, NoAnswer, NoDialtone };

// Classifies a response line. `code` receives the +CME/+CMS error number.
AtFinal atClassifyFinal(const char* line, int* code);
const char* atErrorText(AtFinal f, int code);

// Comma separated argument list after "+XXX: ", quotes stripped.
struct AtArgs {
  static constexpr int kMax = 16;   // +CPSI (LTE) has 14 fields
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
// +CREG / +CGREG / +CEREG in URC or query form. `act` receives the access
// technology field (0 = GSM, 7 = E-UTRAN/LTE) or -1 if absent.
bool parseRegistration(const char* line, const char* prefix, RegState* reg, int* act);
bool parseCops(const char* line, char* op, size_t cap);
// "+CBC: 3.749V" (A76XX, voltage only) or "+CBC: 0,85,4100" (3GPP). pct = -1 if not reported.
bool parseCbc(const char* line, int* pct, int* mv);
bool parseCclk(const char* line, uint32_t* epoch, int* tzQuarters);
bool parseCpin(const char* line, SimState* sim);
bool parseCusd(const char* line, int* m, char* text, size_t cap, int* dcs);
bool parseCmgrHeader(const char* line, int* stat, int* len);
bool parseCmglHeader(const char* line, int* index, int* stat, int* len);
bool parseCpbr(const char* line, int* idx, char* number, size_t ncap, char* name, size_t namecap);
bool parseCnum(const char* line, char* number, size_t cap);

// ---- SIMCom A76XX extensions ----
struct CpsiInfo {
  Rat rat = Rat::None;
  bool online = false;
  char band[16] = "";         // "LTE BAND3", "EGSM 900"
  int rsrpDbm = 0;            // LTE only, 0 = unknown
  int rsrqDb = 0;
  int sinrDb = 0;
};
bool parseCpsi(const char* line, CpsiInfo* out);
// "VOICE CALL: BEGIN" -> begin=true; "VOICE CALL: END: 000017" -> begin=false, seconds=17
bool parseVoiceCall(const char* line, bool* begin, int* seconds);
// "MISSED_CALL: 03:56PM +919876543210" -> the number (may be empty)
bool parseMissedCall(const char* line, char* number, size_t cap);
bool parseCnmp(const char* line, int* mode);
bool parseCtzv(const char* line, int* tzQuarters);
bool parseIccid(const char* line, char* iccid, size_t cap);   // "+ICCID: 8991..." or "+CCID: ..."
bool parseCpmutemp(const char* line, int* celsius);
// "+CGEV: NW PDN DEACT 1" etc. -> true when the network dropped the data context.
bool isPdpDeactivation(const char* line);
// Modem restarted by itself (boot banners that must never appear while running).
bool isModemBootBanner(const char* line);

// RSRP (dBm) -> 0..4 bars; CSQ is used for GSM.
int rsrpToBars(int rsrpDbm);

// Signal quality helpers
int csqToDbm(int csq);        // -113..-51, 0 if unknown
int csqToBars(int csq);       // 0..4

// USSD text decoding (UCS2 hex or plain), output UTF-8.
void decodeUssd(const char* raw, int dcs, char* out, size_t cap);

// True if the line starts a quoted string that is not closed on the same line
// (modems print multi-line USSD menus with raw newlines inside the quotes).
bool atHasOpenQuote(const char* line);
