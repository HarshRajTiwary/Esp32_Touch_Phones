#pragma once
// Event bus: background tasks (modem, Bluetooth) -> UI/application task.
//
// Events carry two integers and an optional heap payload whose type depends on
// the event (see table below). The receiver must call events::release(e).
#include <stdint.h>

enum class Ev : uint8_t {
  // ---- modem task ----
  NetChanged,        // status snapshot changed (signal, operator, battery, SIM...)
  CallChanged,       // call phase / number changed
  CallEnded,         // a = CallEndReason, ptr = CallInfo* (snapshot at the end)
  SmsReceived,       // ptr = SmsRecord*, a = 1 if partial
  SmsSent,           // a = local id, b = 1 ok / 0 failed, ptr = char* error text (may be null)
  UssdReply,         // a = 1 if the network expects a reply, ptr = char* UTF-8 text
  SimContacts,       // ptr = std::vector<Contact>*
  TimeSynced,        // a = epoch seconds
  ModemNotice,       // ptr = char* human readable notice ("Low battery" etc.)
  // ---- Bluetooth ----
  BtChanged,         // a = 1 earbuds connected, 0 link lost, 2 other state change
  BtScanResult,      // discovery list updated
  BtScanDone,
  BtAudio,           // a = 1 connected / 0 disconnected, b = sample rate
  BtAnswer,          // earbud button: answer
  BtHangup,          // earbud button: hang up / reject
  BtRedial,          // earbud button: redial last number
  BtDial,            // ptr = char* number requested by the headset
  BtVolume,          // a = 0 speaker / 1 mic, b = 0..15
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
