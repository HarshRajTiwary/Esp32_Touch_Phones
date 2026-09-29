#pragma once
// Application core: owns the main loop, glues modem, Bluetooth, audio,
// storage and UI together, and implements the phone's behaviour
// (call log, notifications, audio routing, screen timeout...).
#include <stdint.h>

#include "../audio/audio_bridge.h"
#include "../core/phone_types.h"
#include "../store/settings.h"

namespace app {

void setup();
void loop();

// ---- status for the UI (cached, cheap to call every frame) ----
const NetStatus& net();
const CallInfo& call();
int battery();                  // percent, -1 unknown
bool charging();
uint32_t nowLocal();            // local epoch seconds, 0 if the clock is not set yet
bool clockValid();
PhoneState phoneState();

// ---- calls ----
void dial(const char* number);  // USSD codes (*123#) are sent as USSD instead
void answer();
void hangup();
void rejectWithSms(const char* text);
void toggleMute();
void dtmf(char key);
bool earbudsInUse();            // call audio currently goes to the earbuds
void setAudioRoute(AudioRoute r);
void changeCallVolume(int delta);  // +1 / -1 on the earbuds or the SIM800L speaker, whichever is in use
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

// ---- misc ----
void importSimContacts();
void setAudioTest(audio::TestMode m);
void applyAudioSettings();
void lockNow();
void wake();
void factoryReset();
void saveSettingsSoon();
uint32_t uptimeS();

}  // namespace app
