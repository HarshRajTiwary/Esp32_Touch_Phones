// Settings list and its general sub-screens: earbuds, sound, display,
// date/time and USSD codes. Network, data, hotspot: screens_net.cpp.
// Diagnostics and about: screens_diag.cpp.
#include <stdio.h>
#include <string.h>

#include "bt_client.h"
#include "display.h"
#include "keyboard.h"
#include "modem.h"
#include "phone_app.h"
#include "plog.h"
#include "screens.h"
#include "screens_internal.h"
#include "settings.h"
#include "text_util.h"

using lgfx::textdatum_t;

namespace screens {

void settingsRowIcon(Gui& ui, int x, int cy, Icon icon, uint32_t color) {
  ui.g->fillSmoothRoundRect(x, cy - 14, 28, 28, 8, color);
  drawIcon(*ui.g, icon, x + 14, cy, 16, th::WHITE, color);
}

}  // namespace screens

// ===========================================================================
class SettingsScreen : public Screen {
  ScrollState scroll_;

 public:
  void enter(const NavParam&) override { scroll_ = ScrollState(); }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Settings")) {
      nav::back();
      return;
    }
    struct Item { Icon icon; uint32_t color; const char* label; };
    static const Item kItems[] = {
        {Icon::Antenna, th::GREEN, "Network & SIM"},
        {Icon::Data, th::PINK, "Mobile data & APN"},
        {Icon::Wifi, th::TEAL, "Hotspot"},
        {Icon::Headphones, th::INDIGO, "Bluetooth earbuds"},
        {Icon::VolUp, th::TEAL, "Sound & call audio"},
        {Icon::Sun, th::AMBER, "Display"},
        {Icon::Balance, th::ORANGE, "Balance & USSD"},
        {Icon::Sim, th::PINK, "Import SIM contacts"},
        {Icon::Clock, th::PURPLE, "Date & time"},
        {Icon::Touch, th::SLATE, "Touch calibration"},
        {Icon::Chip, th::AMBER, "Diagnostics"},
        {Icon::Info, th::ACCENT, "About"},
        {Icon::Reset, th::RED, "Factory reset"},
    };
    const int n = sizeof kItems / sizeof kItems[0], rowH = 44;
    Rect view(0, th::CONTENT_Y, 240, th::H - th::CONTENT_Y);
    int y0 = ui.beginScroll(scroll_, view, n * rowH);
    for (int k = 0; k < n; ++k) {
      int y = y0 + k * rowH;
      if (y + rowH < view.y || y > view.bottom()) continue;
      Rect row(0, y, 240, rowH);
      bool open = ui.listRow(row);
      screens::settingsRowIcon(ui, 12, y + rowH / 2, kItems[k].icon, kItems[k].color);
      ui.textFit(kItems[k].label, 52, y + rowH / 2, 160, th::fBody(), k == n - 1 ? th::RED : th::TEXT,
                 textdatum_t::middle_left);
      drawIcon(g, Icon::Chevron, 226, y + rowH / 2, 16, th::DIM);
      if (!open) continue;
      switch (k) {
        case 0: nav::push(ScreenId::Network); break;
        case 1: nav::push(ScreenId::Data); break;
        case 2: nav::push(ScreenId::Hotspot); break;
        case 3: nav::push(ScreenId::Bluetooth); break;
        case 4: nav::push(ScreenId::Sound); break;
        case 5: nav::push(ScreenId::Display); break;
        case 6: nav::push(ScreenId::Ussd); break;
        case 7:
          ui.confirm("Import SIM contacts?", "Copies the phone book stored on your SIM card. Duplicates are skipped.",
                     "Import", th::ACCENT, [] { app::importSimContacts(); });
          break;
        case 8: nav::push(ScreenId::DateTime); break;
        case 9: nav::push(ScreenId::Calibrate); break;
        case 10: nav::push(ScreenId::Diagnostics); break;
        case 11: nav::push(ScreenId::About); break;
        case 12:
          ui.confirm("Factory reset?", "Deletes ALL contacts, messages, call history and settings, then restarts.",
                     "Erase", th::RED, [] { app::factoryReset(); });
          break;
      }
    }
    ui.endScroll(scroll_, view);
  }
};

// ===========================================================================
class BluetoothScreen : public Screen {
  ScrollState scroll_;
  bt::Device devs_[16];
  int n_ = 0;
  bt::Device bonded_[8];
  int nb_ = 0;

 public:
  void enter(const NavParam&) override {
    scroll_ = ScrollState();
    n_ = bt::devices(devs_, 16);
    nb_ = bt::bonded(bonded_, 8);
  }
  void onEvent(const AppEvent& e) override {
    if (e.type == Ev::BtScanResult || e.type == Ev::BtScanDone) n_ = bt::devices(devs_, 16);
    if (e.type == Ev::BtChanged) nb_ = bt::bonded(bonded_, 8);
  }
  bool animating() const override { return bt::scanning() || bt::state() == bt::State::Connecting; }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Earbuds")) {
      nav::back();
      return;
    }
    Settings& s = settings::get();
    if (!bt::helperOnline()) {
      drawIcon(g, Icon::Bluetooth, 120, 110, 36, th::DIM);
      ui.text("Bluetooth helper offline", 120, 150, th::fBodyB(), th::AMBER, textdatum_t::middle_center);
      static const char* const kHelp[] = {"The ESP32 board that talks to", "the earbuds does not answer.",
                                          "Check its power and the three", "link wires (TX, RX, GND)."};
      for (int k = 0; k < 4; ++k) ui.text(kHelp[k], 120, 176 + k * 16, th::fSmall(), th::MUTED, textdatum_t::middle_center);
      if (ui.button(Rect(40, 250, 160, 34), "Reset helper", th::SURFACE2)) bt::rebootHelper();
      return;
    }
    if (ui.toggle(Rect(0, th::CONTENT_Y, 240, 40), "Bluetooth", bt::enabled())) {
      s.btEnabled = !bt::enabled();
      bt::setEnabled(s.btEnabled);
      app::saveSettingsSoon();
    }
    int y = th::CONTENT_Y + 44;
    // current earbuds card
    Rect card(8, y, 224, 50);
    g.fillSmoothRoundRect(card.x, card.y, card.w, card.h, 10, th::SURFACE);
    bool conn = bt::connected();
    g.fillSmoothCircle(card.x + 25, card.cy(), 17, conn ? th::INDIGO : th::SURFACE2);
    drawIcon(g, Icon::Headphones, card.x + 25, card.cy(), 20, th::WHITE, conn ? th::INDIGO : th::SURFACE2);
    char peer[40];
    txt::toDisplay(bt::peerName()[0] ? bt::peerName() : "No earbuds paired", peer, sizeof peer);
    ui.textFit(peer, card.x + 50, card.y + 16, 164, th::fBodyB(), th::TEXT, textdatum_t::middle_left);
    char st[48];
    if (conn && bt::peerBattery() >= 0) snprintf(st, sizeof st, "%s  |  %d%%", bt::stateText(), bt::peerBattery());
    else txt::copy(st, bt::stateText(), sizeof st);
    uint32_t sc = conn ? th::GREEN : bt::state() == bt::State::Connecting ? th::AMBER : th::MUTED;
    ui.textFit(st, card.x + 50, card.y + 35, 164, th::fSmall(), sc, textdatum_t::middle_left);
    y += 56;

    uint8_t addr[6];
    bool hasPeer = bt::peerAddress(addr);
    if (hasPeer && bt::enabled()) {
      if (conn) {
        if (ui.button(Rect(8, y, 108, 32), "Disconnect", th::SURFACE2)) bt::disconnect();
      } else {
        if (ui.button(Rect(8, y, 108, 32), "Connect", th::ACCENT, th::WHITE)) bt::connectSaved();
      }
      if (ui.button(Rect(124, y, 108, 32), "Forget", th::SURFACE2, th::RED)) {
        ui.confirm("Forget earbuds?", "You will need to pair them again.", "Forget", th::RED, [] { bt::forget(); });
      }
      y += 38;
    }
    if (!bt::enabled()) return;

    Rect scanBtn(8, y, 224, 32);
    if (bt::scanning()) {
      if (ui.button(scanBtn, "Searching...  (tap to stop)", th::SURFACE2, th::TEXT, th::fSmall())) bt::stopScan();
      ui.spinner(scanBtn.x + 18, scanBtn.cy(), 9, th::ACCENT);
    } else if (ui.button(scanBtn, "Search for earbuds", th::ACCENT, th::WHITE, nullptr, 10, Icon::Search)) {
      bt::startScan();
      n_ = 0;
    }
    y += 36;
    Rect view(0, y, 240, th::H - y);
    // Found devices first, then other remembered (paired) devices
    int total = n_ + nb_;
    if (!total) {
      static const char* const kHint[] = {"Put your earbuds in pairing mode:", "take them out of the case, then hold",
                                          "the touch area / button for ~5 s", "until the light flashes. Then Search."};
      for (int k = 0; k < 4; ++k)
        ui.text(kHint[k], 120, y + 14 + k * 16, th::fSmall(), th::DIM, textdatum_t::middle_center);
      return;
    }
    const int rowH = 38;
    int y0 = ui.beginScroll(scroll_, view, total * rowH + (nb_ ? 20 : 0));
    for (int k = 0; k < total; ++k) {
      bool paired = k >= n_;
      int ry = y0 + k * rowH + (paired ? 20 : 0);
      if (paired && k == n_) ui.text("Paired before", 12, ry - 10, th::fTiny(), th::MUTED, textdatum_t::middle_left);
      if (ry + rowH < view.y || ry > view.bottom()) continue;
      const bt::Device& d = paired ? bonded_[k - n_] : devs_[k];
      Rect row(0, ry, 240, rowH);
      bool tap = ui.listRow(row);
      drawIcon(g, d.audio ? Icon::Headphones : Icon::Bluetooth, 22, ry + rowH / 2, 16, d.audio ? th::TEAL : th::MUTED);
      char name[40];
      if (d.name[0]) txt::toDisplay(d.name, name, sizeof name);
      else bt::formatAddress(d.bda, name);
      ui.textFit(name, 42, ry + rowH / 2, 150, th::fBody(), th::TEXT, textdatum_t::middle_left);
      if (!paired) {
        int bars = d.rssi >= -60 ? 3 : d.rssi >= -75 ? 2 : d.rssi > -127 ? 1 : 0;
        for (int b = 0; b < 3; ++b)
          g.fillRect(212 + b * 5, ry + 24 - (b + 1) * 4, 3, (b + 1) * 4, b < bars ? th::TEXT : th::DIM);
      }
      if (tap) {
        bt::Device dev = d;
        if (paired) {
          static const char* opts[] = {"Connect", "Forget this device"};
          ui.sheet(name, opts, 2, [dev](int o) {
            if (o == 0) bt::connect(dev.bda, dev.name);
            else bt::forgetDevice(dev.bda);
          });
        } else {
          bt::connect(d.bda, d.name);
          char msg[64];
          snprintf(msg, sizeof msg, "Connecting to %s...", name);
          ui.toast(msg, th::INDIGO);
        }
      }
    }
    ui.endScroll(scroll_, view);
  }
};

// ===========================================================================
class SoundScreen : public Screen {
  ScrollState scroll_;

 public:
  void enter(const NavParam&) override { scroll_ = ScrollState(); }
  bool animating() const override { return audio::running(); }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Sound")) {
      if (audio::testMode() != audio::TestMode::None) app::setAudioTest(audio::TestMode::None);
      nav::back();
      return;
    }
    Settings& s = settings::get();
    Rect view(0, th::CONTENT_Y, 240, th::H - th::CONTENT_Y);
    int y = ui.beginScroll(scroll_, view, 560);
    char v[16];
    bool minus, plus;

    ui.text("Calls use", 12, y + 12, th::fSmall(), th::MUTED, textdatum_t::middle_left);
    static const char* const kPrefs[3] = {"Auto", "Earbuds", "Handset"};
    int r = ui.segmented(Rect(8, y + 24, 224, 30), kPrefs, 3, s.audioPref);
    if (r != s.audioPref) app::setAudioPref((AudioPref)r);
    ui.text("Auto = earbuds when connected", 12, y + 64, th::fTiny(), th::DIM, textdatum_t::middle_left);
    y += 76;

    snprintf(v, sizeof v, "%d", s.modemOutGain);
    if (ui.stepper(Rect(0, y, 240, 40), "Call volume", v, &minus, &plus)) {
      if (minus && s.modemOutGain > 0) s.modemOutGain--;
      if (plus && s.modemOutGain < 7) s.modemOutGain++;
      modem::setSpeakerGain(s.modemOutGain);
      app::saveSettingsSoon();
    }
    y += 40;
    snprintf(v, sizeof v, "%d", s.modemMicGain);
    if (ui.stepper(Rect(0, y, 240, 40), "Mic level", v, &minus, &plus)) {
      if (minus && s.modemMicGain > 0) s.modemMicGain--;
      if (plus && s.modemMicGain < 7) s.modemMicGain++;
      modem::setMicGain(s.modemMicGain);
      app::saveSettingsSoon();
    }
    y += 40;
    snprintf(v, sizeof v, "%d", s.earbudVolume);
    if (ui.stepper(Rect(0, y, 240, 40), "Earbud volume", v, &minus, &plus)) {
      if (minus && s.earbudVolume > 0) s.earbudVolume--;
      if (plus && s.earbudVolume < 15) s.earbudVolume++;
      bt::setSpeakerVolume(s.earbudVolume);
      app::saveSettingsSoon();
    }
    y += 40;
    snprintf(v, sizeof v, "%d", s.btSpkStep);
    if (ui.stepper(Rect(0, y, 240, 40), "Bridge level", v, &minus, &plus)) {
      if (minus && s.btSpkStep > 0) s.btSpkStep--;
      if (plus && s.btSpkStep < 15) s.btSpkStep++;
      app::applyAudioSettings();
    }
    y += 40;
    snprintf(v, sizeof v, "%d", s.btMicStep);
    if (ui.stepper(Rect(0, y, 240, 40), "Bridge mic", v, &minus, &plus)) {
      if (minus && s.btMicStep > 0) s.btMicStep--;
      if (plus && s.btMicStep < 15) s.btMicStep++;
      app::applyAudioSettings();
    }
    y += 44;
    if (ui.toggle(Rect(0, y, 240, 40), "Ringtone on the buzzer", s.ringerOn)) {
      s.ringerOn = !s.ringerOn;
      app::saveSettingsSoon();
    }
    y += 40;
    if (ui.toggle(Rect(0, y, 240, 40), "Touch clicks", s.keyClicks)) {
      s.keyClicks = !s.keyClicks;
      app::saveSettingsSoon();
    }
    y += 48;

    ui.text("Earbud test (no call needed)", 12, y + 8, th::fSmall(), th::MUTED, textdatum_t::middle_left);
    y += 20;
    audio::TestMode tm = audio::testMode();
    bool canTest = bt::connected() && app::call().phase == CallPhase::Idle;
    if (!canTest) {
      ui.text(bt::connected() ? "Not available during a call" : "Connect earbuds first", 12, y + 14, th::fSmall(), th::DIM,
              textdatum_t::middle_left);
      y += 34;
    } else {
      bool tone = tm == audio::TestMode::Tone, loop = tm == audio::TestMode::MicLoopback;
      if (ui.button(Rect(8, y, 108, 32), tone ? "Stop tone" : "Test tone", tone ? th::AMBER : th::SURFACE2,
                    tone ? th::BG : th::TEXT))
        app::setAudioTest(tone ? audio::TestMode::None : audio::TestMode::Tone);
      if (ui.button(Rect(124, y, 108, 32), loop ? "Stop echo" : "Mic echo", loop ? th::AMBER : th::SURFACE2,
                    loop ? th::BG : th::TEXT))
        app::setAudioTest(loop ? audio::TestMode::None : audio::TestMode::MicLoopback);
      y += 40;
    }
    audio::Stats st = audio::stats();
    ui.text("To earbuds", 12, y + 10, th::fSmall(), th::MUTED, textdatum_t::middle_left);
    ui.meter(Rect(96, y + 5, 132, 10), st.running ? st.downDb : -96, th::GREEN);
    y += 22;
    ui.text("Earbud mic", 12, y + 10, th::fSmall(), th::MUTED, textdatum_t::middle_left);
    ui.meter(Rect(96, y + 5, 132, 10), st.running ? st.upDb : -96, th::ACCENT);
    y += 22;
    char info[64];
    if (st.running)
      snprintf(info, sizeof info, "%s %u Hz  gaps %u/%u", st.rate == 16000 ? "mSBC" : "CVSD", (unsigned)st.rate,
               (unsigned)st.dacUnderruns, (unsigned)st.hfUnderruns);
    else
      txt::copy(info, "Voice link idle", sizeof info);
    ui.text(info, 12, y + 10, th::fTiny(), th::DIM, textdatum_t::middle_left);
    ui.endScroll(scroll_, view);
  }
};

// ===========================================================================
class DisplayScreen : public Screen {
 public:
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Display")) {
      nav::back();
      return;
    }
    Settings& s = settings::get();
    int y = th::CONTENT_Y + 4;
    char v[16];
    int pct = (s.brightness * 100 + 127) / 255;
    snprintf(v, sizeof v, "%d%%", pct);
    bool minus, plus;
    if (ui.stepper(Rect(0, y, 240, 44), "Brightness", v, &minus, &plus)) {
      int b = s.brightness + (plus ? 26 : -26);
      if (b < 26) b = 26;
      if (b > 255) b = 255;
      s.brightness = (uint8_t)b;
      display::setBrightness(s.brightness);
      app::saveSettingsSoon();
    }
    y += 52;
    ui.text("Screen turns off after", 12, y + 8, th::fSmall(), th::MUTED, textdatum_t::middle_left);
    static const char* const kOpts[5] = {"15s", "30s", "1m", "2m", "Never"};
    static const uint16_t kVals[5] = {15, 30, 60, 120, 0};
    int sel = 1;
    for (int k = 0; k < 5; ++k)
      if (kVals[k] == s.screenTimeoutS) sel = k;
    int n = ui.segmented(Rect(8, y + 20, 224, 30), kOpts, 5, sel);
    if (n != sel) {
      s.screenTimeoutS = kVals[n];
      app::saveSettingsSoon();
    }
    y += 60;
    if (ui.toggle(Rect(0, y, 240, 44), "Auto capital letters", s.autoCaps)) {
      s.autoCaps = !s.autoCaps;
      app::saveSettingsSoon();
    }
    y += 56;
    if (ui.button(Rect(8, y, 224, 38), "Lock screen now", th::SURFACE2, th::TEXT, nullptr, 10, Icon::Lock)) app::lockNow();
    ui.text("Tip: the BOOT button wakes the phone,", 120, y + 58, th::fTiny(), th::DIM, textdatum_t::middle_center);
    ui.text("goes home, and locks when held.", 120, y + 72, th::fTiny(), th::DIM, textdatum_t::middle_center);
  }
};

// ===========================================================================
class DateTimeScreen : public Screen {
  int v_[5];  // day, month, year, hour, minute

 public:
  void enter(const NavParam&) override {
    uint32_t t = app::clockValid() ? app::nowLocal() : txt::makeEpoch(2026, 1, 1, 12, 0, 0);
    txt::splitEpoch(t, &v_[2], &v_[1], &v_[0], &v_[3], &v_[4], nullptr, nullptr);
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Date & time")) {
      nav::back();
      return;
    }
    ui.text(app::clockValid() ? "Set by the network." : "Not set yet - the network, data", 120, 66,
            th::fSmall(), th::MUTED, textdatum_t::middle_center);
    ui.text(app::clockValid() ? "Adjust it here if it is wrong:" : "or the first SMS will set it.", 120, 82,
            th::fSmall(), th::MUTED, textdatum_t::middle_center);
    static const char* const kLabels[5] = {"Day", "Month", "Year", "Hour", "Minute"};
    static const int kMin[5] = {1, 1, 2024, 0, 0}, kMax[5] = {31, 12, 2099, 23, 59};
    int y = 94;
    for (int k = 0; k < 5; ++k) {
      char v[8];
      if (k == 1) txt::copy(v, txt::kMonthShort[v_[1] - 1], sizeof v);
      else snprintf(v, sizeof v, k >= 3 ? "%02d" : "%d", v_[k]);
      bool minus, plus;
      if (ui.stepper(Rect(0, y, 240, 36), kLabels[k], v, &minus, &plus)) {
        v_[k] += plus ? 1 : -1;
        if (v_[k] > kMax[k]) v_[k] = kMin[k];
        if (v_[k] < kMin[k]) v_[k] = kMax[k];
      }
      y += 36;
    }
    if (ui.button(Rect(8, y + 8, 224, 36), "Set time", th::ACCENT, th::WHITE)) {
      uint32_t t = txt::makeEpoch(v_[2], v_[1], v_[0], v_[3], v_[4], 0);
      modem::setClock(t);
      ui.toast("Time set", th::GREEN);
      nav::back();
    }
  }
};

// ===========================================================================
class UssdScreen : public Screen {
  enum class Mode : uint8_t { Entry, Result, Reply };
  Mode mode_ = Mode::Entry;
  char code_[32] = "";
  char reply_[32] = "";
  ScrollState scroll_;

 public:
  void enter(const NavParam& p) override {
    txt::copy(code_, p.s, sizeof code_);
    reply_[0] = 0;
    scroll_ = ScrollState();
    mode_ = (app::ussdWaiting() || app::ussdText()[0]) && !p.s[0] ? Mode::Result : Mode::Entry;
    if (p.i == 1) mode_ = Mode::Result;  // opened because a reply arrived / a code was sent
  }
  void onEvent(const AppEvent& e) override {
    if (e.type == Ev::UssdReply) {
      mode_ = Mode::Result;
      scroll_ = ScrollState();
    }
  }
  bool animating() const override { return app::ussdWaiting(); }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Balance & codes")) {
      if (app::ussdExpectsReply()) app::ussdCancel();
      nav::back();
      return;
    }
    if (mode_ == Mode::Entry || mode_ == Mode::Reply) {
      bool entry = mode_ == Mode::Entry;
      if (entry) {
        if (ui.button(Rect(8, 60, 108, 30), "*123# Balance", th::SURFACE2, th::TEXT, th::fSmall())) {
          app::ussd("*123#");
          mode_ = Mode::Result;
          return;
        }
        if (ui.button(Rect(124, 60, 108, 30), "*121# Offers", th::SURFACE2, th::TEXT, th::fSmall())) {
          app::ussd("*121#");
          mode_ = Mode::Result;
          return;
        }
      } else {
        ui.text("Reply to the menu:", 12, 75, th::fSmall(), th::MUTED, textdatum_t::middle_left);
      }
      char* buf = entry ? code_ : reply_;
      Rect field(8, 98, 224, 40);
      g.fillSmoothRoundRect(field.x, field.y, field.w, field.h, 10, th::SURFACE);
      g.drawRoundRect(field.x, field.y, field.w, field.h, 10, th::ACCENT);
      ui.textFit(buf[0] ? buf : (entry ? "Code, e.g. *123#" : "Option number"), field.x + 12, field.cy(), 200,
                 th::fMedium(), buf[0] ? th::TEXT : th::DIM, textdatum_t::middle_left);
      if (numpad.draw(ui, th::H - NumPad::kHeight, buf, 31, "Send", true, false) == KeyAction::Action && buf[0]) {
        if (entry) app::ussd(buf);
        else app::ussdReply(buf);
        buf[0] = 0;
        mode_ = Mode::Result;
      }
      return;
    }
    // result
    Rect view(8, 62, 224, 206);
    g.fillSmoothRoundRect(view.x, view.y, view.w, view.h, 10, th::SURFACE);
    if (app::ussdWaiting()) {
      ui.spinner(120, 150, 14, th::ACCENT);
      ui.text("Waiting for the network...", 120, 184, th::fBody(), th::MUTED, textdatum_t::middle_center);
    } else {
      static char disp[USSD_TEXT_MAX + 32];
      txt::toDisplay(app::ussdText(), disp, sizeof disp);
      int starts[40], lens[40];
      int n = ui.wrap(disp, view.w - 20, th::fBody(), starts, lens, 40);
      Rect inner = view.inset(6);
      int y0 = ui.beginScroll(scroll_, inner, n * 18 + 8);
      char line[128];
      for (int k = 0; k < n; ++k) {
        int l = lens[k] < 127 ? lens[k] : 127;
        memcpy(line, disp + starts[k], l);
        line[l] = 0;
        ui.text(line, inner.x + 4, y0 + 4 + k * 18, th::fBody(), th::TEXT);
      }
      ui.endScroll(scroll_, inner);
    }
    int by = 276;
    if (app::ussdExpectsReply() && !app::ussdWaiting()) {
      if (ui.button(Rect(8, by, 108, 36), "Reply", th::ACCENT, th::WHITE)) mode_ = Mode::Reply;
      if (ui.button(Rect(124, by, 108, 36), "End session", th::SURFACE2)) {
        app::ussdCancel();
        mode_ = Mode::Entry;
      }
    } else {
      if (ui.button(Rect(8, by, 108, 36), "New code", th::SURFACE2)) mode_ = Mode::Entry;
      if (ui.button(Rect(124, by, 108, 36), "Done", th::ACCENT, th::WHITE)) nav::back();
    }
  }
};

// ===========================================================================
namespace screens {
SettingsScreen s_settings;
BluetoothScreen s_bluetooth;
SoundScreen s_sound;
DisplayScreen s_display;
DateTimeScreen s_dateTime;
UssdScreen s_ussd;

void registerSettings() {
  nav::add(ScreenId::Settings, &s_settings);
  nav::add(ScreenId::Bluetooth, &s_bluetooth);
  nav::add(ScreenId::Sound, &s_sound);
  nav::add(ScreenId::Display, &s_display);
  nav::add(ScreenId::DateTime, &s_dateTime);
  nav::add(ScreenId::Ussd, &s_ussd);
}

void registerAll() {
  registerMain();
  registerPhone();
  registerSms();
  registerContacts();
  registerSettings();
  registerNet();
  registerDiag();
}
}  // namespace screens
