#pragma once
// Bluetooth Classic: device discovery/pairing (GAP) and the Hands-Free
// Audio Gateway (HFP AG) role that lets TWS earbuds act as the phone's
// speaker and microphone, with their buttons answering / ending calls.
#include <stdint.h>

#include "../core/phone_types.h"

namespace bt {

enum class State : uint8_t { Off, Idle, Connecting, Connected };

struct Device {
  uint8_t bda[6];
  char name[32];
  int8_t rssi;
  uint32_t cod;
  bool audio;          // headset / earbuds class of device
  bool nameAsked;      // remote-name request already sent
};

bool begin(bool enabled);
void setEnabled(bool on);
bool enabled();
State state();
bool connected();                 // HFP service level connection is up
bool audioOpen();                 // (e)SCO voice link is up
uint32_t audioRate();
const char* stateText();
const char* peerName();           // connected (or remembered) earbuds
bool peerAddress(uint8_t out[6]);

void startScan();
void stopScan();
bool scanning();
int devices(Device* out, int max);

void connect(const uint8_t bda[6], const char* name);
void connectSaved();
void disconnect();
void forget();
void setDiscoverable(bool on);

void openAudio();
void setSpeakerVolume(int vol0to15);   // earbud speaker level via AT+VGS
int speakerVolume();
void noteSpeakerVolume(int vol0to15);  // the earbuds changed it themselves
void closeAudio();

// Keeps the earbuds' call indicators in sync with the modem.
void onPhoneState(const CallInfo& now, const CallInfo& before);
void onNetStatus(const NetStatus& st, int battPct);
void service(uint32_t nowMs);     // auto-reconnect (call from the UI task)

void formatAddress(const uint8_t bda[6], char* out);  // "AA:BB:CC:DD:EE:FF" (18 bytes)

}  // namespace bt
