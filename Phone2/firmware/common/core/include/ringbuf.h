#pragma once
// Lock-free single-producer / single-consumer ring buffer of int16 samples.
// Safe between one task and one ISR (or two tasks on different cores).
#include <stdint.h>

#include <atomic>

template <uint32_t N>  // N must be a power of two
class SampleRing {
  static_assert((N & (N - 1)) == 0, "N must be a power of two");

 public:
  uint32_t available() const {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }
  uint32_t space() const { return N - available(); }

  // Producer side. Returns the number of samples written (drops what does not fit).
  uint32_t write(const int16_t* src, uint32_t n) {
    uint32_t h = head_.load(std::memory_order_relaxed);
    uint32_t free = N - (h - tail_.load(std::memory_order_acquire));
    if (n > free) n = free;
    for (uint32_t k = 0; k < n; ++k) buf_[(h + k) & (N - 1)] = src[k];
    head_.store(h + n, std::memory_order_release);
    return n;
  }
  bool push(int16_t s) { return write(&s, 1) == 1; }

  // Consumer side.
  uint32_t read(int16_t* dst, uint32_t n) {
    uint32_t t = tail_.load(std::memory_order_relaxed);
    uint32_t avail = head_.load(std::memory_order_acquire) - t;
    if (n > avail) n = avail;
    for (uint32_t k = 0; k < n; ++k) dst[k] = buf_[(t + k) & (N - 1)];
    tail_.store(t + n, std::memory_order_release);
    return n;
  }
  bool pop(int16_t* s) { return read(s, 1) == 1; }
  // Consumer-side latency control: throw away the oldest samples.
  void skip(uint32_t n) {
    uint32_t t = tail_.load(std::memory_order_relaxed);
    uint32_t avail = head_.load(std::memory_order_acquire) - t;
    if (n > avail) n = avail;
    tail_.store(t + n, std::memory_order_release);
  }
  // Only call while neither side is active.
  void clear() {
    head_.store(0);
    tail_.store(0);
  }

 private:
  int16_t buf_[N];
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
};
