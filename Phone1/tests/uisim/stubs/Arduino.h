// Minimal Arduino API stub so LovyanGFX's generic backend and the UI code
// compile on a PC for the screenshot simulator (tests/uisim).
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#ifdef __cplusplus
#include <string>
#endif

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09
#define PROGMEM
#define IRAM_ATTR

unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
inline bool psramFound() { return true; }
inline void yield() {}
#define memcpy_P memcpy
#define memcmp_P memcmp
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_dword(p) (*(const uint32_t*)(p))
#define pgm_read_ptr(p) (*(void* const*)(p))

#ifdef __cplusplus
class String : public std::string {
 public:
  String() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
};

struct EspClass {
  void restart() { exit(0); }
  uint32_t getFreeHeap() { return 150000; }
};
extern EspClass ESP;
#endif
