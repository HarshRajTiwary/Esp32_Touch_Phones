#include "phone_app.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <string>
#include <vector>

#include "../audio/audio_bridge.h"
#include "../bt/bt_manager.h"
#include "../config/board_config.h"
#include "../core/events.h"
#include "../core/log.h"
#include "../core/text_util.h"
#include "../hw/display.h"
#include "../hw/power.h"
#include "../hw/touch.h"
#include "../modem/modem.h"
#include "../store/call_log.h"
#include "../store/contacts.h"
#include "../store/messages.h"
#include "../store/settings.h"
#include "../store/storage.h"
#include "../ui/gui.h"
#include "../ui/screens.h"
#include "console.h"

namespace app {
const char* const kQuickReplies[] = {
    "Busy now, will call you back.",
    "Driving, will call you later.",
    "In a meeting, please SMS.",
    "On my way.",
    "OK",
    "Call me when you are free.",
};
const int kQuickReplyCount = sizeof(kQuickReplies) / sizeof(kQuickReplies[0]);
}  // namespace app

namespace {

constexpr uint32_t kValidEpoch = 1704067200;  // 2024-01-01

NetStatus s_net;
CallInfo s_call;
PhoneState s_phoneState = PhoneState::Boot;
int s_batt = -1;
bool s_lowBattWarned = false;

bool s_dirty = true;
uint32_t s_lastFrameMs = 0, s_lastPollMs = 0, s_lastActivityMs = 0;
TouchSample s_touch;       // latest debounced sample
bool s_swallowTouch = false;  // the press that woke the screen does nothing else

bool s_settingsDirty = false;
uint32_t s_settingsChangedMs = 0;

char s_ussdText[USSD_TEXT_MAX] = "";
bool s_ussdWaiting = false, s_ussdExpectsReply = false;

uint32_t s_phaseSinceMs = 0, s_callEndedMs = 0, s_audioRequestMs = 0;
bool s_audioRequested = false;
bool s_discoverable = false;

char s_rejectNumber[NUMBER_MAX] = "";
char s_rejectText[80] = "";

bool callBusy(const CallInfo& c) {
  return c.phase == CallPhase::Dialing || c.phase == CallPhase::Alerting || c.phase == CallPhase::Active;
}

AudioRoute route() { return (AudioRoute)settings::get().audioRoute; }

bool wantEarbuds() { return bt::connected() && route() != AudioRoute::Wired; }

void markDirty() { s_dirty = true; }

// ---------------------------------------------------------------------------
void notifyText(const char* text, uint32_t color, std::function<void()> onTap = nullptr) {
  ui.toast(text, color, onTap);
  markDirty();
}

void onCallEnded(const CallInfo& c, CallEndReason reason) {
  uint32_t dur = c.everActive ? (millis() - c.activeMs) / 1000 : 0;
  CallType type = CallType::Outgoing;
  if (c.incoming) type = c.everActive ? CallType::Incoming : reason == CallEndReason::Rejected ? CallType::Rejected : CallType::Missed;
  uint32_t now = app::nowLocal();
  calllog::add(c.number, type, now ? now - dur : 0, (uint16_t)(dur > 65535 ? 65535 : dur));
  s_callEndedMs = millis();
  if (type == CallType::Missed) {
    char name[40], msg[64];
    contacts::displayName(c.number, name, sizeof name);
    snprintf(msg, sizeof msg, "Missed call from %s", name);
    app::wake();
    notifyText(msg, th::RED, [] { nav::push(ScreenId::Recents); });
  }
  if (s_rejectText[0] && txt::numbersMatch(s_rejectNumber, c.number)) {
    app::sendSms(s_rejectNumber, s_rejectText);
    s_rejectText[0] = 0;
  }
  if (audio::testMode() != audio::TestMode::None) audio::setTestMode(audio::TestMode::None);
}

void handleEvent(AppEvent& e) {
  switch (e.type) {
    case Ev::NetChanged: {
      s_net = modem::status();
      PhoneState prev = s_phoneState;
      s_phoneState = modem::phoneState();
      s_batt = power::batteryPercent(s_net.battPct, s_net.battMv);
      bt::onNetStatus(s_net, s_batt);
      if (s_phoneState == PhoneState::SimLocked && prev != PhoneState::SimLocked && nav::current() != ScreenId::Boot &&
          nav::current() != ScreenId::SimPin)
        nav::push(ScreenId::SimPin);
      if (s_batt >= 0 && s_batt <= 10 && !power::charging()) {
        if (!s_lowBattWarned) notifyText("Battery low - please charge", th::RED);
        s_lowBattWarned = true;
      } else if (s_batt > 20) {
        s_lowBattWarned = false;
      }
      break;
    }
    case Ev::CallChanged: {
      CallInfo now = modem::call();
      CallInfo before = s_call;
      s_call = now;
      bt::onPhoneState(now, before);
      if (now.phase != before.phase) {
        s_phaseSinceMs = millis();
        s_audioRequested = false;
      }
      if (now.phase != CallPhase::Idle && before.phase == CallPhase::Idle) {
        app::wake();
        if (nav::current() != ScreenId::Call) nav::push(ScreenId::Call);
      }
      break;
    }
    case Ev::CallEnded:
      if (e.ptr) onCallEnded(*static_cast<CallInfo*>(e.ptr), (CallEndReason)e.a);
      break;
    case Ev::SmsReceived: {
      SmsRecord* r = static_cast<SmsRecord*>(e.ptr);
      if (!r) break;
      messages::add(*r);
      app::wake();
      char name[40], text[96], msg[140];
      contacts::displayName(r->number, name, sizeof name);
      txt::copy(text, r->text, sizeof text);
      for (char* p = text; *p; ++p)
        if (*p == '\n') *p = ' ';
      snprintf(msg, sizeof msg, "%s: %s", name, text);
      char number[NUMBER_MAX];
      txt::copy(number, r->number, sizeof number);
      bool viewing = nav::current() == ScreenId::Thread;  // the thread screen refreshes itself
      if (!viewing) {
        std::string num(number);
        notifyText(msg, th::ACCENT, [num] { nav::push(ScreenId::Thread, NavParam::str(num.c_str())); });
      }
      break;
    }
    case Ev::SmsSent: {
      messages::setFlags((uint32_t)e.a, e.b ? 0 : SMS_FAILED, SMS_SENDING);
      if (!e.b) {
        char msg[80];
        snprintf(msg, sizeof msg, "Message not sent: %s", e.ptr ? (const char*)e.ptr : "error");
        notifyText(msg, th::RED);
      }
      break;
    }
    case Ev::UssdReply:
      s_ussdWaiting = false;
      s_ussdExpectsReply = e.a == 1;
      txt::copy(s_ussdText, e.ptr ? (const char*)e.ptr : "", sizeof s_ussdText);
      app::wake();
      if (nav::current() != ScreenId::Ussd) nav::push(ScreenId::Ussd, NavParam::num(1));
      break;
    case Ev::SimContacts: {
      auto* list = static_cast<std::vector<Contact>*>(e.ptr);
      if (!list) break;
      int added = contacts::importList(*list);
      char msg[64];
      if (list->empty()) snprintf(msg, sizeof msg, "No contacts found on the SIM");
      else snprintf(msg, sizeof msg, "Imported %d of %u SIM contacts", added, (unsigned)list->size());
      notifyText(msg, th::GREEN);
      break;
    }
    case Ev::ModemNotice:
      if (e.ptr) notifyText((const char*)e.ptr, th::AMBER);
      break;
    case Ev::BtChanged: {
      Settings& s = settings::get();
      uint8_t addr[6];
      if (bt::connected() && bt::peerAddress(addr)) {
        if (!s.hasPeer || memcmp(s.peer, addr, 6) != 0 || strcmp(s.peerName, bt::peerName()) != 0) {
          memcpy(s.peer, addr, 6);
          s.hasPeer = 1;
          txt::copy(s.peerName, bt::peerName(), sizeof s.peerName);
          app::saveSettingsSoon();
        }
        if (e.a == 1) {
          char msg[64];
          snprintf(msg, sizeof msg, "%s connected", bt::peerName());
          notifyText(msg, th::INDIGO);
        }
      } else if (e.a == 0 && s.hasPeer) {
        notifyText("Earbuds disconnected", th::MUTED);
      }
      break;
    }
    case Ev::BtAudio:
      if (e.a) {
        bool test = audio::testMode() != audio::TestMode::None;
        if (!test && callBusy(s_call) && !wantEarbuds()) {
          bt::closeAudio();  // user chose wired audio
        } else {
          audio::start((uint32_t)e.b);
        }
      } else {
        audio::stop();
      }
      break;
    case Ev::BtAnswer:
      if (s_call.phase == CallPhase::Incoming) app::answer();
      break;
    case Ev::BtHangup:
      if (s_call.phase != CallPhase::Idle) app::hangup();
      break;
    case Ev::BtRedial:
      if (s_call.phase == CallPhase::Idle && calllog::lastDialed()[0]) app::dial(calllog::lastDialed());
      break;
    case Ev::BtDial:
      if (e.ptr && s_call.phase == CallPhase::Idle) app::dial((const char*)e.ptr);
      break;
    case Ev::BtVolume:
      if (e.a == 0) bt::noteSpeakerVolume(e.b);
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
void superviseAudio(uint32_t now) {
  bool test = audio::testMode() != audio::TestMode::None;
  if (callBusy(s_call)) {
    if (wantEarbuds() && !bt::audioOpen()) {
      if (!s_audioRequested && (int32_t)(now - s_phaseSinceMs) > 1500) {
        LOG("AUDIO", "opening earbud voice link");
        bt::openAudio();
        s_audioRequested = true;
        s_audioRequestMs = now;
      } else if (s_audioRequested && (int32_t)(now - s_audioRequestMs) > 6000) {
        s_audioRequested = false;  // retry
      }
    } else if (!wantEarbuds() && bt::audioOpen() && !test) {
      bt::closeAudio();
    }
  } else if (s_call.phase == CallPhase::Idle && bt::audioOpen() && !test && (int32_t)(now - s_callEndedMs) > 2000) {
    bt::closeAudio();
  }
}

void superviseScreen(uint32_t now) {
  if (display::asleep()) return;
  uint16_t timeout = settings::get().screenTimeoutS;
  ScreenId cur = nav::current();
  // Never switch the screen off during a call (ringing, dialing or talking) - the
  // buttons to answer / end must stay visible.
  bool keepOn = s_call.phase != CallPhase::Idle || cur == ScreenId::Boot || cur == ScreenId::Calibrate || ui.in.down;
  if (keepOn) s_lastActivityMs = now;
  // Signed difference: wake() stamps millis() *after* the loop read "now", and an
  // unsigned subtraction would wrap to a huge value and switch the screen off at once.
  if (!timeout || (int32_t)(now - s_lastActivityMs) < (int32_t)timeout * 1000) return;
  LOG("APP", "screen off");
  if (s_call.phase == CallPhase::Idle && cur != ScreenId::Lock) nav::push(ScreenId::Lock);
  display::sleep(true);
}

// The T-Call's blue LED doubles as a notification light:
// fast blink = incoming call, short blip every 3 s = unread messages / missed calls.
void superviseLed(uint32_t now) {
  static bool init = false, lastOn = false;
  if (!init) {
    pinMode(PIN_BOARD_LED, OUTPUT);
    init = true;
  }
  bool on = false;
  if (s_call.phase == CallPhase::Incoming) on = (now / 200) % 2;
  else if (messages::unreadCount() || calllog::missedUnseen()) on = (now % 3000) < 80;
  if (on != lastOn) {
    digitalWrite(PIN_BOARD_LED, on ? HIGH : LOW);
    lastOn = on;
  }
}

void drawCallBanner() {
  if (s_call.phase == CallPhase::Idle || nav::current() == ScreenId::Call) return;
  auto& g = *ui.g;
  g.fillRect(0, th::STATUS_H, th::W, 22, th::GREEN);
  char msg[48], d[16];
  if (s_call.phase == CallPhase::Active) {
    txt::fmtDuration(app::callSeconds(), d);
    snprintf(msg, sizeof msg, "Tap to return to call  %s", d);
  } else {
    txt::copy(msg, s_call.phase == CallPhase::Incoming ? "Incoming call - tap to answer" : "Calling... tap to return", sizeof msg);
  }
  ui.text(msg, th::W / 2, th::STATUS_H + 11, th::fSmall(), th::WHITE, lgfx::textdatum_t::middle_center);
}

void render(uint32_t now) {
  lgfx::LovyanGFX& g = display::gfx();
  bool modal = ui.modalOpen();
  ui.beginFrame(&g, s_swallowTouch ? TouchSample() : s_touch, now, !modal);
  // The "return to call" banner sits on top of the header, so it gets the tap first.
  if (s_call.phase != CallPhase::Idle && nav::current() != ScreenId::Call &&
      ui.clickIn(Rect(0, th::STATUS_H, th::W, 22))) {
    nav::push(ScreenId::Call);
  }
  uint32_t gen = nav::generation();
  Screen* sc = nav::screen();
  if (sc) sc->frame(ui);
  for (int k = 0; k < 3 && nav::generation() != gen; ++k) {
    // navigated during the frame: draw the new screen straight away (no input edges)
    gen = nav::generation();
    ui.beginFrame(&g, s_swallowTouch ? TouchSample() : s_touch, now, !ui.modalOpen());
    sc = nav::screen();
    if (sc) sc->frame(ui);
  }
  drawCallBanner();
  ui.drawOverlays();
  display::present();
  s_lastFrameMs = now;
  s_dirty = false;
}

}  // namespace

// ===========================================================================
namespace app {

void setup() {
  Serial.begin(115200);
  logInit();
  delay(50);
  LOG("APP", "%s %s starting", FW_NAME, FW_VERSION);
  setenv("TZ", "UTC0", 1);  // we keep local time in the clock directly
  tzset();
  events::init();
  settings::load();
  storage::begin();
  contacts::load();
  calllog::load();
  messages::load();
  power::begin();

  Settings& s = settings::get();
  display::begin(s.brightness, s.tftRotation <= 7 ? s.tftRotation : TFT_ROTATION);
  if (s.touchCalValid) touch::setCalibration(s.cal);
  screens::registerAll();
  nav::reset(ScreenId::Boot);
  render(millis());

  audio::begin();
  audio::setGains(s.btSpkStep, s.btMicStep);
  bt::begin(s.btEnabled);
  modem::begin(s.callVolume, s.ringerVolume, s.modemMicGain);
  console::begin();
  s_lastActivityMs = millis();
  LOG("APP", "free internal RAM %u KB", (unsigned)(ESP.getFreeHeap() / 1024));
}

void loop() {
  uint32_t now = millis();

  AppEvent e;
  for (int budget = 0; budget < 16 && events::receive(&e, 0); ++budget) {
    handleEvent(e);
    Screen* cur = nav::screen();
    if (cur) cur->onEvent(e);
    Screen* callScreen = nav::get(ScreenId::Call);
    if (callScreen && callScreen != cur && (e.type == Ev::CallEnded || e.type == Ev::CallChanged)) callScreen->onEvent(e);
    events::release(e);
    markDirty();
  }

  if (now - s_lastPollMs >= UI_TOUCH_POLL_MS) {
    s_lastPollMs = now;
    TouchSample t = touch::read();
    if (display::asleep()) {
      if (t.down && !s_touch.down) {
        wake();
        s_swallowTouch = true;
      }
    } else {
      if (t.down) s_lastActivityMs = now;
      if (s_swallowTouch && !t.down && !s_touch.down) s_swallowTouch = false;
      if (t.down != s_touch.down || (t.down && (t.x != s_touch.x || t.y != s_touch.y))) markDirty();
    }
    s_touch = t;
  }

  console::poll();
  bt::service(now);
  power::service(now);
  superviseAudio(now);
  superviseScreen(now);
  superviseLed(now);

  bool quiet = s_call.phase == CallPhase::Idle && !s_touch.down;
  contacts::service(now, quiet);
  calllog::service(now, quiet);
  messages::service(now, quiet);
  if (s_settingsDirty && quiet && (int32_t)(now - s_settingsChangedMs) > 2000) {
    settings::save();
    s_settingsDirty = false;
  }

  bool onBtScreen = nav::current() == ScreenId::Bluetooth;
  if (onBtScreen != s_discoverable) {
    bt::setDiscoverable(onBtScreen);
    s_discoverable = onBtScreen;
  }

  if (!display::asleep()) {
    Screen* sc = nav::screen();
    bool anim = (sc && sc->animating()) || ui.toastVisible();
    uint32_t since = now - s_lastFrameMs;
    // While the stylus is down keep drawing so long-press / key-repeat timers advance.
    if ((s_dirty && since >= 16) || (anim && since >= 80) || (s_touch.down && since >= 50) || since >= UI_IDLE_FRAME_MS)
      render(now);
  }
  vTaskDelay(pdMS_TO_TICKS(4));
}

// ---------------------------------------------------------------------------
const NetStatus& net() { return s_net; }
const CallInfo& call() { return s_call; }
int battery() { return s_batt; }
bool charging() { return power::charging(); }
bool clockValid() { return time(nullptr) > (time_t)kValidEpoch; }
uint32_t nowLocal() { return clockValid() ? (uint32_t)time(nullptr) : 0; }
PhoneState phoneState() { return s_phoneState; }
uint32_t uptimeS() { return millis() / 1000; }

void dial(const char* number) {
  char clean[NUMBER_MAX];
  txt::sanitizeDial(number, clean, sizeof clean);
  if (!clean[0]) return;
  if (txt::isUssdCode(clean)) {
    ussd(clean);
    nav::push(ScreenId::Ussd, NavParam::num(1));
    return;
  }
  LOG("CALL", "dial %s", logNum(clean));
  modem::dial(clean);
}

void answer() {
  LOG("CALL", "answer pressed");
  modem::answer();
}
void hangup() {
  LOG("CALL", "hang up pressed");
  modem::hangup();
}

void rejectWithSms(const char* text) {
  txt::copy(s_rejectNumber, s_call.number, sizeof s_rejectNumber);
  txt::copy(s_rejectText, text, sizeof s_rejectText);
  modem::hangup();
}

void toggleMute() { modem::mute(!s_call.muted); }
void dtmf(char key) { modem::dtmf(key); }

bool earbudsInUse() { return wantEarbuds(); }

void setAudioRoute(AudioRoute r) {
  settings::get().audioRoute = (uint8_t)r;
  saveSettingsSoon();
  if (!callBusy(s_call)) return;
  if (r == AudioRoute::Wired) bt::closeAudio();
  else if (bt::connected()) {
    bt::openAudio();
    s_audioRequested = true;
    s_audioRequestMs = millis();
  }
}

void changeCallVolume(int delta) {
  if (earbudsInUse()) {
    bt::setSpeakerVolume(bt::speakerVolume() + delta);
  } else {
    Settings& s = settings::get();
    int v = s.callVolume + delta * 10;
    s.callVolume = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
    modem::setVolume(s.callVolume);
    saveSettingsSoon();
  }
}

int callVolumePercent() { return earbudsInUse() ? bt::speakerVolume() * 100 / 15 : settings::get().callVolume; }

uint32_t callSeconds() { return s_call.everActive ? (millis() - s_call.activeMs) / 1000 : 0; }

uint32_t sendSms(const char* number, const char* text) {
  SmsRecord* r = new SmsRecord();
  memset(r, 0, sizeof *r);
  txt::copy(r->number, number, sizeof r->number);
  txt::copy(r->text, text, sizeof r->text);
  r->flags = SMS_OUT | SMS_SENDING;
  r->ts = nowLocal();
  uint32_t id = messages::add(*r);
  delete r;
  if (!id) return 0;
  if (!modem::sendSms(id, number, text)) messages::setFlags(id, SMS_FAILED, SMS_SENDING);
  return id;
}

void resendSms(uint32_t id) {
  SmsRecord* r = messages::byId(id);
  if (!r) return;
  messages::setFlags(id, SMS_SENDING, SMS_FAILED);
  if (!modem::sendSms(id, r->number, r->text)) messages::setFlags(id, SMS_FAILED, SMS_SENDING);
}

void ussd(const char* code) {
  s_ussdWaiting = true;
  s_ussdExpectsReply = false;
  s_ussdText[0] = 0;
  modem::ussd(code);
}

void ussdReply(const char* text) { ussd(text); }

void ussdCancel() {
  modem::ussdCancel();
  s_ussdWaiting = false;
  s_ussdExpectsReply = false;
}

const char* ussdText() { return s_ussdText; }
bool ussdWaiting() { return s_ussdWaiting; }
bool ussdExpectsReply() { return s_ussdExpectsReply; }

void importSimContacts() {
  modem::readSimContacts();
  notifyText("Reading SIM phone book...", th::ACCENT);
}

void setAudioTest(audio::TestMode m) {
  audio::setTestMode(m);
  if (m != audio::TestMode::None) bt::openAudio();
  else if (s_call.phase == CallPhase::Idle) bt::closeAudio();
}

void applyAudioSettings() {
  Settings& s = settings::get();
  audio::setGains(s.btSpkStep, s.btMicStep);
  saveSettingsSoon();
}

void lockNow() {
  if (nav::current() != ScreenId::Lock) nav::push(ScreenId::Lock);
  display::sleep(true);
}

void wake() {
  s_lastActivityMs = millis();
  if (display::asleep()) {
    display::sleep(false);
    markDirty();
  }
}

void factoryReset() {
  LOG("APP", "factory reset");
  contacts::clearAll();
  messages::clearAll();
  calllog::clearAll();
  bt::forget();
  settings::factoryReset();
  storage::formatAll();
  delay(300);
  ESP.restart();
}

void saveSettingsSoon() {
  s_settingsDirty = true;
  s_settingsChangedMs = millis();
}

}  // namespace app
