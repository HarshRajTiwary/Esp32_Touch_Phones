#include "console.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "../bt/bt_manager.h"
#include "../core/log.h"
#include "../core/text_util.h"
#include "../hw/display.h"
#include "../hw/power.h"
#include "../hw/touch.h"
#include "../modem/at_parse.h"
#include "../modem/modem.h"
#include "../store/call_log.h"
#include "../store/contacts.h"
#include "../store/messages.h"
#include "../store/settings.h"
#include "../ui/screens.h"
#include "phone_app.h"

namespace console {

static char s_buf[200];
static size_t s_len = 0;
static uint32_t s_touchUntil = 0, s_nextTouchPrint = 0;

static void help() {
  Serial.println(
      "\n--- TouchPhone console ---\n"
      "status              modem / network / battery / Bluetooth summary\n"
      "at <command>        send a raw AT command to the SIM800L, e.g.  at AT+CSQ\n"
      "dial <number>       place a call          hangup / answer\n"
      "sms <number> <text> send an SMS\n"
      "ussd <code>         e.g.  ussd *123#\n"
      "touch               print raw touch readings for 10 s (wiring check)\n"
      "cal                 open the touch calibration screen\n"
      "rot <0-7>           screen orientation (5 = header on top, 7 = header at bottom), saved\n"
      "bt scan | bt list | bt connect <n> | bt disconnect | bt forget\n"
      "tone on|off         1 kHz test tone to the earbuds\n"
      "heap                memory usage\n"
      "data                number of contacts / messages / calls stored\n"
      "restart-modem       power-cycle the SIM800L\n"
      "reboot              restart the ESP32\n");
}

static void status() {
  NetStatus st = modem::status();
  static const char* const kReg[] = {"unknown", "not registered", "searching", "home", "roaming", "denied"};
  LOG("CON", "phone state : %s", modem::phoneStateText(modem::phoneState()));
  LOG("CON", "operator    : %s  (%s)", st.op[0] ? st.op : "-", kReg[(int)st.reg]);
  LOG("CON", "signal      : CSQ %d = %d dBm, %d bars", st.csq, csqToDbm(st.csq), csqToBars(st.csq));
  static const char* const kSim[] = {"checking", "ready", "not inserted", "PIN required", "PUK required", "error"};
  LOG("CON", "SIM         : %s   IMEI %s", kSim[(int)st.sim], st.imei);
  LOG("CON", "battery     : modem %d%% %u mV, PMU %s level %d%%, charging %d", st.battPct, (unsigned)st.battMv,
      power::typeName(), power::pmuLevel(), power::charging());
  LOG("CON", "bluetooth   : %s (%s)", bt::stateText(), bt::peerName());
  CallInfo c = modem::call();
  LOG("CON", "call        : phase %d number %s", (int)c.phase, logNum(c.number));
}

static void run(char* line) {
  txt::trim(line);
  if (!*line) return;
  char* arg = strchr(line, ' ');
  if (arg) {
    *arg++ = 0;
    while (*arg == ' ') ++arg;
  } else {
    arg = line + strlen(line);
  }
  if (!strcmp(line, "help") || !strcmp(line, "?")) help();
  else if (!strcmp(line, "status")) status();
  else if (!strcmp(line, "at")) modem::rawAt(*arg ? arg : "AT");
  else if (!strcmp(line, "dial")) app::dial(arg);
  else if (!strcmp(line, "hangup")) app::hangup();
  else if (!strcmp(line, "answer")) app::answer();
  else if (!strcmp(line, "sms")) {
    char* text = strchr(arg, ' ');
    if (!text) { LOG("CON", "usage: sms <number> <text>"); return; }
    *text++ = 0;
    uint32_t id = app::sendSms(arg, text);
    LOG("CON", "queued SMS id %u", (unsigned)id);
  } else if (!strcmp(line, "ussd")) app::ussd(arg);
  else if (!strcmp(line, "touch")) {
    s_touchUntil = millis() + 10000;
    LOG("CON", "touch the screen - raw values for 10 s");
  } else if (!strcmp(line, "cal")) nav::push(ScreenId::Calibrate);
  else if (!strcmp(line, "rot")) {
    if (!*arg) {
      LOG("CON", "rotation is %u - usage: rot <0..7>", display::rotation());
      return;
    }
    uint8_t r = (uint8_t)(atoi(arg) & 7);
    display::setRotation(r);
    settings::get().tftRotation = r;
    settings::save();
  }
  else if (!strcmp(line, "bt")) {
    if (!strcmp(arg, "scan")) bt::startScan();
    else if (!strcmp(arg, "disconnect")) bt::disconnect();
    else if (!strcmp(arg, "forget")) bt::forget();
    else if (!strncmp(arg, "connect", 7)) {
      bt::Device d[16];
      int n = bt::devices(d, 16), k = atoi(arg + 7);
      if (k >= 0 && k < n) bt::connect(d[k].bda, d[k].name);
      else bt::connectSaved();
    } else {
      bt::Device d[16];
      int n = bt::devices(d, 16);
      for (int k = 0; k < n; ++k) {
        char a[18];
        bt::formatAddress(d[k].bda, a);
        LOG("CON", "%d: %s %s rssi %d%s", k, a, d[k].name, d[k].rssi, d[k].audio ? " [audio]" : "");
      }
      if (!n) LOG("CON", "no devices - use 'bt scan' first");
    }
  } else if (!strcmp(line, "tone")) app::setAudioTest(!strcmp(arg, "on") ? audio::TestMode::Tone : audio::TestMode::None);
  else if (!strcmp(line, "heap")) {
    LOG("CON", "internal free %u KB (min %u KB, largest %u KB), PSRAM free %u KB",
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
        (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
        (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  } else if (!strcmp(line, "data")) {
    LOG("CON", "%d contacts, %d messages (%d unread), %d calls", contacts::count(), messages::count(),
        messages::unreadCount(), calllog::count());
  } else if (!strcmp(line, "beep")) {
    modem::speakerTest();
  } else if (!strcmp(line, "calls")) {
    static const char* const kType[] = {"outgoing", "incoming", "missed", "declined"};
    for (int i = 0; i < calllog::count(); ++i) {
      const CallLogEntry& c = calllog::at(i);
      LOG("CON", "%d: %-8s %s  %us", i, c.type < 4 ? kType[c.type] : "?", logNum(c.number), (unsigned)c.duration);
    }
    if (!calllog::count()) LOG("CON", "no calls");
  } else if (!strcmp(line, "restart-modem")) modem::restart();
  else if (!strcmp(line, "reboot")) ESP.restart();
  else LOG("CON", "unknown command '%s' - type help", line);
}

void begin() { LOG("CON", "debug console ready - type 'help'"); }

void poll() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      s_buf[s_len] = 0;
      run(s_buf);
      s_len = 0;
    } else if (s_len < sizeof s_buf - 1) {
      s_buf[s_len++] = c;
    }
  }
  uint32_t now = millis();
  if (s_touchUntil && (int32_t)(now - s_touchUntil) < 0 && (int32_t)(now - s_nextTouchPrint) >= 0) {
    s_nextTouchPrint = now + 200;
    int16_t rx, ry;
    if (touch::readRaw(&rx, &ry)) {
      TouchSample t = touch::read();
      LOG("TOUCH", "raw x=%d y=%d -> screen x=%d y=%d", rx, ry, t.x, t.y);
    }
  } else if (s_touchUntil && (int32_t)(now - s_touchUntil) >= 0) {
    s_touchUntil = 0;
    LOG("TOUCH", "done");
  }
}

}  // namespace console
