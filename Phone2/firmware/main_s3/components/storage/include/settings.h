#pragma once
// User settings, stored as one versioned blob in NVS. Only written when the user
// changes something (never periodically).
#include <stdint.h>

#include "app_limits.h"

// Where call audio should go.
enum class AudioPref : uint8_t {
  Auto = 0,      // earbuds when connected, otherwise the handset
  Earbuds = 1,   // always try the earbuds
  Handset = 2,   // never use the earbuds for calls
};

struct Settings {
  uint16_t version = 1;
  // ---- display / input ----
  uint8_t brightness = 200;        // 10..255
  uint16_t screenTimeoutS = 30;    // 0 = never
  uint8_t touchCalValid = 0;
  float cal[6] = {0.0625f, 0.0f, -12.0f, 0.0f, 0.085f, -16.0f};  // x = a*rx + b*ry + c; y = d*rx + e*ry + f
  uint8_t autoCaps = 1;
  // ---- sound ----
  uint8_t audioPref = 0;           // AudioPref
  uint8_t modemOutGain = 4;        // A7672S AT+COUTGAIN 0..7 (handset + earbud bridge level)
  uint8_t modemMicGain = 4;        // A7672S AT+CMICGAIN 0..7
  uint8_t btSpkStep = 8;           // earbud speaker digital gain step (8 = 0 dB)
  uint8_t btMicStep = 8;           // earbud microphone digital gain step
  uint8_t earbudVolume = 12;       // HFP volume 0..15
  uint8_t ringerOn = 1;            // buzzer ringtone
  uint8_t keyClicks = 1;
  // ---- Bluetooth ----
  uint8_t btEnabled = 1;
  // ---- mobile network ----
  uint8_t netMode = 2;             // NetMode (2 = auto 4G/2G)
  uint8_t dataEnabled = 1;
  char apn[APN_MAX] = "airtelgprs.com";
  char apnUser[APN_USER_MAX] = "";
  char apnPass[APN_PASS_MAX] = "";
  // ---- hotspot ----
  uint8_t hotspotAuto = 0;         // start at boot
  uint8_t hotspotMaxClients = 4;
  char hotspotSsid[WIFI_SSID_MAX] = "";   // generated at first boot ("Phone2-XXXX")
  char hotspotPass[WIFI_PASS_MAX] = "";   // random, generated at first boot
};

namespace settings {
Settings& get();
void load();                // also fills generated defaults (hotspot name/password)
void save();
void factoryReset();        // clears NVS settings (not contacts/messages)
}  // namespace settings
