#pragma once
// Battery estimation from the supply voltage.
//
// The A7672S reports its VBAT (AT+CBC). When the module is fed straight from
// the phone's single Li-ion/LiPo cell (docs/POWER.md), that is the battery
// voltage. Pure C++ (host-tested).
#include <stdint.h>

namespace battery {

// Open-circuit Li-ion curve, 3.30 V = 0 %, 4.18 V = 100 %. Returns -1 for 0 mV
// (unknown) and clamps outside the range.
int percentFromMv(int mv);

// Smooths the modem readings (they dip during transmit bursts) and reports a
// percentage that only moves in steps once the reading is stable.
class Estimator {
 public:
  int update(int mv);          // returns the displayed percent (-1 unknown)
  int percent() const { return shown_; }
  bool likelyCharging() const { return charging_; }

 private:
  float avg_ = 0;
  int shown_ = -1;
  int samples_ = 0;
  float lastAvg_ = 0;
  int rising_ = 0;
  bool charging_ = false;
};

}  // namespace battery
