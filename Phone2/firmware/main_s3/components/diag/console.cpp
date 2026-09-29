// USB serial debug console (UART0, 115200 baud). Type "help".
//
// Raw AT access exists only here (never in the normal UI).
#include "console.h"

#include <driver/uart.h>
#include <driver/uart_vfs.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "at_parse.h"
#include "bt_client.h"
#include "call_log.h"
#include "contacts.h"
#include "hotspot.h"
#include "messages.h"
#include "modem.h"
#include "nettools.h"
#include "phone_app.h"
#include "plog.h"
#include "screens.h"
#include "text_util.h"
#include "touch.h"

namespace console {

static char s_buf[200];
static size_t s_len = 0;
static bool s_ok = false;
static uint32_t s_touchUntil = 0, s_nextTouchPrint = 0;

static void help() {
  printf(
      "\n--- Phone2 console ---\n"
      "status              modem / network / data / battery / Bluetooth summary\n"
      "at <command>        raw AT command to the A7672S, e.g.  at AT+CPSI?\n"
      "dial <number>       place a call          hangup / answer\n"
      "sms <number> <text> send an SMS\n"
      "ussd <code>         e.g.  ussd *123#\n"
      "data on|off         mobile data (PPP)     apn <name> [user] [password]\n"
      "netmode auto|4g|2g  preferred network\n"
      "hotspot on|off      Wi-Fi hotspot         nettest   (HTTP connectivity check)\n"
      "touch               print raw touch readings for 10 s (wiring check)\n"
      "cal                 open the touch calibration screen\n"
      "bt scan | bt list | bt connect <n> | bt disconnect | bt forget | bt reboot\n"
      "tone on|off         1 kHz test tone to the earbuds\n"
      "heap                memory usage     store   (contacts / messages / calls)\n"
      "restart-modem       power-cycle the A7672S\n"
      "reboot              restart the phone\n");
}

static void status() {
  NetStatus st = modem::status();
  static const char* const kReg[] = {"unknown", "not registered", "searching", "home", "roaming", "denied"};
  static const char* const kSim[] = {"checking", "ready", "not inserted", "PIN required", "PUK required", "error"};
  LOG("CON", "phone state : %s (modem %s)", modem::phoneStateText(modem::phoneState()), modemStateName(st.modem));
  if (st.failReason[0]) LOG("CON", "last error  : %s", st.failReason);
  LOG("CON", "modem       : %s %s, IMEI %s, %s @ %lu baud", st.model, st.fw, st.imei, st.cmux ? "CMUX" : "single channel",
      (unsigned long)st.baud);
  LOG("CON", "operator    : %s %s (%s)", st.op[0] ? st.op : "-", modem::ratText(st), kReg[(int)st.reg]);
  LOG("CON", "signal      : CSQ %d, RSRP %d dBm, RSRQ %d, band %s, %d bars", st.csq, st.rsrp, st.rsrq, st.band,
      modem::signalBars(st));
  LOG("CON", "SIM         : %s", kSim[(int)st.sim]);
  DataStatus d = modem::data();
  LOG("CON", "data        : %s %s dns %s, rx %lu tx %lu bytes %s", dataStateName(d.state), d.ip, d.dns,
      (unsigned long)d.rxBytes, (unsigned long)d.txBytes, d.lastError);
  hotspot::Status hs = hotspot::status();
  LOG("CON", "hotspot     : %s, %d client(s)%s", hs.on ? hs.ssid : "off", hs.clients, hs.sharing ? ", sharing 4G" : "");
  LOG("CON", "battery     : %d%% (%u mV modem supply), module %d C", app::battery(), (unsigned)st.battMv, st.modemTempC);
  LOG("CON", "bluetooth   : helper %s, %s (%s)", bt::helperOnline() ? "online" : "OFFLINE", bt::stateText(), bt::peerName());
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
    if (!text) {
      LOG("CON", "usage: sms <number> <text>");
      return;
    }
    *text++ = 0;
    uint32_t id = app::sendSms(arg, text);
    LOG("CON", "queued SMS id %u", (unsigned)id);
  } else if (!strcmp(line, "ussd")) app::ussd(arg);
  else if (!strcmp(line, "data")) app::setDataEnabled(!strcmp(arg, "on"));
  else if (!strcmp(line, "apn")) {
    char apn[APN_MAX] = "", user[APN_USER_MAX] = "", pass[APN_PASS_MAX] = "";
    sscanf(arg, "%63s %31s %31s", apn, user, pass);
    if (!apn[0] || strchr(apn, '"')) {
      LOG("CON", "usage: apn <name> [user] [password]");
      return;
    }
    app::setApn(apn, user, pass);
  } else if (!strcmp(line, "netmode")) {
    NetMode m = !strcmp(arg, "4g") ? NetMode::LteOnly : !strcmp(arg, "2g") ? NetMode::GsmOnly : NetMode::Auto;
    app::setNetworkMode(m);
  } else if (!strcmp(line, "hotspot")) app::setHotspot(!strcmp(arg, "on"));
  else if (!strcmp(line, "nettest")) nettools::startConnectivityTest();
  else if (!strcmp(line, "touch")) {
    s_touchUntil = plogMillis() + 10000;
    LOG("CON", "touch the screen - raw values for 10 s");
  } else if (!strcmp(line, "cal")) nav::push(ScreenId::Calibrate);
  else if (!strcmp(line, "bt")) {
    if (!strcmp(arg, "scan")) bt::startScan();
    else if (!strcmp(arg, "disconnect")) bt::disconnect();
    else if (!strcmp(arg, "forget")) bt::forget();
    else if (!strcmp(arg, "reboot")) bt::rebootHelper();
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
  } else if (!strcmp(line, "store")) {
    LOG("CON", "%d contacts, %d messages (%d unread), %d calls", contacts::count(), messages::count(),
        messages::unreadCount(), calllog::count());
  } else if (!strcmp(line, "restart-modem")) modem::restart();
  else if (!strcmp(line, "reboot")) app::restart();
  else LOG("CON", "unknown command '%s' - type help", line);
}

void begin() {
  s_ok = uart_driver_install(UART_NUM_0, 512, 0, 0, nullptr, 0) == ESP_OK;
  if (s_ok) uart_vfs_dev_use_driver(UART_NUM_0);
  LOG("CON", "debug console ready - type 'help'");
}

void poll() {
  if (!s_ok) return;
  uint8_t c;
  while (uart_read_bytes(UART_NUM_0, &c, 1, 0) == 1) {
    if (c == '\r') continue;
    if (c == '\n') {
      s_buf[s_len] = 0;
      run(s_buf);
      s_len = 0;
    } else if ((c == 8 || c == 127) && s_len) {
      --s_len;
    } else if (c >= 32 && s_len < sizeof s_buf - 1) {
      s_buf[s_len++] = (char)c;
    }
  }
  uint32_t now = plogMillis();
  if (s_touchUntil && (int32_t)(now - s_touchUntil) < 0 && (int32_t)(now - s_nextTouchPrint) >= 0) {
    s_nextTouchPrint = now + 200;
    int16_t rx, ry;
    if (touch::readRaw(&rx, &ry)) {
      const TouchSample& t = app::touchSample();
      LOG("TOUCH", "raw x=%d y=%d -> screen x=%d y=%d", rx, ry, t.x, t.y);
    }
  } else if (s_touchUntil && (int32_t)(now - s_touchUntil) >= 0) {
    s_touchUntil = 0;
    LOG("TOUCH", "done");
  }
}

}  // namespace console
