// Diagnostics (live system state + hardware tests), touch / display tests,
// on-screen log viewer, and About.
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

#include "board_init.h"
#include "bt_client.h"
#include "call_log.h"
#include "contacts.h"
#include "display.h"
#include "hotspot.h"
#include "messages.h"
#include "modem.h"
#include "nettools.h"
#include "phone_app.h"
#include "plog.h"
#include "screens.h"
#include "screens_internal.h"
#include "settings.h"
#include "storage.h"
#include "text_util.h"

using lgfx::textdatum_t;

// ===========================================================================
class DiagnosticsScreen : public Screen {
  ScrollState scroll_;

  void section(Gui& ui, int y, const char* title) {
    ui.text(title, 12, y + 10, th::fSmall(), th::ACCENT, textdatum_t::middle_left);
  }

 public:
  void enter(const NavParam&) override {
    scroll_ = ScrollState();
    modem::refresh();
  }
  bool animating() const override { return true; }  // live values
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Diagnostics")) {
      nav::back();
      return;
    }
    Rect view(0, th::CONTENT_Y, 240, th::H - th::CONTENT_Y);
    int y = ui.beginScroll(scroll_, view, 24 * 30 + 4 * 22 + 4 * 42 + 60);
    char v[64];
    const NetStatus& st = app::net();

    // ---- hardware tests ----
    section(ui, y, "Tests");
    y += 22;
    static const char* const kTests[8] = {"Modem", "SMS", "Call", "Bluetooth", "Audio", "Touch", "Display", "Network"};
    for (int k = 0; k < 8; ++k) {
      Rect r(8 + (k % 2) * 116, y + (k / 2) * 42, 108, 36);
      if (!ui.button(r, kTests[k], th::SURFACE2, th::TEXT, th::fSmall())) continue;
      switch (k) {
        case 0:
          modem::refresh();
          ui.toast(modem::phoneStateText(app::phoneState()), th::ACCENT);
          break;
        case 1: nav::push(ScreenId::Compose, NavParam::str(st.ownNumber)); break;
        case 2: nav::push(ScreenId::Dialer, NavParam::str("121")); break;  // Airtel customer care (free)
        case 3: nav::push(ScreenId::Bluetooth); break;
        case 4:
          if (!bt::connected()) ui.toast("Connect earbuds first", th::AMBER);
          else app::setAudioTest(audio::testMode() == audio::TestMode::Tone ? audio::TestMode::None : audio::TestMode::Tone);
          break;
        case 5: nav::push(ScreenId::TestTouch); break;
        case 6: nav::push(ScreenId::TestDisplay); break;
        case 7:
          nettools::startConnectivityTest();
          nav::push(ScreenId::Data);
          break;
      }
    }
    y += 4 * 42 + 4;

    // ---- system ----
    section(ui, y, "System");
    y += 22;
    board_info_t bi;
    board_get_info(&bi);
    char chip[16];
    txt::copy(chip, bi.chip, sizeof chip);
    for (char* p = chip; *p; ++p) *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    snprintf(v, sizeof v, "%s v%d.%d, %d MHz", chip, bi.revision / 100, bi.revision % 100, bi.cpu_mhz);
    screens::infoRow(y, "Chip", v);
    y += 30;
    snprintf(v, sizeof v, "%s %s", FW_NAME, FW_VERSION);
    screens::infoRow(y, "Firmware", v);
    y += 30;
    snprintf(v, sizeof v, "%u KB (min %u)", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024));
    screens::infoRow(y, "Free RAM", v);
    y += 30;
    snprintf(v, sizeof v, "%u of %u KB free", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(bi.psram_bytes / 1024));
    screens::infoRow(y, "PSRAM", bi.psram_bytes ? v : "not found");
    y += 30;
    snprintf(v, sizeof v, "%u MB", (unsigned)(bi.flash_bytes >> 20));
    screens::infoRow(y, "Flash", v);
    y += 30;
    uint32_t up = app::uptimeS();
    snprintf(v, sizeof v, "%uh %02um %02us", (unsigned)(up / 3600), (unsigned)((up / 60) % 60), (unsigned)(up % 60));
    screens::infoRow(y, "Uptime", v);
    y += 34;

    // ---- cellular ----
    section(ui, y, "Cellular (A7672S)");
    y += 22;
    screens::infoRow(y, "Modem", st.failReason[0] && st.modem != ModemState::Ready ? st.failReason : modemStateName(st.modem));
    y += 30;
    const char* sim = st.sim == SimState::Ready ? "ready" : st.sim == SimState::Absent ? "not detected"
                      : st.sim == SimState::PinRequired ? "PIN" : st.sim == SimState::PukRequired ? "PUK" : "checking";
    screens::infoRow(y, "SIM", sim);
    y += 30;
    snprintf(v, sizeof v, "%s %s", st.op, modem::ratText(st));
    screens::infoRow(y, "Operator", v);
    y += 30;
    if (st.rat == Rat::Lte && st.rsrp) snprintf(v, sizeof v, "CSQ %d, RSRP %d dBm", st.csq, st.rsrp);
    else snprintf(v, sizeof v, "CSQ %d (%d dBm)", st.csq, csqToDbmSafe(st.csq));
    screens::infoRow(y, "RSSI", v);
    y += 30;
    const char* reg = st.reg == RegState::Home ? "home" : st.reg == RegState::Roaming ? "roaming"
                      : st.reg == RegState::Searching ? "searching" : st.reg == RegState::Denied ? "denied" : "not registered";
    screens::infoRow(y, "Registration", reg);
    y += 30;
    const DataStatus& d = app::data();
    snprintf(v, sizeof v, "%s %s", dataStateName(d.state), d.ip);
    screens::infoRow(y, "Data / IP", v);
    y += 34;

    // ---- radios / audio ----
    section(ui, y, "Bluetooth, audio, Wi-Fi");
    y += 22;
    snprintf(v, sizeof v, "%s%s%s", bt::helperOnline() ? "online " : "offline", bt::helperOnline() ? "fw " : "",
             bt::helperOnline() ? bt::helperVersion() : "");
    screens::infoRow(y, "BT helper", v);
    y += 30;
    screens::infoRow(y, "HFP", bt::stateText());
    y += 30;
    audio::Stats as = audio::stats();
    if (as.running) snprintf(v, sizeof v, "SCO %u Hz, gaps %u/%u", (unsigned)as.rate, (unsigned)as.dacUnderruns, (unsigned)as.hfUnderruns);
    else snprintf(v, sizeof v, "idle, call audio -> %s", app::audioRoute() == AudioRoute::Earbuds ? "earbuds" : app::audioRoute() == AudioRoute::Speaker ? "speaker" : "handset");
    screens::infoRow(y, "Audio", v);
    y += 30;
    hotspot::Status hs = hotspot::status();
    if (hs.on) snprintf(v, sizeof v, "hotspot on, %d client%s", hs.clients, hs.clients == 1 ? "" : "s");
    else txt::copy(v, "off", sizeof v);
    screens::infoRow(y, "Wi-Fi", v);
    y += 34;

    // ---- display / touch ----
    section(ui, y, "Display & touch");
    y += 22;
    screens::infoRow(y, "Display", display::buffered() ? "ILI9341 240x320, PSRAM buffer" : "ILI9341, direct");
    y += 30;
    const TouchSample& t = app::touchSample();
    if (t.down) snprintf(v, sizeof v, "x %d, y %d", t.x, t.y);
    else txt::copy(v, settings::get().touchCalValid ? "calibrated, not pressed" : "NOT calibrated", sizeof v);
    screens::infoRow(y, "Touch", v);
    y += 38;
    if (ui.button(Rect(8, y, 224, 34), "View log", th::SURFACE2)) nav::push(ScreenId::LogView);
    ui.endScroll(scroll_, view);
  }

  static int csqToDbmSafe(int csq) { return csq >= 0 && csq <= 31 ? -113 + 2 * csq : 0; }
};

// ===========================================================================
// Draws a dot where the stylus is, shows raw + calibrated coordinates.
class TestTouchScreen : public Screen {
  int16_t trail_[64][2];
  int n_ = 0;

 public:
  void enter(const NavParam&) override { n_ = 0; }
  bool animating() const override { return true; }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BLACK);
    for (int x = 0; x < th::W; x += 40) g.drawFastVLine(x, 0, th::H, 0x202020);
    for (int y = 0; y < th::H; y += 40) g.drawFastHLine(0, y, th::W, 0x202020);
    const TouchSample& t = app::touchSample();
    int16_t rx = 0, ry = 0;
    bool raw = touch::readRaw(&rx, &ry);
    if (t.down && n_ < 64) {
      trail_[n_][0] = t.x;
      trail_[n_][1] = t.y;
      ++n_;
    }
    for (int k = 0; k < n_; ++k) g.fillCircle(trail_[k][0], trail_[k][1], 2, th::GREEN);
    char v[64];
    if (t.down) {
      g.drawCircle(t.x, t.y, 10, th::RED);
      snprintf(v, sizeof v, "screen %d,%d  raw %d,%d", t.x, t.y, raw ? rx : 0, raw ? ry : 0);
    } else {
      txt::copy(v, "Draw with the stylus", sizeof v);
    }
    ui.text(v, 120, 12, th::fSmall(), th::TEXT, textdatum_t::middle_center);
    if (ui.button(Rect(8, 280, 108, 34), "Clear", th::SURFACE2)) n_ = 0;
    if (ui.button(Rect(124, 280, 108, 34), "Done", th::ACCENT, th::WHITE)) nav::back();
  }
};

// ===========================================================================
// Colour bars, gradients and an orientation marker. Tap to change the pattern.
class TestDisplayScreen : public Screen {
  int pattern_ = 0;

 public:
  void enter(const NavParam&) override { pattern_ = 0; }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    switch (pattern_) {
      case 0: {
        static const uint32_t kBars[8] = {0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000};
        for (int k = 0; k < 8; ++k) g.fillRect(k * 30, 0, 30, th::H, kBars[k]);
        break;
      }
      case 1:
        for (int y = 0; y < th::H; ++y) {
          uint8_t c = (uint8_t)(y * 255 / (th::H - 1));
          g.drawFastHLine(0, y, 80, (uint32_t)c << 16);
          g.drawFastHLine(80, y, 80, (uint32_t)c << 8);
          g.drawFastHLine(160, y, 80, c);
        }
        break;
      default:
        g.fillScreen(th::BLACK);
        g.drawRect(0, 0, th::W, th::H, th::WHITE);
        g.fillTriangle(0, 0, 30, 0, 0, 30, th::RED);  // top-left marker
        ui.text("TOP", 120, 16, th::fBodyB(), th::WHITE, textdatum_t::middle_center);
        ui.text("Red corner = top-left", 120, 150, th::fBody(), th::WHITE, textdatum_t::middle_center);
        ui.text("Red must look red (else set", 120, 176, th::fSmall(), th::MUTED, textdatum_t::middle_center);
        ui.text("TFT_RGB_ORDER in board_config.h)", 120, 194, th::fSmall(), th::MUTED, textdatum_t::middle_center);
        break;
    }
    if (ui.clickIn(Rect(0, 0, th::W, th::H))) {
      if (++pattern_ > 2) nav::back();
    }
  }
};

// ===========================================================================
class LogViewScreen : public Screen {
  ScrollState scroll_;

 public:
  void enter(const NavParam&) override {
    scroll_ = ScrollState();
    scroll_.offset = 1 << 20;  // start at the newest lines
  }
  bool animating() const override { return true; }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("Log")) {
      nav::back();
      return;
    }
    Rect view(0, th::CONTENT_Y, 240, th::H - th::CONTENT_Y);
    int n = plogHistoryCount();
    const int lineH = 12;
    int y0 = ui.beginScroll(scroll_, view, n * lineH + 8);
    char line[128];
    for (int i = 0; i < n; ++i) {
      int y = y0 + 4 + i * lineH;
      if (y + lineH < view.y || y > view.bottom()) continue;
      if (!plogHistory(i, line, sizeof line)) break;
      const char* p = strchr(line, '[');  // drop the millisecond stamp to fit the width
      ui.textFit(p ? p : line, 4, y + lineH / 2, 232, th::fTiny(), th::TEXT, textdatum_t::middle_left);
    }
    ui.endScroll(scroll_, view);
  }
};

// ===========================================================================
class AboutScreen : public Screen {
  ScrollState scroll_;

 public:
  void enter(const NavParam&) override { scroll_ = ScrollState(); }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.header("About")) {
      nav::back();
      return;
    }
    Rect view(0, th::CONTENT_Y, 240, th::H - th::CONTENT_Y);
    int y = ui.beginScroll(scroll_, view, 9 * 30 + 140);
    char v[64];
    snprintf(v, sizeof v, "%s %s", FW_NAME, FW_VERSION);
    screens::infoRow(y, "Phone", v);
    y += 30;
    screens::infoRow(y, "BT helper", bt::helperOnline() ? bt::helperVersion() : "offline");
    y += 30;
    screens::infoRow(y, "Hardware", "S3 + ESP32 + A7672S");
    y += 30;
    size_t used, total;
    storage::usage(&used, &total);
    snprintf(v, sizeof v, "%u / %u KB", (unsigned)(used / 1024), (unsigned)(total / 1024));
    screens::infoRow(y, "Storage", v);
    y += 30;
    snprintf(v, sizeof v, "%d", contacts::count());
    screens::infoRow(y, "Contacts", v);
    y += 30;
    snprintf(v, sizeof v, "%d / %d", messages::count(), messages::capacity());
    screens::infoRow(y, "Messages", v);
    y += 30;
    snprintf(v, sizeof v, "%d", calllog::count());
    screens::infoRow(y, "Call history", v);
    y += 30;
    const NetStatus& st = app::net();
    screens::infoRow(y, "IMEI", st.imei);
    y += 38;
    if (ui.button(Rect(8, y, 108, 36), "Restart", th::SURFACE2, th::AMBER)) {
      ui.confirm("Restart?", "The phone restarts in a few seconds.", "Restart", th::AMBER, [] { app::restart(); });
    }
    if (ui.button(Rect(124, y, 108, 36), "Reset BT", th::SURFACE2, th::TEXT)) bt::rebootHelper();
    y += 44;
    ui.text("Made from a generic ESP32, an ESP32-S3", 120, y + 8, th::fTiny(), th::DIM, textdatum_t::middle_center);
    ui.text("and a SIMCom A7672S 4G module.", 120, y + 22, th::fTiny(), th::DIM, textdatum_t::middle_center);
    ui.endScroll(scroll_, view);
  }
};

// ===========================================================================
namespace screens {
DiagnosticsScreen s_diag;
TestTouchScreen s_testTouch;
TestDisplayScreen s_testDisplay;
LogViewScreen s_log;
AboutScreen s_about;

void registerDiag() {
  nav::add(ScreenId::Diagnostics, &s_diag);
  nav::add(ScreenId::TestTouch, &s_testTouch);
  nav::add(ScreenId::TestDisplay, &s_testDisplay);
  nav::add(ScreenId::LogView, &s_log);
  nav::add(ScreenId::About, &s_about);
}
}  // namespace screens
