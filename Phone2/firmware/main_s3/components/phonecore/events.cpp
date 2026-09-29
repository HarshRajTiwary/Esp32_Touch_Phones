#include "events.h"

#include <stdlib.h>

#include <vector>

#include "phone_types.h"
#include "plog.h"

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

static QueueHandle_t s_queue;

namespace events {

void init() {
  if (!s_queue) s_queue = xQueueCreate(64, sizeof(AppEvent));
}

bool post(Ev type, int32_t a, int32_t b, void* ptr) {
  AppEvent e{type, a, b, ptr};
  if (!s_queue || xQueueSend(s_queue, &e, pdMS_TO_TICKS(50)) != pdTRUE) {
    LOG("EVT", "queue full, dropped event %d", (int)type);
    release(e);
    return false;
  }
  return true;
}

bool receive(AppEvent* e, uint32_t timeoutMs) {
  return s_queue && xQueueReceive(s_queue, e, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

}  // namespace events
#endif  // ESP_PLATFORM (the host tests / UI simulator provide their own queue)

namespace events {

void release(AppEvent& e) {
  if (!e.ptr) return;
  switch (e.type) {
    case Ev::SmsReceived: delete static_cast<SmsRecord*>(e.ptr); break;
    case Ev::CallEnded: delete static_cast<CallInfo*>(e.ptr); break;
    case Ev::SimContacts: delete static_cast<std::vector<Contact>*>(e.ptr); break;
    default: free(e.ptr); break;  // char* payloads (strdup)
  }
  e.ptr = nullptr;
}

}  // namespace events

const char* modemStateName(ModemState s) {
  switch (s) {
    case ModemState::Off: return "off";
    case ModemState::PoweringOn: return "powering on";
    case ModemState::Booting: return "booting";
    case ModemState::AtTest: return "AT test";
    case ModemState::CmuxStart: return "starting CMUX";
    case ModemState::SimCheck: return "checking SIM";
    case ModemState::NetworkSearch: return "searching network";
    case ModemState::Ready: return "ready";
    case ModemState::NotResponding: return "not responding";
    case ModemState::Recovering: return "recovering";
  }
  return "?";
}

const char* dataStateName(DataState s) {
  switch (s) {
    case DataState::Off: return "off";
    case DataState::WaitNetwork: return "waiting for network";
    case DataState::Dialing: return "dialing";
    case DataState::Negotiating: return "connecting";
    case DataState::Connected: return "connected";
    case DataState::Backoff: return "retrying";
    case DataState::Unavailable: return "unavailable";
  }
  return "?";
}
