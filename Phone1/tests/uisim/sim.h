#pragma once
// Shared state for the PC UI simulator.
#include "../../firmware/TouchPhone/src/core/phone_types.h"
#include "../../firmware/TouchPhone/src/hw/display.h"

struct SimWorld {
  NetStatus net;
  CallInfo call;
  PhoneState phoneState = PhoneState::Ready;
  bool charging = false;
  bool btConnected = true;
  bool audioRunning = true;
  uint32_t now = 0;          // local epoch seconds shown on screen
  uint32_t clockMs = 100000; // value returned by millis()
  uint32_t callSeconds = 0;
  char ussdText[USSD_TEXT_MAX] = "";
  bool ussdExpectsReply = false;
  bool verbose = false;
};

extern SimWorld g_sim;

namespace display {
LGFX_Sprite* canvas();
}
