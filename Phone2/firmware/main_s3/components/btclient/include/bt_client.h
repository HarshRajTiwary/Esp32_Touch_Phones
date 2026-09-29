#pragma once
// Bluetooth, as seen from the ESP32-S3 phone.
//
// The Bluetooth Classic radio (HFP Audio Gateway for TWS earbuds) and the call
// audio bridge live on the ESP32 helper board. This module talks to it over the
// BT link UART (btlink_proto.h) and mirrors its state, so the rest of the phone
// uses a simple local API. All functions are non-blocking.
#include <stdint.h>

#include "app_limits.h"
#include "phone_types.h"

namespace bt {

enum class State : uint8_t { Off, Idle, Connecting, Connected };

struct Device {
  uint8_t bda[6];
  char name[BT_NAME_MAX];
  int8_t rssi;
  uint32_t cod;
  bool audio;          // headset / earbuds class of device
};

bool begin(bool enabled);
bool helperOnline();              // the ESP32 helper answers on the link
const char* helperVersion();

void setEnabled(bool on);
bool enabled();
State state();
bool connected();                 // HFP service level connection is up
bool audioOpen();                 // (e)SCO voice link is up
uint32_t audioRate();
const char* stateText();
const char* peerName();           // connected (or remembered) earbuds
bool peerAddress(uint8_t out[6]);
int peerBattery();                // 0..100 when the earbuds report it, else -1

void startScan();
void stopScan();
bool scanning();
int devices(Device* out, int max);   // last scan, earbuds first
int bonded(Device* out, int max);    // paired devices remembered by the helper

void connect(const uint8_t bda[6], const char* name);
void connectSaved();
void disconnect();
void forget();                        // the current / remembered earbuds
void forgetDevice(const uint8_t bda[6]);
void setDiscoverable(bool on);

// true = call audio to the earbuds (the helper opens the SCO link during calls)
void setRoute(bool earbuds);
bool route();
inline void openAudio() { setRoute(true); }   // older call sites
void setSpeakerVolume(int vol0to15);
int speakerVolume();
void noteSpeakerVolume(int vol0to15);
void setGains(uint8_t spkStep, uint8_t micStep);

// Keeps the earbuds' call indicators in sync with the modem.
void onPhoneState(const CallInfo& now, const CallInfo& before);
void onNetStatus(const NetStatus& st, int battPct);
// Heartbeat / resync / helper watchdog. Call from the UI task (~every 50 ms).
void service(uint32_t nowMs);
void rebootHelper();

void formatAddress(const uint8_t bda[6], char* out);  // "AA:BB:CC:DD:EE:FF" (18 bytes)

}  // namespace bt

// Audio bridge on the helper (levels + test tones), for Settings > Sound.
namespace audio {

enum class TestMode : uint8_t { None = 0, Tone = 1, MicLoopback = 2 };

struct Stats {
  float downDb;          // level going to the earbuds (dBFS)
  float upDb;            // level coming from the earbud mic (dBFS)
  uint32_t dacUnderruns;
  uint32_t hfUnderruns;
  uint32_t overruns;
  bool running;
  uint32_t rate;
};

Stats stats();
bool running();
void setTestMode(TestMode m);
TestMode testMode();
void setGains(uint8_t earbudSpeakerStep, uint8_t earbudMicStep);

}  // namespace audio
