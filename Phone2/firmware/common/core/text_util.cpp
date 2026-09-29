#include "text_util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace txt {

const char* const kMonthShort[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kDayShort[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// ---------------------------------------------------------------------------
void copy(char* dst, const char* src, size_t cap) {
  if (!cap) return;
  if (!src) { dst[0] = 0; return; }
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  memmove(dst, src, n);
  dst[n] = 0;
}

bool startsWith(const char* s, const char* prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool ieq(const char* a, const char* b) { return icmp(a, b) == 0; }

int icmp(const char* a, const char* b) {
  while (*a && *b) {
    int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
    if (ca != cb) return ca - cb;
    ++a; ++b;
  }
  return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

bool icontains(const char* hay, const char* needle) {
  if (!needle || !*needle) return true;
  size_t n = strlen(needle);
  for (const char* p = hay; *p; ++p) {
    size_t i = 0;
    while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) ++i;
    if (i == n) return true;
  }
  return false;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void trim(char* s) {
  size_t n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
  size_t i = 0;
  while (s[i] && isspace((unsigned char)s[i])) ++i;
  if (i) memmove(s, s + i, n - i + 1);
}

uint32_t hash(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return h;
}

// ---------------------------------------------------------------------------
int32_t utf8Next(const char*& p) {
  const uint8_t* s = (const uint8_t*)p;
  if (!*s) return -1;
  uint32_t c = *s;
  int extra;
  if (c < 0x80) { p++; return (int32_t)c; }
  else if ((c & 0xE0) == 0xC0) { c &= 0x1F; extra = 1; }
  else if ((c & 0xF0) == 0xE0) { c &= 0x0F; extra = 2; }
  else if ((c & 0xF8) == 0xF0) { c &= 0x07; extra = 3; }
  else { p++; return 0xFFFD; }
  for (int i = 1; i <= extra; ++i) {
    if ((s[i] & 0xC0) != 0x80) { p += i; return 0xFFFD; }
    c = (c << 6) | (s[i] & 0x3F);
  }
  p += extra + 1;
  return (int32_t)c;
}

int utf8Encode(uint32_t cp, char* o) {
  if (cp < 0x80) { o[0] = (char)cp; return 1; }
  if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000) {
    o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[2] = (char)(0x80 | (cp & 0x3F)); return 3;
  }
  o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

size_t utf8Append(char* dst, size_t used, size_t cap, uint32_t cp) {
  char tmp[4];
  int n = utf8Encode(cp, tmp);
  if (used + n + 1 > cap) return used;
  memcpy(dst + used, tmp, n);
  used += n;
  dst[used] = 0;
  return used;
}

size_t utf8Truncate(char* s, size_t maxBytes) {
  size_t n = strlen(s);
  if (n <= maxBytes) return n;
  n = maxBytes;
  while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) --n;  // back up to a lead byte
  s[n] = 0;
  return n;
}

size_t utf8Length(const char* s) {
  size_t n = 0;
  while (utf8Next(s) >= 0) ++n;
  return n;
}

void utf8PopBack(char* s) {
  size_t n = strlen(s);
  if (!n) return;
  --n;
  while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) --n;
  s[n] = 0;
}

// Latin-1 supplement (U+00C0..U+00FF) -> closest ASCII letter.
static const char kLatin1[] =
    "AAAAAAACEEEEIIII"   // C0-CF
    "DNOOOOOxOUUUUYPs"   // D0-DF
    "aaaaaaaceeeeiiii"   // E0-EF
    "dnooooo/ouuuuypy";  // F0-FF

static const char* translit(uint32_t cp) {
  switch (cp) {
    case 0x00A0: return " ";
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: case 0x00B4: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return "-";
    case 0x2026: return "...";
    case 0x20B9: return "Rs.";
    case 0x20AC: return "EUR";
    case 0x00A3: return "GBP";
    case 0x00A5: return "JPY";
    case 0x2022: case 0x00B7: case 0x25CF: return "*";
    case 0x00A9: return "(c)";
    case 0x00AE: return "(R)";
    case 0x2122: return "TM";
    case 0x00B0: return "deg";
    case 0x00BD: return "1/2";
    case 0x00A1: return "!";
    case 0x00BF: return "?";
    case 0x00A7: return "S";
    case 0x00AB: return "<<";
    case 0x00BB: return ">>";
    case 0x2190: return "<-";
    case 0x2192: return "->";
    case 0x2713: case 0x2714: case 0x2705: return "[ok]";
    case 0x200B: case 0x200C: case 0x200D: case 0xFE0F: case 0xFEFF: return "";  // invisible
    default: break;
  }
  if (cp >= 0x1F600 && cp <= 0x1F64F) return ":)";
  if ((cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF)) return "*";
  return nullptr;
}

bool toDisplay(const char* utf8, char* out, size_t cap) {
  bool lossy = false;
  size_t o = 0;
  bool lastUnknown = false;
  const char* p = utf8;
  int32_t cp;
  if (!cap) return false;
  while ((cp = utf8Next(p)) >= 0 && o + 1 < cap) {
    const char* rep = nullptr;
    char one[2] = {0, 0};
    if (cp == '\r') continue;
    if (cp == '\t') cp = ' ';
    if (cp == '\n' || (cp >= 0x20 && cp < 0x7F)) { one[0] = (char)cp; rep = one; }
    else if (cp >= 0xC0 && cp <= 0xFF) { one[0] = kLatin1[cp - 0xC0]; rep = one; }
    else rep = translit((uint32_t)cp);
    if (!rep) {
      lossy = true;
      if (lastUnknown) continue;  // collapse runs of unknown glyphs
      rep = "?";
      lastUnknown = true;
    } else {
      lastUnknown = false;
    }
    for (const char* r = rep; *r && o + 1 < cap; ++r) out[o++] = *r;
  }
  out[o] = 0;
  return lossy;
}

// ---------------------------------------------------------------------------
void digitsOnly(const char* in, char* out, size_t cap) {
  size_t o = 0;
  for (; *in && o + 1 < cap; ++in)
    if (*in >= '0' && *in <= '9') out[o++] = *in;
  out[o] = 0;
}

bool numbersMatch(const char* a, const char* b) {
  if (!a || !b || !*a || !*b) return false;
  char da[32], db[32];
  digitsOnly(a, da, sizeof da);
  digitsOnly(b, db, sizeof db);
  size_t la = strlen(da), lb = strlen(db);
  if (la < 5 || lb < 5) return ieq(a, b);  // short codes / alphanumeric senders
  size_t n = la < lb ? la : lb;
  if (n > 10) n = 10;
  return strcmp(da + la - n, db + lb - n) == 0;
}

bool looksLikeNumber(const char* s) {
  int digits = 0;
  for (; *s; ++s) {
    if (*s >= '0' && *s <= '9') digits++;
    else if (*s != '+' && *s != ' ' && *s != '-' && *s != '(' && *s != ')') return false;
  }
  return digits >= 3;
}

void sanitizeDial(const char* in, char* out, size_t cap) {
  size_t o = 0;
  for (; *in && o + 1 < cap; ++in) {
    char c = *in;
    if ((c >= '0' && c <= '9') || c == '*' || c == '#' || (c == '+' && o == 0)) out[o++] = c;
  }
  out[o] = 0;
}

bool isUssdCode(const char* s) {
  size_t n = strlen(s);
  return n >= 3 && (s[0] == '*' || s[0] == '#') && s[n - 1] == '#';
}

void maskNumber(const char* in, char* out, size_t cap) {
  size_t n = strlen(in);
  if (n <= 5) { copy(out, in, cap); return; }
  size_t o = 0;
  for (size_t i = 0; i < n && o + 1 < cap; ++i) {
    bool keep = (i < 3) || (i >= n - 4) || !(in[i] >= '0' && in[i] <= '9');
    out[o++] = keep ? in[i] : '*';
  }
  out[o] = 0;
}

void initials(const char* name, char* out3) {
  int o = 0;
  bool newWord = true;
  for (const char* p = name; *p && o < 2; ++p) {
    if (isspace((unsigned char)*p)) { newWord = true; continue; }
    if (newWord && isalnum((unsigned char)*p)) { out3[o++] = (char)toupper((unsigned char)*p); }
    newWord = false;
  }
  if (!o) out3[o++] = '#';
  out3[o] = 0;
}

// ---------------------------------------------------------------------------
// Howard Hinnant's civil-date algorithms.
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

uint32_t makeEpoch(int year, int mon, int day, int hour, int min, int sec) {
  int64_t days = daysFromCivil(year, (unsigned)mon, (unsigned)day);
  int64_t t = days * 86400 + hour * 3600 + min * 60 + sec;
  return t < 0 ? 0 : (uint32_t)t;
}

void splitEpoch(uint32_t t, int* year, int* mon, int* day, int* hour, int* min, int* sec, int* wday) {
  int64_t z = t / 86400;
  uint32_t rem = t % 86400;
  if (hour) *hour = (int)(rem / 3600);
  if (min) *min = (int)((rem / 60) % 60);
  if (sec) *sec = (int)(rem % 60);
  if (wday) *wday = (int)((z + 4) % 7);  // 1970-01-01 was a Thursday
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = (int)(yoe + era * 400);
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  if (year) *year = y + (m <= 2);
  if (mon) *mon = (int)m;
  if (day) *day = (int)d;
}

void fmtClock(uint32_t t, char* out) {
  int h, m;
  splitEpoch(t, nullptr, nullptr, nullptr, &h, &m, nullptr, nullptr);
  snprintf(out, 6, "%02d:%02d", h, m);
}

void fmtDayDate(uint32_t t, char* out) {
  int mon, day, wd;
  splitEpoch(t, nullptr, &mon, &day, nullptr, nullptr, nullptr, &wd);
  snprintf(out, 16, "%s, %d %s", kDayShort[wd], day, kMonthShort[mon - 1]);
}

void fmtWhen(uint32_t t, uint32_t now, char* out, size_t cap) {
  if (!t) { copy(out, "", cap); return; }
  uint32_t dayT = t / 86400, dayN = now / 86400;
  if (now && dayT == dayN) { char c[6]; fmtClock(t, c); copy(out, c, cap); return; }
  if (now && dayN == dayT + 1) { copy(out, "Yesterday", cap); return; }
  int y, mon, day, wd, yn;
  splitEpoch(t, &y, &mon, &day, nullptr, nullptr, nullptr, &wd);
  splitEpoch(now, &yn, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
  if (now && dayN > dayT && dayN - dayT < 7) { copy(out, kDayShort[wd], cap); return; }
  if (!now || y == yn) snprintf(out, cap, "%d %s", day, kMonthShort[mon - 1]);
  else snprintf(out, cap, "%02d/%02d/%02d", day, mon, y % 100);
}

void fmtDuration(uint32_t s, char* out) {
  if (s >= 3600) snprintf(out, 16, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
  else snprintf(out, 16, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

}  // namespace txt
