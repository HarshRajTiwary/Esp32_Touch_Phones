// Voice calls: feeds the modem's lines and our AT results into CallTracker
// (call_tracker.cpp, the pure state machine) and publishes its state to the
// rest of the phone (snapshot under the lock + CallChanged / CallEnded events).
#include <stdio.h>

#include "a7672s_internal.h"
#include "call_tracker.h"
#include "plog.h"
#include "s3_config.h"
#include "text_util.h"

namespace a7672s {
namespace {

CallTracker s_calls;   // only touched from the modem task
uint32_t s_nextPoll = 0;

void publish(uint8_t flags) {
  if (s_calls.notice()[0]) notice(s_calls.notice());
  if (!(flags & CallTracker::kChanged)) return;
  portENTER_CRITICAL(&g_mux);
  g_call = s_calls.info();
  portEXIT_CRITICAL(&g_mux);
  events::post(Ev::CallChanged);
  if (flags & CallTracker::kEnded) {
    const CallInfo& e = s_calls.ended();
    events::post(Ev::CallEnded, (int32_t)e.endReason, 0, new CallInfo(e));
  }
}

bool isEmergency(const char* n) { return !strcmp(n, "112") || !strcmp(n, "100") || !strcmp(n, "108") || !strcmp(n, "911"); }

}  // namespace

bool callHandleLine(const char* line) {
  uint8_t f = s_calls.onLine(line, nowMs());
  publish(f);
  return (f & CallTracker::kConsumed) != 0;
}

void callPeriodic(uint32_t now) {
  if (s_calls.info().phase == CallPhase::Idle) return;
  if ((int32_t)(now - s_nextPoll) < 0) return;
  s_nextPoll = now + MODEM_CALL_POLL_MS;
  int calls = 0;
  AtFinal f = exec("AT+CLCC", 3000, [&](const char* l) {
    if (txt::startsWith(l, "+CLCC:")) calls++;
  });
  if (f == AtFinal::Ok) publish(s_calls.pollResult(calls, nowMs()));
}

void callDial(const char* number) {
  if (s_calls.info().phase != CallPhase::Idle) {
    notice("Already in a call");
    return;
  }
  NetStatus st = statusSnapshot();
  if (!isRegistered(st) && !isEmergency(number)) {
    notice(st.sim == SimState::Ready ? "No network" : "SIM not detected");
    CallInfo* snap = new CallInfo();
    txt::copy(snap->number, number, sizeof snap->number);
    snap->endReason = CallEndReason::NoNetwork;
    events::post(Ev::CallEnded, (int32_t)CallEndReason::NoNetwork, 0, snap);
    return;
  }
  publish(s_calls.dialStarted(number, nowMs()));
  s_nextPoll = nowMs() + MODEM_CALL_POLL_MS;
  char cmd[48];
  snprintf(cmd, sizeof cmd, "ATD%s;", number);
  int code = 0;
  AtFinal f = exec(cmd, 20000, nullptr, true, &code);
  if (f != AtFinal::Ok && f != AtFinal::Busy && f != AtFinal::NoDialtone && f != AtFinal::NoCarrier &&
      f != AtFinal::NoAnswer)
    notice(f == AtFinal::None ? "Call failed (modem timeout)" : atErrorText(f, code));
  publish(s_calls.dialResult(f, nowMs()));
}

void callAnswer() {
  if (s_calls.info().phase != CallPhase::Incoming) return;
  AtFinal f = exec("ATA", 10000, nullptr, true);
  publish(f == AtFinal::Ok ? s_calls.answered(nowMs()) : s_calls.answerFailed(f, nowMs()));
}

void callHangup() {
  if (s_calls.info().phase == CallPhase::Idle) return;
  if (exec("AT+CHUP", 5000) != AtFinal::Ok) exec("ATH", 5000);
  publish(s_calls.hungUp(nowMs()));
}

void callDtmf(char key) {
  if (s_calls.info().phase != CallPhase::Active) return;
  if (!((key >= '0' && key <= '9') || key == '*' || key == '#' || (key >= 'A' && key <= 'D'))) return;
  char cmd[24];
  snprintf(cmd, sizeof cmd, "AT+VTS=\"%c\"", key);
  exec(cmd, 3000);
}

void callMute(bool on) {
  char cmd[16];
  snprintf(cmd, sizeof cmd, "AT+CMUT=%d", on ? 1 : 0);
  if (ok(cmd)) publish(s_calls.setMuted(on));
}

void callEndAll(CallEndReason reason) { publish(s_calls.endAll(reason)); }

}  // namespace a7672s
