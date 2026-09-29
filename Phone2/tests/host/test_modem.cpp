// A76XX (A7672S) response parsers, CMUX frame codec and the call state machine.
#include <string.h>

#include <vector>

#include "at_parse.h"
#include "call_tracker.h"
#include "cmux.h"
#include "test.h"

// ---------------------------------------------------------------------------
static void testA76xxParsers() {
  int pct = 0, mv = 0;
  CHECK(parseCbc("+CBC: 3.749V", &pct, &mv));
  CHECK_EQ(mv, 3749);
  CHECK_EQ(pct, -1);
  CHECK(parseCbc("+CBC: 0,85,4100", &pct, &mv));
  CHECK_EQ(pct, 85);
  CHECK_EQ(mv, 4100);

  RegState r;
  int act = 0;
  // URC form with location + access technology (7 = LTE)
  CHECK(parseRegistration("+CEREG: 1,\"1A2B\",\"01A2D103\",7", "+CEREG:", &r, &act));
  CHECK(r == RegState::Home);
  CHECK_EQ(act, 7);
  // Query form "+CEREG: <n>,<stat>,..."
  CHECK(parseRegistration("+CEREG: 2,5,\"1A2B\",\"01A2D103\",7", "+CEREG:", &r, &act));
  CHECK(r == RegState::Roaming);
  CHECK_EQ(act, 7);
  CHECK(parseRegistration("+CREG: 0,2", "+CREG:", &r, &act));
  CHECK(r == RegState::Searching);
  CHECK_EQ(act, -1);
  CHECK(parseRegistration("+CREG: 1", "+CREG:", &r, &act));
  CHECK(r == RegState::Home);
  CHECK(parseRegistration("+CREG: 3", "+CREG:", &r, &act));
  CHECK(r == RegState::Denied);
  CHECK(!parseRegistration("+CEREG: 1", "+CREG:", &r, &act));

  // AT+CPSI? - LTE example from the SIMCom A76XX AT manual
  CpsiInfo ci;
  CHECK(parseCpsi("+CPSI: LTE,Online,460-01,0x230A,175499523,318,EUTRAN-BAND3,1650,5,0,21,67,255,19", &ci));
  CHECK(ci.rat == Rat::Lte);
  CHECK(ci.online);
  CHECK_STR(ci.band, "LTE B3");
  CHECK_EQ(ci.rsrpDbm, -73);
  CHECK_EQ(ci.rsrqDb, -9);
  CHECK_EQ(ci.sinrDb, 19);
  // Firmware that prints dBm values directly
  CHECK(parseCpsi("+CPSI: LTE,Online,404-10,0x1234,123,45,EUTRAN-BAND40,39150,5,5,-11,-95,-65,12", &ci));
  CHECK_EQ(ci.rsrpDbm, -95);
  CHECK_EQ(ci.rsrqDb, -11);
  CHECK_STR(ci.band, "LTE B40");
  CHECK(parseCpsi("+CPSI: GSM,Online,404-10,0x0079,12345,27 EGSM 900,-64,2110,42-42", &ci));
  CHECK(ci.rat == Rat::Gsm);
  CHECK_STR(ci.band, "EGSM 900");
  CHECK(parseCpsi("+CPSI: NO SERVICE,Online", &ci));
  CHECK(ci.rat == Rat::None);

  bool begin = false;
  int secs = -1;
  CHECK(parseVoiceCall("VOICE CALL: BEGIN", &begin, &secs));
  CHECK(begin);
  CHECK(parseVoiceCall("VOICE CALL: END: 000017", &begin, &secs));
  CHECK(!begin);
  CHECK_EQ(secs, 17);
  CHECK(parseVoiceCall("VOICE CALL: END: 010203", &begin, &secs));
  CHECK_EQ(secs, 3723);
  CHECK(!parseVoiceCall("VOICE: BEGIN", &begin, &secs));

  char num[NUMBER_MAX];
  CHECK(parseMissedCall("MISSED_CALL: 03:56PM +919876543210", num, sizeof num));
  CHECK_STR(num, "+919876543210");

  char iccid[24];
  CHECK(parseIccid("+ICCID: 89914509001234567890", iccid, sizeof iccid));
  CHECK_STR(iccid, "89914509001234567890");
  CHECK(!parseIccid("+ICCID: 12", iccid, sizeof iccid));

  int mode = 0;
  CHECK(parseCnmp("+CNMP: 2", &mode));
  CHECK_EQ(mode, 2);
  CHECK(!parseCnmp("+CNMP: (2,13,38)", &mode));

  int tz = 0;
  CHECK(parseCtzv("+CTZV: +22", &tz));
  CHECK_EQ(tz, 22);
  int temp = 0;
  CHECK(parseCpmutemp("+CPMUTEMP: 29", &temp));
  CHECK_EQ(temp, 29);

  CHECK(isModemBootBanner("RDY"));
  CHECK(isModemBootBanner("*ATREADY: 1"));
  CHECK(!isModemBootBanner("READY"));
  CHECK(isPdpDeactivation("+CGEV: NW PDN DEACT 1"));
  CHECK(isPdpDeactivation("+CGEV: ME DETACH"));
  CHECK(!isPdpDeactivation("+CGEV: NW PDN ACT 1"));

  CHECK_EQ(rsrpToBars(0), 0);
  CHECK_EQ(rsrpToBars(-130), 0);
  CHECK_EQ(rsrpToBars(-120), 1);
  CHECK_EQ(rsrpToBars(-100), 3);
  CHECK_EQ(rsrpToBars(-80), 4);
}

// ---------------------------------------------------------------------------
struct Collected {
  std::vector<cmux::Frame> frames;
  std::vector<std::vector<uint8_t>> data;
  std::string garbage;
};

static void onFrame(void* ctx, const cmux::Frame& f) {
  Collected* c = static_cast<Collected*>(ctx);
  c->frames.push_back(f);
  c->data.emplace_back(f.data, f.data + f.len);
}

static void onGarbage(void* ctx, uint8_t b) { static_cast<Collected*>(ctx)->garbage.push_back((char)b); }

static void testCmux() {
  uint8_t buf[2000];
  // SABM with P bit on DLCI 0: the well-known "F9 03 3F 01 1C F9" from TS 27.010
  size_t n = cmux::build(0, cmux::SABM | cmux::kPF, nullptr, 0, buf, sizeof buf);
  const uint8_t sabm0[] = {0xF9, 0x03, 0x3F, 0x01, 0x1C, 0xF9};
  CHECK_EQ(n, sizeof sabm0);
  CHECK(memcmp(buf, sabm0, sizeof sabm0) == 0);

  // The close-down frame the driver sends blindly at start-up (a7672s.cpp forceMuxClose)
  uint8_t cld[4];
  size_t cl = cmux::buildControl(cmux::MSG_CLD, true, nullptr, 0, cld, sizeof cld);
  CHECK_EQ(cl, 2);
  n = cmux::build(0, cmux::UIH, cld, cl, buf, sizeof buf);
  const uint8_t cldFrame[] = {0xF9, 0x03, 0xEF, 0x05, 0xC3, 0x01, 0xF2, 0xF9};
  CHECK_EQ(n, sizeof cldFrame);
  CHECK(memcmp(buf, cldFrame, sizeof cldFrame) == 0);

  // UA from the module on DLCI 0 ("F9 03 73 01 D7 F9")
  Collected c;
  cmux::Decoder dec;
  dec.setMaxDlci(2);  // as the driver configures it
  const uint8_t ua0[] = {0xF9, 0x03, 0x73, 0x01, 0xD7, 0xF9};
  dec.feed(ua0, sizeof ua0, onFrame, onGarbage, &c);
  CHECK_EQ(c.frames.size(), 1);
  if (!c.frames.empty()) {
    CHECK_EQ(c.frames[0].dlci, 0);
    CHECK_EQ(c.frames[0].ctrl, cmux::UA);
    CHECK(c.frames[0].pf);
  }

  // UIH round trip on DLCI 1, fed one byte at a time after some plain text
  // (what the module prints if it restarted while we were multiplexing).
  c = Collected();
  const char* at = "AT+CSQ\r";
  n = cmux::build(1, cmux::UIH, (const uint8_t*)at, strlen(at), buf, sizeof buf);
  std::string stream = "\r\nRDY\r\n";
  stream.append((const char*)buf, n);
  for (char ch : stream) dec.feed((const uint8_t*)&ch, 1, onFrame, onGarbage, &c);
  CHECK_EQ(c.frames.size(), 1);
  if (!c.frames.empty()) {
    CHECK_EQ(c.frames[0].dlci, 1);
    CHECK_EQ(c.frames[0].ctrl, cmux::UIH);
    CHECK(std::string(c.data[0].begin(), c.data[0].end()) == at);
  }
  CHECK(c.garbage.find("RDY") != std::string::npos);

  // Long frame: two-byte length field
  std::vector<uint8_t> big(1400);
  for (size_t k = 0; k < big.size(); ++k) big[k] = (uint8_t)(k * 7 + 1);
  n = cmux::build(2, cmux::UIH, big.data(), big.size(), buf, sizeof buf);
  CHECK_EQ(n, big.size() + 7);
  c = Collected();
  dec.feed(buf, n, onFrame, onGarbage, &c);
  CHECK_EQ(c.frames.size(), 1);
  if (!c.frames.empty()) CHECK(c.data[0] == big);

  // Data containing the flag byte 0xF9 (basic mode has no escaping: length decides)
  const uint8_t tricky[] = {0xF9, 0xF9, 0x00, 0x7E, 0xF9};
  n = cmux::build(2, cmux::UIH, tricky, sizeof tricky, buf, sizeof buf);
  c = Collected();
  dec.feed(buf, n, onFrame, onGarbage, &c);
  CHECK_EQ(c.frames.size(), 1);
  if (!c.frames.empty()) CHECK(c.data[0] == std::vector<uint8_t>(tricky, tricky + sizeof tricky));

  // A corrupted FCS is rejected and counted; the next frame still decodes
  n = cmux::build(1, cmux::UIH, (const uint8_t*)"OK", 2, buf, sizeof buf);
  buf[n - 2] ^= 0x55;
  uint32_t errs = dec.fcsErrors();
  c = Collected();
  dec.feed(buf, n, onFrame, onGarbage, &c);
  CHECK_EQ(c.frames.size(), 0);
  CHECK_EQ(dec.fcsErrors(), errs + 1);
  n = cmux::build(1, cmux::UIH, (const uint8_t*)"OK", 2, buf, sizeof buf);
  dec.feed(buf, n, onFrame, onGarbage, &c);
  CHECK_EQ(c.frames.size(), 1);

  // MSC command for DLCI 1 (control message layout)
  uint8_t vals[2] = {(uint8_t)((1 << 2) | 0x03), cmux::kV24Ready};
  uint8_t info[8];
  size_t il = cmux::buildControl(cmux::MSG_MSC, true, vals, 2, info, sizeof info);
  CHECK_EQ(il, 4);
  CHECK_EQ(info[0], 0xE3);
  CHECK_EQ(info[1], 0x05);
  CHECK_EQ(info[3], 0x8D);
  il = cmux::buildControl(cmux::MSG_MSC, false, vals, 2, info, sizeof info);
  CHECK_EQ(info[0], 0xE1);  // response form: C/R cleared

  CHECK_EQ(cmux::build(1, cmux::UIH, big.data(), cmux::kMaxInfo + 1, buf, sizeof buf), 0);
}

// ---------------------------------------------------------------------------
static void testCallTracker() {
  const uint8_t C = CallTracker::kChanged, E = CallTracker::kEnded, K = CallTracker::kConsumed;
  uint32_t t = 1000;

  // Outgoing call: dialing -> alerting -> active -> ended normally
  {
    CallTracker ct;
    CHECK_EQ(ct.dialStarted("+911234567890", t), C);
    CHECK(ct.info().phase == CallPhase::Dialing);
    CHECK_EQ(ct.dialResult(AtFinal::Ok, t), 0);
    CHECK_EQ(ct.onLine("+CLCC: 1,0,2,0,0,\"+911234567890\",145", t), K);  // same state: nothing new
    CHECK_EQ(ct.onLine("+CLCC: 1,0,3,0,0,\"+911234567890\",145", t + 2000), K | C);
    CHECK(ct.info().phase == CallPhase::Alerting);
    CHECK_EQ(ct.onLine("+CLCC: 1,0,0,0,0,\"+911234567890\",145", t + 5000), K | C);
    CHECK(ct.info().phase == CallPhase::Active);
    CHECK(ct.info().everActive);
    CHECK_EQ(ct.info().activeMs, t + 5000);
    CHECK_EQ(ct.onLine("VOICE CALL: END: 000010", t + 15000), K | C | E);
    CHECK(ct.info().phase == CallPhase::Idle);
    CHECK(ct.ended().endReason == CallEndReason::Normal);
    CHECK(!ct.ended().incoming);
    CHECK_STR(ct.ended().number, "+911234567890");
    CHECK_EQ(ct.onLine("NO CARRIER", t + 15100), K);  // late final result: no second "ended"
  }
  // Incoming call answered, then the other side hangs up
  {
    CallTracker ct;
    CHECK_EQ(ct.onLine("RING", t), K | C);
    CHECK(ct.info().phase == CallPhase::Incoming);
    CHECK_EQ(ct.onLine("+CLIP: \"9876543210\",129,,,,0", t), K | C);
    CHECK_STR(ct.info().number, "9876543210");
    CHECK_EQ(ct.onLine("RING", t + 3000), K);
    CHECK_EQ(ct.answered(t + 4000), C);
    CHECK(ct.info().phase == CallPhase::Active);
    CHECK_EQ(ct.onLine("VOICE CALL: BEGIN", t + 4100), K);
    CHECK_EQ(ct.onLine("NO CARRIER", t + 30000), K | C | E);
    CHECK(ct.ended().incoming);
    CHECK(ct.ended().everActive);
    CHECK(ct.ended().endReason == CallEndReason::NoCarrier);
  }
  // Missed call (caller gave up) - "+CLCC ... stat 6"
  {
    CallTracker ct;
    ct.onLine("RING", t);
    ct.onLine("+CLIP: \"+919812345678\",145,,,,0", t);
    CHECK_EQ(ct.onLine("+CLCC: 1,1,6,0,0,\"+919812345678\",145", t + 9000), K | C | E);
    CHECK(ct.ended().endReason == CallEndReason::Missed);
  }
  // Declined by the user
  {
    CallTracker ct;
    ct.onLine("RING", t);
    CHECK_EQ(ct.hungUp(t + 1000), C | E);
    CHECK(ct.ended().endReason == CallEndReason::Rejected);
    CHECK_EQ(ct.hungUp(t + 1100), 0);
  }
  // Busy / network failure / modem error while dialing
  {
    CallTracker ct;
    ct.dialStarted("121", t);
    CHECK_EQ(ct.onLine("BUSY", t + 3000), K | C | E);
    CHECK(ct.ended().endReason == CallEndReason::Busy);
    ct.dialStarted("121", t);
    CHECK_EQ(ct.dialResult(AtFinal::NoDialtone, t), C | E);
    CHECK(ct.ended().endReason == CallEndReason::NoNetwork);
    ct.dialStarted("121", t);
    CHECK_EQ(ct.dialResult(AtFinal::CmeError, t), C | E);
    CHECK(ct.ended().endReason == CallEndReason::Failed);
  }
  // Safety polls: a call that silently disappeared is closed after two empty polls
  {
    CallTracker ct;
    ct.dialStarted("+911234567890", t);
    CHECK_EQ(ct.pollResult(0, t + 2000), 0);   // too early
    CHECK_EQ(ct.pollResult(0, t + 5000), 0);   // first empty poll
    CHECK_EQ(ct.pollResult(0, t + 8000), C | E);
    // Ringing that stopped without any final result code
    ct.onLine("RING", t);
    CHECK_EQ(ct.pollResult(0, t + 13000), C | E);
    CHECK(ct.ended().endReason == CallEndReason::Missed);
  }
  // Call waiting on a second line produces a notice, not a state change
  {
    CallTracker ct;
    ct.dialStarted("+911111111111", t);
    ct.onLine("+CLCC: 1,0,0,0,0,\"+911111111111\",145", t + 1000);
    CHECK_EQ(ct.onLine("+CLCC: 2,1,5,0,0,\"+919999999999\",145", t + 2000), K);
    CHECK_STR(ct.notice(), "Call waiting: +919999999999");
    CHECK(ct.info().phase == CallPhase::Active);
    CHECK_EQ(ct.setMuted(true), C);
    CHECK_EQ(ct.setMuted(true), 0);
    CHECK(ct.info().muted);
    CHECK_EQ(ct.endAll(CallEndReason::Failed), C | E);
    CHECK(ct.ended().endReason == CallEndReason::Failed);
  }
  // Data "calls" in +CLCC (mode 1, e.g. the PPP session) are ignored
  {
    CallTracker ct;
    CHECK_EQ(ct.onLine("+CLCC: 1,0,0,1,0,\"*99#\",129", t), K);
    CHECK(ct.info().phase == CallPhase::Idle);
    CHECK_EQ(ct.onLine("+CSQ: 20,99", t), 0);  // not a call line
  }
}

void runModemTests() {
  testA76xxParsers();
  testCmux();
  testCallTracker();
}
