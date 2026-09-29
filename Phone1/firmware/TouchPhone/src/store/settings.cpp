#include "settings.h"

#include <Preferences.h>

#include "../core/log.h"

namespace settings {

static Settings s_settings;

Settings& get() { return s_settings; }

void load() {
  Preferences p;
  if (!p.begin("phone", true)) {
    LOG("CFG", "no saved settings - using defaults");
    return;
  }
  Settings tmp;
  size_t n = p.getBytesLength("cfg");
  if (n == sizeof(Settings) && p.getBytes("cfg", &tmp, sizeof tmp) == sizeof tmp && tmp.version == Settings().version) {
    s_settings = tmp;
    LOG("CFG", "settings loaded");
  } else if (n) {
    LOG("CFG", "settings format changed - using defaults");
  }
  p.end();
}

void save() {
  Preferences p;
  if (!p.begin("phone", false)) return;
  p.putBytes("cfg", &s_settings, sizeof s_settings);
  p.end();
}

void factoryReset() {
  Preferences p;
  if (p.begin("phone", false)) {
    p.clear();
    p.end();
  }
  s_settings = Settings();
}

}  // namespace settings
