#pragma once
// Internal interfaces shared by the a7672s_*.cpp files. Not part of the public
// modem API (see include/modem.h).
#include <freertos/FreeRTOS.h>
#include <string.h>

#include <functional>

#include "at_parse.h"
#include "events.h"
#include "modem.h"
#include "phone_types.h"

// Outgoing SMS request (queued to the modem task, owned by it afterwards)
struct OutgoingSms {
  uint32_t id;
  char number[NUMBER_MAX];
  char text[SMS_TEXT_MAX];
};

namespace a7672s {

using LineFn = std::function<void(const char*)>;

// ---- AT engine (a7672s_at.cpp) -------------------------------------------------
void atInit();
// mport receive callback: splits channel bytes into lines / PPP data.
void onPortBytes(int chan, const uint8_t* data, size_t len);
// Sends a command on the AT channel and waits for its final result code. Every
// other line that arrives meanwhile is dispatched as a URC (handleLine), so
// events are never lost while a command runs. `fn` sees intermediate lines.
// Returns AtFinal::None on timeout.
AtFinal exec(const char* cmd, uint32_t timeoutMs = 3000, const LineFn& fn = nullptr, bool callFinals = false,
             int* errCode = nullptr);
bool ok(const char* cmd, uint32_t timeoutMs = 3000);
// Command text safe for the log: PINs and passwords are hidden.
const char* redactCmd(const char* cmd);
bool readLine(char* out, size_t cap, uint32_t waitMs);   // next AT-channel line
void drainUrcs();                                        // dispatch everything pending
bool waitPrompt(uint32_t timeoutMs);                     // SMS "> " prompt
void sendRaw(const void* data, size_t len);              // bytes on the AT channel
void flushLines();
int consecutiveTimeouts();
void resetTimeouts();

// Data channel (CMUX DLCI 2) for the data task.
bool dataReadLine(char* out, size_t cap, uint32_t waitMs);
void dataSend(const char* s);
void dataFlushLines();
typedef void (*PppInputFn)(const uint8_t* data, size_t len);
void setDataMode(bool ppp, PppInputFn input);            // true = bytes go to PPP

// ---- shared state (a7672s.cpp) ------------------------------------------------
extern portMUX_TYPE g_mux;
extern NetStatus g_st;
extern CallInfo g_call;
extern modem::Config g_cfg;

template <class F>
void updateStatus(F f) {
  NetStatus before, after;
  portENTER_CRITICAL(&g_mux);
  before = g_st;
  f(g_st);
  after = g_st;
  portEXIT_CRITICAL(&g_mux);
  if (memcmp(&before, &after, sizeof before) != 0) events::post(Ev::NetChanged);
}
NetStatus statusSnapshot();
CallInfo callSnapshot();
bool isRegistered(const NetStatus& s);
void notice(const char* text);
void setModemState(ModemState m);
void requestReinit(const char* why);
uint32_t nowMs();
bool clockValid();
uint32_t nowLocal();
void setSystemTime(uint32_t epoch, const char* source);
extern int g_tzQuarters;

// Every line the modem prints (URC or not) passes through here.
void handleLine(const char* line);

// ---- calls (a7672s_call.cpp) ------------------------------------------------------
bool callHandleLine(const char* line);
void callPeriodic(uint32_t now);
void callDial(const char* number);
void callAnswer();
void callHangup();
void callDtmf(char key);
void callMute(bool on);
void callEndAll(CallEndReason reason);   // modem lost: close any call on screen

// ---- SMS / USSD / phone book (a7672s_sms.cpp) ------------------------------------
bool smsHandleLine(const char* line);
void smsConfigure();
void smsPeriodic(uint32_t now);
void smsSend(OutgoingSms* o);
void smsReadSimContacts();
void ussdStart(const char* code);
void ussdCancel();
void smsScheduleSweep(uint32_t atMs);

// ---- network / SIM / time (a7672s_network.cpp) ------------------------------------
bool netHandleLine(const char* line);
void netConfigure();
void netPeriodic(uint32_t now);
void netRefreshSoon();
void netSetMode(NetMode m);
void netReadIdentity();

// ---- audio (a7672s_audio.cpp) -------------------------------------------------------
void audioConfigure();
void audioSetProfile(AudioRoute r);
void audioSetSpeakerGain(int g);
void audioSetMicGain(int g);

// ---- data (a7672s_data.cpp) --------------------------------------------------------
void dataBegin();
void dataModemStarted(bool cmux);       // called after every (re)start
void dataModemLost();                   // modem stopped / restarting
void dataSetEnabled(bool on);
void dataSetApn(const char* apn, const char* user, const char* pass);
DataStatus dataSnapshot();

}  // namespace a7672s
