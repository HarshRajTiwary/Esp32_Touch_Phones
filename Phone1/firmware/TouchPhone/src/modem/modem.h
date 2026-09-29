#pragma once
// SIM800L driver. Runs in its own FreeRTOS task which owns the UART.
// The UI talks to it only through the command queue below and receives
// results as events (core/events.h), so the touchscreen never blocks on AT I/O.
#include <stdint.h>

#include "../core/phone_types.h"

enum class Mc : uint8_t {
  Dial, Answer, HangUp, Dtmf, Mute,
  SetVolume, SetRinger, SetMicGain,
  SendSms, Ussd, UssdCancel, EnterPin,
  ReadSimContacts, SetClock, RawAt, Restart, Refresh, SpeakerTest,
};

struct OutgoingSms {
  uint32_t id;
  char number[NUMBER_MAX];
  char text[SMS_TEXT_MAX];
};

struct ModemCmd {
  Mc type;
  int32_t a;
  char str[48];
  void* ptr;  // OutgoingSms* for SendSms (owned by the modem task afterwards)
};

namespace modem {

void begin(uint8_t callVolume, uint8_t ringerVolume, uint8_t micGain);

void dial(const char* number);
void answer();
void hangup();
void dtmf(char key);
void mute(bool on);
void setVolume(int vol0to100);
void setRinger(int vol0to100);
void setMicGain(int gain0to15);
bool sendSms(uint32_t localId, const char* number, const char* text);
void ussd(const char* code);
void ussdCancel();
void enterPin(const char* pin);
void readSimContacts();
void setClock(uint32_t localEpoch);
void rawAt(const char* cmd);
void restart();
void refresh();
void speakerTest();   // plays a 2 s tone on the SIM800L speaker (SPK+/SPK-)

NetStatus status();       // thread-safe snapshot
CallInfo call();          // thread-safe snapshot
PhoneState phoneState();
const char* phoneStateText(PhoneState s);
bool registered();

}  // namespace modem
