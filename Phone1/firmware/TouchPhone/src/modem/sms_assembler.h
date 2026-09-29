#pragma once
// Re-assembles concatenated ("long") SMS from their parts. Pure C++, host-tested.
//
// Parts stay on the SIM until the whole message is complete, so nothing is
// lost if the phone reboots half way: the next AT+CMGL sweep feeds them in again.
#include <stdint.h>

#include <deque>
#include <string>
#include <vector>

#include "sms_pdu.h"

class SmsAssembler {
 public:
  struct Done {
    SmsPdu msg;                   // merged text, first part's timestamp
    std::vector<int> simIndices;  // delete these from the SIM once stored
    bool partial = false;         // some parts never arrived
  };

  static constexpr int kMaxParts = 10;

  void add(const SmsPdu& pdu, int simIndex, uint32_t nowMs);
  bool pop(Done* out);
  void expire(uint32_t nowMs, uint32_t maxAgeMs);
  bool knowsIndex(int simIndex) const;
  size_t pendingGroups() const { return groups_.size(); }

 private:
  struct Group {
    std::string number;
    uint16_t ref = 0;
    uint8_t total = 0;
    uint32_t firstMs = 0;
    SmsPdu first;                         // header data (number, ts, dcs)
    std::vector<std::string> parts;
    std::vector<bool> have;
    std::vector<int> indices;
  };
  void finish(Group& g, bool partial);

  std::vector<Group> groups_;
  std::deque<Done> done_;
};
