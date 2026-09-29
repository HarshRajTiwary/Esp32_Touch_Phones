#pragma once
// 3GPP TS 27.010 (GSM 07.10) multiplexer, basic option - frame codec.
//
// The A7672S supports only "AT+CMUX=0" (basic mode, no transparency / escaping):
//
//   F9 | address | control | length (1-2 bytes) | information | FCS | F9
//
//   address = DLCI << 2 | C/R << 1 | EA(1)
//   length  = len << 1 | EA(1)            (len <= 127)
//           = len << 1 (EA 0), len >> 7   (two bytes)
//   FCS     = 0xFF - CRC-8 (x^8+x^2+x+1, reflected, init 0xFF) over address,
//             control and length (UIH frames do not cover the information field)
//
// Pure C++ - covered by tests/host.
#include <stddef.h>
#include <stdint.h>

namespace cmux {

constexpr uint8_t kFlag = 0xF9;
constexpr size_t kMaxInfo = 1600;            // >= AT+CMUX N1 (1500 on the A7672S)
constexpr size_t kOverhead = 7;              // 2 flags + addr + ctrl + 2 len + fcs

enum Ctrl : uint8_t {
  SABM = 0x2F,   // set asynchronous balanced mode (open a channel)
  UA = 0x63,     // unnumbered acknowledgement
  DM = 0x0F,     // disconnected mode (channel refused / closed)
  DISC = 0x43,   // disconnect
  UIH = 0xEF,    // unnumbered information with header check (all data)
  UI = 0x03,
};
constexpr uint8_t kPF = 0x10;                // poll/final bit in the control byte

// Control-channel (DLCI 0) message types, command form (C/R = 1, EA = 1).
enum MuxMsg : uint8_t {
  MSG_PN = 0x83,     // parameter negotiation
  MSG_PSC = 0x43,    // power saving control
  MSG_CLD = 0xC3,    // multiplexer close down
  MSG_TEST = 0x23,
  MSG_FCON = 0xA3,   // flow control on (peer may send again)
  MSG_FCOFF = 0x63,  // flow control off (stop sending)
  MSG_MSC = 0xE3,    // modem status command (V.24 signals)
  MSG_NSC = 0x13,    // non-supported command response
  MSG_RPN = 0x93,
  MSG_RLS = 0x53,
  MSG_SNC = 0xD3,
};
// V.24 signal byte used in MSC: EA | RTC | RTR | DV
constexpr uint8_t kV24Ready = 0x01 | 0x04 | 0x08 | 0x80;

uint8_t crc8(const uint8_t* p, size_t n, uint8_t crc = 0xFF);
uint8_t fcs(const uint8_t* p, size_t n);   // 0xFF - crc8

// Writes one complete frame (both flags included). Returns bytes written, 0 if
// `cap` is too small or `len` exceeds kMaxInfo. C/R is always set (we are the
// initiator / "TE" side).
size_t build(uint8_t dlci, uint8_t ctrl, const uint8_t* data, size_t len, uint8_t* out, size_t cap);

// Control-channel message: type (command or response form) + value bytes.
// Returns the information-field length written to `out`.
size_t buildControl(uint8_t type, bool command, const uint8_t* values, size_t n, uint8_t* out, size_t cap);

struct Frame {
  uint8_t dlci;
  uint8_t ctrl;      // without the P/F bit
  bool pf;
  bool cr;
  const uint8_t* data;
  uint16_t len;
};

// Streaming decoder. Bytes outside frames (e.g. the modem's plain-text boot
// banner after an unexpected restart) are reported as "garbage".
class Decoder {
 public:
  typedef void (*FrameFn)(void* ctx, const Frame& f);
  typedef void (*GarbageFn)(void* ctx, uint8_t b);

  void feed(const uint8_t* p, size_t n, FrameFn onFrame, GarbageFn onGarbage, void* ctx);
  void reset() { st_ = St::WaitFlag; }
  // Highest DLCI that may appear. Anything else right after a flag is treated as
  // plain text, so e.g. a restart banner ("RDY") following the last frame is
  // reported as garbage instead of being swallowed as a bogus frame header.
  void setMaxDlci(uint8_t d) { maxDlci_ = d; }

  uint32_t frames() const { return frames_; }
  uint32_t fcsErrors() const { return fcsErrors_; }
  uint32_t garbage() const { return garbage_; }

 private:
  enum class St : uint8_t { WaitFlag, Addr, Ctrl, Len1, Len2, Data, Fcs, End };
  St st_ = St::WaitFlag;
  uint8_t hdr_[4];
  uint8_t hdrLen_ = 0;
  uint16_t len_ = 0, got_ = 0;
  uint8_t fcs_ = 0;
  uint8_t buf_[kMaxInfo];
  uint32_t frames_ = 0, fcsErrors_ = 0, garbage_ = 0;
  uint8_t maxDlci_ = 63;
};

}  // namespace cmux
