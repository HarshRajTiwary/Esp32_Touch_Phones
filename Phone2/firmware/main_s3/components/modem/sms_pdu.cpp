#include "sms_pdu.h"

#include <string.h>

#include "text_util.h"

// GSM 03.38 default alphabet -> Unicode
static const uint16_t kGsm7[128] = {
    0x0040, 0x00A3, 0x0024, 0x00A5, 0x00E8, 0x00E9, 0x00F9, 0x00EC, 0x00F2, 0x00C7, 0x000A, 0x00D8, 0x00F8, 0x000D, 0x00C5, 0x00E5,
    0x0394, 0x005F, 0x03A6, 0x0393, 0x039B, 0x03A9, 0x03A0, 0x03A8, 0x03A3, 0x0398, 0x039E, 0x001B, 0x00C6, 0x00E6, 0x00DF, 0x00C9,
    0x0020, 0x0021, 0x0022, 0x0023, 0x00A4, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x00A1, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005A, 0x00C4, 0x00D6, 0x00D1, 0x00DC, 0x00A7,
    0x00BF, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007A, 0x00E4, 0x00F6, 0x00F1, 0x00FC, 0x00E0,
};

struct ExtChar { uint8_t code; uint16_t cp; };
static const ExtChar kGsm7Ext[] = {
    {0x0A, 0x000C}, {0x14, '^'}, {0x28, '{'}, {0x29, '}'}, {0x2F, '\\'},
    {0x3C, '['},   {0x3D, '~'}, {0x3E, ']'}, {0x40, '|'}, {0x65, 0x20AC},
};

static int gsm7Lookup(uint32_t cp, bool* ext) {
  *ext = false;
  if (cp == '\r') return 0x0D;
  for (int i = 0; i < 128; ++i)
    if (kGsm7[i] == cp && i != 0x1B) return i;
  for (const auto& e : kGsm7Ext)
    if (e.cp == cp) { *ext = true; return e.code; }
  return -1;
}

bool gsm7Encodable(uint32_t cp) {
  bool ext;
  return gsm7Lookup(cp, &ext) >= 0;
}

size_t gsm7UnpackedToUtf8(const uint8_t* s, size_t n, char* out, size_t cap) {
  size_t o = 0;
  if (cap) out[0] = 0;
  for (size_t i = 0; i < n; ++i) {
    uint8_t c = s[i] & 0x7F;
    uint32_t cp;
    if (c == 0x1B && i + 1 < n) {
      uint8_t e = s[++i] & 0x7F;
      cp = ' ';
      for (const auto& x : kGsm7Ext)
        if (x.code == e) cp = x.cp;
    } else {
      cp = kGsm7[c];
    }
    o = txt::utf8Append(out, o, cap, cp);
  }
  return o;
}

size_t ucs2HexToUtf8(const char* hex, char* out, size_t cap) {
  size_t o = 0, n = strlen(hex);
  if (cap) out[0] = 0;
  uint32_t hi = 0;
  for (size_t i = 0; i + 3 < n; i += 4) {
    int a = txt::hexVal(hex[i]), b = txt::hexVal(hex[i + 1]), c = txt::hexVal(hex[i + 2]), d = txt::hexVal(hex[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0) break;
    uint32_t u = (uint32_t)((a << 12) | (b << 8) | (c << 4) | d);
    if (u >= 0xD800 && u <= 0xDBFF) { hi = u; continue; }
    if (u >= 0xDC00 && u <= 0xDFFF) {
      if (!hi) continue;
      u = 0x10000 + ((hi - 0xD800) << 10) + (u - 0xDC00);
      hi = 0;
    }
    o = txt::utf8Append(out, o, cap, u);
  }
  return o;
}

// ---------------------------------------------------------------------------
namespace {

struct Reader {
  const uint8_t* b;
  size_t n;
  size_t i = 0;
  bool ok = true;
  uint8_t u8() {
    if (i >= n) { ok = false; return 0; }
    return b[i++];
  }
};

int bcd(uint8_t v) { return (v & 0x0F) * 10 + (v >> 4); }

uint8_t septetAt(const uint8_t* ud, size_t udBytes, size_t k) {
  size_t bit = k * 7, byte = bit / 8;
  unsigned sh = bit % 8;
  unsigned v = byte < udBytes ? ud[byte] >> sh : 0;
  if (sh > 1 && byte + 1 < udBytes) v |= (unsigned)ud[byte + 1] << (8 - sh);
  return (uint8_t)(v & 0x7F);
}

void decodeAddress(const uint8_t* p, int digits, uint8_t toa, char* out, size_t cap) {
  int ton = (toa >> 4) & 7;
  int octets = (digits + 1) / 2;
  if (ton == 5) {  // alphanumeric, GSM 7-bit packed
    uint8_t sept[24];
    size_t ns = (size_t)(digits * 4) / 7;
    if (ns > sizeof sept) ns = sizeof sept;
    for (size_t k = 0; k < ns; ++k) sept[k] = septetAt(p, (size_t)octets, k);
    gsm7UnpackedToUtf8(sept, ns, out, cap);
    return;
  }
  size_t o = 0;
  if (ton == 1 && o + 1 < cap) out[o++] = '+';
  static const char kDig[] = "0123456789*#abc";
  for (int k = 0; k < digits && o + 1 < cap; ++k) {
    uint8_t v = (k & 1) ? (p[k / 2] >> 4) : (p[k / 2] & 0x0F);
    if (v == 0x0F) break;
    out[o++] = kDig[v];
  }
  out[o] = 0;
}

enum Alphabet { A_GSM7, A_8BIT, A_UCS2 };

Alphabet alphabetFor(uint8_t dcs) {
  if ((dcs & 0xC0) == 0x00 || (dcs & 0xC0) == 0x40) {  // general data coding (+ auto-delete group)
    switch ((dcs >> 2) & 3) {
      case 1: return A_8BIT;
      case 2: return A_UCS2;
      default: return A_GSM7;
    }
  }
  if ((dcs & 0xF0) == 0xF0) return (dcs & 0x04) ? A_8BIT : A_GSM7;
  if ((dcs & 0xF0) == 0xE0) return A_UCS2;
  return A_GSM7;
}

}  // namespace

bool smsDecodePdu(const char* hex, SmsPdu* out) {
  *out = SmsPdu();
  uint8_t buf[200];
  size_t hn = strlen(hex);
  while (hn && (hex[hn - 1] == '\r' || hex[hn - 1] == '\n' || hex[hn - 1] == ' ')) --hn;
  if (hn < 4 || (hn & 1) || hn / 2 > sizeof buf) return false;
  for (size_t k = 0; k < hn / 2; ++k) {
    int a = txt::hexVal(hex[2 * k]), b = txt::hexVal(hex[2 * k + 1]);
    if (a < 0 || b < 0) return false;
    buf[k] = (uint8_t)((a << 4) | b);
  }
  Reader r{buf, hn / 2};
  uint8_t smscLen = r.u8();
  r.i += smscLen;
  uint8_t fo = r.u8();
  int mti = fo & 3;
  bool udhi = fo & 0x40;
  if (mti == 2) { out->isStatusReport = true; return r.ok; }
  if (mti == 1) {
    out->isSubmit = true;
    r.u8();  // message reference
  } else if (mti != 0) {
    return false;
  }
  int addrDigits = r.u8();
  uint8_t toa = r.u8();
  int addrOctets = (addrDigits + 1) / 2;
  if (!r.ok || r.i + addrOctets > r.n) return false;
  decodeAddress(buf + r.i, addrDigits, toa, out->number, sizeof out->number);
  r.i += addrOctets;
  r.u8();  // PID
  uint8_t dcs = r.u8();
  out->dcs = dcs;
  if (mti == 0) {
    if (r.i + 7 > r.n) return false;
    const uint8_t* ts = buf + r.i;
    int yy = bcd(ts[0]), mo = bcd(ts[1]), dd = bcd(ts[2]), hh = bcd(ts[3]), mi = bcd(ts[4]), ss = bcd(ts[5]);
    int tz = (ts[6] & 0x07) * 10 + (ts[6] >> 4);
    if (ts[6] & 0x08) tz = -tz;
    out->tzQuarters = tz;
    if (mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31 && hh < 24 && mi < 60 && ss < 60)
      out->ts = txt::makeEpoch(2000 + yy, mo, dd, hh, mi, ss);
    r.i += 7;
  } else {
    int vpf = (fo >> 3) & 3;
    if (vpf == 2) r.i += 1;
    else if (vpf == 1 || vpf == 3) r.i += 7;
  }
  uint8_t udl = r.u8();
  if (!r.ok) return false;
  const uint8_t* ud = buf + r.i;
  size_t udBytes = r.n - r.i;

  size_t udhOctets = 0;
  if (udhi && udBytes > 0) {
    uint8_t udhl = ud[0];
    udhOctets = (size_t)udhl + 1;
    if (udhOctets > udBytes) return false;
    size_t k = 1;
    while (k + 1 < udhOctets) {
      uint8_t iei = ud[k], iel = ud[k + 1];
      const uint8_t* v = ud + k + 2;
      if (k + 2 + iel > udhOctets) break;
      if (iei == 0x00 && iel == 3) {
        out->concat = true; out->ref = v[0]; out->total = v[1]; out->seq = v[2];
      } else if (iei == 0x08 && iel == 4) {
        out->concat = true; out->ref = (uint16_t)((v[0] << 8) | v[1]); out->total = v[2]; out->seq = v[3];
      }
      k += 2 + iel;
    }
    if (out->concat && (out->total == 0 || out->seq == 0 || out->seq > out->total)) out->concat = false;
  }

  switch (alphabetFor(dcs)) {
    case A_GSM7: {
      size_t hdrSeptets = udhi ? (udhOctets * 8 + 6) / 7 : 0;
      if (udl < hdrSeptets) return false;
      uint8_t sept[160];
      size_t ns = 0;
      for (size_t k = hdrSeptets; k < udl && ns < sizeof sept; ++k) sept[ns++] = septetAt(ud, udBytes, k);
      gsm7UnpackedToUtf8(sept, ns, out->text, sizeof out->text);
      break;
    }
    case A_UCS2: {
      size_t len = udl < udBytes ? udl : udBytes;
      if (len < udhOctets) return false;
      char hexbuf[2 * 140 + 1];
      size_t h = 0;
      static const char kHex[] = "0123456789ABCDEF";
      for (size_t k = udhOctets; k < len && h + 2 < sizeof hexbuf; ++k) {
        hexbuf[h++] = kHex[ud[k] >> 4];
        hexbuf[h++] = kHex[ud[k] & 0x0F];
      }
      hexbuf[h] = 0;
      ucs2HexToUtf8(hexbuf, out->text, sizeof out->text);
      break;
    }
    case A_8BIT:
      out->binary = true;
      txt::copy(out->text, "[data message]", sizeof out->text);
      break;
  }
  return true;
}

// ---------------------------------------------------------------------------
namespace {

struct Units {
  // GSM7: septet values (escape codes included). UCS2: UTF-16 code units.
  uint16_t v[SMS_SEND_MAX_PARTS * 160];
  size_t n = 0;
  bool gsm7 = true;
};

bool toUnits(const char* utf8, Units* u) {
  // First pass: can everything be sent in GSM 7-bit?
  const char* p = utf8;
  int32_t cp;
  u->gsm7 = true;
  while ((cp = txt::utf8Next(p)) >= 0)
    if (!gsm7Encodable((uint32_t)cp)) { u->gsm7 = false; break; }
  u->n = 0;
  const size_t cap = sizeof(u->v) / sizeof(u->v[0]);
  p = utf8;
  while ((cp = txt::utf8Next(p)) >= 0) {
    if (u->gsm7) {
      bool ext;
      int code = gsm7Lookup((uint32_t)cp, &ext);
      if (ext) { if (u->n >= cap) return false; u->v[u->n++] = 0x1B; }
      if (u->n >= cap) return false;
      u->v[u->n++] = (uint16_t)code;
    } else if (cp >= 0x10000) {
      if (u->n + 2 > cap) return false;
      uint32_t c = (uint32_t)cp - 0x10000;
      u->v[u->n++] = (uint16_t)(0xD800 + (c >> 10));
      u->v[u->n++] = (uint16_t)(0xDC00 + (c & 0x3FF));
    } else {
      if (u->n >= cap) return false;
      u->v[u->n++] = (uint16_t)cp;
    }
  }
  return true;
}

// Splits units into parts without breaking escape pairs / surrogate pairs.
int splitParts(const Units& u, size_t starts[], size_t lens[], int maxParts) {
  size_t single = u.gsm7 ? 160 : 70, multi = u.gsm7 ? 153 : 67;
  if (u.n <= single) { starts[0] = 0; lens[0] = u.n; return 1; }
  int parts = 0;
  size_t pos = 0;
  while (pos < u.n) {
    if (parts >= maxParts) return 0;
    size_t len = u.n - pos < multi ? u.n - pos : multi;
    if (pos + len < u.n) {
      uint16_t last = u.v[pos + len - 1];
      if (u.gsm7 ? last == 0x1B : (last >= 0xD800 && last <= 0xDBFF)) --len;
    }
    starts[parts] = pos;
    lens[parts] = len;
    ++parts;
    pos += len;
  }
  return parts;
}

}  // namespace

int smsMeasure(const char* utf8, int* used, int* perPart, bool* unicode) {
  static Units u;  // large; not re-entrant (UI thread only)
  if (!toUnits(utf8, &u)) { *used = 0; *perPart = 0; *unicode = false; return 0; }
  size_t starts[SMS_SEND_MAX_PARTS], lens[SMS_SEND_MAX_PARTS];
  int parts = splitParts(u, starts, lens, SMS_SEND_MAX_PARTS);
  *unicode = !u.gsm7;
  *used = (int)u.n;
  *perPart = parts > 1 ? (u.gsm7 ? 153 : 67) : (u.gsm7 ? 160 : 70);
  return parts;
}

int smsEncodeSubmit(const char* number, const char* utf8, uint8_t ref, SmsPart* parts, int maxParts) {
  static Units u;  // modem task only
  if (!toUnits(utf8, &u)) return 0;

  // Destination address
  char digits[24];
  bool intl = number[0] == '+';
  txt::digitsOnly(number, digits, sizeof digits);
  size_t nd = strlen(digits);
  if (nd == 0 || nd > 20) return 0;
  uint8_t da[12];
  size_t daLen = 0;
  for (size_t k = 0; k < nd; k += 2) {
    uint8_t lo = (uint8_t)(digits[k] - '0');
    uint8_t hi = k + 1 < nd ? (uint8_t)(digits[k + 1] - '0') : 0x0F;
    da[daLen++] = (uint8_t)((hi << 4) | lo);
  }

  size_t starts[SMS_SEND_MAX_PARTS], lens[SMS_SEND_MAX_PARTS];
  if (maxParts > SMS_SEND_MAX_PARTS) maxParts = SMS_SEND_MAX_PARTS;
  int count = splitParts(u, starts, lens, maxParts);
  if (count == 0) return 0;
  bool multi = count > 1;
  static const char kHex[] = "0123456789ABCDEF";

  for (int p = 0; p < count; ++p) {
    uint8_t b[180];
    size_t n = 0;
    b[n++] = 0x00;                                     // SMSC: use the one stored in the SIM
    b[n++] = (uint8_t)(0x01 | 0x10 | (multi ? 0x40 : 0));  // SUBMIT, relative VP, UDHI
    b[n++] = 0x00;                                     // message reference (modem assigns)
    b[n++] = (uint8_t)nd;
    b[n++] = intl ? 0x91 : 0x81;
    memcpy(b + n, da, daLen);
    n += daLen;
    b[n++] = 0x00;                                     // PID
    b[n++] = u.gsm7 ? 0x00 : 0x08;                     // DCS
    b[n++] = 0xAA;                                     // validity: 4 days
    uint8_t udh[6] = {0x05, 0x00, 0x03, ref, (uint8_t)count, (uint8_t)(p + 1)};
    const uint16_t* v = u.v + starts[p];
    size_t len = lens[p];
    if (u.gsm7) {
      size_t off = multi ? 7 : 0;  // 6 header octets + 1 fill bit = 7 septets
      size_t udl = off + len;
      size_t udBytes = (udl * 7 + 7) / 8;
      b[n++] = (uint8_t)udl;
      uint8_t* ud = b + n;
      memset(ud, 0, udBytes);
      if (multi) memcpy(ud, udh, 6);
      for (size_t k = 0; k < len; ++k) {
        size_t bit = (off + k) * 7, byte = bit / 8;
        unsigned sh = bit % 8;
        uint8_t val = (uint8_t)(v[k] & 0x7F);
        ud[byte] |= (uint8_t)(val << sh);
        if (sh > 1) ud[byte + 1] |= (uint8_t)(val >> (8 - sh));
      }
      n += udBytes;
    } else {
      size_t udl = (multi ? 6 : 0) + len * 2;
      b[n++] = (uint8_t)udl;
      if (multi) { memcpy(b + n, udh, 6); n += 6; }
      for (size_t k = 0; k < len; ++k) {
        b[n++] = (uint8_t)(v[k] >> 8);
        b[n++] = (uint8_t)(v[k] & 0xFF);
      }
    }
    for (size_t k = 0; k < n; ++k) {
      parts[p].hex[2 * k] = kHex[b[k] >> 4];
      parts[p].hex[2 * k + 1] = kHex[b[k] & 0x0F];
    }
    parts[p].hex[2 * n] = 0;
    parts[p].tpduLen = (int)n - 1;
  }
  return count;
}
