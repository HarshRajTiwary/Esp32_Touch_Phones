#pragma once
// Small audio DSP blocks for the A7672S analog audio <-> Bluetooth HFP bridge. Pure C++.
#include <stdint.h>

namespace dsp {

// One-pole DC blocker (removes the ADC bias / modem output offset).
class DcBlocker {
 public:
  float process(float x) {
    float y = x - x1_ + r_ * y1_;
    x1_ = x;
    y1_ = y;
    return y;
  }
  void reset() { x1_ = y1_ = 0; }

 private:
  float x1_ = 0, y1_ = 0;
  float r_ = 0.995f;
};

// Windowed-sinc FIR low-pass followed by integer decimation.
class Decimator {
 public:
  static constexpr int kTaps = 48;
  void design(int inRate, int factor, float cutoffHz);
  void reset();
  // Feeds one input sample; returns true (and writes *out) every `factor` samples.
  bool push(float x, float* out);
  bool push(int16_t x, int16_t* out);

 private:
  float h_[kTaps] = {};
  float hist_[kTaps * 2] = {};  // doubled buffer avoids modulo in the dot product
  int pos_ = 0;
  int factor_ = 1;
  int phase_ = 0;
};

// Linear-interpolating 2x upsampler (8 kHz CVSD -> 16 kHz DAC stream).
class Upsampler2x {
 public:
  void process(int16_t x, int16_t out[2]) {
    out[0] = (int16_t)(((int32_t)prev_ + x) / 2);
    out[1] = x;
    prev_ = x;
  }
  void reset() { prev_ = 0; }

 private:
  int16_t prev_ = 0;
};

// Smooth limiter into the int16 range (linear below ~-2.7 dBFS).
float softClip(float x);
// Volume step 0..15 -> linear gain. Step 8 = unity, 2 dB per step.
float gainFromStep(int step);
// RMS level of a block in dBFS (-96 for silence).
float levelDb(const int16_t* x, int n);

}  // namespace dsp
