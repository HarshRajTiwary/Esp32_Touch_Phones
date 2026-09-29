#pragma once
// Bluetooth Classic GAP + HFP Audio Gateway for the TWS earbuds (ESP32 helper).
//
// The ESP32 behaves like a phone towards the earbuds: it reports network/battery
// indicators, rings them with caller ID, reacts to their buttons and opens the
// SCO voice link. The real phone state (call phase, number, signal) comes from
// the ESP32-S3 over the BT link and is mirrored here with setCallState/setNetState.
#include <stdint.h>

#include "btlink_proto.h"

namespace hfp {

struct Callbacks {
  void (*statusChanged)();                                          // anything in StatusMsg changed
  void (*scanResult)(const btl::DeviceMsg& d);
  void (*scanDone)();
  void (*button)(btl::ButtonKind kind, int value, const char* text); // earbud buttons / AT commands
  void (*audioChanged)(bool on, uint32_t sampleRate);               // SCO opened / closed
};

bool begin(const Callbacks& cb);

void setEnabled(bool on);
void startScan(int seconds);
void stopScan();
void connect(const uint8_t bda[6], const char* name);
void disconnect();
void forget(const uint8_t bda[6]);
void setDiscoverable(bool on);

void setCallState(const btl::CallStateMsg& c);
void setNetState(const btl::NetStateMsg& n);
void setRoute(bool earbuds);                  // true = call audio should go to the earbuds
void setAudioTest(btl::AudioTestMode m);      // opens SCO without a call for tone / echo tests
void setSpeakerVolume(int v0to15);

void fillStatus(btl::StatusMsg* s);
int bonded(btl::DeviceMsg* out, int max);
// Reconnect + SCO policy. Call every ~100 ms.
void service(uint32_t nowMs);

}  // namespace hfp
