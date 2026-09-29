// Fake hardware / modem / Bluetooth state for the PC UI simulator.
// The real UI, navigation, keyboard and data-store code run unchanged on top.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>


#include "../../firmware/TouchPhone/src/app/phone_app.h"
#include "../../firmware/TouchPhone/src/audio/audio_bridge.h"
#include "../../firmware/TouchPhone/src/bt/bt_manager.h"
#include "../../firmware/TouchPhone/src/core/text_util.h"
#include "../../firmware/TouchPhone/src/hw/display.h"
#include "../../firmware/TouchPhone/src/hw/power.h"
#include "../../firmware/TouchPhone/src/hw/touch.h"
#include "../../firmware/TouchPhone/src/modem/modem.h"
#include "../../firmware/TouchPhone/src/store/settings.h"
#include "../../firmware/TouchPhone/src/store/storage.h"
#include "sim.h"

EspClass ESP;
SPIClass SPI;
SimWorld g_sim;

unsigned long millis(void) { return g_sim.clockMs; }
unsigned long micros(void) { return millis() * 1000; }
void delay(unsigned long) {}
void delayMicroseconds(unsigned int) {}

void logPrintf(const char* tag, const char* fmt, ...) {
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

// ---------------------------------------------------------------------------
namespace display {
static LGFX_Sprite* s_canvas = nullptr;
bool begin(uint8_t, uint8_t) {
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
void setRotation(uint8_t) {}
uint8_t rotation() { return 5; }
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
bool computeCalibration(const int16_t[][2], const int16_t[][2], int, float*) { return false; }
void mapRaw(const float*, int16_t rx, int16_t ry, int16_t* x, int16_t* y) { *x = rx; *y = ry; }
}  // namespace touch

namespace storage {
bool begin() { return true; }
bool mounted() { return true; }
void usage(size_t* u, size_t* t) { *u = 48 * 1024; *t = 896 * 1024; }
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
void load() {}
void save() {}
void factoryReset() {}
}  // namespace settings

namespace power {
void begin() {}
PmuType type() { return PmuType::IP5306; }
const char* typeName() { return "IP5306"; }
bool charging() { return g_sim.charging; }
bool usbPowered() { return false; }
int pmuLevel() { return 75; }
int batteryPercent(int p, int) { return p; }
void service(uint32_t) {}
}  // namespace power

namespace audio {
static TestMode s_mode = TestMode::None;
bool begin() { return true; }
void start(uint32_t) {}
void stop() {}
bool running() { return g_sim.audioRunning; }
void setGains(uint8_t, uint8_t) {}
void setTestMode(TestMode m) { s_mode = m; }
TestMode testMode() { return s_mode; }
Stats stats() {
  Stats s{};
  s.downDb = -18;
  s.upDb = -31;
  s.running = g_sim.audioRunning;
  s.rate = 16000;
  return s;
}
void onHfIncoming(const uint8_t*, uint32_t) {}
uint32_t onHfOutgoing(uint8_t*, uint32_t) { return 0; }
}  // namespace audio

namespace bt {
bool begin(bool) { return true; }
void setEnabled(bool) {}
bool enabled() { return true; }
State state() { return g_sim.btConnected ? State::Connected : State::Idle; }
bool connected() { return g_sim.btConnected; }
bool audioOpen() { return g_sim.btConnected && g_sim.audioRunning; }
uint32_t audioRate() { return 16000; }
const char* stateText() { return g_sim.btConnected ? "Connected - audio on" : "Not connected"; }
const char* peerName() { return "boAt Airdopes 141"; }
bool peerAddress(uint8_t out[6]) {
  static const uint8_t a[6] = {0x41, 0x42, 0xA1, 0x3C, 0x22, 0x9F};
  memcpy(out, a, 6);
  return true;
}
void startScan() {}
void stopScan() {}
bool scanning() { return false; }
int devices(Device* out, int max) {
  static const char* names[] = {"boAt Airdopes 141", "realme Buds T100", "", "Living room TV"};
  static const int8_t rssi[] = {-52, -68, -80, -71};
  int n = 0;
  for (int k = 0; k < 4 && n < max; ++k) {
    Device d{};
    d.bda[0] = (uint8_t)(0x10 + k);
    d.bda[5] = (uint8_t)(0xA0 + k);
    txt::copy(d.name, names[k], sizeof d.name);
    d.rssi = rssi[k];
    d.audio = k < 3;
    out[n++] = d;
  }
  return n;
}
void connect(const uint8_t*, const char*) {}
void connectSaved() {}
void disconnect() {}
void forget() {}
void setDiscoverable(bool) {}
void openAudio() {}
void setSpeakerVolume(int) {}
int speakerVolume() { return 11; }
void noteSpeakerVolume(int) {}
void closeAudio() {}
void onPhoneState(const CallInfo&, const CallInfo&) {}
void onNetStatus(const NetStatus&, int) {}
void service(uint32_t) {}
void formatAddress(const uint8_t b[6], char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
}
}  // namespace bt

namespace modem {
void begin(uint8_t, uint8_t, uint8_t) {}
void dial(const char*) {}
void answer() {}
void hangup() {}
void dtmf(char) {}
void mute(bool) {}
void setVolume(int) {}
void setRinger(int) {}
void setMicGain(int) {}
bool sendSms(uint32_t, const char*, const char*) { return true; }
void ussd(const char*) {}
void ussdCancel() {}
void enterPin(const char*) {}
void readSimContacts() {}
void setClock(uint32_t) {}
void rawAt(const char*) {}
void restart() {}
void refresh() {}
void speakerTest() {}
NetStatus status() { return g_sim.net; }
CallInfo call() { return g_sim.call; }
PhoneState phoneState() { return g_sim.phoneState; }
bool registered() { return true; }
const char* phoneStateText(PhoneState s) {
  switch (s) {
    case PhoneState::Boot: return "Starting modem...";
    case PhoneState::InitializingModem: return "Searching SIM...";
    case PhoneState::NoSim: return "No SIM card";
    case PhoneState::SimLocked: return "SIM locked";
    case PhoneState::SearchingNetwork: return "Searching network...";
    case PhoneState::Ready: return "Ready";
    case PhoneState::Error: return "Modem not responding";
  }
  return "";
}
}  // namespace modem

// ---------------------------------------------------------------------------
namespace app {
const char* const kQuickReplies[] = {
    "Busy now, will call you back.", "Driving, will call you later.", "In a meeting, please SMS.",
    "On my way.", "OK", "Call me when you are free.",
};
const int kQuickReplyCount = 6;

void setup() {}
void loop() {}
const NetStatus& net() { return g_sim.net; }
const CallInfo& call() { return g_sim.call; }
int battery() { return g_sim.net.battPct; }
bool charging() { return g_sim.charging; }
uint32_t nowLocal() { return g_sim.now; }
bool clockValid() { return g_sim.now != 0; }
PhoneState phoneState() { return g_sim.phoneState; }
void dial(const char*) {}
void answer() {}
void hangup() {}
void rejectWithSms(const char*) {}
void toggleMute() {}
void dtmf(char) {}
bool earbudsInUse() { return g_sim.btConnected; }
void setAudioRoute(AudioRoute r) { settings::get().audioRoute = (uint8_t)r; }
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
void importSimContacts() {}
void setAudioTest(audio::TestMode) {}
void applyAudioSettings() {}
void lockNow() {}
void wake() {}
void factoryReset() {}
void saveSettingsSoon() {}
uint32_t uptimeS() { return 3 * 3600 + 25 * 60; }
}  // namespace app
