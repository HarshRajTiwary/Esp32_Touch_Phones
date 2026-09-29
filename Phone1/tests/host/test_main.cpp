// Host-side unit tests for the hardware-independent firmware code:
// SMS PDU codec, long-SMS assembler, AT response parsers, text/time helpers
// and the audio DSP blocks.
//
// Build & run:  powershell -File tools/test.ps1   (or see tools/test.ps1 for the g++ line)
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "../../firmware/TouchPhone/src/audio/dsp.h"
#include "../../firmware/TouchPhone/src/core/text_util.h"
#include "../../firmware/TouchPhone/src/modem/at_parse.h"
#include "../../firmware/TouchPhone/src/modem/sms_assembler.h"
#include "../../firmware/TouchPhone/src/modem/sms_pdu.h"

static int g_fail = 0, g_pass = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (cond) { ++g_pass; }                                             \
    else { ++g_fail; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)
#define CHECK_STR(a, b)                                                               \
  do {                                                                                \
    std::string _a = (a), _b = (b);                                                   \
    if (_a == _b) { ++g_pass; }                                                       \
    else { ++g_fail; printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); } \
  } while (0)

// ---------------------------------------------------------------------------
static void testPduDecode() {
  // Classic SMS-DELIVER example (dreamfabric.com): "hellohello" from 27838890001
  SmsPdu p;
  CHECK(smsDecodePdu("07917283010010F5040BC87238880900F10000993092516195800AE8329BFD4697D9EC37", &p));
  CHECK(!p.isSubmit);
  CHECK_STR(p.number, "27838890001");
  CHECK_STR(p.text, "hellohello");
  int y, mo, d, h, mi, s;
  txt::splitEpoch(p.ts, &y, &mo, &d, &h, &mi, &s, nullptr);
  CHECK(y == 2099 && mo == 3 && d == 29 && h == 15 && mi == 16 && s == 59);
  CHECK(p.tzQuarters == 8);

  // Alphanumeric sender "AX-AIRTEL"-style: build it with the encoder helpers below.
}

static void testPduEncode() {
  // Classic SMS-SUBMIT example: "hellohello" to +46708251358, VP relative 0xAA
  SmsPart parts[4];
  int n = smsEncodeSubmit("+46708251358", "hellohello", 0, parts, 4);
  CHECK(n == 1);
  CHECK_STR(parts[0].hex, "0011000B916407281553F80000AA0AE8329BFD4697D9EC37");
  CHECK(parts[0].tpduLen == 23);

  // Round trip through our own decoder (decoder understands SMS-SUBMIT too)
  SmsPdu p;
  CHECK(smsDecodePdu(parts[0].hex, &p));
  CHECK(p.isSubmit);
  CHECK_STR(p.number, "+46708251358");
  CHECK_STR(p.text, "hellohello");

  // GSM extension characters use two septets
  n = smsEncodeSubmit("9876543210", "Price: {5} [ok] ~ | \\ ^ EUR", 0, parts, 4);
  CHECK(n == 1);
  CHECK(smsDecodePdu(parts[0].hex, &p));
  CHECK_STR(p.text, "Price: {5} [ok] ~ | \\ ^ EUR");
  CHECK_STR(p.number, "9876543210");

  // Long GSM message -> 3 concatenated parts that reassemble exactly
  std::string longText;
  for (int k = 0; k < 40; ++k) longText += "Part-" + std::to_string(k) + " lorem ipsum. ";
  longText.resize(400);
  n = smsEncodeSubmit("+919812345678", longText.c_str(), 77, parts, 4);
  CHECK(n == 3);
  SmsAssembler as;
  for (int k = n - 1; k >= 0; --k) {  // arrive out of order
    SmsPdu part;
    CHECK(smsDecodePdu(parts[k].hex, &part));
    CHECK(part.concat && part.total == 3 && part.seq == k + 1 && part.ref == 77);
    as.add(part, 10 + k, 1000);
  }
  SmsAssembler::Done done;
  CHECK(as.pop(&done));
  CHECK_STR(done.msg.text, longText);
  CHECK(done.simIndices.size() == 3);
  CHECK(!done.partial);
  CHECK(!as.pop(&done));

  // Unicode (Hindi + rupee + emoji) -> UCS2 round trip
  const char* hindi = "\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87 \xE2\x82\xB9" "500 \xF0\x9F\x98\x80";
  n = smsEncodeSubmit("+919812345678", hindi, 1, parts, 4);
  CHECK(n == 1);
  CHECK(smsDecodePdu(parts[0].hex, &p));
  CHECK(p.dcs == 0x08);
  CHECK_STR(p.text, hindi);

  // Long unicode -> multiple parts, surrogate pairs never split
  std::string emo;
  for (int k = 0; k < 80; ++k) emo += "\xF0\x9F\x98\x80";  // 80 emoji = 160 UTF-16 units
  n = smsEncodeSubmit("+919812345678", emo.c_str(), 5, parts, 4);
  CHECK(n == 3);
  SmsAssembler as2;
  for (int k = 0; k < n; ++k) {
    SmsPdu part;
    CHECK(smsDecodePdu(parts[k].hex, &part));
    as2.add(part, k, 0);
  }
  CHECK(as2.pop(&done));
  CHECK_STR(done.msg.text, emo);

  // Measuring for the composer counter
  int used, per;
  bool uni;
  CHECK(smsMeasure("hello", &used, &per, &uni) == 1 && used == 5 && per == 160 && !uni);
  CHECK(smsMeasure(longText.c_str(), &used, &per, &uni) == 3 && per == 153);
  CHECK(smsMeasure("\xE2\x82\xB9", &used, &per, &uni) == 1 && uni && per == 70);

  // Too long for SMS_SEND_MAX_PARTS
  std::string huge(700, 'a');
  CHECK(smsEncodeSubmit("+91981", huge.c_str(), 1, parts, 4) == 0);
}

static void testAlphaSender() {
  // Hand-built SMS-DELIVER from alphanumeric sender "AIRTEL" (TON=5, 7-bit packed).
  // "AIRTEL" = 6 septets = 42 bits -> packed C1 A4 94 5A 64 02, length 11 semi-octets.
  const char* pdu =
      "00"                         // no SMSC
      "04"                         // SMS-DELIVER
      "0B" "D0" "C1A4945A6402"     // alphanumeric originating address
      "00" "00"                    // PID, DCS (GSM 7-bit)
      "62902261000022"             // 2026-09-22 16:00:00 +05:30
      "05" "C8329BFD06";           // "Hello"
  SmsPdu p;
  CHECK(smsDecodePdu(pdu, &p));
  CHECK_STR(p.number, "AIRTEL");
  CHECK_STR(p.text, "Hello");
  CHECK(p.tzQuarters == 22);  // +05:30 IST
}

static void testConcatPartialExpire() {
  SmsPdu part;
  part.concat = true; part.total = 2; part.seq = 2; part.ref = 9;
  txt::copy(part.number, "+911234567890", sizeof part.number);
  txt::copy(part.text, "second half", sizeof part.text);
  SmsAssembler as;
  as.add(part, 4, 100);
  SmsAssembler::Done d;
  CHECK(!as.pop(&d));
  CHECK(as.knowsIndex(4));
  as.expire(100 + 5000, 10000);
  CHECK(!as.pop(&d));
  as.expire(100 + 10000, 10000);
  CHECK(as.pop(&d));
  CHECK(d.partial);
  CHECK_STR(d.msg.text, "...second half");
}

// ---------------------------------------------------------------------------
static void testAtParsers() {
  int code;
  CHECK(atClassifyFinal("OK", &code) == AtFinal::Ok);
  CHECK(atClassifyFinal("+CME ERROR: 10", &code) == AtFinal::CmeError && code == 10);
  CHECK(atClassifyFinal("+CMS ERROR: 500", &code) == AtFinal::CmsError && code == 500);
  CHECK(atClassifyFinal("NO CARRIER", &code) == AtFinal::NoCarrier);
  CHECK(atClassifyFinal("+CSQ: 20,0", &code) == AtFinal::None);

  ClccInfo c;
  CHECK(parseClcc("+CLCC: 1,1,4,0,0,\"919876543210\",145,\"\"", &c));
  CHECK(c.idx == 1 && c.dir == 1 && c.stat == 4);
  CHECK_STR(c.number, "+919876543210");
  CHECK(parseClcc("+CLCC: 1,0,2,0,0,\"9876543210\",129,\"\"", &c));
  CHECK_STR(c.number, "9876543210");
  CHECK(c.stat == 2 && c.dir == 0);

  char num[NUMBER_MAX];
  CHECK(parseClip("+CLIP: \"+919876543210\",145,\"\",0,\"\",0", num, sizeof num));
  CHECK_STR(num, "+919876543210");

  int idx;
  CHECK(parseCmti("+CMTI: \"SM\",7", &idx) && idx == 7);
  int rssi;
  CHECK(parseCsq("+CSQ: 18,0", &rssi) && rssi == 18);

  RegState r;
  CHECK(parseCreg("+CREG: 1", &r) && r == RegState::Home);        // URC form
  CHECK(parseCreg("+CREG: 1,5", &r) && r == RegState::Roaming);   // query form
  CHECK(parseCreg("+CREG: 0,2", &r) && r == RegState::Searching);

  char op[24];
  CHECK(parseCops("+COPS: 0,0,\"IND airtel\"", op, sizeof op));
  CHECK_STR(op, "Airtel");
  CHECK(parseCops("+COPS: 0", op, sizeof op));
  CHECK_STR(op, "");

  int pct, mv;
  CHECK(parseCbc("+CBC: 0,78,3950", &pct, &mv) && pct == 78 && mv == 3950);

  uint32_t t;
  int tz;
  CHECK(parseCclk("+CCLK: \"26/09/26,14:05:33+22\"", &t, &tz));
  CHECK(tz == 22);
  char clk[8];
  txt::fmtClock(t, clk);
  CHECK_STR(clk, "14:05");

  SimState s;
  CHECK(parseCpin("+CPIN: READY", &s) && s == SimState::Ready);
  CHECK(parseCpin("+CPIN: SIM PIN", &s) && s == SimState::PinRequired);
  CHECK(parseCpin("+CPIN: NOT INSERTED", &s) && s == SimState::Absent);

  int m, dcs;
  char ussd[200];
  CHECK(parseCusd("+CUSD: 0,\"Main Bal: Rs 12.50, valid till 30-09\",15", &m, ussd, sizeof ussd, &dcs));
  CHECK(m == 0 && dcs == 15);
  CHECK_STR(ussd, "Main Bal: Rs 12.50, valid till 30-09");
  char out[200];
  decodeUssd("00480069", 72, out, sizeof out);
  CHECK_STR(out, "Hi");
  CHECK(atHasOpenQuote("+CUSD: 1,\"1.Balance"));
  CHECK(!atHasOpenQuote("+CUSD: 1,\"1.Balance\",15"));

  int st, len;
  CHECK(parseCmgrHeader("+CMGR: 0,,23", &st, &len) && st == 0 && len == 23);
  CHECK(parseCmglHeader("+CMGL: 3,1,,40", &idx, &st, &len) && idx == 3 && st == 1 && len == 40);

  char name[CONTACT_NAME_MAX];
  CHECK(parseCpbr("+CPBR: 2,\"9876543210\",129,\"Mom\"", &idx, num, sizeof num, name, sizeof name));
  CHECK(idx == 2);
  CHECK_STR(num, "9876543210");
  CHECK_STR(name, "Mom");
  CHECK(parseCpbr("+CPBR: 3,\"919812345678\",145,\"80004D0061\"", &idx, num, sizeof num, name, sizeof name));
  CHECK_STR(num, "+919812345678");
  CHECK_STR(name, "Ma");

  CHECK(csqToBars(99) == 0 && csqToBars(20) == 4 && csqToBars(10) == 2);
  CHECK(csqToDbm(31) == -51);
}

static void testText() {
  CHECK(txt::numbersMatch("+919876543210", "09876543210"));
  CHECK(txt::numbersMatch("9876543210", "+91 98765 43210"));
  CHECK(!txt::numbersMatch("9876543210", "9876543211"));
  CHECK(txt::numbersMatch("AX-AIRTEL", "ax-airtel"));
  CHECK(!txt::numbersMatch("121", "9876543121"));

  char out[64];
  bool lossy = txt::toDisplay("Pay \xE2\x82\xB9" "500 \xE2\x80\x9Cnow\xE2\x80\x9D caf\xC3\xA9", out, sizeof out);
  CHECK(!lossy);
  CHECK_STR(out, "Pay Rs.500 \"now\" cafe");
  lossy = txt::toDisplay("OTP 1234 \xE0\xA4\xA8\xE0\xA4\xAE", out, sizeof out);
  CHECK(lossy);
  CHECK_STR(out, "OTP 1234 ?");

  txt::maskNumber("+919876543210", out, sizeof out);
  CHECK_STR(out, "+91******3210");
  txt::sanitizeDial("+91 (98) 76-5*#x", out, sizeof out);
  CHECK_STR(out, "+9198765*#");
  CHECK(txt::isUssdCode("*123#"));
  CHECK(!txt::isUssdCode("123"));
  txt::initials("ravi kumar sharma", out);
  CHECK_STR(out, "RK");

  uint32_t t = txt::makeEpoch(2026, 9, 26, 14, 5, 0);
  int y, mo, d, h, mi, s, wd;
  txt::splitEpoch(t, &y, &mo, &d, &h, &mi, &s, &wd);
  CHECK(y == 2026 && mo == 9 && d == 26 && h == 14 && mi == 5 && wd == 6);  // Saturday
  txt::fmtDayDate(t, out);
  CHECK_STR(out, "Sat, 26 Sep");
  txt::fmtWhen(t - 86400, t, out, sizeof out);
  CHECK_STR(out, "Yesterday");
  txt::fmtWhen(t - 3 * 86400, t, out, sizeof out);
  CHECK_STR(out, "Wed");
  txt::fmtDuration(3723, out);
  CHECK_STR(out, "1:02:03");

  char u[16] = "ab\xE2\x82\xB9";
  txt::utf8PopBack(u);
  CHECK_STR(u, "ab");
  char v[8] = "abc\xE2\x82\xB9";
  txt::utf8Truncate(v, 5);
  CHECK_STR(v, "abc");
}

// ---------------------------------------------------------------------------
static void testDsp() {
  // Decimating low-pass: a 1 kHz tone passes, an 8 kHz tone is rejected.
  for (int rate : {24000, 32000}) {
    int factor = rate == 24000 ? 3 : 2;
    dsp::Decimator dec;
    dec.design(rate, factor, 3600.0f);
    auto run = [&](float f) {
      double acc = 0;
      int n = 0;
      for (int k = 0; k < rate / 2; ++k) {
        int16_t x = (int16_t)(8000 * sinf(2 * 3.14159265f * f * k / rate));
        int16_t y;
        if (dec.push(x, &y) && k > rate / 8) { acc += (double)y * y; ++n; }
      }
      return sqrt(acc / (n ? n : 1));
    };
    double pass = run(1000), stop = run(8000);
    CHECK(pass > 5000 && pass < 6500);  // 8000/sqrt(2) = 5657
    CHECK(stop < 300);
  }
  dsp::DcBlocker dc;
  double lastAbs = 0;
  for (int k = 0; k < 8000; ++k) lastAbs = fabs(dc.process(2000.0f));
  CHECK(lastAbs < 50);
  CHECK(dsp::softClip(40000.0f) <= 32767.0f && dsp::softClip(-40000.0f) >= -32768.0f);
  CHECK(fabsf(dsp::gainFromStep(8) - 1.0f) < 0.01f);
}

int main() {
  testPduDecode();
  testPduEncode();
  testAlphaSender();
  testConcatPartialExpire();
  testAtParsers();
  testText();
  testDsp();
  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
