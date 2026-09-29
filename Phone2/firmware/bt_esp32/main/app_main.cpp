// =============================================================================
//  Phone2 - ESP32 Bluetooth helper
//
//  Bluetooth Classic HFP Audio Gateway for TWS earbuds + analog call-audio
//  bridge to the A7672S, controlled by the ESP32-S3 phone over the BT link.
//
//  Tasks:
//    main       : HFP service (reconnect / SCO policy), heartbeat, LED
//    btlink_rx  : link frames from the S3 -> hfp / audio calls
//    audio      : ADC DMA -> earbuds (plus DAC timer ISR for the uplink)
//    console    : optional USB serial commands for testing without the S3
// =============================================================================
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <string.h>

#include "audio_bridge.h"
#include "board_config.h"
#include "board_init.h"
#include "btlink_uart.h"
#include "helper.h"
#include "hfp_ag.h"
#include "plog.h"
#include "text_util.h"

namespace {

volatile bool s_statusDirty = true;
volatile bool s_linkUp = false;
volatile uint32_t s_lastPhoneMs = 0;
volatile bool s_forwardLog = false;
volatile bool s_audioOn = false;
volatile uint32_t s_audioRate = 0;
uint8_t s_route = 0;

// ---- helper -> S3 -----------------------------------------------------------
void sendStatus() {
  btl::StatusMsg s;
  hfp::fillStatus(&s);
  s.uptimeS = plogMillis() / 1000;
  s.linkRxErrors = (uint16_t)btl::rxErrors();
  s.freeHeapKb = (uint16_t)(esp_get_free_heap_size() / 1024);
  btl::sendMsg(btl::Msg::Status, s);
}

void sendBonded() {
  static btl::DeviceMsg list[8];
  int n = hfp::bonded(list, 8);
  if (n == 0) {
    btl::DeviceMsg none = {};
    none.count = 0;
    btl::sendMsg(btl::Msg::Bonded, none);
    return;
  }
  for (int i = 0; i < n; ++i) {
    list[i].index = (uint8_t)i;
    list[i].count = (uint8_t)n;
    btl::sendMsg(btl::Msg::Bonded, list[i]);
  }
}

void sendHello(btl::Msg type) {
  btl::HelloMsg h = {};
  h.proto = btl::kProtoVersion;
  h.role = 2;
  txt::copy(h.fw, FW_VERSION, sizeof h.fw);
  btl::sendMsg(type, h);
}

// Log lines are forwarded to the S3 console once the phone has said hello.
void logSink(const char* line) {
  if (!s_forwardLog) return;
  const char* p = strstr(line, "] ");  // strip the helper's own timestamp, the S3 adds one
  p = p ? p + 2 : line;
  btl::send(btl::Msg::Log, p, strnlen(p, btl::kMaxPayload));
}

// ---- callbacks from the HFP layer ---------------------------------------------
void onStatusChanged() { s_statusDirty = true; }
void onScanResult(const btl::DeviceMsg& d) {
  helperNoteScanResult(d);
  btl::sendMsg(btl::Msg::ScanResult, d);
}
void onScanDone() { btl::send(btl::Msg::ScanDone); }

void onButton(btl::ButtonKind kind, int value, const char* text) {
  btl::ButtonMsg b = {};
  b.kind = (uint8_t)kind;
  b.value = (int16_t)value;
  txt::copy(b.text, text, sizeof b.text);
  btl::sendMsg(btl::Msg::Button, b);
}

void onAudioChanged(bool on, uint32_t rate) {
  s_audioOn = on;
  s_audioRate = rate;
  if (on) {
    board_audio_switch(true);
    audio::start(rate);
  } else {
    audio::stop();
    board_audio_switch(false);
  }
  s_statusDirty = true;
}

// ---- S3 -> helper -------------------------------------------------------------
template <class T>
bool payload(const btl::Frame& f, T* out) {
  if (f.len < sizeof(T)) {
    LOG("LINK", "short %s frame (%u bytes)", btl::msgName(f.type), f.len);
    return false;
  }
  memcpy(out, f.payload, sizeof(T));
  return true;
}

void onFrame(const btl::Frame& f) {
  s_lastPhoneMs = plogMillis();
  if (!s_linkUp) {
    s_linkUp = true;
    LOG("LINK", "phone (ESP32-S3) connected");
  }
  switch (f.type) {
    case btl::Msg::Hello: {
      btl::HelloMsg h;
      if (payload(f, &h) && h.proto != btl::kProtoVersion)
        LOG("LINK", "protocol mismatch: phone v%u, helper v%u - update both boards", h.proto, btl::kProtoVersion);
      sendHello(btl::Msg::HelloAck);
      s_forwardLog = true;
      sendStatus();
      sendBonded();
      break;
    }
    case btl::Msg::SetEnabled:
      if (f.len) hfp::setEnabled(f.payload[0] != 0);
      break;
    case btl::Msg::ScanStart:
      hfp::startScan(f.len ? f.payload[0] : 0);
      break;
    case btl::Msg::ScanStop:
      hfp::stopScan();
      break;
    case btl::Msg::Connect: {
      btl::DeviceRef d;
      if (payload(f, &d)) {
        d.name[sizeof d.name - 1] = 0;
        hfp::connect(d.bda, d.name);
      }
      break;
    }
    case btl::Msg::Disconnect:
      hfp::disconnect();
      break;
    case btl::Msg::Forget: {
      btl::DeviceRef d;
      if (payload(f, &d)) hfp::forget(d.bda);
      sendBonded();
      break;
    }
    case btl::Msg::Discoverable:
      if (f.len) hfp::setDiscoverable(f.payload[0] != 0);
      break;
    case btl::Msg::CallState: {
      btl::CallStateMsg c;
      if (payload(f, &c)) hfp::setCallState(c);
      break;
    }
    case btl::Msg::NetState: {
      btl::NetStateMsg n;
      if (payload(f, &n)) hfp::setNetState(n);
      break;
    }
    case btl::Msg::Volume:
      if (f.len) hfp::setSpeakerVolume(f.payload[0]);
      break;
    case btl::Msg::Gains: {
      btl::GainsMsg g;
      if (payload(f, &g)) audio::setGains(g.spkStep, g.micStep);
      break;
    }
    case btl::Msg::AudioTest:
      if (f.len) hfp::setAudioTest((btl::AudioTestMode)f.payload[0]);
      break;
    case btl::Msg::AudioRoute:
      if (f.len) {
        s_route = f.payload[0];
        hfp::setRoute(s_route != 0);
        s_statusDirty = true;
      }
      break;
    case btl::Msg::GetStatus:
      s_statusDirty = true;
      break;
    case btl::Msg::ListBonded:
      sendBonded();
      break;
    case btl::Msg::Reboot:
      LOG("LINK", "reboot requested by the phone");
      vTaskDelay(pdMS_TO_TICKS(100));
      esp_restart();
      break;
    default:
      LOG("LINK", "unexpected %s frame", btl::msgName(f.type));
      break;
  }
}

// LED: off = Bluetooth off, slow blink = no earbuds, fast blink = connecting,
// on = earbuds connected, double flash = voice link open.
void updateLed(uint32_t now) {
  btl::StatusMsg s;
  hfp::fillStatus(&s);
  bool on;
  switch ((btl::BtState)s.state) {
    case btl::BtState::Off: on = false; break;
    case btl::BtState::Idle: on = (now % 2000) < 100; break;
    case btl::BtState::Connecting: on = (now % 300) < 150; break;
    default:
      on = s.audio ? ((now % 1000) < 100 || ((now % 1000) >= 200 && (now % 1000) < 300)) : true;
      break;
  }
  board_led(on);
}

}  // namespace

// Used by the console to simulate the phone.
namespace helper {
void markStatusDirty() { s_statusDirty = true; }
bool phoneLinked() { return s_linkUp; }
}  // namespace helper

extern "C" void app_main(void) {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  plogInit();
  board_init();
  LOG("APP", "%s Bluetooth helper %s starting", FW_NAME, FW_VERSION);

  audio::begin();
  hfp::Callbacks cb = {};
  cb.statusChanged = onStatusChanged;
  cb.scanResult = onScanResult;
  cb.scanDone = onScanDone;
  cb.button = onButton;
  cb.audioChanged = onAudioChanged;
  if (!hfp::begin(cb)) LOG("APP", "Bluetooth failed to start - earbud calls will not work");

  btl::UartConfig lc = {LINK_UART, PIN_LINK_TX, PIN_LINK_RX, btl::kBaud};
  btl::start(lc, onFrame);
  plogSetSink(logSink);
  sendHello(btl::Msg::HelloAck);  // tells a running phone that the helper (re)started
  helper::startConsole();

  uint32_t lastStatus = 0, lastStats = 0;
  for (;;) {
    uint32_t now = plogMillis();
    hfp::service(now);
    if (s_statusDirty || now - lastStatus >= btl::kHeartbeatMs) {
      s_statusDirty = false;
      lastStatus = now;
      sendStatus();
    }
    if (audio::running() && now - lastStats >= 500) {
      lastStats = now;
      audio::Stats a = audio::stats();
      btl::AudioStatsMsg m = {};
      m.downDb = (int8_t)(a.downDb < -96 ? -96 : a.downDb);
      m.upDb = (int8_t)(a.upDb < -96 ? -96 : a.upDb);
      m.rate = (uint16_t)a.rate;
      m.dacUnderruns = (uint16_t)a.dacUnderruns;
      m.hfUnderruns = (uint16_t)a.hfUnderruns;
      m.overruns = (uint16_t)a.overruns;
      m.running = a.running;
      m.testMode = (uint8_t)audio::testMode();
      btl::sendMsg(btl::Msg::AudioStats, m);
    }
    if (s_linkUp && now - s_lastPhoneMs > btl::kLinkTimeoutMs) {
      s_linkUp = false;
      s_forwardLog = false;
      LOG("LINK", "phone link lost (no frames for %lu ms)", (unsigned long)btl::kLinkTimeoutMs);
    }
    updateLed(now);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
