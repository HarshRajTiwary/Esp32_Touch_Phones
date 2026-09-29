#pragma once
// Consistent Overhead Byte Stuffing + CRC-16 helpers used to frame messages on
// the UART link between the ESP32-S3 and the ESP32 Bluetooth helper.
// A COBS frame never contains 0x00, so 0x00 marks the end of every frame and the
// receiver can always resynchronise after noise. Pure C++ (host-tested).
#include <stddef.h>
#include <stdint.h>

// Worst-case encoded size for n input bytes (without the 0x00 delimiter).
constexpr size_t cobsMaxEncoded(size_t n) { return n + n / 254 + 1; }

// Returns the encoded length, or 0 if `cap` is too small.
size_t cobsEncode(const uint8_t* in, size_t n, uint8_t* out, size_t cap);
// Decodes one frame (without the 0x00 delimiter). Returns the decoded length,
// or 0 on malformed input / insufficient capacity.
size_t cobsDecode(const uint8_t* in, size_t n, uint8_t* out, size_t cap);

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
uint16_t crc16(const uint8_t* data, size_t n, uint16_t crc = 0xFFFF);
