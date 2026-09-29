#include "battery.h"

namespace battery {

int percentFromMv(int mv) {
  if (mv <= 0) return -1;
  // (mV, %) points of a typical LiPo discharge curve at light load
  static const int kPts[][2] = {{3300, 0}, {3500, 5},  {3600, 10}, {3680, 20}, {3740, 30}, {3780, 40},
                                {3820, 50}, {3870, 60}, {3930, 70}, {4000, 80}, {4080, 90}, {4180, 100}};
  const int n = sizeof kPts / sizeof kPts[0];
  if (mv <= kPts[0][0]) return 0;
  if (mv >= kPts[n - 1][0]) return 100;
  for (int i = 1; i < n; ++i) {
    if (mv <= kPts[i][0]) {
      int x0 = kPts[i - 1][0], y0 = kPts[i - 1][1], x1 = kPts[i][0], y1 = kPts[i][1];
      return y0 + (mv - x0) * (y1 - y0) / (x1 - x0);
    }
  }
  return 100;
}

int Estimator::update(int mv) {
  if (mv <= 0) return shown_;
  if (samples_ == 0) avg_ = (float)mv;
  else avg_ += ((float)mv - avg_) * 0.25f;  // readings every ~minute: settles in a few
  samples_++;
  // A steadily rising voltage while idle means a charger is connected.
  if (samples_ > 1) {
    if (avg_ > lastAvg_ + 4) rising_ = rising_ < 5 ? rising_ + 1 : 5;
    else if (avg_ < lastAvg_ - 4) rising_ = rising_ > -5 ? rising_ - 1 : -5;
    charging_ = rising_ >= 3 || avg_ > 4200;
  }
  lastAvg_ = avg_;
  int p = percentFromMv((int)(avg_ + 0.5f));
  // Hysteresis: only move in 5 % steps unless the change is large
  if (shown_ < 0 || p - shown_ >= 5 || shown_ - p >= 5 || p == 0 || p == 100) shown_ = p;
  return shown_;
}

}  // namespace battery
