#pragma once
// When should the Bluetooth helper open / close the SCO voice link to the
// earbuds? Pure function, used by hfp_ag.cpp and covered by tests/host.
//
//  * open  : earbuds connected AND (call audio routed to them during dialing /
//            ringing-back / an active call, OR an audio test is running);
//            retried every kScoRetryMs until the earbuds accept.
//  * close : call ended, or the phone moved the audio back to the handset, or
//            the earbuds opened a link on their own with no call (closed after
//            kScoOrphanMs so they do not drain their battery).
#include <stdint.h>

#include "btlink_proto.h"

namespace btl {

constexpr uint32_t kScoRetryMs = 3000;
constexpr uint32_t kScoOrphanMs = 5000;

struct ScoInputs {
  bool connected;          // HFP service level connection up
  bool audioOpen;          // SCO link up
  bool pending;            // we asked to open and wait for the result
  bool closing;            // we asked to close and wait for the result
  bool route;              // the phone wants call audio in the earbuds
  CallPhaseW phase;
  AudioTestMode test;
  uint32_t nowMs;
  uint32_t lastTryMs;      // last open attempt
  uint32_t audioOpenedMs;  // when the current SCO link opened
};

enum class ScoAction : uint8_t { None, Open, Close };

ScoAction scoDecide(const ScoInputs& in);

}  // namespace btl
