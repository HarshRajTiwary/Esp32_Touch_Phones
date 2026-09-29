#include "call_tracker.h"

#include <stdio.h>
#include <string.h>

#include "plog.h"
#include "text_util.h"

uint8_t CallTracker::setPhase(CallPhase ph, const char* number, int incoming, uint32_t now) {
  if (call_.phase == ph && (!number || !*number || strcmp(call_.number, number) == 0)) return 0;
  const char* before = call_.number;
  char prevNumber[NUMBER_MAX];
  txt::copy(prevNumber, before, sizeof prevNumber);
  if (call_.phase == CallPhase::Idle) {
    call_ = CallInfo();
    call_.setupMs = now;
  }
  call_.phase = ph;
  if (incoming >= 0) call_.incoming = incoming;
  if (number && *number) txt::copy(call_.number, number, sizeof call_.number);
  if (ph == CallPhase::Active && !call_.everActive) {
    call_.everActive = true;
    call_.activeMs = now;
  }
  static const char* kNames[] = {"idle", "dialing", "alerting", "incoming", "active"};
  const char* num = number && *number ? number : prevNumber;
  if (ph == CallPhase::Incoming) LOG("CALL", "Incoming: %s", logNum(num));
  else LOG("CALL", "%s %s", kNames[(int)ph], logNum(num));
  return kChanged;
}

uint8_t CallTracker::endCall(CallEndReason reason) {
  if (call_.phase == CallPhase::Idle) return 0;
  CallInfo snap = call_;
  if (snap.incoming && !snap.everActive) reason = userRejected_ ? CallEndReason::Rejected : CallEndReason::Missed;
  else if (reason == CallEndReason::None) reason = pendingEnd_ != CallEndReason::None ? pendingEnd_ : CallEndReason::Normal;
  snap.endReason = reason;
  static const char* kReason[] = {"none", "normal", "busy", "no answer", "no carrier", "rejected", "missed", "failed", "no network"};
  LOG("CALL", "ended (%s) %s", kReason[(int)reason], logNum(snap.number));
  ended_ = snap;
  call_ = CallInfo();
  userRejected_ = false;
  trackedIdx_ = 0;
  pendingEnd_ = CallEndReason::None;
  noCallPolls_ = 0;
  return kChanged | kEnded;
}

uint8_t CallTracker::onClcc(const ClccInfo& c, uint32_t now) {
  if (c.mode != 0) return 0;  // data / fax "calls" are not ours
  if (trackedIdx_ && c.idx != trackedIdx_) {
    if (c.stat == 5) snprintf(notice_, sizeof notice_, "Call waiting: %s", c.number);
    return 0;
  }
  noCallPolls_ = 0;
  switch (c.stat) {
    case 0:
    case 1:
      trackedIdx_ = c.idx;
      return setPhase(CallPhase::Active, c.number, c.dir == 1, now);
    case 2:
      trackedIdx_ = c.idx;
      return setPhase(CallPhase::Dialing, c.number, 0, now);
    case 3:
      trackedIdx_ = c.idx;
      return setPhase(CallPhase::Alerting, c.number, 0, now);
    case 4:
    case 5:
      trackedIdx_ = c.idx;
      lastRingMs_ = now;
      return setPhase(CallPhase::Incoming, c.number, 1, now);
    case 6:
      return endCall(CallEndReason::None);
  }
  return 0;
}

uint8_t CallTracker::onLine(const char* line, uint32_t now) {
  notice_[0] = 0;
  ClccInfo clcc;
  char num[NUMBER_MAX];
  bool begin;
  int secs;
  if (strcmp(line, "RING") == 0) {
    lastRingMs_ = now;
    return kConsumed | (call_.phase == CallPhase::Idle ? setPhase(CallPhase::Incoming, nullptr, 1, now) : 0);
  }
  if (parseClip(line, num, sizeof num)) {
    lastRingMs_ = now;
    bool ringing = call_.phase == CallPhase::Idle || call_.phase == CallPhase::Incoming;
    return kConsumed | (ringing ? setPhase(CallPhase::Incoming, num, 1, now) : 0);
  }
  if (parseClcc(line, &clcc)) return kConsumed | onClcc(clcc, now);
  if (parseVoiceCall(line, &begin, &secs)) {
    return kConsumed | (begin ? setPhase(CallPhase::Active, nullptr, -1, now) : endCall(CallEndReason::None));
  }
  if (parseMissedCall(line, num, sizeof num)) {
    // The call is already over; make sure the screen does not keep ringing.
    return kConsumed | (call_.phase == CallPhase::Incoming ? endCall(CallEndReason::Missed) : 0);
  }
  if (strcmp(line, "NO CARRIER") == 0) return kConsumed | endCall(CallEndReason::NoCarrier);
  if (strcmp(line, "BUSY") == 0) {
    pendingEnd_ = CallEndReason::Busy;
    return kConsumed | endCall(CallEndReason::Busy);
  }
  if (strcmp(line, "NO ANSWER") == 0) return kConsumed | endCall(CallEndReason::NoAnswer);
  if (strcmp(line, "NO DIALTONE") == 0) return kConsumed | endCall(CallEndReason::NoNetwork);
  return 0;
}

uint8_t CallTracker::dialStarted(const char* number, uint32_t now) { return setPhase(CallPhase::Dialing, number, 0, now); }

uint8_t CallTracker::dialResult(AtFinal f, uint32_t) {
  switch (f) {
    case AtFinal::Ok: return 0;
    case AtFinal::Busy: return endCall(CallEndReason::Busy);
    case AtFinal::NoDialtone: return endCall(CallEndReason::NoNetwork);
    case AtFinal::NoCarrier:
    case AtFinal::NoAnswer: return endCall(CallEndReason::NoAnswer);
    default: return endCall(CallEndReason::Failed);
  }
}

uint8_t CallTracker::answered(uint32_t now) { return setPhase(CallPhase::Active, nullptr, -1, now); }

uint8_t CallTracker::answerFailed(AtFinal f, uint32_t) {
  return f == AtFinal::NoCarrier ? endCall(CallEndReason::None) : 0;
}

uint8_t CallTracker::hungUp(uint32_t) {
  if (call_.phase == CallPhase::Idle) return 0;
  if (call_.phase == CallPhase::Incoming) userRejected_ = true;
  return endCall(CallEndReason::Normal);
}

uint8_t CallTracker::setMuted(bool on) {
  if (call_.muted == on) return 0;
  call_.muted = on;
  return kChanged;
}

uint8_t CallTracker::pollResult(int calls, uint32_t now) {
  if (call_.phase == CallPhase::Idle) return 0;
  if (calls == 0 && now - call_.setupMs > 4000) {
    if (++noCallPolls_ >= 2) return endCall(CallEndReason::None);
  }
  if (call_.phase == CallPhase::Incoming && calls == 0 && now - lastRingMs_ > 12000) return endCall(CallEndReason::None);
  return 0;
}

uint8_t CallTracker::endAll(CallEndReason reason) { return endCall(reason); }
