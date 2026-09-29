#include "cmux.h"

#include <string.h>

namespace cmux {

uint8_t crc8(const uint8_t* p, size_t n, uint8_t crc) {
  for (size_t i = 0; i < n; ++i) {
    crc ^= p[i];
    for (int b = 0; b < 8; ++b) crc = (crc & 1) ? (uint8_t)((crc >> 1) ^ 0xE0) : (uint8_t)(crc >> 1);
  }
  return crc;
}

uint8_t fcs(const uint8_t* p, size_t n) { return (uint8_t)(0xFF - crc8(p, n)); }

size_t build(uint8_t dlci, uint8_t ctrl, const uint8_t* data, size_t len, uint8_t* out, size_t cap) {
  if (len > kMaxInfo || cap < len + kOverhead) return 0;
  size_t w = 0;
  out[w++] = kFlag;
  size_t hdrStart = w;
  out[w++] = (uint8_t)((dlci << 2) | 0x02 | 0x01);  // C/R = 1, EA = 1
  out[w++] = ctrl;
  if (len <= 127) {
    out[w++] = (uint8_t)((len << 1) | 1);
  } else {
    out[w++] = (uint8_t)((len & 0x7F) << 1);
    out[w++] = (uint8_t)(len >> 7);
  }
  size_t hdrLen = w - hdrStart;
  if (len) memcpy(out + w, data, len);
  w += len;
  out[w++] = fcs(out + hdrStart, hdrLen);
  out[w++] = kFlag;
  return w;
}

size_t buildControl(uint8_t type, bool command, const uint8_t* values, size_t n, uint8_t* out, size_t cap) {
  if (n > 127 || cap < n + 2) return 0;
  out[0] = command ? (uint8_t)(type | 0x02) : (uint8_t)(type & ~0x02);
  out[1] = (uint8_t)((n << 1) | 1);
  if (n) memcpy(out + 2, values, n);
  return n + 2;
}

void Decoder::feed(const uint8_t* p, size_t n, FrameFn onFrame, GarbageFn onGarbage, void* ctx) {
  for (size_t i = 0; i < n; ++i) {
    uint8_t b = p[i];
    switch (st_) {
      case St::WaitFlag:
        if (b == kFlag) {
          st_ = St::Addr;
        } else {
          garbage_++;
          if (onGarbage) onGarbage(ctx, b);
        }
        break;
      case St::Addr:
        if (b == kFlag) break;  // repeated flags between frames
        if (!(b & 1) || (b >> 2) > maxDlci_) {  // EA must be 1, DLCI must be one we use
          st_ = St::WaitFlag;
          garbage_++;
          if (onGarbage) onGarbage(ctx, b);
          break;
        }
        hdr_[0] = b;
        hdrLen_ = 1;
        st_ = St::Ctrl;
        break;
      case St::Ctrl: {
        uint8_t c = (uint8_t)(b & ~kPF);
        if (c != SABM && c != UA && c != DM && c != DISC && c != UIH && c != UI) {
          st_ = St::WaitFlag;  // not a frame after all
          garbage_ += 2;
          if (onGarbage) {
            onGarbage(ctx, hdr_[0]);
            onGarbage(ctx, b);
          }
          break;
        }
        hdr_[hdrLen_++] = b;
        st_ = St::Len1;
        break;
      }
      case St::Len1:
        hdr_[hdrLen_++] = b;
        len_ = b >> 1;
        if (b & 1) {
          got_ = 0;
          st_ = len_ ? St::Data : St::Fcs;
          if (len_ > kMaxInfo) st_ = St::WaitFlag;
        } else {
          st_ = St::Len2;
        }
        break;
      case St::Len2:
        hdr_[hdrLen_++] = b;
        len_ = (uint16_t)(len_ | ((uint16_t)b << 7));
        got_ = 0;
        if (len_ > kMaxInfo) {
          st_ = St::WaitFlag;
          break;
        }
        st_ = len_ ? St::Data : St::Fcs;
        break;
      case St::Data:
        buf_[got_++] = b;
        if (got_ >= len_) st_ = St::Fcs;
        break;
      case St::Fcs:
        fcs_ = b;
        st_ = St::End;
        break;
      case St::End: {
        if (b != kFlag) {
          st_ = St::WaitFlag;
          break;
        }
        // Valid when the CRC over header + received FCS equals the magic 0xCF.
        uint8_t c = crc8(hdr_, hdrLen_);
        c = crc8(&fcs_, 1, c);
        if (c != 0xCF) {
          fcsErrors_++;
        } else {
          frames_++;
          Frame f;
          f.dlci = hdr_[0] >> 2;
          f.cr = (hdr_[0] & 0x02) != 0;
          f.pf = (hdr_[1] & kPF) != 0;
          f.ctrl = (uint8_t)(hdr_[1] & ~kPF);
          f.data = buf_;
          f.len = len_;
          if (onFrame) onFrame(ctx, f);
        }
        st_ = St::Addr;  // the closing flag can also open the next frame
        break;
      }
    }
  }
}

}  // namespace cmux
