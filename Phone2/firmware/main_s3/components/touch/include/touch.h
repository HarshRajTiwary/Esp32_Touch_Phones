#pragma once
// XPT2046 resistive touch: raw sampling, debouncing and calibration.
#include <stdint.h>

struct TouchSample {
  bool down = false;
  int16_t x = 0, y = 0;
};

namespace touch {

void setCalibration(const float c[6]);
// Raw 12-bit reading. Returns false when the stylus is not pressing.
bool readRaw(int16_t* rx, int16_t* ry);
// Calibrated, debounced and smoothed screen position.
TouchSample read();
// Least-squares affine fit from n>=3 (raw, screen) pairs.
bool computeCalibration(const int16_t raw[][2], const int16_t scr[][2], int n, float out[6]);
void mapRaw(const float c[6], int16_t rx, int16_t ry, int16_t* x, int16_t* y);

}  // namespace touch
