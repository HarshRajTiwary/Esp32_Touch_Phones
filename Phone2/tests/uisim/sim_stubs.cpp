// Fake hardware / modem / Bluetooth helper / hotspot for the PC UI simulator.
// The real UI, navigation, keyboard, data-store and parser code run unchanged on top.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "at_parse.h"
#include "board_init.h"
#include "bt_client.h"
#include "display.h"
#include "hotspot.h"
#include "modem.h"
#include "nettools.h"
#include "phone_app.h"
#include "plog.h"
#include "settings.h"
#include "sim.h"
#include "storage.h"
#include "text_util.h"
#include "touch.h"

EspClass ESP;
SPIClass SPI;
SimWorld g_sim;

unsigned long millis(void) { return g_sim.clockMs; }
unsigned long micros(void) { return millis() * 1000; }
void delay(unsigned long) {}
void delayMicroseconds(unsigned int) {}

// ---- plog (time comes from the simulated clock) -------------------------------
void plogInit() {}
void plogSetSink(PlogSink) {}
uint32_t plogMillis() { return g_sim.clockMs; }
void plogPrintf(const char* tag, const char* fmt, ...) {
  if (!g_sim.verbose) return;
  va_list ap;
  va_start(ap, fmt);
  printf("[%s] ", tag);
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
}
const char* logNum(const char* n) { return n; }
const char* logText(const char* t) { return t; }
static const char* kLog[] = {
    "  4210 [MODEM] ready (A7672S-LASE, IMEI 86******0123, CMUX)",
    "  6044 [SIM] READY",
    "  9870 [NET] Registered (4G)",
    "  9871 [NET] Operator: Airtel",
    " 12312 [DATA] connected",
    " 12313 [NET] IP acquired: 100.72.14.9 (DNS 49.44.59.65)",
    " 12400 [BT] Bluetooth helper online (firmware 0.9.0)",
    " 13020 [BT] Headset connected: boAt Airdopes 141",
    " 15008 [HOTSPOT] Started \"Phone2-1A2B\" (WPA2, channel 6, up to 4 devices)",
    " 41230 [HOTSPOT] device connected (3C:22:..:9F), 1 total",
    " 88100 [CALL] Incoming: +91******5678",
    " 90450 [AUDIO] SCO connected (16000 Hz)",
};
int plogHistoryCount() { return (int)(sizeof kLog / sizeof kLog[0]); }
bool plogHistory(int i, char* out, size_t cap) {
  if (i < 0 || i >= plogHistoryCount()) return false;
  txt::copy(out, kLog[i], cap);
  return true;
}

// ---- board ----------------------------------------------------------------------
extern "C" void board_get_info(board_info_t* out) {
  memset(out, 0, sizeof *out);
  out->chip = "esp32s3";
  out->cores = 2;
  out->revision = 2;
  out->flash_bytes = 16u << 20;
  out->psram_bytes = 8u << 20;
  out->cpu_mhz = 240;
}
extern "C" void board_led(uint8_t, uint8_t, uint8_t) {}
extern "C" bool board_home_pressed(void) { return false; }

// ---- display / touch / storage / settings ----------------------------------------
namespace display {
static LGFX_Sprite* s_canvas = nullptr;
bool begin(uint8_t) {
  static lgfx::LGFX_Device dummy;
  s_canvas = new LGFX_Sprite(&dummy);
  s_canvas->setColorDepth(16);
  s_canvas->createSprite(240, 320);
  return true;
}
lgfx::LovyanGFX& gfx() { return *s_canvas; }
lgfx::LGFX_Device& panel() {
  static lgfx::LGFX_Device dummy;
  return dummy;
}
void present() {}
bool buffered() { return true; }
void setBrightness(uint8_t) {}
void sleep(bool) {}
bool asleep() { return false; }
LGFX_Sprite* canvas() { return s_canvas; }
}  // namespace display

namespace touch {
void setCalibration(const float*) {}
bool readRaw(int16_t*, int16_t*) { return false; }
TouchSample read() { return TouchSample(); }
}  // namespace touch

namespace storage {
bool begin() { return true; }
bool mounted() { return true; }
void usage(size_t* u, size_t* t) { *u = 180 * 1024; *t = 8000 * 1024; }
bool formatAll() { return true; }
bool saveFile(const char*, WriterFn, void*) { return true; }
bool loadFile(const char*, ReaderFn, void*) { return false; }
bool removeFile(const char*) { return true; }
void* allocLarge(size_t n) { return calloc(1, n); }
bool hasPsram() { return true; }
void Deferred::touch() { dirty = true; }
bool Deferred::due(uint32_t, bool) const { return false; }
}  // namespace storage

namespace settings {
static Settings s;
Settings& get() { return s; }
void load() {
  txt::copy(s.hotspotSsid, "Phone2-1A2B", sizeof s.hotspotSsid);
  txt::copy(s.hotspotPass, "kx7mq2tp9w", sizeof s.hotspotPass);
}
void save() {}
void factoryReset() {}
}  // namespace settings

// ---- Bluetooth helper ---------------------------------------------------------------
namespace bt {
bool begin(bool) { return true; }
bool helperOnline() { return g_sim.btOnline; }
const char* helperVersion() { return "0.9.0"; }
void setEnabled(bool) {}
bool enabled() { return true; }
State state() { return !g_sim.btOnline ? State::Off : g_sim.btConnected ? State::Connected : State::Idle; }
bool connected() { return g_sim.btOnline && g_sim.btConnected; }
bool audioOpen() { return connected() && g_sim.audioRunning; }
uint32_t audioRate() { return 16000; }
const char* stateText() {
  if (!g_sim.btOnline) return "Bluetooth helper offline";
  return g_sim.btConnected ? (g_sim.audioRunning ? "Connected - call audio" : "Connected") : "Not connected";
}
const char* peerName() { return "boAt Airdopes 141"; }
bool peerAddress(uint8_t out[6]) {
  static const uint8_t a[6] = {0x41, 0x42, 0xA1, 0x3C, 0x22, 0x9F};
  memcpy(out, a, 6);
  return true;
}
int peerBattery() { return connected() ? 80 : -1; }
void startScan() {}
void stopScan() {}
bool scanning() { return false; }
int devices(Device* out, int max) {
  static const char* names[] = {"realme Buds T100", "", "Living room TV"};
  static const int8_t rssi[] = {-58, -80, -71};
  int n = 0;
  for (int k = 0; k < 3 && n < max; ++k) {
    Device d{};
    d.bda[0] = (uint8_t)(0x10 + k);
    d.bda[5] = (uint8_t)(0xA0 + k);
    txt::copy(d.name, names[k], sizeof d.name);
    d.rssi = rssi[k];
    d.audio = k < 2;
    out[n++] = d;
  }
  return n;
}
int bonded(Device* out, int max) {
  if (max < 1) return 0;
  Device d{};
  txt::copy(d.name, "OnePlus Buds Z2", sizeof d.name);
  d.audio = true;
  d.rssi = -127;
  out[0] = d;
  return 1;
}
void connect(const uint8_t*, const char*) {}
void connectSaved() {}
void disconnect() {}
void forget() {}
void forgetDevice(const uint8_t*) {}
void setDiscoverable(bool) {}
void setRoute(bool) {}
bool route() { return g_sim.route == AudioRoute::Earbuds; }
void setSpeakerVolume(int) {}
int speakerVolume() { return 11; }
void noteSpeakerVolume(int) {}
void setGains(uint8_t, uint8_t) {}
void onPhoneState(const CallInfo&, const CallInfo&) {}
void onNetStatus(const NetStatus&, int) {}
void service(uint32_t) {}
void rebootHelper() {}
void formatAddress(const uint8_t b[6], char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
}
}  // namespace bt

namespace audio {
static TestMode s_mode = TestMode::None;
Stats stats() {
  Stats s{};
  s.downDb = -18;
  s.upDb = -31;
  s.running = g_sim.audioRunning;
  s.rate = 16000;
  return s;
}
bool running() { return g_sim.audioRunning; }
void setTestMode(TestMode m) { s_mode = m; }
TestMode testMode() { return s_mode; }
void setGains(uint8_t, uint8_t) {}
}  // namespace audio

// ---- modem -------------------------------------------------------------------------
namespace modem {
void begin(const Config&) {}
void dial(const char*) {}
void answer() {}
void hangup() {}
void dtmf(char) {}
void mute(bool) {}
void setAudioProfile(AudioRoute) {}
void setSpeakerGain(int) {}
void setMicGain(int) {}
bool sendSms(uint32_t, const char*, const char*) { return true; }
void ussd(const char*) {}
void ussdCancel() {}
void enterPin(const char*) {}
void readSimContacts() {}
void setClock(uint32_t) {}
void setNetworkMode(NetMode) {}
void setDataEnabled(bool) {}
void setApn(const char*, const char*, const char*) {}
DataStatus data() { return g_sim.data; }
void* pppNetif() { return nullptr; }
void rawAt(const char*) {}
void restart() {}
void refresh() {}
void powerOff() {}
NetStatus status() { return g_sim.net; }
CallInfo call() { return g_sim.call; }
PhoneState phoneState() { return g_sim.phoneState; }
bool registered() { return g_sim.net.reg == RegState::Home || g_sim.net.reg == RegState::Roaming; }
const char* phoneStateText(PhoneState s) {
  switch (s) {
    case PhoneState::Boot: return "Starting modem...";
    case PhoneState::InitializingModem: return "Checking SIM...";
    case PhoneState::NoSim: return "SIM not detected";
    case PhoneState::SimLocked: return "SIM locked";
    case PhoneState::SearchingNetwork: return "Searching for network";
    case PhoneState::Ready: return "Ready";
    case PhoneState::Error: return "Modem not responding";
  }
  return "";
}
int signalBars(const NetStatus& s) {
  bool reg = s.reg == RegState::Home || s.reg == RegState::Roaming;
  if (!reg) return 0;
  if (s.rat == Rat::Lte && s.rsrp != 0) return rsrpToBars(s.rsrp);
  return csqToBars(s.csq);
}
const char* ratText(const NetStatus& s) {
  bool reg = s.reg == RegState::Home || s.reg == RegState::Roaming;
  if (!reg) return "";
  return s.rat == Rat::Lte ? "4G" : s.rat == Rat::Gsm ? "2G" : "";
}
}  // namespace modem

// ---- hotspot / net tools -------------------------------------------------------------
namespace hotspot {
void begin() {}
bool start() { return true; }
void stop() {}
bool running() { return g_sim.hotspotOn; }
Status status() {
  Status s;
  s.on = g_sim.hotspotOn;
  s.sharing = g_sim.hotspotOn && g_sim.data.state == DataState::Connected;
  s.clients = g_sim.hotspotOn ? g_sim.hotspotClients : 0;
  txt::copy(s.ssid, settings::get().hotspotSsid, sizeof s.ssid);
  if (s.on) txt::copy(s.ip, "192.168.4.1", sizeof s.ip);
  return s;
}
int clients(Client*, int) { return 0; }
void applySettings() {}
}  // namespace hotspot

namespace nettools {
void begin() {}
void startConnectivityTest() {}
bool testRunning() { return false; }
const char* lastTestResult() { return "Internet OK (412 ms)"; }
}  // namespace nettools

// ---- application ---------------------------------------------------------------------
namespace app {
const char* const kQuickReplies[] = {
    "Busy now, will call you back.", "Driving, will call you later.", "In a meeting, please SMS.",
    "On my way.", "OK", "Call me when you are free.",
};
const int kQuickReplyCount = 6;
static TouchSample s_touch;

void setup() {}
void loop() {}
const NetStatus& net() { return g_sim.net; }
const CallInfo& call() { return g_sim.call; }
const DataStatus& data() { return g_sim.data; }
int battery() { return g_sim.battery; }
bool charging() { return g_sim.charging; }
uint32_t nowLocal() { return g_sim.now; }
bool clockValid() { return g_sim.now != 0; }
PhoneState phoneState() { return g_sim.phoneState; }
const TouchSample& touchSample() { return s_touch; }
void dial(const char*) {}
void answer() {}
void hangup() {}
void rejectWithSms(const char*) {}
void toggleMute() {}
void dtmf(char) {}
AudioRoute audioRoute() { return g_sim.route; }
void setAudioRoute(AudioRoute r) { g_sim.route = r; }
bool earbudsInUse() { return g_sim.route == AudioRoute::Earbuds; }
void setAudioPref(AudioPref p) { settings::get().audioPref = (uint8_t)p; }
void changeCallVolume(int) {}
int callVolumePercent() { return 73; }
uint32_t callSeconds() { return g_sim.callSeconds; }
uint32_t sendSms(const char*, const char*) { return 1; }
void resendSms(uint32_t) {}
void ussd(const char*) {}
void ussdReply(const char*) {}
void ussdCancel() {}
const char* ussdText() { return g_sim.ussdText; }
bool ussdWaiting() { return false; }
bool ussdExpectsReply() { return g_sim.ussdExpectsReply; }
void setDataEnabled(bool on) { settings::get().dataEnabled = on; }
void setApn(const char*, const char*, const char*) {}
void setNetworkMode(NetMode m) { settings::get().netMode = (uint8_t)m; }
void setHotspot(bool on) { g_sim.hotspotOn = on; }
void importSimContacts() {}
void setAudioTest(audio::TestMode) {}
void applyAudioSettings() {}
void lockNow() {}
void wake() {}
void factoryReset() {}
void restart() {}
void saveSettingsSoon() {}
uint32_t uptimeS() { return 3 * 3600 + 25 * 60 + 12; }
}  // namespace app
