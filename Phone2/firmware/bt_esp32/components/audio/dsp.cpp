#include "dsp.h"

#include <math.h>
#include <string.h>

namespace dsp {

static const float kPi = 3.14159265358979f;

void Decimator::design(int inRate, int factor, float cutoffHz) {
  factor_ = factor < 1 ? 1 : factor;
  const float fc = cutoffHz / (float)inRate;  // normalised cutoff (cycles/sample)
  const float m = (float)(kTaps - 1);
  float sum = 0;
  for (int n = 0; n < kTaps; ++n) {
    float k = (float)n - m / 2.0f;
    float sinc = (fabsf(k) < 1e-6f) ? 2.0f * fc : sinf(2.0f * kPi * fc * k) / (kPi * k);
    // Blackman window
    float w = 0.42f - 0.5f * cosf(2.0f * kPi * n / m) + 0.08f * cosf(4.0f * kPi * n / m);
    h_[n] = sinc * w;
    sum += h_[n];
  }
  for (int n = 0; n < kTaps; ++n) h_[n] /= sum;  // unity gain at DC
  reset();
}

void Decimator::reset() {
  memset(hist_, 0, sizeof hist_);
  pos_ = 0;
  phase_ = 0;
}

bool Decimator::push(float x, float* out) {
  hist_[pos_] = x;
  hist_[pos_ + kTaps] = x;
  pos_ = (pos_ + 1) % kTaps;
  if (++phase_ < factor_) return false;
  phase_ = 0;
  // hist_[pos_ .. pos_+kTaps-1] holds the last kTaps samples, oldest first.
  const float* s = hist_ + pos_;
  float acc = 0;
  for (int n = 0; n < kTaps; ++n) acc += h_[n] * s[kTaps - 1 - n];
  *out = acc;
  return true;
}

bool Decimator::push(int16_t x, int16_t* out) {
  float y;
  if (!push((float)x, &y)) return false;
  y = softClip(y);
  *out = (int16_t)lrintf(y);
  return true;
}

float softClip(float x) {
  const float knee = 24000.0f, room = 32767.0f - knee;
  float a = fabsf(x);
  if (a <= knee) return x;
  float y = knee + room * tanhf((a - knee) / room);
  return x < 0 ? -y : y;
}

float gainFromStep(int step) {
  if (step < 0) step = 0;
  if (step > 15) step = 15;
  return powf(10.0f, (float)((step - 8) * 2) / 20.0f);
}

float levelDb(const int16_t* x, int n) {
  if (n <= 0) return -96.0f;
  double acc = 0;
  for (int k = 0; k < n; ++k) acc += (double)x[k] * x[k];
  double rms = sqrt(acc / n);
  if (rms < 1.0) return -96.0f;
  return (float)(20.0 * log10(rms / 32768.0));
}

}  // namespace dsp
