#include "at_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text_util.h"
#include "sms_pdu.h"

AtFinal atClassifyFinal(const char* line, int* code) {
  if (code) *code = 0;
  if (strcmp(line, "OK") == 0) return AtFinal::Ok;
  if (strcmp(line, "ERROR") == 0) return AtFinal::Error;
  if (txt::startsWith(line, "+CME ERROR:")) {
    if (code) *code = atoi(line + 11);
    return AtFinal::CmeError;
  }
  if (txt::startsWith(line, "+CMS ERROR:")) {
    if (code) *code = atoi(line + 11);
    return AtFinal::CmsError;
  }
  if (strcmp(line, "NO CARRIER") == 0) return AtFinal::NoCarrier;
  if (strcmp(line, "BUSY") == 0) return AtFinal::Busy;
  if (strcmp(line, "NO ANSWER") == 0) return AtFinal::NoAnswer;
  if (strcmp(line, "NO DIALTONE") == 0) return AtFinal::NoDialtone;
  return AtFinal::None;
}

const char* atErrorText(AtFinal f, int code) {
  switch (f) {
    case AtFinal::Ok: return "OK";
    case AtFinal::NoCarrier: return "Call ended";
    case AtFinal::Busy: return "Number busy";
    case AtFinal::NoAnswer: return "No answer";
    case AtFinal::NoDialtone: return "No network";
    case AtFinal::CmeError:
      switch (code) {
        case 3: return "Not allowed now";
        case 4: return "Not supported";
        case 10: return "No SIM card";
        case 11: return "SIM PIN required";
        case 12: return "SIM PUK required";
        case 13: return "SIM failure";
        case 14: return "SIM busy";
        case 16: return "Wrong PIN";
        case 20: return "Memory full";
        case 30: return "No network";
        case 31: return "Network timeout";
        case 32: return "Emergency calls only";
        default: return "Modem error";
      }
    case AtFinal::CmsError:
      switch (code) {
        case 38: case 41: case 42: case 47: return "Network busy, try later";
        case 50: case 69: return "SMS not allowed";
        case 304: case 305: return "Invalid SMS";
        case 310: return "No SIM card";
        case 311: return "SIM PIN required";
        case 322: return "SIM memory full";
        case 330: return "SMS centre unknown";
        case 331: return "No network";
        case 332: return "Network timeout";
        case 500: return "SMS failed";
        default: return "SMS error";
      }
    default: return "Error";
  }
}

int AtArgs::i(int k, int def) const {
  if (k >= n || !f[k][0]) return def;
  return atoi(f[k]);
}

bool atParseArgs(const char* line, AtArgs* out) {
  out->n = 0;
  const char* p = strchr(line, ':');
  p = p ? p + 1 : line;
  while (*p == ' ') ++p;
  while (out->n < AtArgs::kMax) {
    char* dst = out->f[out->n];
    size_t o = 0;
    bool q = false;
    out->quoted[out->n] = false;
    while (*p == ' ') ++p;
    if (*p == '"') {
      q = true;
      out->quoted[out->n] = true;
      ++p;
      while (*p && *p != '"') {
        if (o + 1 < (size_t)AtArgs::kCap) dst[o++] = *p;
        ++p;
      }
      if (*p == '"') ++p;
      while (*p && *p != ',') ++p;  // skip anything after the closing quote
    } else {
      while (*p && *p != ',') {
        if (o + 1 < (size_t)AtArgs::kCap) dst[o++] = *p;
        ++p;
      }
      while (o && dst[o - 1] == ' ') --o;
    }
    (void)q;
    dst[o] = 0;
    out->n++;
    if (*p != ',') break;
    ++p;
  }
  return out->n > 0;
}

bool parseClcc(const char* line, ClccInfo* out) {
  if (!txt::startsWith(line, "+CLCC:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 5) return false;
  out->idx = a.i(0);
  out->dir = a.i(1);
  out->stat = a.i(2);
  out->mode = a.i(3);
  out->mpty = a.i(4);
  txt::copy(out->number, a.s(5), sizeof out->number);
  out->type = a.i(6, 129);
  if (out->type == 145 && out->number[0] && out->number[0] != '+') {
    char tmp[NUMBER_MAX];
    tmp[0] = '+';
    txt::copy(tmp + 1, out->number, sizeof tmp - 1);
    txt::copy(out->number, tmp, sizeof out->number);
  }
  return true;
}

bool parseClip(const char* line, char* number, size_t cap) {
  if (!txt::startsWith(line, "+CLIP:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 1) return false;
  if (a.i(1) == 145 && a.s(0)[0] && a.s(0)[0] != '+') {
    char tmp[NUMBER_MAX];
    tmp[0] = '+';
    txt::copy(tmp + 1, a.s(0), sizeof tmp - 1);
    txt::copy(number, tmp, cap);
  } else {
    txt::copy(number, a.s(0), cap);
  }
  return true;
}

bool parseCmti(const char* line, int* index) {
  if (!txt::startsWith(line, "+CMTI:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 2) return false;
  *index = a.i(1);
  return *index >= 0;
}

bool parseCsq(const char* line, int* rssi) {
  if (!txt::startsWith(line, "+CSQ:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 1) return false;
  *rssi = a.i(0, 99);
  return true;
}

static RegState regFromStat(int stat) {
  switch (stat) {
    case 0: return RegState::NotRegistered;
    case 1: return RegState::Home;
    case 2: return RegState::Searching;
    case 3: return RegState::Denied;
    case 5: return RegState::Roaming;
    default: return RegState::Unknown;
  }
}

bool parseRegistration(const char* line, const char* prefix, RegState* reg, int* act) {
  if (!txt::startsWith(line, prefix)) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 1) return false;
  // URC form "+CREG: <stat>[,<lac>,<ci>[,<AcT>]]" vs query form "+CREG: <n>,<stat>[,...]":
  // in the URC form the second field (if any) is the quoted location code.
  bool query = a.n >= 2 && !a.quoted[1];
  int base = query ? 1 : 0;
  *reg = regFromStat(a.i(base));
  if (act) *act = a.n > base + 3 ? a.i(base + 3, -1) : -1;
  return true;
}

bool parseCreg(const char* line, RegState* reg) { return parseRegistration(line, "+CREG:", reg, nullptr); }

bool parseCops(const char* line, char* op, size_t cap) {
  if (!txt::startsWith(line, "+COPS:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 3 || !a.s(2)[0]) { op[0] = 0; return true; }
  const char* name = a.s(2);
  if (txt::icontains(name, "airtel")) txt::copy(op, "Airtel", cap);
  else if (txt::icontains(name, "vodafone") || txt::icontains(name, "idea") || txt::ieq(name, "Vi")) txt::copy(op, "Vi", cap);
  else if (txt::icontains(name, "bsnl") || txt::icontains(name, "cellone")) txt::copy(op, "BSNL", cap);
  else txt::copy(op, name, cap);
  return true;
}

bool parseCbc(const char* line, int* pct, int* mv) {
  if (!txt::startsWith(line, "+CBC:")) return false;
  const char* v = line + 5;
  while (*v == ' ') ++v;
  if (strchr(v, '.') || strchr(v, 'V')) {  // A76XX: "+CBC: 3.749V"
    float volts = (float)atof(v);
    if (volts < 1.0f || volts > 6.0f) return false;
    *mv = (int)(volts * 1000.0f + 0.5f);
    *pct = -1;
    return true;
  }
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 3) return false;
  *pct = a.i(1, 0);
  *mv = a.i(2, 0);
  return true;
}

bool parseCclk(const char* line, uint32_t* epoch, int* tzQuarters) {
  if (!txt::startsWith(line, "+CCLK:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  // "yy/MM/dd,hh:mm:ss+zz" - the comma splits it into two fields when unquoted,
  // but most modems quote it, so it is one field.
  char buf[2 * AtArgs::kCap + 2];
  if (a.n >= 2 && !a.quoted[0]) snprintf(buf, sizeof buf, "%s,%s", a.s(0), a.s(1));
  else txt::copy(buf, a.s(0), sizeof buf);
  int yy, mo, dd, hh, mi, ss, tz = 0;
  char sign = '+';
  int got = sscanf(buf, "%d/%d/%d,%d:%d:%d%c%d", &yy, &mo, &dd, &hh, &mi, &ss, &sign, &tz);
  if (got < 6) return false;
  if (sign == '-') tz = -tz;
  if (mo < 1 || mo > 12 || dd < 1 || dd > 31 || hh > 23 || mi > 59 || ss > 60) return false;
  *epoch = txt::makeEpoch(2000 + yy, mo, dd, hh, mi, ss);
  if (tzQuarters) *tzQuarters = tz;
  return true;
}

bool parseCpin(const char* line, SimState* sim) {
  if (!txt::startsWith(line, "+CPIN:")) return false;
  const char* v = line + 6;
  while (*v == ' ') ++v;
  if (txt::startsWith(v, "READY")) *sim = SimState::Ready;
  else if (txt::startsWith(v, "SIM PIN")) *sim = SimState::PinRequired;
  else if (txt::startsWith(v, "SIM PUK")) *sim = SimState::PukRequired;
  else if (txt::startsWith(v, "NOT INSERTED")) *sim = SimState::Absent;
  else if (txt::startsWith(v, "NOT READY")) *sim = SimState::Unknown;  // normal for a few s after power-up
  else *sim = SimState::Error;
  return true;
}

bool atHasOpenQuote(const char* line) {
  int q = 0;
  for (const char* p = line; *p; ++p) q += *p == '"';
  return q & 1;
}

bool parseCusd(const char* line, int* m, char* text, size_t cap, int* dcs) {
  if (!txt::startsWith(line, "+CUSD:")) return false;
  const char* p = line + 6;
  while (*p == ' ') ++p;
  *m = atoi(p);
  *dcs = 15;
  text[0] = 0;
  const char* q1 = strchr(p, '"');
  if (!q1) return true;
  const char* q2 = strrchr(line, '"');
  if (q2 <= q1) return true;
  size_t n = (size_t)(q2 - q1 - 1);
  if (n >= cap) n = cap - 1;
  memcpy(text, q1 + 1, n);
  text[n] = 0;
  const char* c = strchr(q2, ',');
  if (c) *dcs = atoi(c + 1);
  return true;
}

void decodeUssd(const char* raw, int dcs, char* out, size_t cap) {
  size_t n = strlen(raw);
  bool allHex = n >= 4 && (n % 4) == 0;
  for (size_t k = 0; k < n && allHex; ++k) allHex = txt::hexVal(raw[k]) >= 0;
  bool ucs2 = dcs == 72 || dcs == 0x11 || ((dcs & 0xF0) == 0x40 && (dcs & 0x0C) == 0x08) ||
              (allHex && n >= 8 && (raw[0] == '0') && (raw[1] == '0' || raw[1] == '9'));
  if (ucs2 && allHex) ucs2HexToUtf8(raw, out, cap);
  else txt::copy(out, raw, cap);
}

bool parseCmgrHeader(const char* line, int* stat, int* len) {
  if (!txt::startsWith(line, "+CMGR:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 3) return false;
  *stat = a.i(0);
  *len = a.i(a.n - 1);
  return true;
}

bool parseCmglHeader(const char* line, int* index, int* stat, int* len) {
  if (!txt::startsWith(line, "+CMGL:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 4) return false;
  *index = a.i(0);
  *stat = a.i(1);
  *len = a.i(a.n - 1);
  return true;
}

bool parseCpbr(const char* line, int* idx, char* number, size_t ncap, char* name, size_t namecap) {
  if (!txt::startsWith(line, "+CPBR:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 4) return false;
  *idx = a.i(0);
  if (a.i(2) == 145 && a.s(1)[0] != '+') {
    char tmp[NUMBER_MAX];
    tmp[0] = '+';
    txt::copy(tmp + 1, a.s(1), sizeof tmp - 1);
    txt::copy(number, tmp, ncap);
  } else {
    txt::copy(number, a.s(1), ncap);
  }
  // Names stored as UCS2 come back as hex ("80" prefix or plain 4-hex groups).
  const char* nm = a.s(3);
  size_t nl = strlen(nm);
  bool hex = nl >= 4 && nl % 2 == 0;
  for (size_t k = 0; k < nl && hex; ++k) hex = txt::hexVal(nm[k]) >= 0;
  if (hex && txt::startsWith(nm, "80") && (nl - 2) % 4 == 0) ucs2HexToUtf8(nm + 2, name, namecap);
  else if (hex && nl % 4 == 0 && nm[0] == '0' && nm[1] == '0') ucs2HexToUtf8(nm, name, namecap);
  else txt::copy(name, nm, namecap);
  return true;
}

bool parseCnum(const char* line, char* number, size_t cap) {
  if (!txt::startsWith(line, "+CNUM:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 2) return false;
  if (a.i(2) == 145 && a.s(1)[0] != '+') {
    char tmp[NUMBER_MAX];
    tmp[0] = '+';
    txt::copy(tmp + 1, a.s(1), sizeof tmp - 1);
    txt::copy(number, tmp, cap);
  } else {
    txt::copy(number, a.s(1), cap);
  }
  return number[0] != 0;
}

int csqToDbm(int csq) {
  if (csq < 0 || csq > 31) return 0;
  return -113 + 2 * csq;
}

int csqToBars(int csq) {
  if (csq < 2 || csq > 31) return 0;
  if (csq < 8) return 1;
  if (csq < 14) return 2;
  if (csq < 20) return 3;
  return 4;
}

// ---------------------------------------------------------------------------
// SIMCom A76XX extensions
// ---------------------------------------------------------------------------
static int lteRsrp(int v) {
  if (v < -200) return v / 10;          // some firmware reports tenths of dBm
  if (v < 0) return v;                  // already dBm
  if (v >= 0 && v <= 97) return v - 140;  // 3GPP TS 36.133 report value
  return 0;
}

static int lteRsrq(int v) {
  if (v < -50) return v / 10;
  if (v < 0) return v;
  if (v >= 0 && v <= 34) return (v - 40) / 2;
  return 0;
}

bool parseCpsi(const char* line, CpsiInfo* out) {
  if (!txt::startsWith(line, "+CPSI:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  *out = CpsiInfo();
  if (a.n < 1) return false;
  const char* sys = a.s(0);
  if (txt::ieq(sys, "LTE")) out->rat = Rat::Lte;
  else if (txt::ieq(sys, "GSM")) out->rat = Rat::Gsm;
  else out->rat = Rat::None;  // "NO SERVICE", "WCDMA" (not on this module)
  out->online = a.n > 1 && txt::ieq(a.s(1), "Online");
  if (out->rat == Rat::Lte && a.n >= 12) {
    const char* b = a.s(6);  // "EUTRAN-BAND3"
    const char* num = strstr(b, "BAND");
    if (num) snprintf(out->band, sizeof out->band, "LTE B%s", num + 4);
    else txt::copy(out->band, b, sizeof out->band);
    out->rsrqDb = lteRsrq(a.i(10, 0));
    out->rsrpDbm = lteRsrp(a.i(11, 0));
    int sinr = a.n > 13 ? a.i(13, 0) : 0;
    out->sinrDb = (sinr > 60 || sinr < -30) ? sinr / 10 : sinr;
  } else if (out->rat == Rat::Gsm && a.n >= 6) {
    const char* b = strchr(a.s(5), ' ');  // "27 EGSM 900"
    txt::copy(out->band, b ? b + 1 : a.s(5), sizeof out->band);
  }
  return true;
}

bool parseVoiceCall(const char* line, bool* begin, int* seconds) {
  if (!txt::startsWith(line, "VOICE CALL:")) return false;
  const char* v = line + 11;
  while (*v == ' ') ++v;
  if (txt::startsWith(v, "BEGIN")) {
    *begin = true;
    if (seconds) *seconds = 0;
    return true;
  }
  if (!txt::startsWith(v, "END")) return false;
  *begin = false;
  if (seconds) {
    *seconds = 0;
    const char* t = strchr(v, ':');
    if (t) {
      ++t;
      while (*t == ' ') ++t;
      int digits = 0;
      for (const char* q = t; *q >= '0' && *q <= '9'; ++q) ++digits;
      long raw = atol(t);
      if (digits == 6) *seconds = (int)(raw / 10000 * 3600 + (raw / 100 % 100) * 60 + raw % 100);  // HHMMSS
      else *seconds = (int)raw;
    }
  }
  return true;
}

bool parseMissedCall(const char* line, char* number, size_t cap) {
  if (!txt::startsWith(line, "MISSED_CALL:")) return false;
  number[0] = 0;
  const char* last = strrchr(line, ' ');
  if (last && txt::looksLikeNumber(last + 1)) txt::copy(number, last + 1, cap);
  return true;
}

bool parseCnmp(const char* line, int* mode) {
  if (!txt::startsWith(line, "+CNMP:")) return false;
  AtArgs a;
  atParseArgs(line, &a);
  if (a.n < 1 || !a.s(0)[0] || a.s(0)[0] == '(') return false;  // skip the "=?" list form
  *mode = a.i(0);
  return true;
}

bool parseCtzv(const char* line, int* tzQuarters) {
  if (!txt::startsWith(line, "+CTZV:")) return false;
  const char* v = line + 6;
  while (*v == ' ') ++v;
  *tzQuarters = atoi(v);
  return true;
}

bool parseIccid(const char* line, char* iccid, size_t cap) {
  const char* v = nullptr;
  if (txt::startsWith(line, "+ICCID:")) v = line + 7;
  else if (txt::startsWith(line, "+CCID:")) v = line + 6;
  else if (txt::startsWith(line, "+CICCID:")) v = line + 8;
  if (!v) return false;
  while (*v == ' ' || *v == '"') ++v;
  size_t n = 0;
  while (v[n] && v[n] != '"' && n + 1 < cap) {
    iccid[n] = v[n];
    ++n;
  }
  iccid[n] = 0;
  return n >= 10;
}

bool parseCpmutemp(const char* line, int* celsius) {
  if (!txt::startsWith(line, "+CPMUTEMP:")) return false;
  *celsius = atoi(line + 10);
  return true;
}

bool isPdpDeactivation(const char* line) {
  if (!txt::startsWith(line, "+CGEV:")) return false;
  return strstr(line, "DEACT") || strstr(line, "DETACH");
}

bool isModemBootBanner(const char* line) {
  return strcmp(line, "RDY") == 0 || txt::startsWith(line, "*ATREADY") || strcmp(line, "START") == 0;
}

int rsrpToBars(int rsrp) {
  if (rsrp == 0 || rsrp < -125) return 0;
  if (rsrp < -115) return 1;
  if (rsrp < -105) return 2;
  if (rsrp < -95) return 3;
  return 4;
}
