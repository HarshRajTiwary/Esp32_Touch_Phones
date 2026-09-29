// Mobile network & SIM, mobile data & APN, Wi-Fi hotspot, and a text editor
// for their text fields.
#include <stdio.h>
#include <string.h>

#include "at_parse.h"
#include "hotspot.h"
#include "keyboard.h"
#include "modem.h"
#include "net_rules.h"
#include "nettools.h"
#include "phone_app.h"
#include "plog.h"
#include "screens.h"
#include "screens_internal.h"
#include "settings.h"
#include "text_util.h"

using lgfx::textdatum_t;

namespace {

enum class TextField : int { Apn = 0, ApnUser, ApnPass, HotspotName, HotspotPass };

void fmtBytes(uint32_t b, char* out, size_t cap) {
  if (b < 10 * 1024) snprintf(out, cap, "%u B", (unsigned)b);
  else if (b < 10 * 1024 * 1024) snprintf(out, cap, "%u KB", (unsigned)(b / 1024));
  else snprintf(out, cap, "%u.%u MB", (unsigned)(b / 1048576), (unsigned)((b % 1048576) * 10 / 1048576));
}

// "8991 0000 ... 1234": enough to identify the SIM without exposing the number
void maskId(const char* id, char* out, size_t cap) {
  size_t n = strlen(id);
  if (n < 10) {
    txt::copy(out, id, cap);
    return;
  }
  snprintf(out, cap, "%.6s...%s", id, id + n - 4);
}

}  // namespace

// ===========================================================================
class NetworkScreen : public Screen {
  ScrollState scroll_;

 public:
  void enter(const NavParam&) override {
    scroll_ = ScrollState();
    modem::refresh();
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Network & SIM")) {
      nav::back();
      return;
    }
    const NetStatus& st = app::net();
    Settings& s = settings::get();
    Rect view(0, th::CONTENT_Y, 240, th::H - th::CONTENT_Y);
    int y = ui.beginScroll(scroll_, view, 15 * 30 + 150);
    char v[64];

    ui.text("Network mode", 12, y + 12, th::fSmall(), th::MUTED, textdatum_t::middle_left);
    static const char* const kModes[3] = {"Auto", "4G only", "2G only"};
    int sel = s.netMode == (uint8_t)NetMode::LteOnly ? 1 : s.netMode == (uint8_t)NetMode::GsmOnly ? 2 : 0;
    int n = ui.segmented(Rect(8, y + 24, 224, 30), kModes, 3, sel);
    if (n != sel) {
      static const NetMode kVals[3] = {NetMode::Auto, NetMode::LteOnly, NetMode::GsmOnly};
      app::setNetworkMode(kVals[n]);
      ui.toast(n == 1 ? "4G only: calls need VoLTE support" : "Network mode changed", th::ACCENT);
    }
    y += 62;

    screens::infoRow(y, "Operator", st.op[0] ? st.op : "-");
    y += 30;
    const char* reg = "Unknown";
    switch (st.reg) {
      case RegState::Home: reg = st.rat == Rat::Lte ? "Registered (4G)" : st.rat == Rat::Gsm ? "Registered (2G)" : "Registered"; break;
      case RegState::Roaming: reg = "Roaming"; break;
      case RegState::Searching: reg = "Searching..."; break;
      case RegState::Denied: reg = "Denied - emergency only"; break;
      case RegState::NotRegistered: reg = "Not registered"; break;
      default: break;
    }
    screens::infoRow(y, "Network", reg);
    y += 30;
    if (st.rat == Rat::Lte && st.rsrp) snprintf(v, sizeof v, "RSRP %d dBm (%d/4)", st.rsrp, modem::signalBars(st));
    else if (st.csq >= 0 && st.csq <= 31) snprintf(v, sizeof v, "%d dBm (%d/4)", csqToDbm(st.csq), modem::signalBars(st));
    else txt::copy(v, "No signal", sizeof v);
    screens::infoRow(y, "Signal", v);
    y += 30;
    if (st.rat == Rat::Lte && st.rsrq) snprintf(v, sizeof v, "%d / %d dB", st.rsrq, st.sinr);
    else txt::copy(v, "-", sizeof v);
    screens::infoRow(y, "RSRQ / SINR", v);
    y += 30;
    screens::infoRow(y, "Band", st.band[0] ? st.band : "-");
    y += 30;
    const char* sim = st.sim == SimState::Ready ? "Ready" : st.sim == SimState::Absent ? "Not detected"
                      : st.sim == SimState::PinRequired ? "PIN required" : st.sim == SimState::PukRequired ? "PUK required" : "Checking";
    screens::infoRow(y, "SIM card", sim);
    y += 30;
    screens::infoRow(y, "My number", st.ownNumber[0] ? st.ownNumber : "Unknown (dial *121#)");
    y += 30;
    maskId(st.iccid, v, sizeof v);
    screens::infoRow(y, "SIM ICCID", v);
    y += 30;
    screens::infoRow(y, "IMEI", st.imei);
    y += 30;
    snprintf(v, sizeof v, "%s %s", st.model, st.fw);
    screens::infoRow(y, "Modem", v);
    y += 30;
    if (st.battMv) snprintf(v, sizeof v, "%u.%02u V", st.battMv / 1000, (st.battMv % 1000) / 10);
    else txt::copy(v, "-", sizeof v);
    if (st.modemTempC > -100) snprintf(v + strlen(v), sizeof v - strlen(v), ", %d C", st.modemTempC);
    screens::infoRow(y, "Supply / temp", v);
    y += 30;
    snprintf(v, sizeof v, "%s, %lu baud", st.cmux ? "CMUX (AT + data)" : "single channel", (unsigned long)st.baud);
    screens::infoRow(y, "Serial link", v);
    y += 30;
    snprintf(v, sizeof v, "%u", (unsigned)st.modemResets);
    screens::infoRow(y, "Modem restarts", v);
    y += 30;
    screens::infoRow(y, "Modem state", st.failReason[0] && st.modem != ModemState::Ready ? st.failReason : modemStateName(st.modem));
    y += 38;
    if (ui.button(Rect(8, y, 108, 34), "Refresh", th::SURFACE2)) modem::refresh();
    if (ui.button(Rect(124, y, 108, 34), "Restart modem", th::SURFACE2, th::AMBER, th::fSmall())) {
      ui.confirm("Restart modem?", "Calls, messages and data stop for ~30 seconds.", "Restart", th::AMBER, [] { modem::restart(); });
    }
    ui.endScroll(scroll_, view);
  }
};

// ===========================================================================
class DataScreen : public Screen {
  ScrollState scroll_;

 public:
  void enter(const NavParam&) override { scroll_ = ScrollState(); }
  bool animating() const override {
    DataState s = app::data().state;
    return s == DataState::Dialing || s == DataState::Negotiating || nettools::testRunning();
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Mobile data")) {
      nav::back();
      return;
    }
    Settings& s = settings::get();
    const DataStatus& d = app::data();
    Rect view(0, th::CONTENT_Y, 240, th::H - th::CONTENT_Y);
    int y = ui.beginScroll(scroll_, view, 470);
    if (ui.toggle(Rect(0, y, 240, 42), "Mobile data (4G)", s.dataEnabled)) app::setDataEnabled(!s.dataEnabled);
    y += 46;

    // status card
    Rect card(8, y, 224, 46);
    bool up = d.state == DataState::Connected;
    g.fillSmoothRoundRect(card.x, card.y, card.w, card.h, 10, th::SURFACE);
    g.fillSmoothCircle(card.x + 23, card.cy(), 15, up ? th::GREEN : th::SURFACE2);
    drawIcon(g, Icon::Data, card.x + 23, card.cy(), 18, th::WHITE, up ? th::GREEN : th::SURFACE2);
    char st[48];
    snprintf(st, sizeof st, "%s", dataStateName(d.state));
    st[0] = (char)(st[0] >= 'a' && st[0] <= 'z' ? st[0] - 32 : st[0]);
    ui.text(st, card.x + 46, card.y + 15, th::fBodyB(), up ? th::GREEN : th::TEXT, textdatum_t::middle_left);
    const char* sub = up ? d.ip : d.lastError[0] ? d.lastError : (d.state == DataState::WaitNetwork ? "waiting for the network" : "");
    ui.textFit(sub, card.x + 46, card.y + 33, 170, th::fSmall(), th::MUTED, textdatum_t::middle_left);
    if (d.state == DataState::Dialing || d.state == DataState::Negotiating) ui.spinner(card.right() - 20, card.cy(), 9, th::ACCENT);
    y += 52;

    char v[48], a[16], b[16];
    fmtBytes(d.rxBytes, a, sizeof a);
    fmtBytes(d.txBytes, b, sizeof b);
    snprintf(v, sizeof v, "%s / %s", a, b);
    screens::infoRow(y, "Down / up", v);
    y += 30;
    screens::infoRow(y, "DNS", d.dns[0] ? d.dns : "-");
    y += 30;
    if (up) {
      uint32_t secs = (plogMillis() - d.upSinceMs) / 1000;
      txt::fmtDuration(secs, v);
    } else {
      txt::copy(v, "-", sizeof v);
    }
    screens::infoRow(y, "Connected for", v);
    y += 36;

    ui.text("Access point - tap to change", 12, y + 8, th::fSmall(), th::MUTED, textdatum_t::middle_left);
    y += 18;
    struct Row { const char* label; const char* value; TextField field; };
    char masked[APN_PASS_MAX];
    size_t pl = strlen(s.apnPass);
    for (size_t k = 0; k < pl && k < sizeof masked - 1; ++k) masked[k] = '*';
    masked[pl < sizeof masked ? pl : sizeof masked - 1] = 0;
    Row rows[3] = {{"APN", s.apn, TextField::Apn},
                   {"User name", s.apnUser[0] ? s.apnUser : "(none)", TextField::ApnUser},
                   {"Password", pl ? masked : "(none)", TextField::ApnPass}};
    for (auto& r : rows) {
      Rect row(0, y, 240, 32);
      bool tap = ui.listRow(row, false);
      screens::infoRow(y, r.label, r.value);
      if (tap) nav::push(ScreenId::TextEdit, NavParam::num((int)r.field));
      y += 32;
    }
    y += 8;
    if (nettools::testRunning()) {
      ui.button(Rect(8, y, 224, 34), "Testing...", th::SURFACE2, th::MUTED);
      ui.spinner(28, y + 17, 8, th::ACCENT);
    } else if (ui.button(Rect(8, y, 224, 34), "Test internet connection", th::ACCENT, th::WHITE, th::fSmall(), 10, Icon::Globe)) {
      nettools::startConnectivityTest();
    }
    y += 40;
    if (nettools::lastTestResult()[0])
      ui.textFit(nettools::lastTestResult(), 120, y + 8, 224, th::fSmall(), txt::startsWith(nettools::lastTestResult(), "Internet OK") ? th::GREEN : th::AMBER,
                 textdatum_t::middle_center);
    y += 22;
    ui.text("Airtel India usually works with the", 120, y + 8, th::fTiny(), th::DIM, textdatum_t::middle_center);
    ui.text("APN airtelgprs.com and no password.", 120, y + 22, th::fTiny(), th::DIM, textdatum_t::middle_center);
    ui.endScroll(scroll_, view);
  }
};

// ===========================================================================
class HotspotScreen : public Screen {
  bool showPass_ = false;
  ScrollState scroll_;

 public:
  void enter(const NavParam&) override {
    showPass_ = false;
    scroll_ = ScrollState();
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Hotspot")) {
      nav::back();
      return;
    }
    Settings& s = settings::get();
    hotspot::Status hs = hotspot::status();
    Rect view(0, th::CONTENT_Y, 240, th::H - th::CONTENT_Y);
    int y = ui.beginScroll(scroll_, view, 420);

    // big status + on/off
    g.fillSmoothCircle(46, y + 34, 26, hs.on ? th::TEAL : th::SURFACE2);
    drawIcon(g, Icon::Wifi, 46, y + 34, 30, th::WHITE, hs.on ? th::TEAL : th::SURFACE2);
    ui.text(hs.on ? "ON" : "OFF", 84, y + 22, th::fTitle(), hs.on ? th::TEAL : th::MUTED, textdatum_t::middle_left);
    const char* sub = !hs.on ? "Share 4G with a laptop" : hs.sharing ? "Sharing mobile data" : "Waiting for mobile data";
    ui.textFit(sub, 84, y + 46, 150, th::fSmall(), hs.on && !hs.sharing ? th::AMBER : th::MUTED, textdatum_t::middle_left);
    y += 74;
    if (ui.button(Rect(8, y, 224, 38), hs.on ? "Turn OFF" : "Turn ON", hs.on ? th::SURFACE2 : th::TEAL,
                  hs.on ? th::TEXT : th::WHITE)) {
      app::setHotspot(!hs.on);
    }
    y += 46;

    Rect nameRow(0, y, 240, 30);
    if (ui.listRow(nameRow, false)) nav::push(ScreenId::TextEdit, NavParam::num((int)TextField::HotspotName));
    screens::infoRow(y, "Network", s.hotspotSsid);
    y += 30;
    char pw[WIFI_PASS_MAX];
    if (showPass_) {
      txt::copy(pw, s.hotspotPass, sizeof pw);
    } else {
      size_t n = strlen(s.hotspotPass);
      for (size_t k = 0; k < n && k < 12; ++k) pw[k] = '*';
      pw[n < 12 ? n : 12] = 0;
    }
    Rect passRow(0, y, 240, 30);
    if (ui.listRow(passRow, false)) {
      static const char* opts[] = {"Show / hide password", "Change password"};
      ui.sheet("Hotspot password", opts, 2, [this](int o) {
        if (o == 0) showPass_ = !showPass_;
        else nav::push(ScreenId::TextEdit, NavParam::num((int)TextField::HotspotPass));
      });
    }
    screens::infoRow(y, "Password", pw);
    y += 30;
    char v[64];
    snprintf(v, sizeof v, "%d device%s", hs.clients, hs.clients == 1 ? "" : "s");
    screens::infoRow(y, "Connected", hs.on ? v : "-");
    y += 30;
    screens::infoRow(y, "IP", hs.on && hs.ip[0] ? hs.ip : "-");
    y += 30;
    const DataStatus& d = app::data();
    char a[16], b[16];
    fmtBytes(d.rxBytes, a, sizeof a);
    fmtBytes(d.txBytes, b, sizeof b);
    snprintf(v, sizeof v, "%s / %s", a, b);
    screens::infoRow(y, "Down / up", v);
    y += 34;

    bool minus, plus;
    snprintf(v, sizeof v, "%d", s.hotspotMaxClients);
    if (ui.stepper(Rect(0, y, 240, 40), "Max devices", v, &minus, &plus)) {
      if (minus && s.hotspotMaxClients > 1) s.hotspotMaxClients--;
      if (plus && s.hotspotMaxClients < 8) s.hotspotMaxClients++;
      app::saveSettingsSoon();
      hotspot::applySettings();
    }
    y += 40;
    if (ui.toggle(Rect(0, y, 240, 40), "Turn on at start-up", s.hotspotAuto)) {
      s.hotspotAuto = !s.hotspotAuto;
      app::saveSettingsSoon();
    }
    y += 44;
    if (!s.dataEnabled) ui.text("Mobile data is off - turn it on first", 120, y + 8, th::fSmall(), th::AMBER, textdatum_t::middle_center);
    if (hs.lastError[0]) ui.textFit(hs.lastError, 120, y + 24, 224, th::fSmall(), th::RED, textdatum_t::middle_center);
    ui.endScroll(scroll_, view);
  }
};

// ===========================================================================
// Single-line text editor for the network settings. Input is validated here:
// APN / user / password end up inside AT commands, so quotes and control
// characters are refused.
class TextEditScreen : public Screen {
  TextField field_ = TextField::Apn;
  char buf_[WIFI_PASS_MAX] = "";
  char err_[48] = "";

  size_t cap() const {
    switch (field_) {
      case TextField::Apn: return APN_MAX;
      case TextField::ApnUser: return APN_USER_MAX;
      case TextField::ApnPass: return APN_PASS_MAX;
      case TextField::HotspotName: return WIFI_SSID_MAX;
      case TextField::HotspotPass: return WIFI_PASS_MAX;
    }
    return 16;
  }
  const char* title() const {
    switch (field_) {
      case TextField::Apn: return "APN";
      case TextField::ApnUser: return "APN user name";
      case TextField::ApnPass: return "APN password";
      case TextField::HotspotName: return "Hotspot name";
      case TextField::HotspotPass: return "Hotspot password";
    }
    return "";
  }
  bool valid() {
    const char* why = nullptr;
    bool ok = true;
    switch (field_) {
      case TextField::Apn: ok = netrules::validApn(buf_, &why); break;
      case TextField::ApnUser:
      case TextField::ApnPass:
        ok = netrules::atSafe(buf_);
        why = "Only plain letters, digits and symbols (no quotes)";
        break;
      case TextField::HotspotName: ok = netrules::validSsid(buf_, &why); break;
      case TextField::HotspotPass: ok = netrules::validWpaPass(buf_, &why); break;
    }
    if (!ok) txt::copy(err_, why ? why : "Not allowed", sizeof err_);
    return ok;
  }
  void apply() {
    Settings& s = settings::get();
    switch (field_) {
      case TextField::Apn: app::setApn(buf_, s.apnUser, s.apnPass); break;
      case TextField::ApnUser: app::setApn(s.apn, buf_, s.apnPass); break;
      case TextField::ApnPass: app::setApn(s.apn, s.apnUser, buf_); break;
      case TextField::HotspotName:
        txt::copy(s.hotspotSsid, buf_, sizeof s.hotspotSsid);
        app::saveSettingsSoon();
        hotspot::applySettings();
        break;
      case TextField::HotspotPass:
        txt::copy(s.hotspotPass, buf_, sizeof s.hotspotPass);
        app::saveSettingsSoon();
        hotspot::applySettings();
        break;
    }
  }

 public:
  void enter(const NavParam& p) override {
    field_ = (TextField)p.i;
    Settings& s = settings::get();
    const char* cur = "";
    switch (field_) {
      case TextField::Apn: cur = s.apn; break;
      case TextField::ApnUser: cur = s.apnUser; break;
      case TextField::ApnPass: cur = s.apnPass; break;
      case TextField::HotspotName: cur = s.hotspotSsid; break;
      case TextField::HotspotPass: cur = s.hotspotPass; break;
    }
    txt::copy(buf_, cur, sizeof buf_);
    err_[0] = 0;
    kb.reset("x");  // no auto-capital for technical fields
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header(title())) {
      nav::back();
      return;
    }
    Rect field(8, 66, 224, 40);
    g.fillSmoothRoundRect(field.x, field.y, field.w, field.h, 10, th::SURFACE);
    g.drawRoundRect(field.x, field.y, field.w, field.h, 10, th::ACCENT);
    ui.textFit(buf_[0] ? buf_ : "(empty)", field.x + 10, field.cy(), field.w - 20, th::fBody(), buf_[0] ? th::TEXT : th::DIM,
               textdatum_t::middle_left);
    if (err_[0]) ui.textFit(err_, 120, 120, 224, th::fSmall(), th::RED, textdatum_t::middle_center);
    if (kb.draw(ui, th::H - Keyboard::kHeight, buf_, cap(), "Save") == KeyAction::Action) {
      if (valid()) {
        apply();
        ui.toast("Saved", th::GREEN);
        nav::back();
      }
    }
  }
};

// ===========================================================================
namespace screens {
NetworkScreen s_network;
DataScreen s_data;
HotspotScreen s_hotspot;
TextEditScreen s_textEdit;

void registerNet() {
  nav::add(ScreenId::Network, &s_network);
  nav::add(ScreenId::Data, &s_data);
  nav::add(ScreenId::Hotspot, &s_hotspot);
  nav::add(ScreenId::TextEdit, &s_textEdit);
}
}  // namespace screens
