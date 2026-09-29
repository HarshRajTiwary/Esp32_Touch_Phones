#pragma once
#include <stdint.h>
#include <stddef.h>
class Stream {
 public:
  virtual ~Stream() {}
  virtual size_t write(const uint8_t*, size_t n) { return n; }
  virtual size_t write(uint8_t) { return 1; }
  virtual int read() { return -1; }
  virtual int available() { return 0; }
  size_t readBytes(uint8_t*, size_t) { return 0; }
  size_t readBytes(char*, size_t) { return 0; }
  virtual void flush() {}
};
