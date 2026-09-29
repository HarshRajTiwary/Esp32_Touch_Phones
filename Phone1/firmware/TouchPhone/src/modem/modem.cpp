#include "modem.h"

#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../config/board_config.h"
#include "../core/events.h"
#include "../core/log.h"
#include "../core/text_util.h"
#include "at_parse.h"
#include "sms_assembler.h"
#include "sms_pdu.h"

namespace {

using LineFn = std::function<void(const char*)>;

constexpr uint32_t kValidEpoch = 1704067200;  // 2024-01-01: anything earlier = clock not set

HardwareSerial& SerialAT = Serial1;
QueueHandle_t s_cmdQ;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

NetStatus s_st;
CallInfo s_call;
uint8_t s_volume = 80, s_ringer = 80, s_mic = 10;

// ---- line reader ----
char s_lineBuf[640];
size_t s_lineLen = 0;
std::string s_cusd;           // multi-line +CUSD accumulation
bool s_cusdOpen = false;

// ---- state ----
bool s_ready = false;
bool s_needReinit = false;
bool s_smsSetupPending = false;  // SMS storage must be (re)configured once "SMS Ready"
uint32_t s_tSmsSetup = 0;
int s_timeouts = 0;
int s_trackedIdx = 0;
bool s_userRejected = false;
CallEndReason s_pendingEnd = CallEndReason::None;
uint32_t s_lastRingMs = 0;
int s_noCallPolls = 0;
uint32_t s_tPoll = 0, s_tSlow = 0, s_tSweep = 0, s_tClock = 0, s_tCallPoll = 0, s_ussdDeadline = 0;
int s_tzQuarters = 22;  // IST until the network tells us otherwise
uint8_t s_smsRef = 1;
std::vector<int> s_pendingSms;
SmsAssembler s_assembler;

// ---------------------------------------------------------------------------
bool isRegistered(const NetStatus& s) { return s.reg == RegState::Home || s.reg == RegState::Roaming; }

bool clockValid() { return time(nullptr) > (time_t)kValidEpoch; }

uint32_t nowLocal() { return clockValid() ? (uint32_t)time(nullptr) : 0; }

template <class F>
void updateStatus(F f) {
  NetStatus before, after;
  portENTER_CRITICAL(&s_mux);
  before = s_st;
  f(s_st);
  after = s_st;
  portEXIT_CRITICAL(&s_mux);
  if (memcmp(&before, &after, sizeof before) != 0) events::post(Ev::NetChanged);
}

template <class F>
void updateCall(F f) {
  portENTER_CRITICAL(&s_mux);
  f(s_call);
  portEXIT_CRITICAL(&s_mux);
  events::post(Ev::CallChanged);
}

CallInfo callSnapshot() {
  portENTER_CRITICAL(&s_mux);
  CallInfo c = s_call;
  portEXIT_CRITICAL(&s_mux);
  return c;
}

void notice(const char* text) {
  events::post(Ev::ModemNotice, 0, 0, strdup(text));
}

void setSystemTime(uint32_t epoch, const char* source) {
  if (epoch < kValidEpoch) return;
  time_t cur = time(nullptr);
  if (llabs((long long)cur - (long long)epoch) <= 2) return;
  struct timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
  char c[6];
  txt::fmtClock(epoch, c);
  LOG("TIME", "clock set to %s from %s", c, source);
  events::post(Ev::TimeSynced, (int32_t)epoch);
}

// ---------------------------------------------------------------------------
// Serial line handling
// ---------------------------------------------------------------------------

// Returns true when a complete line (or the SMS "> " prompt in promptMode) is available.
bool readLine(char* out, size_t cap, uint32_t waitMs, bool promptMode = false) {
  uint32_t start = millis();
  for (;;) {
    while (SerialAT.available()) {
      char c = (char)SerialAT.read();
      if (c == '\r') continue;
      if (c == '\n') {
        if (s_lineLen == 0) continue;
        s_lineBuf[s_lineLen] = 0;
        txt::copy(out, s_lineBuf, cap);
        s_lineLen = 0;
        return true;
      }
      if (s_lineLen < sizeof s_lineBuf - 1) s_lineBuf[s_lineLen++] = c;
      if (promptMode && s_lineLen == 1 && s_lineBuf[0] == '>') {
        s_lineLen = 0;
        txt::copy(out, ">", cap);
        return true;
      }
    }
    if (millis() - start >= waitMs) return false;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void handleLine(const char* line);

AtFinal exec(const char* cmd, uint32_t timeoutMs = 2000, const LineFn& fn = nullptr, bool callFinals = false,
             int* errCode = nullptr) {
  char line[640];
  while (readLine(line, sizeof line, 0)) handleLine(line);  // flush pending URCs first
  SerialAT.print(cmd);
  SerialAT.print("\r");
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (!readLine(line, sizeof line, 20)) continue;
    if (strcmp(line, cmd) == 0) continue;  // command echo (before ATE0 took effect)
    int code = 0;
    AtFinal f = atClassifyFinal(line, &code);
    bool callResult = f == AtFinal::NoCarrier || f == AtFinal::Busy || f == AtFinal::NoAnswer || f == AtFinal::NoDialtone;
    if (f != AtFinal::None && !s_cusdOpen && (!callResult || callFinals)) {
      if (callResult) handleLine(line);
      if (errCode) *errCode = code;
      s_timeouts = 0;
      if (f != AtFinal::Ok) LOG("MODEM", "%s -> %s", cmd, line);
      return f;
    }
    handleLine(line);
    if (fn) fn(line);
  }
  s_timeouts++;
  LOG("MODEM", "timeout: %s", cmd);
  return AtFinal::None;
}

bool ok(const char* cmd, uint32_t timeoutMs = 2000) { return exec(cmd, timeoutMs) == AtFinal::Ok; }

// ---------------------------------------------------------------------------
// Call state
// ---------------------------------------------------------------------------
void endCall(CallEndReason reason) {
  CallInfo snap = callSnapshot();
  if (snap.phase == CallPhase::Idle) return;
  if (snap.incoming && !snap.everActive) reason = s_userRejected ? CallEndReason::Rejected : CallEndReason::Missed;
  else if (reason == CallEndReason::None) reason = s_pendingEnd != CallEndReason::None ? s_pendingEnd : CallEndReason::Normal;
  snap.endReason = reason;
  LOG("CALL", "ended (%d) %s", (int)reason, logNum(snap.number));
  portENTER_CRITICAL(&s_mux);
  s_call = CallInfo();
  portEXIT_CRITICAL(&s_mux);
  s_userRejected = false;
  s_trackedIdx = 0;
  s_pendingEnd = CallEndReason::None;
  s_noCallPolls = 0;
  events::post(Ev::CallChanged);
  events::post(Ev::CallEnded, (int32_t)reason, 0, new CallInfo(snap));
}

void setPhase(CallPhase ph, const char* number, int incoming /* -1 = keep */) {
  CallInfo before = callSnapshot();
  if (before.phase == ph && (!number || !*number || strcmp(before.number, number) == 0)) return;
  updateCall([&](CallInfo& c) {
    if (c.phase == CallPhase::Idle) {
      c = CallInfo();
      c.setupMs = millis();
    }
    c.phase = ph;
    if (incoming >= 0) c.incoming = incoming;
    if (number && *number) txt::copy(c.number, number, sizeof c.number);
    if (ph == CallPhase::Active && !c.everActive) {
      c.everActive = true;
      c.activeMs = millis();
    }
  });
  static const char* kNames[] = {"idle", "dialing", "alerting", "incoming", "active"};
  LOG("CALL", "%s %s", kNames[(int)ph], logNum(number ? number : before.number));
}

void onClcc(const ClccInfo& c) {
  if (s_trackedIdx && c.idx != s_trackedIdx) {
    if (c.stat == 5) {
      char msg[64];
      snprintf(msg, sizeof msg, "Call waiting: %s", c.number);
      notice(msg);
    }
    return;
  }
  s_noCallPolls = 0;
  switch (c.stat) {
    case 0:
    case 1:
      s_trackedIdx = c.idx;
      setPhase(CallPhase::Active, c.number, c.dir == 1);
      break;
    case 2:
      s_trackedIdx = c.idx;
      setPhase(CallPhase::Dialing, c.number, 0);
      break;
    case 3:
      s_trackedIdx = c.idx;
      setPhase(CallPhase::Alerting, c.number, 0);
      break;
    case 4:
    case 5:
      s_trackedIdx = c.idx;
      s_lastRingMs = millis();
      setPhase(CallPhase::Incoming, c.number, 1);
      break;
    case 6:
      endCall(CallEndReason::None);
      break;
  }
}

// ---------------------------------------------------------------------------
// USSD
// ---------------------------------------------------------------------------
void onCusd(const char* full) {
  int m = 2, dcs = 15;
  std::string raw(full);
  char* text = (char*)malloc(USSD_TEXT_MAX);
  char* body = (char*)malloc(USSD_TEXT_MAX);
  if (!text || !body) { free(text); free(body); return; }
  parseCusd(raw.c_str(), &m, body, USSD_TEXT_MAX, &dcs);
  decodeUssd(body, dcs, text, USSD_TEXT_MAX);
  free(body);
  if (!text[0]) txt::copy(text, m == 2 ? "Session ended" : "(empty reply)", USSD_TEXT_MAX);
  if (m == 4) txt::copy(text, "Operation not supported", USSD_TEXT_MAX);
  s_ussdDeadline = 0;
  LOG("USSD", "reply m=%d dcs=%d %s", m, dcs, logText(text));
  events::post(Ev::UssdReply, m == 1 ? 1 : 0, 0, text);
}

// ---------------------------------------------------------------------------
// Unsolicited result codes and status lines. Every line the modem prints
// passes through here, so status fields stay current whatever command ran.
// ---------------------------------------------------------------------------
void handleLine(const char* line) {
  if (s_cusdOpen) {
    s_cusd += "\n";
    s_cusd += line;
    // An odd number of quotes on this line closes the string. The size limit
    // guards against a malformed reply keeping the parser stuck forever.
    if (atHasOpenQuote(line) || s_cusd.size() > USSD_TEXT_MAX * 2) {
      s_cusdOpen = false;
      onCusd(s_cusd.c_str());
    }
    return;
  }

  ClccInfo clcc;
  char buf[NUMBER_MAX];
  int n1, n2;
  SimState sim;
  RegState reg;
  uint32_t epoch;

  static const char* const kCallUrcs[] = {"RING", "NO CARRIER", "BUSY", "NO ANSWER", "NO DIALTONE", "MO RING",
                                          "MO CONNECTED"};
  for (const char* u : kCallUrcs)
    if (strcmp(line, u) == 0) LOG("CALL", "modem: %s", line);

  if (strcmp(line, "RING") == 0) {
    s_lastRingMs = millis();
    if (callSnapshot().phase == CallPhase::Idle) setPhase(CallPhase::Incoming, nullptr, 1);
  } else if (parseClip(line, buf, sizeof buf)) {
    s_lastRingMs = millis();
    CallPhase ph = callSnapshot().phase;
    if (ph == CallPhase::Idle || ph == CallPhase::Incoming) setPhase(CallPhase::Incoming, buf, 1);
  } else if (parseClcc(line, &clcc)) {
    LOG("CALL", "clcc id %d dir %d stat %d (0 active 2 dialing 3 ringing 4 incoming 6 ended)", clcc.idx, clcc.dir,
        clcc.stat);
    onClcc(clcc);
  } else if (strcmp(line, "NO CARRIER") == 0) {
    endCall(CallEndReason::NoCarrier);
  } else if (strcmp(line, "BUSY") == 0) {
    s_pendingEnd = CallEndReason::Busy;
    endCall(CallEndReason::Busy);
  } else if (strcmp(line, "NO ANSWER") == 0) {
    endCall(CallEndReason::NoAnswer);
  } else if (strcmp(line, "NO DIALTONE") == 0) {
    endCall(CallEndReason::NoNetwork);
  } else if (strcmp(line, "MO RING") == 0) {
    if (callSnapshot().phase == CallPhase::Dialing) setPhase(CallPhase::Alerting, nullptr, -1);
  } else if (strcmp(line, "MO CONNECTED") == 0) {
    setPhase(CallPhase::Active, nullptr, -1);
  } else if (parseCmti(line, &n1)) {
    LOG("SMS", "new message in slot %d", n1);
    s_pendingSms.push_back(n1);
  } else if (txt::startsWith(line, "+CUSD:")) {
    if (atHasOpenQuote(line)) {
      s_cusd = line;
      s_cusdOpen = true;
    } else {
      onCusd(line);
    }
  } else if (parseCpin(line, &sim)) {
    updateStatus([&](NetStatus& s) { s.sim = sim; });
  } else if (parseCreg(line, &reg)) {
    bool was = isRegistered(modem::status());
    updateStatus([&](NetStatus& s) { s.reg = reg; });
    if (!was && (reg == RegState::Home || reg == RegState::Roaming)) {
      LOG("MODEM", "network registered%s", reg == RegState::Roaming ? " (roaming)" : "");
      s_tSlow = s_tClock = millis() + 500;  // fetch operator name and network time
    }
  } else if (parseCsq(line, &n1)) {
    updateStatus([&](NetStatus& s) { s.csq = (int8_t)n1; });
  } else if (txt::startsWith(line, "+COPS:")) {
    char op[24];
    if (parseCops(line, op, sizeof op) && op[0]) updateStatus([&](NetStatus& s) { txt::copy(s.op, op, sizeof s.op); });
  } else if (parseCbc(line, &n1, &n2)) {
    updateStatus([&](NetStatus& s) { s.battPct = (uint8_t)n1; s.battMv = (uint16_t)n2; });
  } else if (parseCclk(line, &epoch, &n1)) {
    if (epoch >= kValidEpoch) {
      s_tzQuarters = n1;
      setSystemTime(epoch, "network");
    }
  } else if (strcmp(line, "Call Ready") == 0) {
    updateStatus([](NetStatus& s) { s.callReady = true; });
  } else if (strcmp(line, "SMS Ready") == 0) {
    updateStatus([](NetStatus& s) { s.smsReady = true; });
    s_tSweep = millis() + 1000;
    s_smsSetupPending = true;  // storage commands fail with CMS 302 until now
    s_tSmsSetup = millis() + 500;
  } else if (txt::startsWith(line, "*PSUTTZ") || txt::startsWith(line, "DST:") || txt::startsWith(line, "+CTZV")) {
    s_tClock = millis() + 1500;
  } else if (strcmp(line, "RDY") == 0 || strcmp(line, "NORMAL POWER DOWN") == 0 ||
             strcmp(line, "UNDER-VOLTAGE POWER DOWN") == 0) {
    if (s_ready) {
      LOG("MODEM", "modem restarted by itself (%s) - re-initialising", line);
      s_needReinit = true;
      if (line[0] == 'U') notice("Battery too low for the modem");
    }
  } else if (txt::startsWith(line, "UNDER-VOLTAGE WARN")) {
    notice("Low battery");
  } else if (txt::startsWith(line, "OVER-VOLTAGE")) {
    notice("Supply voltage too high");
  }
}

// ---------------------------------------------------------------------------
// SMS
// ---------------------------------------------------------------------------
void flushAssembler() {
  SmsAssembler::Done d;
  while (s_assembler.pop(&d)) {
    SmsRecord* r = new SmsRecord();
    memset(r, 0, sizeof *r);
    txt::copy(r->number, d.msg.number, sizeof r->number);
    txt::copy(r->text, d.msg.text, sizeof r->text);
    r->len = (uint16_t)strlen(r->text);
    r->ts = d.msg.ts ? d.msg.ts : nowLocal();
    r->flags = SMS_UNREAD | (d.partial ? SMS_PARTIAL : 0);
    if (!clockValid() && d.msg.ts) setSystemTime(d.msg.ts, "SMS timestamp");
    LOG("SMS", "received from %s: %s%s", logNum(r->number), logText(r->text), d.partial ? " (partial)" : "");
    events::post(Ev::SmsReceived, d.partial ? 1 : 0, 0, r);
    for (int idx : d.simIndices) {
      char cmd[20];
      snprintf(cmd, sizeof cmd, "AT+CMGD=%d", idx);
      exec(cmd, 5000);
    }
  }
}

void deliverPdu(const char* hex, int index) {
  if (s_assembler.knowsIndex(index)) return;
  SmsPdu* p = new SmsPdu();
  bool okDecode = smsDecodePdu(hex, p);
  if (!okDecode || p->isStatusReport || p->isSubmit || !p->text[0]) {
    // Not a readable incoming message: free the SIM slot so storage never fills up.
    if (!okDecode) LOG("SMS", "slot %d: undecodable PDU, deleting", index);
    char cmd[20];
    snprintf(cmd, sizeof cmd, "AT+CMGD=%d", index);
    exec(cmd, 5000);
  } else {
    s_assembler.add(*p, index, millis());
  }
  delete p;
  flushAssembler();
}

void readSmsSlot(int index) {
  char cmd[20];
  snprintf(cmd, sizeof cmd, "AT+CMGR=%d", index);
  std::string pdu;
  bool expectPdu = false;
  AtFinal f = exec(cmd, 5000, [&](const char* line) {
    int st, len;
    if (parseCmgrHeader(line, &st, &len)) { expectPdu = true; return; }
    if (expectPdu) { pdu = line; expectPdu = false; }
  });
  if (f == AtFinal::Ok && !pdu.empty()) deliverPdu(pdu.c_str(), index);
}

void sweepSms() {
  std::vector<std::pair<int, std::string>> found;
  int curIndex = -1;
  exec("AT+CMGL=4", 20000, [&](const char* line) {
    int idx, st, len;
    if (parseCmglHeader(line, &idx, &st, &len)) { curIndex = idx; return; }
    if (curIndex >= 0) {
      found.emplace_back(curIndex, std::string(line));
      curIndex = -1;
    }
  });
  if (!found.empty()) LOG("SMS", "sweep found %u stored message(s)", (unsigned)found.size());
  for (auto& f : found) deliverPdu(f.second.c_str(), f.first);
}

bool waitPrompt(uint32_t timeoutMs) {
  char line[640];
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (!readLine(line, sizeof line, 20, true)) continue;
    if (line[0] == '>') return true;
    int code;
    if (atClassifyFinal(line, &code) != AtFinal::None && strcmp(line, "NO CARRIER") != 0) return false;
    handleLine(line);
  }
  return false;
}

void doSendSms(OutgoingSms* o) {
  static SmsPart parts[SMS_SEND_MAX_PARTS];
  const char* err = nullptr;
  NetStatus st = modem::status();
  int n = 0;
  if (st.sim != SimState::Ready) err = "No SIM card";
  else if (!isRegistered(st)) err = "No network";
  else if ((n = smsEncodeSubmit(o->number, o->text, s_smsRef++, parts, SMS_SEND_MAX_PARTS)) == 0) err = "Message too long or bad number";
  for (int i = 0; i < n && !err; ++i) {
    char line[640];
    while (readLine(line, sizeof line, 0)) handleLine(line);
    char cmd[24];
    snprintf(cmd, sizeof cmd, "AT+CMGS=%d\r", parts[i].tpduLen);
    SerialAT.print(cmd);
    if (!waitPrompt(6000)) {
      SerialAT.write(0x1B);  // abort any half-open prompt
      err = "Modem did not accept the message";
      break;
    }
    SerialAT.print(parts[i].hex);
    SerialAT.write(0x1A);
    int code = 0;
    AtFinal f = AtFinal::None;
    uint32_t start = millis();
    while (millis() - start < 60000) {
      if (!readLine(line, sizeof line, 50)) continue;
      f = atClassifyFinal(line, &code);
      if (f != AtFinal::None && f != AtFinal::NoCarrier) break;
      handleLine(line);
      f = AtFinal::None;
    }
    if (f != AtFinal::Ok) err = f == AtFinal::None ? "Timed out" : atErrorText(f, code);
  }
  LOG("SMS", "send to %s (%d part%s): %s", logNum(o->number), n, n == 1 ? "" : "s", err ? err : "OK");
  events::post(Ev::SmsSent, (int32_t)o->id, err ? 0 : 1, err ? strdup(err) : nullptr);
  delete o;
}

// ---------------------------------------------------------------------------
// SIM phone book import
// ---------------------------------------------------------------------------
void doReadSimContacts() {
  auto* list = new std::vector<Contact>();
  exec("AT+CPBS=\"SM\"");
  int maxIdx = 250;
  exec("AT+CPBR=?", 3000, [&](const char* line) {
    const char* dash = strchr(line, '-');
    if (txt::startsWith(line, "+CPBR:") && dash) maxIdx = atoi(dash + 1);
  });
  if (maxIdx < 1 || maxIdx > 500) maxIdx = 250;
  char cmd[32];
  snprintf(cmd, sizeof cmd, "AT+CPBR=1,%d", maxIdx);
  exec(cmd, 30000, [&](const char* line) {
    int idx;
    Contact c;
    if (parseCpbr(line, &idx, c.number, sizeof c.number, c.name, sizeof c.name) && c.number[0]) {
      if (!c.name[0]) txt::copy(c.name, c.number, sizeof c.name);
      list->push_back(c);
    }
  });
  LOG("MODEM", "SIM phone book: %u entries", (unsigned)list->size());
  events::post(Ev::SimContacts, 0, 0, list);
}

// ---------------------------------------------------------------------------
// Power / init
// ---------------------------------------------------------------------------
void powerOn() {
  pinMode(PIN_MODEM_PWRKEY, OUTPUT);
  pinMode(PIN_MODEM_RST, OUTPUT);
  pinMode(PIN_MODEM_POWER_ON, OUTPUT);
  digitalWrite(PIN_MODEM_PWRKEY, LOW);
  digitalWrite(PIN_MODEM_RST, HIGH);
  digitalWrite(PIN_MODEM_POWER_ON, HIGH);
}

void hardReset() {
  LOG("MODEM", "power-cycling SIM800L");
  digitalWrite(PIN_MODEM_POWER_ON, LOW);
  digitalWrite(PIN_MODEM_RST, LOW);
  vTaskDelay(pdMS_TO_TICKS(1500));
  powerOn();
  vTaskDelay(pdMS_TO_TICKS(3000));
  updateStatus([](NetStatus& s) {
    uint32_t resets = s.modemResets + 1;
    NetStatus fresh;
    fresh.modemResets = resets;
    txt::copy(fresh.imei, s.imei, sizeof fresh.imei);
    s = fresh;
  });
  endCall(CallEndReason::Failed);
  s_timeouts = 0;
}

void setModemState(ModemState m) {
  updateStatus([m](NetStatus& s) { s.modem = m; });
}

bool initModem() {
  s_cusdOpen = false;  // drop any half-received output from before the (re)start
  s_cusd.clear();
  s_lineLen = 0;
  setModemState(ModemState::Booting);
  LOG("MODEM", "initializing");
  bool alive = false;
  for (int i = 0; i < 25 && !alive; ++i) alive = ok("AT", 400);  // also locks SIM800 auto-baud
  if (!alive) {
    LOG("MODEM", "no response on UART (check the board is a T-Call and the antenna/power)");
    setModemState(ModemState::NotResponding);
    return false;
  }
  setModemState(ModemState::Initializing);
  ok("ATE0");
  ok("AT+CMEE=1");
  ok("AT+CFUN=1", 10000);

  // SIM (it can report NOT READY for a few seconds after power-up)
  for (int i = 0; i < 8; ++i) {
    int code = 0;
    AtFinal f = exec("AT+CPIN?", 5000, nullptr, false, &code);
    if (f == AtFinal::CmeError && code == 10) updateStatus([](NetStatus& s) { s.sim = SimState::Absent; });
    SimState sim = modem::status().sim;
    if (sim == SimState::Ready || sim == SimState::PinRequired || sim == SimState::PukRequired) break;
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  NetStatus st = modem::status();
  LOG("MODEM", "SIM %s", st.sim == SimState::Ready ? "READY" : st.sim == SimState::Absent ? "NOT INSERTED"
                         : st.sim == SimState::PinRequired ? "PIN REQUIRED" : "not ready");

  ok("AT+CLIP=1");            // caller ID
  ok("AT+CLCC=1");            // automatic +CLCC on every call state change
  ok("AT+MORING=1");          // "MO RING" / "MO CONNECTED" for outgoing calls
  ok("AT+CRC=0");
  ok("AT+CMGF=0");            // PDU mode
  ok("AT+CNMI=2,1,0,0,0");    // new SMS -> stored + +CMTI
  if (!ok("AT+CPMS=\"SM\",\"SM\",\"SM\"", 5000)) {
    s_smsSetupPending = true;  // SMS part not ready yet - retried from periodic()
    s_tSmsSetup = millis() + 5000;
  }
  ok("AT+CSCS=\"IRA\"");
  ok("AT+CREG=1");
  ok("AT+COPS=3,0");          // long alphanumeric operator name
  ok("AT+SIDET=0,0");         // no side-tone: avoids echo through the Bluetooth bridge
  ok("AT+CALM=0");
  char cmd[40];
  snprintf(cmd, sizeof cmd, "AT+CLVL=%u", s_volume);
  ok(cmd);
  snprintf(cmd, sizeof cmd, "AT+CRSL=%u", s_ringer);
  ok(cmd);
  snprintf(cmd, sizeof cmd, "AT+CMIC=0,%u", s_mic);
  ok(cmd);

  // Network time (NITZ). Needs saving + a restart of the modem to take effect once.
  bool cltsOff = false;
  exec("AT+CLTS?", 2000, [&](const char* l) { if (txt::startsWith(l, "+CLTS: 0")) cltsOff = true; });
  if (cltsOff) {
    ok("AT+CLTS=1");
    ok("AT&W");
    LOG("MODEM", "enabled network time sync (active after next modem restart)");
  }

  // Identity
  exec("AT+GSN", 2000, [](const char* l) {
    if (strlen(l) >= 14 && l[0] >= '0' && l[0] <= '9') updateStatus([&](NetStatus& s) { txt::copy(s.imei, l, sizeof s.imei); });
  });
  exec("AT+CCID", 2000, [](const char* l) {
    if (strlen(l) >= 18 && l[0] >= '0' && l[0] <= '9') updateStatus([&](NetStatus& s) { txt::copy(s.iccid, l, sizeof s.iccid); });
  });
  exec("AT+GMR", 2000, [](const char* l) {
    if (txt::startsWith(l, "Revision:")) updateStatus([&](NetStatus& s) { txt::copy(s.fw, l + 9, sizeof s.fw); });
  });
  exec("AT+CNUM", 3000, [](const char* l) {
    char num[NUMBER_MAX];
    if (parseCnum(l, num, sizeof num)) updateStatus([&](NetStatus& s) { txt::copy(s.ownNumber, num, sizeof s.ownNumber); });
  });

  setModemState(ModemState::Ready);
  uint32_t now = millis();
  s_tPoll = s_tSlow = now;
  s_tClock = now + 3000;
  s_tSweep = now + 2000;
  s_needReinit = false;
  LOG("MODEM", "ready (IMEI %s)", modem::status().imei);
  return true;
}

// ---------------------------------------------------------------------------
void pollCalls() {
  int calls = 0;
  AtFinal f = exec("AT+CLCC", 3000, [&](const char* l) { if (txt::startsWith(l, "+CLCC:")) calls++; });
  if (f != AtFinal::Ok) return;
  CallInfo c = callSnapshot();
  if (calls == 0 && c.phase != CallPhase::Idle && millis() - c.setupMs > 4000) {
    // While an outgoing call is still being set up the modem may not list it yet;
    // the network's NO CARRIER / BUSY / NO ANSWER ends it. Only give up locally after
    // a long silence, and then really hang up so modem and screen agree.
    bool settingUp = c.phase == CallPhase::Dialing || c.phase == CallPhase::Alerting;
    if (++s_noCallPolls >= (settingUp ? 24 : 2)) {
      LOG("CALL", "modem lists no call any more - ending it (%s)", settingUp ? "setup" : "active");
      exec("ATH", 5000);
      endCall(CallEndReason::None);
    }
  }
  if (c.phase == CallPhase::Incoming && calls == 0 && millis() - s_lastRingMs > 12000) endCall(CallEndReason::None);
}

void periodic() {
  uint32_t now = millis();
  NetStatus st = modem::status();
  CallInfo c = callSnapshot();
  if (c.phase != CallPhase::Idle && (int32_t)(now - s_tCallPoll) >= 0) {
    pollCalls();
    s_tCallPoll = now + MODEM_CALL_POLL_MS;
  }
  if ((int32_t)(now - s_tPoll) >= 0) {
    exec("AT+CSQ");
    exec("AT+CREG?");
    s_tPoll = now + (isRegistered(modem::status()) ? MODEM_POLL_IDLE_MS : MODEM_POLL_SEARCH_MS);
  }
  if ((int32_t)(now - s_tSlow) >= 0) {
    exec("AT+CBC");
    if (isRegistered(st)) exec("AT+COPS?", 5000);
    if (st.sim != SimState::Ready) exec("AT+CPIN?", 5000);
    s_tSlow = now + MODEM_POLL_SLOW_MS;
  }
  if ((int32_t)(now - s_tClock) >= 0) {
    if (isRegistered(st)) exec("AT+CCLK?");
    s_tClock = now + MODEM_CLOCK_POLL_MS;
  }
  if ((int32_t)(now - s_tSweep) >= 0) {
    if (st.sim == SimState::Ready) sweepSms();
    s_tSweep = now + MODEM_SMS_SWEEP_MS;
  }
  while (!s_pendingSms.empty()) {
    int idx = s_pendingSms.front();
    s_pendingSms.erase(s_pendingSms.begin());
    readSmsSlot(idx);
  }
  if (s_smsSetupPending && (int32_t)(now - s_tSmsSetup) >= 0) {
    bool okAll = ok("AT+CMGF=0") && ok("AT+CNMI=2,1,0,0,0") && ok("AT+CPMS=\"SM\",\"SM\",\"SM\"", 5000);
    s_smsSetupPending = !okAll;
    s_tSmsSetup = now + 10000;
    if (okAll) {
      LOG("SMS", "message storage ready");
      s_tSweep = now + 500;
    }
  }
  s_assembler.expire(now, SMS_CONCAT_TIMEOUT_MS);
  flushAssembler();
  if (s_ussdDeadline && (int32_t)(now - s_ussdDeadline) >= 0) {
    s_ussdDeadline = 0;
    events::post(Ev::UssdReply, 0, 0, strdup("No reply from the network. Try again."));
  }
}

// ---------------------------------------------------------------------------
void handleCmd(ModemCmd& c) {
  char cmd[80];
  CallInfo call = callSnapshot();
  switch (c.type) {
    case Mc::Dial: {
      if (call.phase != CallPhase::Idle) { notice("Already in a call"); break; }
      NetStatus st = modem::status();
      bool emergency = strcmp(c.str, "112") == 0 || strcmp(c.str, "100") == 0 || strcmp(c.str, "108") == 0;
      if (!isRegistered(st) && !emergency) {
        notice(st.sim == SimState::Ready ? "No network" : "No SIM card");
        CallInfo* snap = new CallInfo();
        txt::copy(snap->number, c.str, sizeof snap->number);
        snap->endReason = CallEndReason::NoNetwork;
        events::post(Ev::CallEnded, (int32_t)CallEndReason::NoNetwork, 0, snap);
        break;
      }
      setPhase(CallPhase::Dialing, c.str, 0);
      snprintf(cmd, sizeof cmd, "ATD%s;", c.str);
      int code = 0;
      AtFinal f = exec(cmd, 20000, nullptr, true, &code);
      if (f == AtFinal::Ok) break;
      if (f == AtFinal::Busy) endCall(CallEndReason::Busy);
      else if (f == AtFinal::NoDialtone) endCall(CallEndReason::NoNetwork);
      else if (f == AtFinal::NoCarrier || f == AtFinal::NoAnswer) endCall(CallEndReason::NoAnswer);
      else {
        notice(f == AtFinal::None ? "Call failed (modem timeout)" : atErrorText(f, code));
        endCall(CallEndReason::Failed);
      }
      break;
    }
    case Mc::Answer: {
      // Answer if the modem says "incoming" OR it rang recently (state reports can lag).
      bool rangRecently = s_lastRingMs && millis() - s_lastRingMs < 10000;
      if (call.phase != CallPhase::Incoming && !rangRecently) {
        LOG("CALL", "answer ignored: no incoming call (phase %d)", (int)call.phase);
        break;
      }
      AtFinal f = exec("ATA", 10000, nullptr, true);
      LOG("CALL", "ATA -> %s", f == AtFinal::None ? "timeout" : atErrorText(f, 0));
      if (f == AtFinal::Ok) setPhase(CallPhase::Active, nullptr, -1);
      else if (f == AtFinal::NoCarrier) endCall(CallEndReason::None);
      break;
    }
    case Mc::HangUp: {
      if (call.phase == CallPhase::Idle) break;
      if (call.phase == CallPhase::Incoming) s_userRejected = true;
      if (exec("ATH", 5000) != AtFinal::Ok) exec("AT+CHUP", 5000);
      endCall(CallEndReason::Normal);
      break;
    }
    case Mc::Dtmf:
      if (call.phase == CallPhase::Active) {
        snprintf(cmd, sizeof cmd, "AT+VTS=\"%c\"", (char)c.a);
        exec(cmd, 3000);
      }
      break;
    case Mc::Mute:
      snprintf(cmd, sizeof cmd, "AT+CMUT=%d", c.a ? 1 : 0);
      if (ok(cmd)) updateCall([&](CallInfo& ci) { ci.muted = c.a != 0; });
      break;
    case Mc::SetVolume:
      s_volume = (uint8_t)c.a;
      snprintf(cmd, sizeof cmd, "AT+CLVL=%d", (int)c.a);
      ok(cmd);
      break;
    case Mc::SetRinger:
      s_ringer = (uint8_t)c.a;
      snprintf(cmd, sizeof cmd, "AT+CRSL=%d", (int)c.a);
      ok(cmd);
      break;
    case Mc::SetMicGain:
      s_mic = (uint8_t)c.a;
      snprintf(cmd, sizeof cmd, "AT+CMIC=0,%d", (int)c.a);
      ok(cmd);
      break;
    case Mc::SendSms:
      doSendSms(static_cast<OutgoingSms*>(c.ptr));
      break;
    case Mc::Ussd: {
      if (!isRegistered(modem::status())) {
        events::post(Ev::UssdReply, 0, 0, strdup("No network"));
        break;
      }
      snprintf(cmd, sizeof cmd, "AT+CUSD=1,\"%s\",15", c.str);
      int code = 0;
      AtFinal f = exec(cmd, 10000, nullptr, false, &code);
      if (f == AtFinal::Ok) s_ussdDeadline = millis() + 30000;
      else events::post(Ev::UssdReply, 0, 0, strdup(f == AtFinal::None ? "Modem timeout" : atErrorText(f, code)));
      break;
    }
    case Mc::UssdCancel:
      s_ussdDeadline = 0;
      exec("AT+CUSD=2", 3000);
      break;
    case Mc::EnterPin: {
      snprintf(cmd, sizeof cmd, "AT+CPIN=\"%s\"", c.str);
      if (exec(cmd, 10000) == AtFinal::Ok) {
        notice("SIM unlocked");
        s_needReinit = true;  // re-run the SIM dependent setup
      } else {
        notice("Wrong PIN");
      }
      exec("AT+CPIN?", 5000);
      break;
    }
    case Mc::ReadSimContacts:
      doReadSimContacts();
      break;
    case Mc::SetClock: {
      int y, mo, d, h, mi, s;
      txt::splitEpoch((uint32_t)c.a, &y, &mo, &d, &h, &mi, &s, nullptr);
      snprintf(cmd, sizeof cmd, "AT+CCLK=\"%02d/%02d/%02d,%02d:%02d:%02d%+03d\"", y % 100, mo, d, h, mi, s, s_tzQuarters);
      ok(cmd);
      setSystemTime((uint32_t)c.a, "manual");
      break;
    }
    case Mc::RawAt: {
      LOG("AT", "> %s", c.str);
      AtFinal f = exec(c.str, 15000, [](const char* l) { LOG("AT", "< %s", l); });
      LOG("AT", "< %s", f == AtFinal::None ? "(timeout)" : atErrorText(f, 0));
      break;
    }
    case Mc::Restart:
      hardReset();
      s_ready = false;
      break;
    case Mc::Refresh:
      s_tPoll = s_tSlow = s_tClock = millis();
      break;
    case Mc::SpeakerTest: {
      // SIM toolkit "general beep" for 2 s on the main audio channel (SPK+/SPK-).
      AtFinal f = exec("AT+STTONE=1,16,2000", 3000);
      if (f != AtFinal::Ok) {  // older firmware: preview ring tone 1 instead
        f = exec("AT+CALS=1,1", 3000);
        vTaskDelay(pdMS_TO_TICKS(2000));
        exec("AT+CALS=1,0", 3000);
      }
      LOG("AUDIO", "speaker test tone %s (volume %u%%)", f == AtFinal::Ok ? "played" : "FAILED", s_volume);
      break;
    }
  }
}

void modemTask(void*) {
  powerOn();
  SerialAT.setRxBufferSize(4096);
  SerialAT.begin(MODEM_BAUD, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);
  vTaskDelay(pdMS_TO_TICKS(3000));  // SIM800L boot time
  int failures = 0;
  for (;;) {
    if (s_needReinit) s_ready = false;
    if (!s_ready) {
      if (initModem()) {
        s_ready = true;
        failures = 0;
      } else {
        ++failures;
        hardReset();
        if (failures > 3) vTaskDelay(pdMS_TO_TICKS(10000));  // back off, keep the UI responsive
        continue;
      }
    }
    ModemCmd c;
    if (xQueueReceive(s_cmdQ, &c, pdMS_TO_TICKS(10)) == pdTRUE) handleCmd(c);
    char line[640];
    while (readLine(line, sizeof line, 0)) handleLine(line);
    periodic();
    if (s_timeouts >= 4) {
      LOG("MODEM", "not responding - resetting");
      setModemState(ModemState::NotResponding);
      hardReset();
      s_ready = false;
    }
  }
}

bool postCmd(Mc type, int32_t a = 0, const char* str = nullptr, void* ptr = nullptr) {
  ModemCmd c{};
  c.type = type;
  c.a = a;
  c.ptr = ptr;
  if (str) txt::copy(c.str, str, sizeof c.str);
  if (!s_cmdQ || xQueueSend(s_cmdQ, &c, pdMS_TO_TICKS(100)) != pdTRUE) {
    LOG("MODEM", "command queue full");
    return false;
  }
  return true;
}

}  // namespace

// ===========================================================================
namespace modem {

void begin(uint8_t callVolume, uint8_t ringerVolume, uint8_t micGain) {
  s_volume = callVolume;
  s_ringer = ringerVolume;
  s_mic = micGain;
  s_cmdQ = xQueueCreate(16, sizeof(ModemCmd));
  xTaskCreatePinnedToCore(modemTask, "modem", 12288, nullptr, 5, nullptr, 1);
}

void dial(const char* number) {
  char clean[NUMBER_MAX];
  txt::sanitizeDial(number, clean, sizeof clean);
  if (clean[0]) postCmd(Mc::Dial, 0, clean);
}
void answer() { postCmd(Mc::Answer); }
void hangup() { postCmd(Mc::HangUp); }
void dtmf(char key) { postCmd(Mc::Dtmf, key); }
void mute(bool on) { postCmd(Mc::Mute, on ? 1 : 0); }
void setVolume(int v) { postCmd(Mc::SetVolume, v < 0 ? 0 : v > 100 ? 100 : v); }
void setRinger(int v) { postCmd(Mc::SetRinger, v < 0 ? 0 : v > 100 ? 100 : v); }
void setMicGain(int g) { postCmd(Mc::SetMicGain, g < 0 ? 0 : g > 15 ? 15 : g); }

bool sendSms(uint32_t localId, const char* number, const char* text) {
  auto* o = new OutgoingSms();
  o->id = localId;
  txt::copy(o->number, number, sizeof o->number);
  txt::copy(o->text, text, sizeof o->text);
  if (!postCmd(Mc::SendSms, 0, nullptr, o)) {
    delete o;
    return false;
  }
  return true;
}

void ussd(const char* code) { postCmd(Mc::Ussd, 0, code); }
void ussdCancel() { postCmd(Mc::UssdCancel); }
void enterPin(const char* pin) { postCmd(Mc::EnterPin, 0, pin); }
void readSimContacts() { postCmd(Mc::ReadSimContacts); }
void setClock(uint32_t localEpoch) { postCmd(Mc::SetClock, (int32_t)localEpoch); }
void rawAt(const char* cmd) { postCmd(Mc::RawAt, 0, cmd); }
void restart() { postCmd(Mc::Restart); }
void refresh() { postCmd(Mc::Refresh); }
void speakerTest() { postCmd(Mc::SpeakerTest); }

NetStatus status() {
  portENTER_CRITICAL(&s_mux);
  NetStatus s = s_st;
  portEXIT_CRITICAL(&s_mux);
  return s;
}

CallInfo call() { return callSnapshot(); }

bool registered() { return isRegistered(status()); }

PhoneState phoneState() {
  NetStatus s = status();
  switch (s.modem) {
    case ModemState::Off:
    case ModemState::Booting: return PhoneState::Boot;
    case ModemState::Initializing: return PhoneState::InitializingModem;
    case ModemState::NotResponding: return PhoneState::Error;
    case ModemState::Ready: break;
  }
  if (s.sim == SimState::Absent) return PhoneState::NoSim;
  if (s.sim == SimState::PinRequired || s.sim == SimState::PukRequired) return PhoneState::SimLocked;
  if (s.sim != SimState::Ready) return PhoneState::InitializingModem;
  return isRegistered(s) ? PhoneState::Ready : PhoneState::SearchingNetwork;
}

const char* phoneStateText(PhoneState s) {
  switch (s) {
    case PhoneState::Boot: return "Starting modem...";
    case PhoneState::InitializingModem: return "Searching SIM...";
    case PhoneState::NoSim: return "No SIM card";
    case PhoneState::SimLocked: return "SIM locked";
    case PhoneState::SearchingNetwork: return "Searching network...";
    case PhoneState::Ready: return "Ready";
    case PhoneState::Error: return "Modem not responding";
  }
  return "";
}

}  // namespace modem
