#include "bt_client.h"

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "at_parse.h"
#include "board_config.h"
#include "btlink_uart.h"
#include "events.h"
#include "modem.h"
#include "plog.h"
#include "text_util.h"

namespace bt {
namespace {

constexpr int kMaxDevices = 20;
constexpr uint32_t kHelperResetAfterMs = 15000;   // offline this long -> pulse the helper's EN pin
constexpr uint32_t kHelperResetEveryMs = 60000;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
bool s_started = false;
volatile bool s_online = false;
char s_helperFw[16] = "";
btl::StatusMsg s_status = {};
bool s_haveStatus = false;
bool s_enabled = true;
bool s_route = false;
int s_spkVol = 12;
uint8_t s_gainSpk = 8, s_gainMic = 8;

Device s_devs[kMaxDevices];
int s_devCount = 0;
Device s_bonded[8];
int s_bondedCount = 0;

btl::CallStateMsg s_call = {};
btl::NetStateMsg s_net = {};
uint32_t s_lastSync = 0;
uint32_t s_offlineSince = 0;
uint32_t s_lastHelperReset = 0;
volatile bool s_resync = true;

audio::Stats s_audio = {-96, -96, 0, 0, 0, false, 0};
audio::TestMode s_test = audio::TestMode::None;

void toDevice(const btl::DeviceMsg& m, Device* d) {
  memcpy(d->bda, m.bda, 6);
  txt::copy(d->name, m.name, sizeof d->name);
  d->name[sizeof d->name - 1] = 0;
  d->rssi = m.rssi;
  d->cod = m.cod;
  d->audio = m.isAudio;
}

// Sends everything the helper needs to know. Used after it (re)starts and as
// the periodic heartbeat, so a lost frame heals within kHeartbeatMs.
void syncAll() {
  uint8_t on = s_enabled;
  btl::send(btl::Msg::SetEnabled, &on, 1);
  btl::sendMsg(btl::Msg::CallState, s_call);
  btl::sendMsg(btl::Msg::NetState, s_net);
  uint8_t r = s_route;
  btl::send(btl::Msg::AudioRoute, &r, 1);
  btl::GainsMsg g = {s_gainSpk, s_gainMic};
  btl::sendMsg(btl::Msg::Gains, g);
}

void onStatus(const btl::StatusMsg& st) {
  btl::StatusMsg before;
  bool had;
  portENTER_CRITICAL(&s_mux);
  before = s_status;
  had = s_haveStatus;
  s_status = st;
  s_status.peerName[sizeof s_status.peerName - 1] = 0;
  s_haveStatus = true;
  if (st.spkVolume >= 0) s_spkVol = st.spkVolume;
  portEXIT_CRITICAL(&s_mux);
  bool wasConn = had && before.state == (uint8_t)btl::BtState::Connected;
  bool isConn = st.state == (uint8_t)btl::BtState::Connected;
  if (isConn && !wasConn) {
    LOG("BT", "Headset connected: %s", st.peerName[0] ? st.peerName : "earbuds");
    events::post(Ev::BtChanged, 1);
  } else if (!isConn && wasConn) {
    LOG("BT", "Headset disconnected");
    events::post(Ev::BtChanged, 0);
  } else if (!had || before.state != st.state || before.enabled != st.enabled || before.scanning != st.scanning ||
             before.peerBattery != st.peerBattery || strcmp(before.peerName, st.peerName) != 0) {
    events::post(Ev::BtChanged, 2);
  }
  if (!had || before.audio != st.audio) {
    if (st.audio) LOG("AUDIO", "SCO connected (%u Hz)", st.audioRate);
    events::post(Ev::BtAudio, st.audio, st.audioRate);
  }
}

void onButton(const btl::ButtonMsg& b) {
  switch ((btl::ButtonKind)b.kind) {
    case btl::ButtonKind::Answer: events::post(Ev::BtAnswer); break;
    case btl::ButtonKind::Hangup: events::post(Ev::BtHangup); break;
    case btl::ButtonKind::Redial: events::post(Ev::BtRedial); break;
    case btl::ButtonKind::Dial: {
      char num[NUMBER_MAX];
      txt::copy(num, b.text, sizeof num);
      num[NUMBER_MAX - 1] = 0;
      events::post(Ev::BtDial, 0, 0, strdup(num));
      break;
    }
    case btl::ButtonKind::Dtmf: modem::dtmf((char)b.value); break;
    case btl::ButtonKind::SpeakerVol: events::post(Ev::BtVolume, 0, b.value); break;
    case btl::ButtonKind::MicVol: events::post(Ev::BtVolume, 1, b.value); break;
    case btl::ButtonKind::AudioOn: events::post(Ev::BtAudioToEarbuds, 1); break;
    case btl::ButtonKind::AudioOff: events::post(Ev::BtAudioToEarbuds, 0); break;
    default: break;
  }
}

void onScanResult(const btl::DeviceMsg& m) {
  portENTER_CRITICAL(&s_mux);
  Device* d = nullptr;
  for (int i = 0; i < s_devCount; ++i)
    if (!memcmp(s_devs[i].bda, m.bda, 6)) d = &s_devs[i];
  if (!d && s_devCount < kMaxDevices) d = &s_devs[s_devCount++];
  if (d) toDevice(m, d);
  portEXIT_CRITICAL(&s_mux);
  events::post(Ev::BtScanResult);
}

// Runs in the btlink_rx task: copy + post only.
void onFrame(const btl::Frame& f) {
  bool wasOnline = s_online;
  s_online = true;
  s_offlineSince = 0;
  if (!wasOnline) events::post(Ev::BtHelper, 1);
  switch (f.type) {
    case btl::Msg::HelloAck: {
      btl::HelloMsg h = {};
      memcpy(&h, f.payload, f.len < sizeof h ? f.len : sizeof h);
      h.fw[sizeof h.fw - 1] = 0;
      txt::copy(s_helperFw, h.fw, sizeof s_helperFw);
      if (h.proto != btl::kProtoVersion)
        LOG("BT", "helper speaks protocol v%u, phone v%u - flash both boards with the same release", h.proto,
            btl::kProtoVersion);
      else
        LOG("BT", "Bluetooth helper online (firmware %s)", s_helperFw);
      s_resync = true;  // the helper may have rebooted: give it the full state
      break;
    }
    case btl::Msg::Status:
      if (f.len >= sizeof(btl::StatusMsg)) {
        btl::StatusMsg st;
        memcpy(&st, f.payload, sizeof st);
        onStatus(st);
      }
      break;
    case btl::Msg::ScanResult:
      if (f.len >= sizeof(btl::DeviceMsg)) {
        btl::DeviceMsg m;
        memcpy(&m, f.payload, sizeof m);
        onScanResult(m);
      }
      break;
    case btl::Msg::ScanDone:
      events::post(Ev::BtScanDone);
      break;
    case btl::Msg::Bonded:
      if (f.len >= sizeof(btl::DeviceMsg)) {
        btl::DeviceMsg m;
        memcpy(&m, f.payload, sizeof m);
        portENTER_CRITICAL(&s_mux);
        if (m.count == 0) s_bondedCount = 0;
        else if (m.index < 8) {
          toDevice(m, &s_bonded[m.index]);
          s_bondedCount = m.count > 8 ? 8 : m.count;
        }
        portEXIT_CRITICAL(&s_mux);
        events::post(Ev::BtChanged, 2);
      }
      break;
    case btl::Msg::Button:
      if (f.len >= sizeof(btl::ButtonMsg)) {
        btl::ButtonMsg b;
        memcpy(&b, f.payload, sizeof b);
        onButton(b);
      }
      break;
    case btl::Msg::AudioStats:
      if (f.len >= sizeof(btl::AudioStatsMsg)) {
        btl::AudioStatsMsg a;
        memcpy(&a, f.payload, sizeof a);
        portENTER_CRITICAL(&s_mux);
        s_audio.downDb = a.downDb;
        s_audio.upDb = a.upDb;
        s_audio.rate = a.rate;
        s_audio.dacUnderruns = a.dacUnderruns;
        s_audio.hfUnderruns = a.hfUnderruns;
        s_audio.overruns = a.overruns;
        s_audio.running = a.running;
        portEXIT_CRITICAL(&s_mux);
      }
      break;
    case btl::Msg::Log:
      LOG("BT", "%.*s", (int)f.len, (const char*)f.payload);
      break;
    default:
      break;
  }
}

void sendRef(btl::Msg type, const uint8_t bda[6], const char* name) {
  btl::DeviceRef r = {};
  memcpy(r.bda, bda, 6);
  if (name) txt::copy(r.name, name, sizeof r.name);
  btl::sendMsg(type, r);
}

int hfpLevel(int bars0to4) { return bars0to4 <= 0 ? 0 : bars0to4 + 1; }  // 0..5

}  // namespace

bool begin(bool enabled) {
  s_enabled = enabled;
  btl::UartConfig c = {LINK_UART, PIN_LINK_TX, PIN_LINK_RX, btl::kBaud};
  if (!btl::start(c, onFrame)) return false;
  s_started = true;
  btl::HelloMsg h = {};
  h.proto = btl::kProtoVersion;
  h.role = 1;
  txt::copy(h.fw, FW_VERSION, sizeof h.fw);
  btl::sendMsg(btl::Msg::Hello, h);
  s_offlineSince = plogMillis();
  return true;
}

bool helperOnline() { return s_online; }
const char* helperVersion() { return s_helperFw; }

void setEnabled(bool on) {
  s_enabled = on;
  uint8_t v = on;
  btl::send(btl::Msg::SetEnabled, &v, 1);
}

bool enabled() { return s_enabled; }

State state() {
  if (!s_online || !s_haveStatus) return State::Off;
  return (State)s_status.state;
}

bool connected() { return state() == State::Connected; }
bool audioOpen() { return s_online && s_status.audio; }
uint32_t audioRate() { return s_online && s_status.audio ? s_status.audioRate : 0; }

const char* stateText() {
  if (!s_online) return "Bluetooth helper offline";
  switch (state()) {
    case State::Off: return "Off";
    case State::Idle: return s_status.hasPeer ? "Not connected" : "No earbuds paired";
    case State::Connecting: return "Connecting...";
    case State::Connected: return s_status.audio ? "Connected - call audio" : "Connected";
  }
  return "";
}

const char* peerName() {
  static char name[BT_NAME_MAX];
  portENTER_CRITICAL(&s_mux);
  txt::copy(name, s_status.peerName[0] ? s_status.peerName : (s_status.hasPeer ? "Earbuds" : ""), sizeof name);
  portEXIT_CRITICAL(&s_mux);
  return name;
}

bool peerAddress(uint8_t out[6]) {
  if (!s_status.hasPeer) return false;
  memcpy(out, s_status.peer, 6);
  return true;
}

int peerBattery() { return connected() ? s_status.peerBattery : -1; }

void startScan() {
  portENTER_CRITICAL(&s_mux);
  s_devCount = 0;
  portEXIT_CRITICAL(&s_mux);
  uint8_t secs = 12;
  btl::send(btl::Msg::ScanStart, &secs, 1);
}

void stopScan() { btl::send(btl::Msg::ScanStop); }
bool scanning() { return s_online && s_status.scanning; }

int devices(Device* out, int max) {
  portENTER_CRITICAL(&s_mux);
  int n = 0;
  for (int pass = 0; pass < 2; ++pass)  // earbuds/headsets first
    for (int i = 0; i < s_devCount && n < max; ++i)
      if (s_devs[i].audio == (pass == 0)) out[n++] = s_devs[i];
  portEXIT_CRITICAL(&s_mux);
  return n;
}

int bonded(Device* out, int max) {
  portENTER_CRITICAL(&s_mux);
  int n = s_bondedCount < max ? s_bondedCount : max;
  for (int i = 0; i < n; ++i) out[i] = s_bonded[i];
  portEXIT_CRITICAL(&s_mux);
  return n;
}

void connect(const uint8_t bda[6], const char* name) { sendRef(btl::Msg::Connect, bda, name); }

void connectSaved() {
  uint8_t bda[6];
  if (peerAddress(bda)) connect(bda, s_status.peerName);
}

void disconnect() { btl::send(btl::Msg::Disconnect); }

void forget() {
  uint8_t bda[6];
  if (peerAddress(bda)) forgetDevice(bda);
}

void forgetDevice(const uint8_t bda[6]) { sendRef(btl::Msg::Forget, bda, nullptr); }

void setDiscoverable(bool on) {
  uint8_t v = on;
  btl::send(btl::Msg::Discoverable, &v, 1);
}

void setRoute(bool earbuds) {
  if (s_route != earbuds) LOG("AUDIO", "call audio -> %s", earbuds ? "earbuds" : "handset");
  s_route = earbuds;
  uint8_t v = earbuds;
  btl::send(btl::Msg::AudioRoute, &v, 1);
}

bool route() { return s_route; }

void setSpeakerVolume(int v) {
  s_spkVol = v < 0 ? 0 : v > 15 ? 15 : v;
  uint8_t b = (uint8_t)s_spkVol;
  btl::send(btl::Msg::Volume, &b, 1);
}

int speakerVolume() { return s_spkVol; }
void noteSpeakerVolume(int v) { s_spkVol = v; }

void setGains(uint8_t spk, uint8_t mic) {
  s_gainSpk = spk;
  s_gainMic = mic;
  btl::GainsMsg g = {spk, mic};
  btl::sendMsg(btl::Msg::Gains, g);
}

void onPhoneState(const CallInfo& now, const CallInfo&) {
  btl::CallStateMsg c = {};
  c.phase = (uint8_t)now.phase;  // same numbering as btl::CallPhaseW
  c.incoming = now.incoming;
  c.muted = now.muted;
  txt::copy(c.number, now.number, sizeof c.number);
  if (memcmp(&c, &s_call, sizeof c) == 0) return;
  s_call = c;
  btl::sendMsg(btl::Msg::CallState, s_call);
}

void onNetStatus(const NetStatus& st, int battPct) {
  btl::NetStateMsg n = {};
  n.service = modem::registered() ? 1 : 0;
  n.signal = (uint8_t)hfpLevel(modem::signalBars(st));
  n.roaming = st.reg == RegState::Roaming;
  n.battery = battPct < 0 ? 5 : (uint8_t)((battPct + 10) / 20 > 5 ? 5 : (battPct + 10) / 20);
  txt::copy(n.op, st.op, sizeof n.op);
  txt::copy(n.ownNumber, st.ownNumber, sizeof n.ownNumber);
  if (memcmp(&n, &s_net, sizeof n) == 0) return;
  s_net = n;
  btl::sendMsg(btl::Msg::NetState, s_net);
}

void service(uint32_t now) {
  if (!s_started) return;
  if (s_resync || now - s_lastSync >= btl::kHeartbeatMs) {
    s_resync = false;
    s_lastSync = now;
    syncAll();
  }
  uint32_t last = btl::lastRxMs();
  bool alive = last && now - last < btl::kLinkTimeoutMs;
  if (s_online && !alive) {
    s_online = false;
    s_offlineSince = now;
    LOG("BT", "Bluetooth helper stopped answering");
    events::post(Ev::BtHelper, 0);
    events::post(Ev::BtChanged, 0);
  }
  if (!s_online) {
    if (!s_offlineSince) s_offlineSince = now;
    // Say hello now and then (the helper answers with HelloAck when it is there)
    static uint32_t lastHello = 0;
    if (now - lastHello > 3000) {
      lastHello = now;
      btl::HelloMsg h = {};
      h.proto = btl::kProtoVersion;
      h.role = 1;
      txt::copy(h.fw, FW_VERSION, sizeof h.fw);
      btl::sendMsg(btl::Msg::Hello, h);
    }
    if (PIN_LINK_RESET >= 0 && now - s_offlineSince > kHelperResetAfterMs &&
        (s_lastHelperReset == 0 || now - s_lastHelperReset > kHelperResetEveryMs)) {
      s_lastHelperReset = now;
      rebootHelper();
    }
  }
}

void rebootHelper() {
  if (PIN_LINK_RESET >= 0) {
    LOG("BT", "resetting the Bluetooth helper (EN pin)");
    gpio_set_level((gpio_num_t)PIN_LINK_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level((gpio_num_t)PIN_LINK_RESET, 1);  // open-drain: released
  } else {
    btl::send(btl::Msg::Reboot);
  }
}

void formatAddress(const uint8_t b[6], char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
}

}  // namespace bt

namespace audio {

Stats stats() {
  portENTER_CRITICAL(&bt::s_mux);
  Stats s = bt::s_audio;
  portEXIT_CRITICAL(&bt::s_mux);
  if (!bt::audioOpen()) {
    s.running = false;
    s.downDb = s.upDb = -96;
  }
  return s;
}

bool running() { return bt::audioOpen(); }

void setTestMode(TestMode m) {
  bt::s_test = m;
  uint8_t v = (uint8_t)m;
  btl::send(btl::Msg::AudioTest, &v, 1);
}

TestMode testMode() { return bt::s_test; }

void setGains(uint8_t spk, uint8_t mic) { bt::setGains(spk, mic); }

}  // namespace audio
