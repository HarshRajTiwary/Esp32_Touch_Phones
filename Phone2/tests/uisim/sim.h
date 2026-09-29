#pragma once
// Shared state for the PC UI simulator (tests/uisim): the "world" the fake
// modem, Bluetooth helper, hotspot and application stubs report to the screens.
#include "display.h"
#include "phone_types.h"

struct SimWorld {
  NetStatus net;
  CallInfo call;
  DataStatus data;
  PhoneState phoneState = PhoneState::Ready;
  AudioRoute route = AudioRoute::Earbuds;
  int battery = 78;
  bool charging = false;
  bool btOnline = true;
  bool btConnected = true;
  bool audioRunning = false;
  bool hotspotOn = true;
  int hotspotClients = 1;
  uint32_t now = 0;          // local epoch seconds shown on screen
  uint32_t clockMs = 100000; // value returned by plogMillis()
  uint32_t callSeconds = 0;
  char ussdText[USSD_TEXT_MAX] = "";
  bool ussdExpectsReply = false;
  bool verbose = false;
};

extern SimWorld g_sim;

namespace display {
LGFX_Sprite* canvas();
}
