// USB serial console of the Bluetooth helper (115200 baud).
//
// Lets the earbud side be tested on its own, before the ESP32-S3 phone is
// wired: scan, pair, fake a call so the earbuds ring, open the voice link and
// play a test tone. Type "help".
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_bridge.h"
#include "helper.h"
#include "hfp_ag.h"
#include "plog.h"
#include "text_util.h"

namespace {

btl::DeviceMsg s_found[20];
int s_foundN = 0;

void cmdHelp() {
  printf(
      "Phone2 Bluetooth helper - commands:\n"
      "  status                 Bluetooth + audio state\n"
      "  scan                   search for earbuds (put them in pairing mode first)\n"
      "  list                   devices found by the last scan\n"
      "  pair <n>               pair + connect device n from 'list'\n"
      "  bonded                 paired devices\n"
      "  disc | forget          disconnect / forget the current earbuds\n"
      "  ring <number>          fake an incoming call (earbuds ring, press = answer)\n"
      "  talk | idle            fake an active call / end it\n"
      "  route on|off           send call audio to the earbuds or not\n"
      "  tone | echo | quiet    open the voice link: 1 kHz tone / mic echo / stop test\n"
      "  vol <0-15>             earbud volume\n"
      "  on | off               Bluetooth on / off\n"
      "  reboot\n"
      "The ESP32-S3 overrides fake call states as soon as it is connected.\n");
}

void fakeCall(btl::CallPhaseW ph, const char* number) {
  btl::CallStateMsg c = {};
  c.phase = (uint8_t)ph;
  c.incoming = ph == btl::CallPhaseW::Incoming || ph == btl::CallPhaseW::Active;
  txt::copy(c.number, number ? number : "", sizeof c.number);
  hfp::setCallState(c);
  btl::NetStateMsg n = {};
  n.service = 1;
  n.signal = 4;
  n.battery = 4;
  txt::copy(n.op, "Test", sizeof n.op);
  hfp::setNetState(n);
}

void cmdStatus() {
  btl::StatusMsg s;
  hfp::fillStatus(&s);
  static const char* kState[] = {"off", "idle", "connecting", "connected"};
  printf("bluetooth : %s%s, earbuds %s (%s)\n", s.enabled ? "on" : "off", s.scanning ? ", scanning" : "",
         kState[s.state & 3], s.hasPeer ? s.peerName : "none paired");
  printf("voice link: %s %u Hz, route %s, bridge %s, volume %d, earbud battery %d%%\n", s.audio ? "open" : "closed",
         s.audioRate, s.route ? "earbuds" : "handset", s.bridgeRunning ? "running" : "stopped", s.spkVolume,
         s.peerBattery);
  audio::Stats a = audio::stats();
  printf("audio     : to earbuds %.0f dB, from mic %.0f dB, DAC underruns %lu, HF underruns %lu, overruns %lu\n",
         a.downDb, a.upDb, (unsigned long)a.dacUnderruns, (unsigned long)a.hfUnderruns, (unsigned long)a.overruns);
  printf("phone link: %s, free heap %lu KB\n", helper::phoneLinked() ? "connected" : "not connected",
         (unsigned long)(esp_get_free_heap_size() / 1024));
}

void handle(char* line) {
  txt::trim(line);
  char* arg = strchr(line, ' ');
  if (arg) *arg++ = 0;
  if (!line[0]) return;
  if (!strcmp(line, "help")) cmdHelp();
  else if (!strcmp(line, "status")) cmdStatus();
  else if (!strcmp(line, "scan")) { s_foundN = 0; hfp::startScan(12); }
  else if (!strcmp(line, "list")) {
    for (int i = 0; i < s_foundN; ++i)
      printf("  %d: %-24s %02X:%02X:%02X:%02X:%02X:%02X rssi %d%s\n", i, s_found[i].name[0] ? s_found[i].name : "?",
             s_found[i].bda[0], s_found[i].bda[1], s_found[i].bda[2], s_found[i].bda[3], s_found[i].bda[4],
             s_found[i].bda[5], s_found[i].rssi, s_found[i].isAudio ? "  [audio]" : "");
    if (!s_foundN) printf("  nothing yet - run 'scan' and wait ~12 s\n");
  } else if (!strcmp(line, "pair") && arg) {
    int i = atoi(arg);
    if (i >= 0 && i < s_foundN) hfp::connect(s_found[i].bda, s_found[i].name);
  } else if (!strcmp(line, "bonded")) {
    btl::DeviceMsg list[8];
    int n = hfp::bonded(list, 8);
    for (int i = 0; i < n; ++i)
      printf("  %s %02X:%02X:%02X:%02X:%02X:%02X\n", list[i].name[0] ? list[i].name : "?", list[i].bda[0],
             list[i].bda[1], list[i].bda[2], list[i].bda[3], list[i].bda[4], list[i].bda[5]);
    if (!n) printf("  no paired devices\n");
  } else if (!strcmp(line, "disc")) hfp::disconnect();
  else if (!strcmp(line, "forget")) {
    btl::StatusMsg s;
    hfp::fillStatus(&s);
    if (s.hasPeer) hfp::forget(s.peer);
  } else if (!strcmp(line, "ring")) fakeCall(btl::CallPhaseW::Incoming, arg ? arg : "+911234567890");
  else if (!strcmp(line, "talk")) fakeCall(btl::CallPhaseW::Active, arg ? arg : "+911234567890");
  else if (!strcmp(line, "idle")) fakeCall(btl::CallPhaseW::Idle, "");
  else if (!strcmp(line, "route") && arg) hfp::setRoute(!strcmp(arg, "on"));
  else if (!strcmp(line, "tone")) hfp::setAudioTest(btl::AudioTestMode::Tone);
  else if (!strcmp(line, "echo")) hfp::setAudioTest(btl::AudioTestMode::MicLoopback);
  else if (!strcmp(line, "quiet")) hfp::setAudioTest(btl::AudioTestMode::None);
  else if (!strcmp(line, "vol") && arg) hfp::setSpeakerVolume(atoi(arg));
  else if (!strcmp(line, "on")) hfp::setEnabled(true);
  else if (!strcmp(line, "off")) hfp::setEnabled(false);
  else if (!strcmp(line, "reboot")) esp_restart();
  else printf("unknown command '%s' - type help\n", line);
  helper::markStatusDirty();
}

void consoleTask(void*) {
  char line[96];
  size_t n = 0;
  for (;;) {
    uint8_t c;
    if (uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY) != 1) continue;
    if (c == '\r' || c == '\n') {
      if (n) {
        line[n] = 0;
        printf("\n");
        handle(line);
        n = 0;
      }
    } else if ((c == 8 || c == 127) && n) {
      --n;
    } else if (c >= 32 && n < sizeof line - 1) {
      line[n++] = (char)c;
    }
  }
}

}  // namespace

// Scan results also land here so "list" works without the S3.
void helperNoteScanResult(const btl::DeviceMsg& d) {
  for (int i = 0; i < s_foundN; ++i)
    if (!memcmp(s_found[i].bda, d.bda, 6)) {
      s_found[i] = d;
      return;
    }
  if (s_foundN < (int)(sizeof s_found / sizeof s_found[0])) s_found[s_foundN++] = d;
}

namespace helper {
void startConsole() {
  if (uart_driver_install(UART_NUM_0, 512, 0, 0, nullptr, 0) != ESP_OK) return;
  uart_vfs_dev_use_driver(UART_NUM_0);
  xTaskCreate(consoleTask, "console", 4096, nullptr, 3, nullptr);
  LOG("CON", "console ready - type 'help'");
}
}  // namespace helper
