#pragma once
// Event bus: background tasks (modem, data, Bluetooth link, hotspot) -> the
// UI/application task. Nothing else calls into the UI.
//
// Events carry two integers and an optional heap payload whose type depends on
// the event (see the table below). The receiver must call events::release(e).
#include <stdint.h>

enum class Ev : uint8_t {
  // ---- modem task ----
  NetChanged,        // status snapshot changed (signal, operator, SIM, modem state...)
  CallChanged,       // call phase / number changed
  CallEnded,         // a = CallEndReason, ptr = CallInfo* (snapshot at the end)
  SmsReceived,       // ptr = SmsRecord*, a = 1 if partial
  SmsSent,           // a = local id, b = 1 ok / 0 failed, ptr = char* error text (may be null)
  UssdReply,         // a = 1 if the network expects a reply, ptr = char* UTF-8 text
  SimContacts,       // ptr = std::vector<Contact>*
  TimeSynced,        // a = epoch seconds
  ModemNotice,       // ptr = char* human readable notice ("Low battery" etc.)
  // ---- cellular data / hotspot ----
  DataChanged,       // PPP state or IP changed (modem::data())
  HotspotChanged,    // a = number of connected clients
  // ---- Bluetooth helper (over the BT link) ----
  BtHelper,          // a = 1 helper online, 0 offline
  BtChanged,         // a = 1 earbuds connected, 0 link lost, 2 other state change
  BtScanResult,      // discovery list updated
  BtScanDone,
  BtAudio,           // a = 1 voice link open / 0 closed, b = sample rate
  BtAnswer,          // earbud button: answer
  BtHangup,          // earbud button: hang up / reject
  BtRedial,          // earbud button: redial last number
  BtDial,            // ptr = char* number requested by the headset
  BtVolume,          // a = 0 speaker / 1 mic, b = 0..15
  BtAudioToEarbuds,  // the user moved the call audio from the earbuds' own controls; a = 1 earbuds, 0 phone
  // ---- power ----
  BatteryChanged,    // a = percent (-1 unknown), b = millivolts
};

struct AppEvent {
  Ev type;
  int32_t a;
  int32_t b;
  void* ptr;
};

namespace events {
void init();
bool post(Ev type, int32_t a = 0, int32_t b = 0, void* ptr = nullptr);
bool receive(AppEvent* e, uint32_t timeoutMs);
void release(AppEvent& e);  // frees the payload
}  // namespace events
