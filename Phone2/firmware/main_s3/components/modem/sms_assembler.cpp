#include "sms_assembler.h"

#include <string.h>

#include "text_util.h"

void SmsAssembler::add(const SmsPdu& pdu, int simIndex, uint32_t nowMs) {
  if (!pdu.concat || pdu.total <= 1 || pdu.total > kMaxParts) {
    Done d;
    d.msg = pdu;
    d.simIndices.push_back(simIndex);
    done_.push_back(d);
    return;
  }
  Group* g = nullptr;
  for (auto& x : groups_)
    if (x.ref == pdu.ref && x.total == pdu.total && x.number == pdu.number) { g = &x; break; }
  if (!g) {
    groups_.emplace_back();
    g = &groups_.back();
    g->number = pdu.number;
    g->ref = pdu.ref;
    g->total = pdu.total;
    g->firstMs = nowMs;
    g->first = pdu;
    g->parts.assign(pdu.total, std::string());
    g->have.assign(pdu.total, false);
  }
  int k = pdu.seq - 1;
  for (int idx : g->indices)
    if (idx == simIndex) return;  // already have this SIM slot
  g->indices.push_back(simIndex);
  if (!g->have[k]) {
    g->have[k] = true;
    g->parts[k] = pdu.text;
    if (pdu.seq == 1) g->first = pdu;  // part 1 carries the canonical header/timestamp
  }
  bool complete = true;
  for (bool h : g->have) complete = complete && h;
  if (complete) {
    finish(*g, false);
    groups_.erase(groups_.begin() + (g - groups_.data()));
  }
}

void SmsAssembler::finish(Group& g, bool partial) {
  Done d;
  d.msg = g.first;
  d.partial = partial;
  d.simIndices = g.indices;
  std::string text;
  for (size_t k = 0; k < g.parts.size(); ++k) {
    if (g.have[k]) text += g.parts[k];
    else text += "...";
  }
  txt::copy(d.msg.text, text.c_str(), sizeof d.msg.text);
  txt::utf8Truncate(d.msg.text, sizeof d.msg.text - 1);
  d.msg.concat = false;
  done_.push_back(d);
}

bool SmsAssembler::pop(Done* out) {
  if (done_.empty()) return false;
  *out = done_.front();
  done_.pop_front();
  return true;
}

void SmsAssembler::expire(uint32_t nowMs, uint32_t maxAgeMs) {
  for (size_t i = 0; i < groups_.size();) {
    if (nowMs - groups_[i].firstMs >= maxAgeMs) {
      finish(groups_[i], true);
      groups_.erase(groups_.begin() + i);
    } else {
      ++i;
    }
  }
}

bool SmsAssembler::knowsIndex(int simIndex) const {
  for (const auto& g : groups_)
    for (int idx : g.indices)
      if (idx == simIndex) return true;
  return false;
}
