#pragma once
// =============================================================================
//  CellularModem - the only modem API the application uses.
//
//  Implementation: SIMCom A7672S (a7672s*.cpp). The driver runs in its own
//  FreeRTOS task which owns the modem UART; the UI talks to it only through
//  the command queue behind these functions and receives results as events
//  (events.h), so the touchscreen never waits for AT I/O.
//
//  A second backend (e.g. the SIM800L driver of the older TouchPhone firmware)
//  would implement this same header; the application would not change.
// =============================================================================
#include <stdint.h>

#include "phone_types.h"

namespace modem {

struct Config {
  uint8_t speakerGain = 4;      // AT+COUTGAIN 0..7
  uint8_t micGain = 4;          // AT+CMICGAIN 0..7
  NetMode netMode = NetMode::Auto;
  bool dataEnabled = true;      // 4G data (PPP) on the second CMUX channel
  char apn[APN_MAX] = "";
  char apnUser[APN_USER_MAX] = "";
  char apnPass[APN_PASS_MAX] = "";
};

void begin(const Config& cfg);

// ---- calls ----
void dial(const char* number);
void answer();
void hangup();
void dtmf(char key);
void mute(bool on);
void setAudioProfile(AudioRoute r);   // handset / loudspeaker tuning inside the modem
void setSpeakerGain(int gain0to7);
void setMicGain(int gain0to7);

// ---- messages / supplementary services / SIM ----
bool sendSms(uint32_t localId, const char* number, const char* text);
void ussd(const char* code);
void ussdCancel();
void enterPin(const char* pin);
void readSimContacts();
void setClock(uint32_t localEpoch);
void setNetworkMode(NetMode m);

// ---- cellular data ----
void setDataEnabled(bool on);
void setApn(const char* apn, const char* user, const char* pass);
DataStatus data();                    // thread-safe snapshot
void* pppNetif();                     // esp_netif_t* of the PPP link (nullptr before begin)

// ---- maintenance ----
// Sends an arbitrary AT command and logs the reply. Serial console only - the
// normal UI never exposes raw AT access.
void rawAt(const char* cmd);
void restart();                       // full power cycle of the module
void refresh();                       // poll signal/operator/battery now
void powerOff();                      // AT+CPOF (before switching the phone off)

// ---- state ----
NetStatus status();                   // thread-safe snapshot
CallInfo call();                      // thread-safe snapshot
PhoneState phoneState();
const char* phoneStateText(PhoneState s);
bool registered();
int signalBars(const NetStatus& s);   // 0..4 (LTE uses RSRP when known)
const char* ratText(const NetStatus& s);  // "4G", "2G" or ""
int tzQuarters();                     // network time zone in quarter hours (IST = +22)

}  // namespace modem
