#include "cobs.h"

size_t cobsEncode(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
  if (cap < cobsMaxEncoded(n)) return 0;
  size_t codePos = 0, w = 1;
  uint8_t code = 1;
  for (size_t i = 0; i < n; ++i) {
    if (in[i] == 0) {
      out[codePos] = code;
      codePos = w++;
      code = 1;
    } else {
      out[w++] = in[i];
      if (++code == 0xFF) {
        out[codePos] = code;
        codePos = w++;
        code = 1;
      }
    }
  }
  out[codePos] = code;
  return w;
}

size_t cobsDecode(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
  size_t r = 0, w = 0;
  while (r < n) {
    uint8_t code = in[r++];
    if (code == 0) return 0;
    for (uint8_t k = 1; k < code; ++k) {
      if (r >= n || w >= cap) return 0;
      uint8_t b = in[r++];
      if (b == 0) return 0;
      out[w++] = b;
    }
    if (code != 0xFF && r < n) {
      if (w >= cap) return 0;
      out[w++] = 0;
    }
  }
  return w;
}

uint16_t crc16(const uint8_t* data, size_t n, uint16_t crc) {
  for (size_t i = 0; i < n; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}
