#pragma once
#include <stdint.h>
#include <stddef.h>
#define MSBFIRST 1
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
struct SPISettings { SPISettings(uint32_t, uint8_t, uint8_t) {} SPISettings() {} };
class SPIClass {
 public:
  void begin(int = -1, int = -1, int = -1, int = -1) {}
  void end() {}
  void beginTransaction(const SPISettings&) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t) { return 0; }
  uint16_t transfer16(uint16_t) { return 0; }
  uint32_t transfer32(uint32_t) { return 0; }
  void transfer(void*, size_t) {}
  void transferBytes(const uint8_t*, uint8_t*, size_t) {}
  void writeBytes(const uint8_t*, size_t) {}
  void write(uint8_t) {}
  void write16(uint16_t) {}
  void write32(uint32_t) {}
  void setFrequency(uint32_t) {}
  void setDataMode(uint8_t) {}
};
extern SPIClass SPI;
