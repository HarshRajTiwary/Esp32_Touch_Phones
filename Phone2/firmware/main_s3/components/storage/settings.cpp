#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "plog.h"
#include "text_util.h"

#ifdef ESP_PLATFORM
#include <esp_mac.h>
#include <esp_random.h>
#include <nvs.h>
#endif

namespace settings {

static Settings s_settings;

Settings& get() { return s_settings; }

// Fills the per-device defaults: a hotspot name from the MAC address and a
// random password (never a password written in the source code).
static void fillGenerated() {
#ifdef ESP_PLATFORM
  bool changed = false;
  if (!s_settings.hotspotSsid[0]) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_settings.hotspotSsid, sizeof s_settings.hotspotSsid, "Phone2-%02X%02X", mac[4], mac[5]);
    changed = true;
  }
  if (strlen(s_settings.hotspotPass) < 8) {
    static const char kAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";  // no l/1/o/0 confusion
    for (int i = 0; i < 10; ++i) s_settings.hotspotPass[i] = kAlphabet[esp_random() % (sizeof kAlphabet - 1)];
    s_settings.hotspotPass[10] = 0;
    changed = true;
  }
  if (changed) {
    save();
    LOG("CFG", "hotspot defaults generated (name %s)", s_settings.hotspotSsid);
  }
#endif
}

void load() {
#ifdef ESP_PLATFORM
  nvs_handle_t h;
  if (nvs_open("phone", NVS_READONLY, &h) == ESP_OK) {
    Settings tmp;
    size_t n = sizeof tmp;
    esp_err_t e = nvs_get_blob(h, "cfg", &tmp, &n);
    if (e == ESP_OK && n == sizeof tmp && tmp.version == Settings().version) {
      s_settings = tmp;
      LOG("CFG", "settings loaded");
    } else if (e == ESP_OK || e == ESP_ERR_NVS_INVALID_LENGTH) {
      LOG("CFG", "settings format changed - using defaults");
    }
    nvs_close(h);
  } else {
    LOG("CFG", "no saved settings - using defaults");
  }
#endif
  // Keep strings terminated whatever was stored
  s_settings.apn[sizeof s_settings.apn - 1] = 0;
  s_settings.apnUser[sizeof s_settings.apnUser - 1] = 0;
  s_settings.apnPass[sizeof s_settings.apnPass - 1] = 0;
  s_settings.hotspotSsid[sizeof s_settings.hotspotSsid - 1] = 0;
  s_settings.hotspotPass[sizeof s_settings.hotspotPass - 1] = 0;
  fillGenerated();
}

void save() {
#ifdef ESP_PLATFORM
  nvs_handle_t h;
  if (nvs_open("phone", NVS_READWRITE, &h) != ESP_OK) return;
  // Skip the flash write when nothing changed (wear).
  Settings old;
  size_t n = sizeof old;
  bool same = nvs_get_blob(h, "cfg", &old, &n) == ESP_OK && n == sizeof old &&
              memcmp(&old, &s_settings, sizeof old) == 0;
  if (!same) {
    nvs_set_blob(h, "cfg", &s_settings, sizeof s_settings);
    nvs_commit(h);
  }
  nvs_close(h);
#endif
}

void factoryReset() {
#ifdef ESP_PLATFORM
  nvs_handle_t h;
  if (nvs_open("phone", NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
  }
#endif
  s_settings = Settings();
  fillGenerated();
}

}  // namespace settings
