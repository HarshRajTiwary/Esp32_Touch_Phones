// SMS (PDU mode), USSD and the SIM phone book.
//
// Received messages stay on the SIM until they have been decoded and handed to
// the application (long messages: until every part arrived or the join timed
// out); only then is the SIM slot deleted. A periodic sweep re-reads the SIM in
// case a +CMTI notification was lost.
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <utility>
#include <vector>

#include "a7672s_internal.h"
#include "plog.h"
#include "s3_config.h"
#include "sms_assembler.h"
#include "sms_pdu.h"
#include "text_util.h"

namespace a7672s {
namespace {

std::vector<int> s_pendingSms;
SmsAssembler s_assembler;
uint8_t s_smsRef = 1;
uint32_t s_nextSweep = 0;
uint32_t s_ussdDeadline = 0;
std::string s_cusd;       // multi-line +CUSD accumulation
bool s_cusdOpen = false;

void onCusd(const char* full) {
  int m = 2, dcs = 15;
  char* text = (char*)malloc(USSD_TEXT_MAX);
  char* body = (char*)malloc(USSD_TEXT_MAX);
  if (!text || !body) {
    free(text);
    free(body);
    return;
  }
  parseCusd(full, &m, body, USSD_TEXT_MAX, &dcs);
  decodeUssd(body, dcs, text, USSD_TEXT_MAX);
  free(body);
  if (!text[0]) txt::copy(text, m == 2 ? "Session ended" : "(empty reply)", USSD_TEXT_MAX);
  if (m == 4) txt::copy(text, "Operation not supported", USSD_TEXT_MAX);
  s_ussdDeadline = 0;
  LOG("USSD", "reply m=%d dcs=%d %s", m, dcs, logText(text));
  events::post(Ev::UssdReply, m == 1 ? 1 : 0, 0, text);
}

void deleteSlot(int index) {
  char cmd[20];
  snprintf(cmd, sizeof cmd, "AT+CMGD=%d", index);
  exec(cmd, 5000);
}

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
    for (int idx : d.simIndices) deleteSlot(idx);
  }
}

void deliverPdu(const char* hex, int index) {
  if (s_assembler.knowsIndex(index)) return;
  SmsPdu* p = new SmsPdu();
  bool okDecode = smsDecodePdu(hex, p);
  if (!okDecode || p->isStatusReport || p->isSubmit || !p->text[0]) {
    // Not a readable incoming message: free the SIM slot so storage never fills up.
    if (!okDecode) LOG("SMS", "slot %d: undecodable PDU, deleting", index);
    deleteSlot(index);
  } else {
    s_assembler.add(*p, index, nowMs());
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
    if (parseCmgrHeader(line, &st, &len)) {
      expectPdu = true;
      return;
    }
    if (expectPdu) {
      pdu = line;
      expectPdu = false;
    }
  });
  if (f == AtFinal::Ok && !pdu.empty()) deliverPdu(pdu.c_str(), index);
}

void sweepSms() {
  std::vector<std::pair<int, std::string>> found;
  int curIndex = -1;
  exec("AT+CMGL=4", 20000, [&](const char* line) {
    int idx, st, len;
    if (parseCmglHeader(line, &idx, &st, &len)) {
      curIndex = idx;
      return;
    }
    if (curIndex >= 0) {
      found.emplace_back(curIndex, std::string(line));
      curIndex = -1;
    }
  });
  if (!found.empty()) LOG("SMS", "sweep found %u stored message(s)", (unsigned)found.size());
  for (auto& f : found) deliverPdu(f.second.c_str(), f.first);
}

}  // namespace

bool smsHandleLine(const char* line) {
  if (s_cusdOpen) {
    s_cusd += "\n";
    s_cusd += line;
    // An odd number of quotes on this line closes the string. The size limit
    // guards against a malformed reply keeping the parser stuck forever.
    if (atHasOpenQuote(line) || s_cusd.size() > USSD_TEXT_MAX * 2) {
      s_cusdOpen = false;
      onCusd(s_cusd.c_str());
    }
    return true;
  }
  int n;
  if (parseCmti(line, &n)) {
    LOG("SMS", "new message in slot %d", n);
    s_pendingSms.push_back(n);
    return true;
  }
  if (txt::startsWith(line, "+CUSD:")) {
    if (atHasOpenQuote(line)) {
      s_cusd = line;
      s_cusdOpen = true;
    } else {
      onCusd(line);
    }
    return true;
  }
  if (strcmp(line, "SMS DONE") == 0) {
    updateStatus([](NetStatus& s) { s.smsReady = true; });
    s_nextSweep = nowMs() + 1000;
    return true;
  }
  return false;
}

void smsConfigure() {
  s_cusdOpen = false;
  s_cusd.clear();
  ok("AT+CMGF=0");                              // PDU mode (long + Unicode messages)
  ok("AT+CSCS=\"IRA\"");
  ok("AT+CPMS=\"SM\",\"SM\",\"SM\"", 5000);     // messages on the SIM until stored safely
  ok("AT+CNMI=2,1,0,0,0");                      // store + "+CMTI" notification
  ok("AT+CGEREP=2,1");                          // +CGEV: data context events
  updateStatus([](NetStatus& s) { s.smsReady = true; });
}

void smsScheduleSweep(uint32_t atMs) { s_nextSweep = atMs; }

void smsPeriodic(uint32_t now) {
  NetStatus st = statusSnapshot();
  if ((int32_t)(now - s_nextSweep) >= 0) {
    if (st.sim == SimState::Ready) sweepSms();
    s_nextSweep = now + MODEM_SMS_SWEEP_MS;
  }
  while (!s_pendingSms.empty()) {
    int idx = s_pendingSms.front();
    s_pendingSms.erase(s_pendingSms.begin());
    readSmsSlot(idx);
  }
  s_assembler.expire(now, SMS_CONCAT_TIMEOUT_MS);
  flushAssembler();
  if (s_ussdDeadline && (int32_t)(now - s_ussdDeadline) >= 0) {
    s_ussdDeadline = 0;
    events::post(Ev::UssdReply, 0, 0, strdup("No reply from the network. Try again."));
  }
}

void smsSend(OutgoingSms* o) {
  static SmsPart parts[SMS_SEND_MAX_PARTS];
  const char* err = nullptr;
  NetStatus st = statusSnapshot();
  int n = 0;
  if (st.sim != SimState::Ready) err = "SIM not detected";
  else if (!isRegistered(st)) err = "No network";
  else if ((n = smsEncodeSubmit(o->number, o->text, s_smsRef++, parts, SMS_SEND_MAX_PARTS)) == 0)
    err = "Message too long or bad number";
  for (int i = 0; i < n && !err; ++i) {
    drainUrcs();
    char cmd[24];
    snprintf(cmd, sizeof cmd, "AT+CMGS=%d\r", parts[i].tpduLen);
    sendRaw(cmd, strlen(cmd));
    if (!waitPrompt(6000)) {
      const char esc = 0x1B;  // abort any half-open prompt
      sendRaw(&esc, 1);
      err = "Modem did not accept the message";
      break;
    }
    sendRaw(parts[i].hex, strlen(parts[i].hex));
    const char ctrlZ = 0x1A;
    sendRaw(&ctrlZ, 1);
    // The network can take up to a minute to confirm.
    AtFinal f = AtFinal::None;
    int code = 0;
    char line[160];
    uint32_t start = nowMs();
    while (nowMs() - start < 60000) {
      if (!readLine(line, sizeof line, 200)) continue;
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

void smsReadSimContacts() {
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

void ussdStart(const char* code) {
  if (!isRegistered(statusSnapshot())) {
    events::post(Ev::UssdReply, 0, 0, strdup("No network"));
    return;
  }
  // Only USSD characters reach the modem (input validation)
  for (const char* p = code; *p; ++p)
    if (!((*p >= '0' && *p <= '9') || *p == '*' || *p == '#' || *p == '+')) {
      events::post(Ev::UssdReply, 0, 0, strdup("Invalid code"));
      return;
    }
  char cmd[80];
  snprintf(cmd, sizeof cmd, "AT+CUSD=1,\"%s\",15", code);
  int err = 0;
  AtFinal f = exec(cmd, 10000, nullptr, false, &err);
  if (f == AtFinal::Ok) s_ussdDeadline = nowMs() + 30000;
  else events::post(Ev::UssdReply, 0, 0, strdup(f == AtFinal::None ? "Modem timeout" : atErrorText(f, err)));
}

void ussdCancel() {
  s_ussdDeadline = 0;
  exec("AT+CUSD=2", 3000);
}

}  // namespace a7672s
