#include "phone_app.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <vector>

#include "battery.h"
#include "board_config.h"
#include "board_init.h"
#include "bt_client.h"
#include "call_log.h"
#include "console.h"
#include "contacts.h"
#include "display.h"
#include "events.h"
#include "gui.h"
#include "hotspot.h"
#include "messages.h"
#include "modem.h"
#include "plog.h"
#include "ringer.h"
#include "s3_config.h"
#include "screens.h"
#include "settings.h"
#include "storage.h"
#include "text_util.h"
#include "touch.h"

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
DataStatus s_data;
PhoneState s_phoneState = PhoneState::Boot;
battery::Estimator s_batt;
bool s_lowBattWarned = false;

bool s_dirty = true;
uint32_t s_lastFrameMs = 0, s_lastPollMs = 0, s_lastActivityMs = 0;
TouchSample s_touch;          // latest debounced sample
bool s_swallowTouch = false;  // the press that woke the screen does nothing else

bool s_settingsDirty = false;
uint32_t s_settingsChangedMs = 0;

char s_ussdText[USSD_TEXT_MAX] = "";
bool s_ussdWaiting = false, s_ussdExpectsReply = false;

AudioRoute s_route = AudioRoute::Handset;
uint32_t s_callEndedMs = 0;
bool s_discoverable = false;

char s_rejectNumber[NUMBER_MAX] = "";
char s_rejectText[80] = "";

// Home button (BOOT): short press = wake / home, long press = lock
bool s_homeDown = false;
uint32_t s_homeDownMs = 0;
bool s_homeLongDone = false;

bool callBusy(const CallInfo& c) {
  return c.phase == CallPhase::Dialing || c.phase == CallPhase::Alerting || c.phase == CallPhase::Active;
}

AudioPref pref() { return (AudioPref)settings::get().audioPref; }

void markDirty() { s_dirty = true; }

void notifyText(const char* text, uint32_t color, std::function<void()> onTap = nullptr) {
  ui.toast(text, color, onTap);
  markDirty();
}

// Pushes the route to the helper (SCO) and the modem (handset / speaker tuning).
void applyRoute(AudioRoute r, const char* why) {
  if (r == AudioRoute::Earbuds && !bt::connected()) r = AudioRoute::Handset;
  if (r != s_route) LOG("AUDIO", "call audio: %s (%s)", r == AudioRoute::Earbuds ? "earbuds" : r == AudioRoute::Speaker ? "speaker" : "handset", why);
  s_route = r;
  bt::setRoute(r == AudioRoute::Earbuds);
  modem::setAudioProfile(r);
  markDirty();
}

AudioRoute routeForNewCall() {
  if (pref() == AudioPref::Handset || !bt::connected()) return AudioRoute::Handset;
  return AudioRoute::Earbuds;  // Auto and Earbuds both prefer connected earbuds
}

// ---------------------------------------------------------------------------
void onCallEnded(const CallInfo& c, CallEndReason reason) {
  uint32_t dur = c.everActive ? (plogMillis() - c.activeMs) / 1000 : 0;
  CallType type = CallType::Outgoing;
  if (c.incoming) type = c.everActive ? CallType::Incoming : reason == CallEndReason::Rejected ? CallType::Rejected : CallType::Missed;
  uint32_t now = app::nowLocal();
  calllog::add(c.number, type, now ? now - dur : 0, (uint16_t)(dur > 65535 ? 65535 : dur));
  s_callEndedMs = plogMillis();
  ringer::stop();
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
  applyRoute(AudioRoute::Handset, "call ended");
}

void onNetChanged() {
  s_net = modem::status();
  PhoneState prev = s_phoneState;
  s_phoneState = modem::phoneState();
  static int lastPct = -2;
  int pct = s_batt.update(s_net.battMv);
  if (pct != lastPct) {
    lastPct = pct;
    events::post(Ev::BatteryChanged, pct, s_net.battMv);
  }
  bt::onNetStatus(s_net, pct);
  if (s_phoneState == PhoneState::SimLocked && prev != PhoneState::SimLocked && nav::current() != ScreenId::Boot &&
      nav::current() != ScreenId::SimPin)
    nav::push(ScreenId::SimPin);
  if (pct >= 0 && pct <= 10 && !s_batt.likelyCharging()) {
    if (!s_lowBattWarned) notifyText("Battery low - please charge", th::RED);
    s_lowBattWarned = true;
  } else if (pct > 20) {
    s_lowBattWarned = false;
  }
}

void handleEvent(AppEvent& e) {
  switch (e.type) {
    case Ev::NetChanged:
      onNetChanged();
      break;
    case Ev::CallChanged: {
      CallInfo now = modem::call();
      CallInfo before = s_call;
      s_call = now;
      bt::onPhoneState(now, before);
      if (now.phase != CallPhase::Idle && before.phase == CallPhase::Idle) {
        app::wake();
        applyRoute(routeForNewCall(), "new call");
        if (nav::current() != ScreenId::Call) nav::push(ScreenId::Call);
      }
      // Ring on the phone unless the earbuds (which ring themselves) take the call
      if (now.phase == CallPhase::Incoming && settings::get().ringerOn && !bt::connected()) ringer::play(ringer::Pattern::Ring);
      if (now.phase != CallPhase::Incoming && ringer::ringing()) ringer::stop();
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
      if (settings::get().ringerOn && s_call.phase == CallPhase::Idle) ringer::play(ringer::Pattern::Message);
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
    case Ev::SmsSent:
      messages::setFlags((uint32_t)e.a, e.b ? 0 : SMS_FAILED, SMS_SENDING);
      if (!e.b) {
        char msg[80];
        snprintf(msg, sizeof msg, "Message not sent: %s", e.ptr ? (const char*)e.ptr : "error");
        notifyText(msg, th::RED);
      }
      break;
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
    case Ev::DataChanged: {
      DataState before = s_data.state;
      s_data = modem::data();
      if (before != DataState::Connected && s_data.state == DataState::Connected && nav::current() != ScreenId::Boot)
        notifyText("Mobile data connected", th::GREEN);
      break;
    }
    case Ev::HotspotChanged:
      break;
    case Ev::BtHelper:
      if (!e.a) notifyText("Bluetooth helper not responding", th::AMBER);
      else bt::setGains(settings::get().btSpkStep, settings::get().btMicStep);
      break;
    case Ev::BtChanged:
      if (e.a == 1) {
        char msg[64];
        snprintf(msg, sizeof msg, "%s connected", bt::peerName());
        notifyText(msg, th::INDIGO);
        // Earbuds joined during a call: move the audio there (Auto / Earbuds preference)
        if (callBusy(s_call) && pref() != AudioPref::Handset) applyRoute(AudioRoute::Earbuds, "earbuds connected");
        bt::setSpeakerVolume(settings::get().earbudVolume);
      } else if (e.a == 0) {
        // Earbuds gone (or the whole helper): never leave a call without audio
        if (bt::helperOnline()) notifyText("Earbuds disconnected", th::MUTED);
        if (s_route == AudioRoute::Earbuds) applyRoute(AudioRoute::Handset, "earbuds disconnected");
      }
      break;
    case Ev::BtAudioToEarbuds:
      // The user moved the call audio with the earbuds' own controls
      if (s_call.phase != CallPhase::Idle) applyRoute(e.a ? AudioRoute::Earbuds : AudioRoute::Handset, "earbud controls");
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
      if (e.a == 0) {
        bt::noteSpeakerVolume(e.b);
        settings::get().earbudVolume = (uint8_t)e.b;
        app::saveSettingsSoon();
      }
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
void superviseScreen(uint32_t now) {
  if (display::asleep()) return;
  uint16_t timeout = settings::get().screenTimeoutS;
  ScreenId cur = nav::current();
  bool keepOn = s_call.phase == CallPhase::Incoming || cur == ScreenId::Boot || cur == ScreenId::Calibrate ||
                cur == ScreenId::TestTouch || ui.in.down ||
                (s_call.phase != CallPhase::Idle && cur == ScreenId::Call && s_route != AudioRoute::Earbuds);
  if (keepOn) s_lastActivityMs = now;
  if (!timeout || now - s_lastActivityMs < (uint32_t)timeout * 1000) return;
  LOG("APP", "screen off");
  if (s_call.phase == CallPhase::Idle && cur != ScreenId::Lock) nav::push(ScreenId::Lock);
  display::sleep(true);
}

// On-board RGB LED as a notification light:
// green blink = incoming call, blue blip = unread messages / missed calls,
// red blip = battery low, dim cyan = hotspot on.
void superviseLed(uint32_t now) {
  static uint32_t last = 0xFFFFFFFF;
  uint32_t rgb = 0;
  if (s_call.phase == CallPhase::Incoming) rgb = (now / 200) % 2 ? 0x003000 : 0;
  else if (s_lowBattWarned) rgb = (now % 4000) < 80 ? 0x300000 : 0;
  else if (messages::unreadCount() || calllog::missedUnseen()) rgb = (now % 3000) < 80 ? 0x000030 : 0;
  else if (hotspot::running()) rgb = 0x000404;
  if (rgb != last) {
    board_led((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    last = rgb;
  }
}

void superviseHomeButton(uint32_t now) {
  bool down = board_home_pressed();
  if (down && !s_homeDown) {
    s_homeDownMs = now;
    s_homeLongDone = false;
  }
  if (down && !s_homeLongDone && now - s_homeDownMs > 1500) {
    s_homeLongDone = true;
    if (s_call.phase == CallPhase::Idle) app::lockNow();
  }
  if (!down && s_homeDown && !s_homeLongDone && now - s_homeDownMs > 40) {
    if (display::asleep()) {
      app::wake();
    } else if (s_call.phase != CallPhase::Idle) {
      if (nav::current() != ScreenId::Call) nav::push(ScreenId::Call);
    } else if (nav::current() != ScreenId::Lock && nav::current() != ScreenId::Boot) {
      nav::home();
    }
    markDirty();
  }
  s_homeDown = down;
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
  LOG("APP", "%s %s starting", FW_NAME, FW_VERSION);
  setenv("TZ", "UTC0", 1);  // we keep local time in the clock directly
  tzset();
  storage::begin();
  settings::load();
  contacts::load();
  calllog::load();
  messages::load();
  ringer::begin();

  Settings& s = settings::get();
  display::begin(s.brightness);
  if (s.touchCalValid) touch::setCalibration(s.cal);
  screens::registerAll();
  nav::reset(ScreenId::Boot);
  render(plogMillis());

  bt::begin(s.btEnabled);
  bt::setGains(s.btSpkStep, s.btMicStep);
  modem::Config mc;
  mc.speakerGain = s.modemOutGain;
  mc.micGain = s.modemMicGain;
  mc.netMode = (NetMode)s.netMode;
  mc.dataEnabled = s.dataEnabled;
  txt::copy(mc.apn, s.apn, sizeof mc.apn);
  txt::copy(mc.apnUser, s.apnUser, sizeof mc.apnUser);
  txt::copy(mc.apnPass, s.apnPass, sizeof mc.apnPass);
  modem::begin(mc);
  hotspot::begin();
  if (s.hotspotAuto) hotspot::start();
  console::begin();
  s_lastActivityMs = plogMillis();
  LOG("APP", "free RAM: internal %u KB, PSRAM %u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
      (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

void loop() {
  uint32_t now = plogMillis();

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
      if (t.down && !s_touch.down && settings::get().keyClicks && !ringer::ringing()) ringer::play(ringer::Pattern::Click);
    }
    s_touch = t;
  }

  console::poll();
  bt::service(now);
  ringer::service(now);
  superviseHomeButton(now);
  superviseScreen(now);
  superviseLed(now);

  bool quiet = s_call.phase == CallPhase::Idle && !s_touch.down;
  contacts::service(now, quiet);
  calllog::service(now, quiet);
  messages::service(now, quiet);
  if (s_settingsDirty && quiet && now - s_settingsChangedMs > 2000) {
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
const DataStatus& data() { return s_data; }
int battery() { return s_batt.percent(); }
bool charging() { return s_batt.likelyCharging(); }
bool clockValid() { return time(nullptr) > (time_t)kValidEpoch; }
uint32_t nowLocal() { return clockValid() ? (uint32_t)time(nullptr) : 0; }
PhoneState phoneState() { return s_phoneState; }
uint32_t uptimeS() { return plogMillis() / 1000; }
const TouchSample& touchSample() { return s_touch; }

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
  ringer::stop();
  modem::answer();
}

void hangup() {
  ringer::stop();
  modem::hangup();
}

void rejectWithSms(const char* text) {
  txt::copy(s_rejectNumber, s_call.number, sizeof s_rejectNumber);
  txt::copy(s_rejectText, text, sizeof s_rejectText);
  hangup();
}

void toggleMute() { modem::mute(!s_call.muted); }
void dtmf(char key) { modem::dtmf(key); }

AudioRoute audioRoute() { return s_route; }
bool earbudsInUse() { return s_route == AudioRoute::Earbuds && bt::connected(); }

void setAudioRoute(AudioRoute r) {
  if (r == AudioRoute::Earbuds && !bt::connected()) {
    notifyText(bt::helperOnline() ? "No earbuds connected" : "Bluetooth helper offline", th::AMBER);
    return;
  }
  applyRoute(r, "user");
}

void setAudioPref(AudioPref p) {
  settings::get().audioPref = (uint8_t)p;
  saveSettingsSoon();
  if (!callBusy(s_call)) return;
  if (p == AudioPref::Handset && s_route == AudioRoute::Earbuds) applyRoute(AudioRoute::Handset, "preference");
  else if (p != AudioPref::Handset && bt::connected() && s_route == AudioRoute::Handset)
    applyRoute(AudioRoute::Earbuds, "preference");
}

void changeCallVolume(int delta) {
  Settings& s = settings::get();
  if (earbudsInUse()) {
    int v = bt::speakerVolume() + delta;
    v = v < 0 ? 0 : v > 15 ? 15 : v;
    bt::setSpeakerVolume(v);
    s.earbudVolume = (uint8_t)v;
  } else {
    int v = s.modemOutGain + delta;
    s.modemOutGain = (uint8_t)(v < 0 ? 0 : v > 7 ? 7 : v);
    modem::setSpeakerGain(s.modemOutGain);
  }
  saveSettingsSoon();
}

int callVolumePercent() {
  return earbudsInUse() ? bt::speakerVolume() * 100 / 15 : settings::get().modemOutGain * 100 / 7;
}

uint32_t callSeconds() { return s_call.everActive ? (plogMillis() - s_call.activeMs) / 1000 : 0; }

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

void setDataEnabled(bool on) {
  settings::get().dataEnabled = on;
  saveSettingsSoon();
  modem::setDataEnabled(on);
}

void setApn(const char* apn, const char* user, const char* pass) {
  Settings& s = settings::get();
  txt::copy(s.apn, apn, sizeof s.apn);
  txt::copy(s.apnUser, user, sizeof s.apnUser);
  txt::copy(s.apnPass, pass, sizeof s.apnPass);
  saveSettingsSoon();
  modem::setApn(s.apn, s.apnUser, s.apnPass);
}

void setNetworkMode(NetMode m) {
  settings::get().netMode = (uint8_t)m;
  saveSettingsSoon();
  modem::setNetworkMode(m);
}

void setHotspot(bool on) {
  if (on) {
    if (!hotspot::start()) notifyText(hotspot::status().lastError, th::RED);
    else if (s_data.state != DataState::Connected) notifyText("Hotspot on - waiting for mobile data", th::AMBER);
  } else {
    hotspot::stop();
  }
  markDirty();
}

void importSimContacts() {
  modem::readSimContacts();
  notifyText("Reading SIM phone book...", th::ACCENT);
}

void setAudioTest(audio::TestMode m) { audio::setTestMode(m); }

void applyAudioSettings() {
  Settings& s = settings::get();
  bt::setGains(s.btSpkStep, s.btMicStep);
  saveSettingsSoon();
}

void lockNow() {
  if (nav::current() != ScreenId::Lock) nav::push(ScreenId::Lock);
  display::sleep(true);
}

void wake() {
  s_lastActivityMs = plogMillis();
  if (display::asleep()) {
    display::sleep(false);
    markDirty();
  }
}

void factoryReset() {
  LOG("APP", "factory reset");
  hotspot::stop();
  contacts::clearAll();
  messages::clearAll();
  calllog::clearAll();
  bt::forget();
  settings::factoryReset();
  storage::formatAll();
  vTaskDelay(pdMS_TO_TICKS(300));
  esp_restart();
}

void restart() {
  LOG("APP", "restart requested");
  settings::save();
  vTaskDelay(pdMS_TO_TICKS(200));
  esp_restart();
}

void saveSettingsSoon() {
  s_settingsDirty = true;
  s_settingsChangedMs = plogMillis();
}

}  // namespace app
