#include "events.h"

#include <Arduino.h>

#include <vector>

#include "log.h"
#include "phone_types.h"

static QueueHandle_t s_queue;

namespace events {

void init() {
  s_queue = xQueueCreate(48, sizeof(AppEvent));
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
