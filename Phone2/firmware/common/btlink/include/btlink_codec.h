#pragma once
// Frame encoder / streaming decoder for the BT link (see btlink_proto.h).
// Pure C++ - covered by tests/host.
#include <stddef.h>
#include <stdint.h>

#include "btlink_proto.h"
#include "cobs.h"

namespace btl {

struct Frame {
  Msg type;
  uint8_t seq;
  uint8_t len;
  uint8_t payload[kMaxPayload];
};

// Raw (pre-COBS) size: type + seq + payload + crc
constexpr size_t kMaxRaw = 2 + kMaxPayload + 2;
// Encoded size including the trailing 0x00 delimiter.
constexpr size_t kMaxWire = cobsMaxEncoded(kMaxRaw) + 1;

// Encodes a frame. Returns the number of bytes written to `out` (incl. the 0x00),
// or 0 if the payload is too long.
size_t encode(Msg type, uint8_t seq, const void* payload, size_t len, uint8_t* out, size_t cap);

// Byte-at-a-time decoder that resynchronises on every 0x00.
class Decoder {
 public:
  // Feeds one received byte. Returns true when `out` holds a complete, valid frame.
  bool push(uint8_t b, Frame* out);
  uint32_t crcErrors() const { return crcErrors_; }
  uint32_t framingErrors() const { return framingErrors_; }
  void reset() { n_ = 0; overflow_ = false; }

 private:
  uint8_t buf_[kMaxWire];
  size_t n_ = 0;
  bool overflow_ = false;
  uint32_t crcErrors_ = 0, framingErrors_ = 0;
};

}  // namespace btl
