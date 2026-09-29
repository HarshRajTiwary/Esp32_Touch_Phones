#pragma once
// Voice call state machine, fed with the modem's lines and the driver's actions.
// Pure C++ (no RTOS): the A7672S driver wraps it with locking and events, and
// tests/host drives it with scripted URC sequences.
//
//   IDLE -> DIALING -> ALERTING -> ACTIVE -> ended     outgoing
//   IDLE -> INCOMING ------------> ACTIVE -> ended     incoming
//
// Every method returns a bit set of kChanged / kEnded. After kEnded, ended()
// holds the snapshot of the finished call (with its CallEndReason).
#include <stdint.h>

#include "at_parse.h"
#include "phone_types.h"

class CallTracker {
 public:
  static constexpr uint8_t kChanged = 1;
  static constexpr uint8_t kEnded = 2;
  static constexpr uint8_t kConsumed = 0x80;   // onLine(): the line was call-related

  // A line printed by the modem (URC or command output).
  uint8_t onLine(const char* line, uint32_t nowMs);
  // Driver actions
  uint8_t dialStarted(const char* number, uint32_t nowMs);
  uint8_t dialResult(AtFinal f, uint32_t nowMs);          // result of ATD...;
  uint8_t answered(uint32_t nowMs);                        // ATA returned OK
  uint8_t answerFailed(AtFinal f, uint32_t nowMs);
  uint8_t hungUp(uint32_t nowMs);                          // user pressed end / decline
  uint8_t setMuted(bool on);
  // Result of an AT+CLCC safety poll: number of +CLCC lines seen.
  uint8_t pollResult(int calls, uint32_t nowMs);
  uint8_t endAll(CallEndReason reason);                    // modem lost

  const CallInfo& info() const { return call_; }
  const CallInfo& ended() const { return ended_; }
  // Non-empty after onLine() when something deserves a notice ("Call waiting: ...")
  const char* notice() const { return notice_; }

 private:
  uint8_t setPhase(CallPhase ph, const char* number, int incoming, uint32_t now);
  uint8_t endCall(CallEndReason reason);
  uint8_t onClcc(const ClccInfo& c, uint32_t now);

  CallInfo call_;
  CallInfo ended_;
  int trackedIdx_ = 0;
  bool userRejected_ = false;
  CallEndReason pendingEnd_ = CallEndReason::None;
  uint32_t lastRingMs_ = 0;
  int noCallPolls_ = 0;
  char notice_[64] = "";
};
