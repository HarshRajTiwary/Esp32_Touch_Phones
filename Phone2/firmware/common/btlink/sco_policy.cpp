#include "sco_policy.h"

namespace btl {

ScoAction scoDecide(const ScoInputs& in) {
  if (!in.connected) return ScoAction::None;
  bool callAudio = in.phase == CallPhaseW::Active || in.phase == CallPhaseW::Dialing || in.phase == CallPhaseW::Alerting;
  bool want = (in.route && callAudio) || in.test != AudioTestMode::None;
  if (want) {
    if (!in.audioOpen && !in.pending && in.nowMs - in.lastTryMs >= kScoRetryMs) return ScoAction::Open;
    return ScoAction::None;
  }
  if (!in.audioOpen || in.closing) return ScoAction::None;
  // Not wanted but open:
  if (!in.route && callAudio) return ScoAction::Close;                    // moved back to the handset
  if (in.phase == CallPhaseW::Idle && in.route) return ScoAction::Close;  // our call ended
  if (in.phase == CallPhaseW::Idle && in.nowMs - in.audioOpenedMs > kScoOrphanMs) return ScoAction::Close;  // opened by the earbuds
  return ScoAction::None;
}

}  // namespace btl
