#pragma once
// =============================================================================
//  "BT link" - the UART protocol between the ESP32-S3 (phone) and the ESP32
//  (Bluetooth Classic HFP helper).
//
//  Wire format (both directions):
//      COBS( type | seq | payload[0..kMaxPayload] | crc16_be ) 0x00
//
//  * type    - btl::Msg
//  * seq     - sender's running counter (used by Ack and to spot lost frames)
//  * payload - one of the packed structs below (little-endian, both ends are
//              little-endian Xtensa cores)
//  * crc16   - CRC-16/CCITT-FALSE over type..payload
//
//  Design rule: the S3 owns the phone state. It re-sends the complete call and
//  network state (CallState / NetState) whenever it changes AND every 2 s, so a
//  lost frame or a helper reboot heals itself without any retransmission logic.
//  The helper answers with Status on every change and every 2 s (heartbeat).
// =============================================================================
#include <stdint.h>

#include "app_limits.h"

namespace btl {

constexpr uint8_t kProtoVersion = 1;
constexpr uint32_t kBaud = 921600;
constexpr int kMaxPayload = 120;
constexpr uint32_t kHeartbeatMs = 2000;
constexpr uint32_t kLinkTimeoutMs = 6500;   // 3 missed heartbeats = helper offline

enum class Msg : uint8_t {
  // ---- S3 -> helper -------------------------------------------------------
  Hello        = 0x01,  // HelloMsg
  SetEnabled   = 0x02,  // u8 on
  ScanStart    = 0x03,  // u8 seconds (0 = default ~12 s)
  ScanStop     = 0x04,
  Connect      = 0x05,  // DeviceRef (pairs if needed, then opens HFP)
  Disconnect   = 0x06,
  Forget       = 0x07,  // DeviceRef (bda only)
  Discoverable = 0x08,  // u8 on
  CallState    = 0x09,  // CallStateMsg
  NetState     = 0x0A,  // NetStateMsg
  Volume       = 0x0B,  // u8 earbud speaker volume 0..15
  Gains        = 0x0C,  // GainsMsg (digital gains of the audio bridge)
  AudioTest    = 0x0D,  // u8 AudioTestMode
  GetStatus    = 0x0E,
  Reboot       = 0x0F,
  ListBonded   = 0x10,
  AudioRoute   = 0x11,  // u8 1 = send call audio to the earbuds, 0 = keep it on the handset

  // ---- helper -> S3 -------------------------------------------------------
  HelloAck     = 0x81,  // HelloMsg
  Status       = 0x82,  // StatusMsg (on change + every kHeartbeatMs)
  ScanResult   = 0x83,  // DeviceMsg
  ScanDone     = 0x84,
  Bonded       = 0x85,  // DeviceMsg (index/count set), count 0 = none
  Button       = 0x86,  // ButtonMsg (earbud buttons / headset commands)
  AudioStats   = 0x87,  // AudioStatsMsg (every 500 ms while the bridge runs)
  Log          = 0x88,  // UTF-8 text (not NUL-terminated), forwarded to the S3 console
};

enum class BtState : uint8_t { Off = 0, Idle = 1, Connecting = 2, Connected = 3 };
enum class CallPhaseW : uint8_t { Idle = 0, Dialing = 1, Alerting = 2, Incoming = 3, Active = 4 };
enum class ButtonKind : uint8_t { Answer = 1, Hangup = 2, Redial = 3, Dial = 4, Dtmf = 5, SpeakerVol = 6, MicVol = 7,
                                  AudioOn = 8, AudioOff = 9, VoiceRecognition = 10 };
enum class AudioTestMode : uint8_t { None = 0, Tone = 1, MicLoopback = 2 };

#pragma pack(push, 1)

struct HelloMsg {
  uint8_t proto;              // kProtoVersion
  uint8_t role;               // 1 = S3 phone, 2 = BT helper
  char fw[16];                // firmware version string
};

struct DeviceRef {
  uint8_t bda[6];
  char name[BT_NAME_MAX];     // may be empty
};

struct DeviceMsg {
  uint8_t bda[6];
  int8_t rssi;                // -127 = unknown
  uint8_t isAudio;            // Class of Device says audio/headset
  uint32_t cod;
  uint8_t index, count;       // for Bonded lists
  char name[BT_NAME_MAX];
};

struct CallStateMsg {
  uint8_t phase;              // CallPhaseW
  uint8_t incoming;
  uint8_t muted;
  char number[NUMBER_MAX];
};

struct NetStateMsg {
  uint8_t service;            // 1 = registered
  uint8_t signal;             // 0..5 (HFP scale)
  uint8_t roaming;
  uint8_t battery;            // 0..5 (HFP scale)
  char op[16];                // operator name for AT+COPS?
  char ownNumber[NUMBER_MAX]; // for AT+CNUM
};

struct GainsMsg {
  uint8_t spkStep;            // 0..15, 8 = unity (earbud side of the bridge)
  uint8_t micStep;
};

struct StatusMsg {
  uint8_t enabled;
  uint8_t state;              // BtState
  uint8_t scanning;
  uint8_t audio;              // SCO link open
  uint16_t audioRate;         // 8000 (CVSD) / 16000 (mSBC), 0 = closed
  uint8_t hasPeer;
  uint8_t peer[6];
  char peerName[BT_NAME_MAX];
  int8_t spkVolume;           // 0..15, -1 = unknown
  int8_t peerBattery;         // 0..100 from HF indicators, -1 = unknown
  uint8_t bridgeRunning;
  uint8_t route;              // last AudioRoute received
  uint32_t uptimeS;
  uint16_t linkRxErrors;      // frames with bad CRC seen by the helper
  uint16_t freeHeapKb;
};

struct ButtonMsg {
  uint8_t kind;               // ButtonKind
  int16_t value;              // volume 0..15 / DTMF char
  char text[NUMBER_MAX];      // number for Dial
};

struct AudioStatsMsg {
  int8_t downDb;              // level sent to the earbuds, dBFS
  int8_t upDb;                // level received from the earbud microphone
  uint16_t rate;
  uint16_t dacUnderruns, hfUnderruns, overruns;
  uint8_t running;
  uint8_t testMode;
};

#pragma pack(pop)

static_assert(sizeof(StatusMsg) <= kMaxPayload, "StatusMsg too big");
static_assert(sizeof(DeviceMsg) <= kMaxPayload, "DeviceMsg too big");
static_assert(sizeof(NetStateMsg) <= kMaxPayload, "NetStateMsg too big");

const char* msgName(Msg m);

}  // namespace btl
