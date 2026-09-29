// Boot, lock, home, touch calibration and SIM PIN screens.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hotspot.h"
#include "phone_app.h"
#include "plog.h"
#include "bt_client.h"
#include "text_util.h"
#include "touch.h"
#include "modem.h"
#include "call_log.h"
#include "contacts.h"
#include "messages.h"
#include "settings.h"
#include "keyboard.h"
#include "screens.h"
#include "screens_internal.h"

using lgfx::textdatum_t;

// ===========================================================================
class BootScreen : public Screen {
  uint32_t entered_ = 0;

 public:
  void enter(const NavParam&) override { entered_ = plogMillis(); }
  bool animating() const override { return true; }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    g.fillSmoothRoundRect(88, 64, 64, 64, 18, th::GREEN);
    drawIcon(g, Icon::Phone, 120, 96, 38, th::WHITE, th::GREEN);
    ui.text(FW_NAME, 120, 152, th::fTitle(), th::TEXT, textdatum_t::middle_center);
    ui.text("ESP32-S3 + A7672S 4G", 120, 176, th::fSmall(), th::MUTED, textdatum_t::middle_center);
    PhoneState ps = app::phoneState();
    ui.text(modem::phoneStateText(ps), 120, 236, th::fBody(), ps == PhoneState::Error ? th::AMBER : th::MUTED,
            textdatum_t::middle_center);
    ui.spinner(120, 272, 12, th::ACCENT);

    uint32_t el = plogMillis() - entered_;
    if (el < 1500) return;
    if (ps == PhoneState::SimLocked) {
      nav::reset(ScreenId::SimPin);
      return;
    }
    bool done = ps == PhoneState::Ready || ps == PhoneState::NoSim || (ps == PhoneState::SearchingNetwork && el > 6000) ||
                (ps == PhoneState::Error && el > 8000) || el > 15000;
    if (!done) return;
    if (!settings::get().touchCalValid) nav::reset(ScreenId::Calibrate, NavParam::num(1));
    else nav::reset(ScreenId::Home);
  }
};

// ===========================================================================
class LockScreen : public Screen {
 public:
  bool animating() const override { return ui.in.down; }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    char clk[8] = "--:--", date[20] = "Time not set";
    if (app::clockValid()) {
      txt::fmtClock(app::nowLocal(), clk);
      txt::fmtDayDate(app::nowLocal(), date);
    }
    ui.text(clk, 120, 88, th::fHuge(), th::TEXT, textdatum_t::middle_center);
    ui.text(date, 120, 140, th::fMedium(), th::MUTED, textdatum_t::middle_center);

    int y = 166;
    int missed = calllog::missedUnseen();
    int unread = messages::unreadCount();
    char line[40];
    if (missed) {
      snprintf(line, sizeof line, "%d missed call%s", missed, missed == 1 ? "" : "s");
      g.fillSmoothRoundRect(16, y, 208, 34, 10, th::SURFACE);
      drawIcon(g, Icon::CallMissed, 36, y + 17, 16, th::RED, th::SURFACE);
      ui.text(line, 56, y + 17, th::fBody(), th::TEXT, textdatum_t::middle_left);
      y += 40;
    }
    if (unread) {
      snprintf(line, sizeof line, "%d new message%s", unread, unread == 1 ? "" : "s");
      g.fillSmoothRoundRect(16, y, 208, 34, 10, th::SURFACE);
      drawIcon(g, Icon::Message, 36, y + 17, 16, th::ACCENT, th::SURFACE);
      ui.text(line, 56, y + 17, th::fBody(), th::TEXT, textdatum_t::middle_left);
    }

    Rect sos(70, 244, 100, 20);
    if (ui.clickIn(sos)) {
      ui.confirm("Emergency call", "Call 112 (national emergency number)?", "Call 112", th::RED, [] { app::dial("112"); });
    }
    ui.text("Emergency call", sos.cx(), sos.cy(), th::fSmall(), th::RED, textdatum_t::middle_center);

    Rect slider(24, 270, 192, 40);
    uint32_t held = ui.holdMs(slider);
    float p = held / 600.0f;
    if (p > 1) p = 1;
    g.fillSmoothRoundRect(slider.x, slider.y, slider.w, slider.h, 20, th::SURFACE2);
    if (p > 0) g.fillSmoothRoundRect(slider.x, slider.y, (int)(slider.h + (slider.w - slider.h) * p), slider.h, 20, th::ACCENT);
    drawIcon(g, Icon::Lock, slider.x + 20, slider.cy(), 18, th::WHITE, p > 0 ? th::ACCENT : th::SURFACE2);
    ui.text(held ? "Keep holding..." : "Hold to unlock", slider.cx() + 12, slider.cy(), th::fBody(), th::TEXT,
            textdatum_t::middle_center);
    if (ui.longPress(slider, 600)) nav::back();
  }
};

// ===========================================================================
class HomeScreen : public Screen {
  struct Tile { Icon icon; uint32_t color; const char* label; ScreenId target; };

 public:
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();

    char clk[8] = "--:--", date[32] = "Set the time in Settings";
    if (app::clockValid()) {
      txt::fmtClock(app::nowLocal(), clk);
      txt::fmtDayDate(app::nowLocal(), date);
    }
    Rect clockArea(0, 22, 240, 70);
    if (ui.clickIn(clockArea)) {
      app::lockNow();
      return;
    }
    ui.text(clk, 120, 53, th::fClock(), th::TEXT, textdatum_t::middle_center);
    ui.text(date, 120, 87, th::fSmall(), th::MUTED, textdatum_t::middle_center);

    // one status line: operator | 4G | earbuds / problem
    PhoneState ps = app::phoneState();
    const NetStatus& st = app::net();
    char status[64];
    uint32_t sc = th::MUTED;
    if (ps != PhoneState::Ready) {
      txt::copy(status, modem::phoneStateText(ps), sizeof status);
      sc = th::AMBER;
    } else {
      const char* rat = modem::ratText(st);
      if (bt::connected()) {
        snprintf(status, sizeof status, "%s %s  |  %s", st.op, rat, bt::peerName());
        sc = th::TEAL;
      } else {
        snprintf(status, sizeof status, "%s %s%s", st.op, rat,
                 app::data().state == DataState::Connected ? "  |  data on" : "");
      }
    }
    char disp[64];
    txt::toDisplay(status, disp, sizeof disp);
    ui.textFit(disp, 120, 102, 228, th::fSmall(), sc, textdatum_t::middle_center);

    static const Tile kTiles[9] = {
        {Icon::Phone, th::GREEN, "Phone", ScreenId::Dialer},
        {Icon::Message, th::ACCENT, "Messages", ScreenId::Messages},
        {Icon::Person, th::ORANGE, "Contacts", ScreenId::Contacts},
        {Icon::Recents, th::PURPLE, "Recents", ScreenId::Recents},
        {Icon::Headphones, th::INDIGO, "Earbuds", ScreenId::Bluetooth},
        {Icon::Wifi, th::TEAL, "Hotspot", ScreenId::Hotspot},
        {Icon::Data, th::PINK, "Mobile data", ScreenId::Data},
        {Icon::Gear, th::SLATE, "Settings", ScreenId::Settings},
        {Icon::Chip, th::AMBER, "Diagnostics", ScreenId::Diagnostics},
    };
    // 3 x 3 launcher grid (tiles >= 70 x 66 px: easy stylus / finger targets)
    int unread = messages::unreadCount(), missed = calllog::missedUnseen();
    for (int i = 0; i < 9; ++i) {
      const Tile& t = kTiles[i];
      Rect r(6 + (i % 3) * 78, 112 + (i / 3) * 70, 72, 66);
      bool pressed = ui.pressingIn(r);
      g.fillSmoothRoundRect(r.x, r.y, r.w, r.h, 14, pressed ? th::PRESSED : th::SURFACE);
      int icy = r.y + 26;
      g.fillSmoothCircle(r.cx(), icy, pressed ? 17 : 18, t.color);
      drawIcon(g, t.icon, r.cx(), icy, 20, th::WHITE, t.color);
      const lgfx::IFont* lf = ui.textW(t.label, th::fSmall()) <= r.w - 6 ? th::fSmall() : th::fTiny();
      ui.textFit(t.label, r.cx(), r.bottom() - 11, r.w - 4, lf, th::TEXT, textdatum_t::middle_center);
      if (i == 1) ui.badge(r.cx() + 17, icy - 14, unread, th::ACCENT);
      if (i == 3) ui.badge(r.cx() + 17, icy - 14, missed);
      bool dot = (i == 4 && bt::connected()) || (i == 5 && hotspot::running()) ||
                 (i == 6 && app::data().state == DataState::Connected);
      if (dot) g.fillSmoothCircle(r.cx() + 15, icy + 13, 4, th::GREEN);
      if (ui.clickIn(r)) {
        nav::push(t.target);
        return;
      }
    }
  }
};

// ===========================================================================
class CalibrateScreen : public Screen {
  static constexpr int kPoints = 5;
  const int16_t targets_[kPoints][2] = {{24, 24}, {216, 24}, {216, 296}, {24, 296}, {120, 160}};
  int16_t raw_[kPoints][2] = {};
  int idx_ = 0;
  int32_t sumX_ = 0, sumY_ = 0;
  int n_ = 0, skip_ = 0;
  bool pressed_ = false, firstRun_ = false;
  char msg_[48] = "";
  uint32_t doneAt_ = 0;

 public:
  void enter(const NavParam& p) override {
    firstRun_ = p.i == 1;
    idx_ = 0;
    n_ = 0;
    pressed_ = false;
    doneAt_ = 0;
    msg_[0] = 0;
  }
  bool animating() const override { return true; }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BLACK);
    if (doneAt_) {
      drawIcon(g, Icon::Check, 120, 140, 48, th::GREEN, th::BLACK);
      ui.text("Touch calibrated", 120, 190, th::fTitle(), th::TEXT, textdatum_t::middle_center);
      if (plogMillis() - doneAt_ > 1200) {
        if (firstRun_) nav::reset(ScreenId::Home);
        else nav::back();
      }
      return;
    }
    int tx = targets_[idx_][0], ty = targets_[idx_][1];
    g.drawCircle(tx, ty, 12, th::RED);
    g.drawCircle(tx, ty, 4, th::RED);
    g.drawFastHLine(tx - 18, ty, 36, th::WHITE);
    g.drawFastVLine(tx, ty - 18, 36, th::WHITE);
    ui.text(idx_ < 4 ? "Touch calibration" : "Check", 120, 110, th::fTitle(), th::TEXT, textdatum_t::middle_center);
    ui.text("Press the centre of the", 120, 190, th::fBody(), th::MUTED, textdatum_t::middle_center);
    ui.text("target with the stylus", 120, 210, th::fBody(), th::MUTED, textdatum_t::middle_center);
    char step[16];
    snprintf(step, sizeof step, "%d / %d", idx_ + 1, kPoints);
    ui.text(step, 120, 236, th::fSmall(), th::DIM, textdatum_t::middle_center);
    if (msg_[0]) ui.text(msg_, 120, 140, th::fBody(), th::AMBER, textdatum_t::middle_center);

    int16_t rx, ry;
    if (touch::readRaw(&rx, &ry)) {
      if (!pressed_) {
        pressed_ = true;
        sumX_ = sumY_ = 0;
        n_ = 0;
        skip_ = 3;  // let the resistive layers settle
      }
      if (skip_ > 0) --skip_;
      else {
        sumX_ += rx;
        sumY_ += ry;
        ++n_;
      }
      g.fillCircle(tx, ty, 6, th::GREEN);
    } else if (pressed_) {
      pressed_ = false;
      if (n_ < 3) return;  // too short, ignore
      raw_[idx_][0] = (int16_t)(sumX_ / n_);
      raw_[idx_][1] = (int16_t)(sumY_ / n_);
      if (idx_ < kPoints - 1) {
        ++idx_;
        return;
      }
      float cal[6];
      bool ok = touch::computeCalibration(raw_, targets_, 4, cal);
      if (ok) {
        int16_t x, y;
        touch::mapRaw(cal, raw_[4][0], raw_[4][1], &x, &y);
        int dx = x - targets_[4][0], dy = y - targets_[4][1];
        ok = dx * dx + dy * dy < 20 * 20;
      }
      if (!ok) {
        txt::copy(msg_, "Not accurate - let's try again", sizeof msg_);
        idx_ = 0;
        return;
      }
      Settings& s = settings::get();
      memcpy(s.cal, cal, sizeof s.cal);
      s.touchCalValid = 1;
      settings::save();
      touch::setCalibration(cal);
      doneAt_ = plogMillis();
    }
  }
};

// ===========================================================================
class SimPinScreen : public Screen {
  char pin_[12] = "";
  bool sent_ = false;

 public:
  void enter(const NavParam&) override {
    pin_[0] = 0;
    sent_ = false;
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("SIM locked")) {
      nav::reset(ScreenId::Home);
      return;
    }
    const NetStatus& st = app::net();
    if (st.sim == SimState::Ready) {
      nav::reset(ScreenId::Home);
      return;
    }
    drawIcon(g, Icon::Sim, 120, 84, 30, th::AMBER);
    ui.text(st.sim == SimState::PukRequired ? "SIM needs its PUK code" : "Enter SIM PIN", 120, 116, th::fBody(), th::TEXT,
            textdatum_t::middle_center);
    char masked[12];
    size_t n = strlen(pin_);
    for (size_t k = 0; k < n; ++k) masked[k] = '*';
    masked[n] = 0;
    g.fillSmoothRoundRect(40, 128, 160, 26, 8, th::SURFACE2);
    ui.text(n ? masked : (sent_ ? "Checking..." : ""), 120, 141, th::fMedium(), th::TEXT, textdatum_t::middle_center);
    if (numpad.draw(ui, th::H - NumPad::kHeight, pin_, 9, "OK", false, false) == KeyAction::Action && strlen(pin_) >= 4) {
      modem::enterPin(pin_);
      pin_[0] = 0;
      sent_ = true;
    }
  }
};

// ===========================================================================
namespace screens {
BootScreen s_boot;
LockScreen s_lock;
HomeScreen s_home;
CalibrateScreen s_calibrate;
SimPinScreen s_simPin;

void registerMain() {
  nav::add(ScreenId::Boot, &s_boot);
  nav::add(ScreenId::Lock, &s_lock);
  nav::add(ScreenId::Home, &s_home);
  nav::add(ScreenId::Calibrate, &s_calibrate);
  nav::add(ScreenId::SimPin, &s_simPin);
}
}  // namespace screens
