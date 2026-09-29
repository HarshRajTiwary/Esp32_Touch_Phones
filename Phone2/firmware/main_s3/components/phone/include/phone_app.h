#pragma once
// Application core: owns the UI loop, glues the modem, Bluetooth helper, data,
// hotspot, storage and screens together, and implements the phone's behaviour
// (call log, notifications, audio routing, screen timeout, ringtone...).
#include <stdint.h>

#include "bt_client.h"
#include "phone_types.h"
#include "settings.h"
#include "touch.h"

namespace app {

void setup();
void loop();

// ---- status for the UI (cached, cheap to call every frame) ----
const NetStatus& net();
const CallInfo& call();
const DataStatus& data();
int battery();                  // percent, -1 unknown
bool charging();
uint32_t nowLocal();            // local epoch seconds, 0 if the clock is not set yet
bool clockValid();
PhoneState phoneState();
const TouchSample& touchSample();   // latest calibrated touch (diagnostics)

// ---- calls ----
void dial(const char* number);  // USSD codes (*123#) are sent as USSD instead
void answer();
void hangup();
void rejectWithSms(const char* text);
void toggleMute();
void dtmf(char key);
AudioRoute audioRoute();        // where the current call's audio goes
void setAudioRoute(AudioRoute r);   // during a call: Handset / Speaker / Earbuds
bool earbudsInUse();
void setAudioPref(AudioPref p);     // Settings > Sound: Auto / Earbuds / Handset
void changeCallVolume(int delta);   // on the earbuds or the modem, whichever is in use
int callVolumePercent();
uint32_t callSeconds();

// ---- messages ----
uint32_t sendSms(const char* number, const char* text);  // returns message id (0 = failed)
void resendSms(uint32_t id);
extern const char* const kQuickReplies[];
extern const int kQuickReplyCount;

// ---- USSD ----
void ussd(const char* code);
void ussdReply(const char* text);
void ussdCancel();
const char* ussdText();
bool ussdWaiting();
bool ussdExpectsReply();

// ---- mobile data / hotspot ----
void setDataEnabled(bool on);
void setApn(const char* apn, const char* user, const char* pass);
void setNetworkMode(NetMode m);
void setHotspot(bool on);

// ---- misc ----
void importSimContacts();
void setAudioTest(audio::TestMode m);
void applyAudioSettings();
void lockNow();
void wake();
void factoryReset();
void restart();
void saveSettingsSoon();
uint32_t uptimeS();

}  // namespace app
