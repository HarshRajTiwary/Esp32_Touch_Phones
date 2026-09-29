// COBS + CRC framing, the S3 <-> ESP32 BT link codec and the SCO voice-link policy.
#include <string.h>

#include <vector>

#include "btlink_codec.h"
#include "cobs.h"
#include "phone_types.h"
#include "sco_policy.h"
#include "test.h"

// The link sends phone_types.h CallPhase values as btl::CallPhaseW.
static_assert((int)CallPhase::Idle == (int)btl::CallPhaseW::Idle, "phase numbering");
static_assert((int)CallPhase::Dialing == (int)btl::CallPhaseW::Dialing, "phase numbering");
static_assert((int)CallPhase::Alerting == (int)btl::CallPhaseW::Alerting, "phase numbering");
static_assert((int)CallPhase::Incoming == (int)btl::CallPhaseW::Incoming, "phase numbering");
static_assert((int)CallPhase::Active == (int)btl::CallPhaseW::Active, "phase numbering");

static bool cobsRoundTrip(const std::vector<uint8_t>& in) {
  std::vector<uint8_t> enc(cobsMaxEncoded(in.size()) + 1), dec(in.size() + 8);
  size_t n = cobsEncode(in.data(), in.size(), enc.data(), enc.size());
  if (!n) return false;
  for (size_t k = 0; k < n; ++k)
    if (enc[k] == 0) return false;  // COBS output never contains the delimiter
  size_t m = cobsDecode(enc.data(), n, dec.data(), dec.size());
  return m == in.size() && memcmp(dec.data(), in.data(), m) == 0;
}

static void testCobsCrc() {
  CHECK_EQ(crc16((const uint8_t*)"123456789", 9), 0x29B1);  // CRC-16/CCITT-FALSE check value
  CHECK(cobsRoundTrip({}));
  CHECK(cobsRoundTrip({0}));
  CHECK(cobsRoundTrip({0, 0, 0}));
  CHECK(cobsRoundTrip({1, 2, 0, 3}));
  std::vector<uint8_t> v254(254, 0x11), v255(255, 0x22), v600;
  CHECK(cobsRoundTrip(v254));
  CHECK(cobsRoundTrip(v255));
  for (int k = 0; k < 600; ++k) v600.push_back((uint8_t)(k % 7 == 0 ? 0 : k));
  CHECK(cobsRoundTrip(v600));
  // Known encoding: 11 22 00 33 -> 03 11 22 02 33
  const uint8_t in[] = {0x11, 0x22, 0x00, 0x33}, want[] = {0x03, 0x11, 0x22, 0x02, 0x33};
  uint8_t out[8];
  CHECK_EQ(cobsEncode(in, 4, out, sizeof out), 5);
  CHECK(memcmp(out, want, 5) == 0);
  // Malformed input (embedded zero / truncated group) is rejected
  const uint8_t bad1[] = {0x03, 0x11, 0x00}, bad2[] = {0x05, 0x11};
  uint8_t dec[8];
  CHECK_EQ(cobsDecode(bad1, 3, dec, sizeof dec), 0);
  CHECK_EQ(cobsDecode(bad2, 2, dec, sizeof dec), 0);
}

static void testLinkCodec() {
  uint8_t wire[btl::kMaxWire * 3];
  btl::CallStateMsg c = {};
  c.phase = (uint8_t)btl::CallPhaseW::Incoming;
  c.incoming = 1;
  strcpy(c.number, "+919876543210");
  size_t n = btl::encode(btl::Msg::CallState, 7, &c, sizeof c, wire, sizeof wire);
  CHECK(n > sizeof c);
  CHECK_EQ(wire[n - 1], 0);  // delimiter

  btl::Decoder d;
  btl::Frame f;
  int got = 0;
  for (size_t k = 0; k < n; ++k)
    if (d.push(wire[k], &f)) ++got;
  CHECK_EQ(got, 1);
  CHECK(f.type == btl::Msg::CallState);
  CHECK_EQ(f.seq, 7);
  CHECK_EQ(f.len, sizeof c);
  CHECK(memcmp(f.payload, &c, sizeof c) == 0);

  // Noise before a frame, two frames back to back, empty payload
  std::vector<uint8_t> stream = {0x55, 0x13, 0x00, 0x00};  // junk ends at a delimiter
  size_t n2 = btl::encode(btl::Msg::ScanDone, 8, nullptr, 0, wire, sizeof wire);
  stream.insert(stream.end(), wire, wire + n2);
  btl::StatusMsg st = {};
  st.state = (uint8_t)btl::BtState::Connected;
  strcpy(st.peerName, "Boat Airdopes 141");
  size_t n3 = btl::encode(btl::Msg::Status, 9, &st, sizeof st, wire, sizeof wire);
  stream.insert(stream.end(), wire, wire + n3);
  std::vector<btl::Msg> types;
  for (uint8_t b : stream)
    if (d.push(b, &f)) types.push_back(f.type);
  CHECK_EQ(types.size(), 2);
  if (types.size() == 2) {
    CHECK(types[0] == btl::Msg::ScanDone);
    CHECK(types[1] == btl::Msg::Status);
    btl::StatusMsg back;
    memcpy(&back, f.payload, sizeof back);
    CHECK_STR(back.peerName, "Boat Airdopes 141");
  }
  CHECK(d.framingErrors() >= 1);  // the junk counted as a bad frame

  // A flipped bit is caught by the CRC
  n = btl::encode(btl::Msg::Volume, 1, "\x0c", 1, wire, sizeof wire);
  wire[2] ^= 0x04;
  uint32_t crcBefore = d.crcErrors(), fr = d.framingErrors();
  got = 0;
  for (size_t k = 0; k < n; ++k)
    if (d.push(wire[k], &f)) ++got;
  CHECK_EQ(got, 0);
  CHECK(d.crcErrors() + d.framingErrors() > crcBefore + fr);

  // Too long / every message fits
  uint8_t big[btl::kMaxPayload + 1] = {};
  CHECK_EQ(btl::encode(btl::Msg::Log, 0, big, sizeof big, wire, sizeof wire), 0);
  CHECK(btl::encode(btl::Msg::Log, 0, big, btl::kMaxPayload, wire, sizeof wire) > 0);
  CHECK(sizeof(btl::StatusMsg) <= (size_t)btl::kMaxPayload);
  CHECK(sizeof(btl::DeviceMsg) <= (size_t)btl::kMaxPayload);
  CHECK(sizeof(btl::NetStateMsg) <= (size_t)btl::kMaxPayload);
  CHECK(sizeof(btl::ButtonMsg) <= (size_t)btl::kMaxPayload);
  CHECK_STR(btl::msgName(btl::Msg::AudioRoute), "AudioRoute");
}

static void testScoPolicy() {
  btl::ScoInputs in = {};
  in.connected = true;
  in.route = true;
  in.phase = btl::CallPhaseW::Active;
  in.test = btl::AudioTestMode::None;
  in.nowMs = 10000;
  in.lastTryMs = 0;
  // Active call routed to the earbuds: open
  CHECK(btl::scoDecide(in) == btl::ScoAction::Open);
  in.pending = true;
  CHECK(btl::scoDecide(in) == btl::ScoAction::None);  // already asked
  in.pending = false;
  in.lastTryMs = 9000;
  CHECK(btl::scoDecide(in) == btl::ScoAction::None);  // retry only every 3 s
  in.lastTryMs = 0;
  // Ringing: the earbuds ring by themselves, no SCO yet
  in.phase = btl::CallPhaseW::Incoming;
  CHECK(btl::scoDecide(in) == btl::ScoAction::None);
  // Ring-back tone while dialing is heard in the earbuds
  in.phase = btl::CallPhaseW::Alerting;
  CHECK(btl::scoDecide(in) == btl::ScoAction::Open);
  // Not connected: nothing to do
  in.connected = false;
  CHECK(btl::scoDecide(in) == btl::ScoAction::None);
  in.connected = true;
  // Audio test without a call
  in.phase = btl::CallPhaseW::Idle;
  in.route = false;
  in.test = btl::AudioTestMode::Tone;
  CHECK(btl::scoDecide(in) == btl::ScoAction::Open);
  in.test = btl::AudioTestMode::None;
  // Open link, call moved back to the handset: close
  in.audioOpen = true;
  in.phase = btl::CallPhaseW::Active;
  in.route = false;
  CHECK(btl::scoDecide(in) == btl::ScoAction::Close);
  in.closing = true;
  CHECK(btl::scoDecide(in) == btl::ScoAction::None);  // already closing
  in.closing = false;
  // Our call ended: close
  in.phase = btl::CallPhaseW::Idle;
  in.route = true;
  CHECK(btl::scoDecide(in) == btl::ScoAction::Close);
  // Earbuds opened a link with no call: allow 5 s, then close
  in.route = false;
  in.audioOpenedMs = 8000;
  CHECK(btl::scoDecide(in) == btl::ScoAction::None);
  in.nowMs = 13001;
  CHECK(btl::scoDecide(in) == btl::ScoAction::Close);
  // Wanted and already open: nothing
  in.route = true;
  in.phase = btl::CallPhaseW::Active;
  CHECK(btl::scoDecide(in) == btl::ScoAction::None);
}

void runLinkTests() {
  testCobsCrc();
  testLinkCodec();
  testScoPolicy();
}
