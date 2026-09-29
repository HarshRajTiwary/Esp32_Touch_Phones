#pragma once
// User settings, stored as one blob in NVS (Preferences).
#include <stdint.h>

enum class AudioRoute : uint8_t { Auto = 0, Earbuds = 1, Wired = 2 };

struct Settings {
  uint16_t version = 5;
  uint8_t brightness = 200;        // 10..255
  uint16_t screenTimeoutS = 30;    // 0 = never
  uint8_t audioRoute = 0;          // AudioRoute
  uint8_t callVolume = 80;         // SIM800L AT+CLVL 0..100
  uint8_t ringerVolume = 80;       // SIM800L AT+CRSL 0..100
  uint8_t modemMicGain = 10;       // SIM800L AT+CMIC 0..15
  uint8_t btSpkStep = 8;           // earbud speaker digital gain step (8 = 0 dB)
  uint8_t btMicStep = 8;           // earbud microphone digital gain step
  uint8_t btEnabled = 1;
  uint8_t hasPeer = 0;
  uint8_t peer[6] = {0};
  char peerName[32] = "";
  uint8_t touchCalValid = 0;
  float cal[6] = {0.0625f, 0.0f, -12.0f, 0.0f, 0.085f, -16.0f};  // x = a*rx + b*ry + c; y = d*rx + e*ry + f
  uint8_t autoCaps = 1;
  uint8_t tftRotation = 0xFF;      // 0..7 set with the console "rot" command, 0xFF = TFT_ROTATION
};

namespace settings {
Settings& get();
void load();
void save();
void factoryReset();  // clears NVS settings (not contacts/messages)
}  // namespace settings
