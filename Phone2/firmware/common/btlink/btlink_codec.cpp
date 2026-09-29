#include "btlink_codec.h"

#include <string.h>

namespace btl {

size_t encode(Msg type, uint8_t seq, const void* payload, size_t len, uint8_t* out, size_t cap) {
  if (len > (size_t)kMaxPayload) return 0;
  uint8_t raw[kMaxRaw];
  raw[0] = (uint8_t)type;
  raw[1] = seq;
  if (len) memcpy(raw + 2, payload, len);
  uint16_t crc = crc16(raw, 2 + len);
  raw[2 + len] = (uint8_t)(crc >> 8);
  raw[3 + len] = (uint8_t)crc;
  size_t n = cobsEncode(raw, 4 + len, out, cap);
  if (n == 0 || n + 1 > cap) return 0;
  out[n] = 0;
  return n + 1;
}

bool Decoder::push(uint8_t b, Frame* out) {
  if (b != 0) {
    if (n_ < sizeof buf_) buf_[n_++] = b;
    else overflow_ = true;
    return false;
  }
  // End of frame
  size_t n = n_;
  bool overflow = overflow_;
  n_ = 0;
  overflow_ = false;
  if (n == 0) return false;  // idle delimiter
  if (overflow) {
    framingErrors_++;
    return false;
  }
  uint8_t raw[kMaxRaw];
  size_t len = cobsDecode(buf_, n, raw, sizeof raw);
  if (len < 4) {
    framingErrors_++;
    return false;
  }
  uint16_t want = (uint16_t)((raw[len - 2] << 8) | raw[len - 1]);
  if (crc16(raw, len - 2) != want) {
    crcErrors_++;
    return false;
  }
  out->type = (Msg)raw[0];
  out->seq = raw[1];
  out->len = (uint8_t)(len - 4);
  memcpy(out->payload, raw + 2, out->len);
  return true;
}

const char* msgName(Msg m) {
  switch (m) {
    case Msg::Hello: return "Hello";
    case Msg::SetEnabled: return "SetEnabled";
    case Msg::ScanStart: return "ScanStart";
    case Msg::ScanStop: return "ScanStop";
    case Msg::Connect: return "Connect";
    case Msg::Disconnect: return "Disconnect";
    case Msg::Forget: return "Forget";
    case Msg::Discoverable: return "Discoverable";
    case Msg::CallState: return "CallState";
    case Msg::NetState: return "NetState";
    case Msg::Volume: return "Volume";
    case Msg::Gains: return "Gains";
    case Msg::AudioTest: return "AudioTest";
    case Msg::GetStatus: return "GetStatus";
    case Msg::Reboot: return "Reboot";
    case Msg::ListBonded: return "ListBonded";
    case Msg::AudioRoute: return "AudioRoute";
    case Msg::HelloAck: return "HelloAck";
    case Msg::Status: return "Status";
    case Msg::ScanResult: return "ScanResult";
    case Msg::ScanDone: return "ScanDone";
    case Msg::Bonded: return "Bonded";
    case Msg::Button: return "Button";
    case Msg::AudioStats: return "AudioStats";
    case Msg::Log: return "Log";
  }
  return "?";
}

}  // namespace btl
