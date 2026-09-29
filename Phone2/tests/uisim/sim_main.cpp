// Renders every Phone2 screen to a PNG using the real UI code and fake
// phone data, so layouts can be checked (and shown in the docs) without hardware.
//
//   powershell -File tools\uisim.ps1     -> docs\screens\*.png
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <functional>
#include <string>

#include "call_log.h"
#include "contacts.h"
#include "gui.h"
#include "messages.h"
#include "phone_app.h"
#include "screens.h"
#include "settings.h"
#include "sim.h"
#include "text_util.h"

static std::string s_outDir = "screens";
static int s_count = 0;
static LGFX_Sprite* s_sheet = nullptr;  // 3x3 contact sheet for quick review
static int s_sheetSlot = 0, s_sheetNo = 0;

static void writePng(lgfx::LovyanGFX& src, int w, int h, const std::string& path) {
  size_t len = 0;
  void* png = src.createPng(&len, 0, 0, w, h);
  if (!png) {
    printf("PNG encode failed for %s\n", path.c_str());
    return;
  }
  FILE* f = fopen(path.c_str(), "wb");
  if (f) {
    fwrite(png, 1, len, f);
    fclose(f);
  }
  free(png);
}

static void flushSheet() {
  if (!s_sheet || !s_sheetSlot) return;
  char name[64];
  snprintf(name, sizeof name, "/sheet_%d.png", ++s_sheetNo);
  writePng(*s_sheet, 3 * 244 + 4, 3 * 324 + 4, s_outDir + name);
  s_sheet->fillScreen(0x404040u);
  s_sheetSlot = 0;
}

static void savePng(const char* name) {
  writePng(*display::canvas(), 240, 320, s_outDir + "/" + name + ".png");
  ++s_count;
  printf("  %s\n", name);
  if (!s_sheet) {
    static lgfx::LGFX_Device dummy;
    s_sheet = new LGFX_Sprite(&dummy);
    s_sheet->setColorDepth(16);
    s_sheet->createSprite(3 * 244 + 4, 3 * 324 + 4);
    s_sheet->fillScreen(0x404040u);
  }
  display::canvas()->pushSprite(s_sheet, 4 + (s_sheetSlot % 3) * 244, 4 + (s_sheetSlot / 3) * 324);
  if (++s_sheetSlot == 9) flushSheet();
}

// Renders the current screen once (plus a follow-up frame if it navigated).
static void frame(const TouchSample& t = TouchSample()) {
  lgfx::LovyanGFX& g = display::gfx();
  ui.beginFrame(&g, t, g_sim.clockMs, !ui.modalOpen());
  uint32_t gen = nav::generation();
  nav::screen()->frame(ui);
  if (nav::generation() != gen) {
    ui.beginFrame(&g, t, g_sim.clockMs, !ui.modalOpen());
    nav::screen()->frame(ui);
  }
  ui.drawOverlays();
}

// Screen position of a key on the on-screen keyboard (see ui/keyboard.cpp).
static TouchSample keyPress(char c) {
  static const char* rows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  static const int x0[3] = {0, 12, 36};
  const int top = 320 - 152, rowH = 38;
  TouchSample t;
  t.down = true;
  if (c == ' ') {
    t.x = 116;
    t.y = top + 3 * rowH + 19;
    return t;
  }
  for (int r = 0; r < 3; ++r) {
    const char* p = strchr(rows[r], c);
    if (p) {
      t.x = (int16_t)(x0[r] + (p - rows[r]) * 24 + 12);
      t.y = (int16_t)(top + r * rowH + 19);
    }
  }
  return t;
}

// Types through the real keyboard: press + release on each key.
static void typeText(const char* s) {
  for (; *s; ++s) {
    TouchSample down = keyPress((char)tolower((unsigned char)*s));
    frame(down);
    TouchSample up = down;
    up.down = false;
    frame(up);
    g_sim.clockMs += 150;
  }
}

static void shot(const char* name, ScreenId id, const NavParam& p = NavParam(), std::function<void()> before = nullptr,
                 TouchSample touch = TouchSample()) {
  g_sim.clockMs += 5000;  // lets toasts from the previous shot expire
  ui.closeModal();
  ui.in = Gui::Input();
  nav::reset(ScreenId::Home);
  if (id != ScreenId::Home) nav::push(id, p);
  frame();  // settle (layout caches, first frame)
  if (before) before();
  frame(touch);
  if (getenv("SIM_DEBUG")) printf("    clock=%lu toast=%d\n", (unsigned long)g_sim.clockMs, (int)ui.toastVisible());
  savePng(name);
  ui.in = Gui::Input();
}

static void seed() {
  uint32_t now = g_sim.now;
  contacts::add("Mom", "+919812345678");
  contacts::add("Ravi Kumar", "+919876543210");
  contacts::add("Priya Sharma", "+918800112233");
  contacts::add("Office", "01141234567");
  contacts::add("Amit Verma", "+919900011122");
  contacts::add("Dr. Mehta Clinic", "+912224445566");
  contacts::add("Neha", "+917700990011");
  contacts::add("Suresh Uncle", "+919811100022");

  auto sms = [](const char* num, const char* text, uint32_t ts, uint8_t flags) {
    SmsRecord r;
    memset(&r, 0, sizeof r);
    txt::copy(r.number, num, sizeof r.number);
    txt::copy(r.text, text, sizeof r.text);
    r.ts = ts;
    r.flags = flags;
    messages::add(r);
  };
  sms("AX-AIRTEL", "Dear Customer, your recharge of \xE2\x82\xB9" "299 is successful. Validity 28 days, 1.5GB/day. Dial *121# for offers.",
      now - 2 * 86400, 0);
  sms("+918800112233", "Happy birthday Priya! Have a great year ahead", now - 26 * 3600, SMS_OUT | SMS_FAILED);
  sms("+919876543210", "Meeting shifted to 4 PM, conference room B.", now - 5 * 3600, 0);
  sms("+919812345678", "Beta, dinner is ready at 8. Call me when you are free.", now - 3600, 0);
  sms("+919812345678", "Coming in 20 minutes, stuck in traffic", now - 3300, SMS_OUT);
  sms("+919812345678", "OK. Drive safe! Bring milk on the way home please.", now - 900, SMS_UNREAD);
  sms("+919876543210", "Also send me the report before 3.", now - 600, SMS_UNREAD);

  calllog::add("+919812345678", CallType::Incoming, now - 7200, 125);
  calllog::add("+919000012345", CallType::Outgoing, now - 5000, 42);
  calllog::add("01141234567", CallType::Rejected, now - 4000, 0);
  calllog::add("+919876543210", CallType::Missed, now - 1800, 0);
  calllog::add("+918800112233", CallType::Missed, now - 1200, 0);
  calllog::add("+919812345678", CallType::Outgoing, now - 600, 318);
}

int main(int argc, char** argv) {
  if (argc > 1) s_outDir = argv[1];
  g_sim.now = txt::makeEpoch(2026, 9, 26, 14, 5, 0);
  NetStatus& n = g_sim.net;
  n.modem = ModemState::Ready;
  n.sim = SimState::Ready;
  n.reg = RegState::Home;
  n.rat = Rat::Lte;
  n.csq = 22;
  n.rsrp = -93;
  n.rsrq = -9;
  n.sinr = 14;
  n.battMv = 3960;
  n.modemTempC = 34;
  n.cmux = true;
  n.baud = 115200;
  txt::copy(n.band, "LTE B40", sizeof n.band);
  txt::copy(n.op, "Airtel", sizeof n.op);
  txt::copy(n.imei, "861234567890123", sizeof n.imei);
  txt::copy(n.iccid, "89914509001234567890", sizeof n.iccid);
  txt::copy(n.ownNumber, "+919812300000", sizeof n.ownNumber);
  txt::copy(n.model, "A7672S-LASE", sizeof n.model);
  txt::copy(n.fw, "A011B07A7672M7", sizeof n.fw);
  DataStatus& d = g_sim.data;
  d.state = DataState::Connected;
  txt::copy(d.ip, "100.72.14.9", sizeof d.ip);
  txt::copy(d.dns, "49.44.59.65", sizeof d.dns);
  d.rxBytes = 38 * 1048576 + 220000;
  d.txBytes = 4 * 1048576 + 91000;
  d.sessions = 1;
  d.upSinceMs = g_sim.clockMs - 5400000;
  settings::load();
  settings::get().touchCalValid = 1;

  display::begin(200);
  screens::registerAll();
  seed();
  printf("Rendering screens to %s\n", s_outDir.c_str());

  shot("01_boot", ScreenId::Boot);
  shot("02_lock", ScreenId::Lock);
  shot("03_home", ScreenId::Home);
  shot("04_home_toast", ScreenId::Home, NavParam(), [] {
    ui.toast("Mom: OK. Drive safe! Bring milk on the way home please.", th::ACCENT);
  });
  shot("05_dialer", ScreenId::Dialer, NavParam::str("+919876543210"));

  g_sim.call.phase = CallPhase::Incoming;
  g_sim.call.incoming = true;
  txt::copy(g_sim.call.number, "+919812345678", sizeof g_sim.call.number);
  shot("06_call_incoming", ScreenId::Call);
  g_sim.call.phase = CallPhase::Active;
  g_sim.call.everActive = true;
  g_sim.callSeconds = 83;
  g_sim.audioRunning = true;
  shot("07_call_earbuds", ScreenId::Call);
  g_sim.route = AudioRoute::Speaker;
  g_sim.audioRunning = false;
  shot("08_call_speaker", ScreenId::Call);
  shot("09_call_banner", ScreenId::Messages);
  g_sim.route = AudioRoute::Earbuds;
  g_sim.call = CallInfo();

  shot("10_messages", ScreenId::Messages);
  shot("11_thread", ScreenId::Thread, NavParam::str("+919812345678"));
  shot("12_compose", ScreenId::Compose, NavParam::str("+919876543210", 1), [] {
    typeText("See you at the station at four");
  }, keyPress('t'));
  shot("13_thread_airtel", ScreenId::Thread, NavParam::str("AX-AIRTEL"));

  shot("14_contacts", ScreenId::Contacts);
  shot("15_contact_view", ScreenId::ContactView, NavParam::num(contacts::findByNumber("+919876543210")));
  shot("16_contact_edit", ScreenId::ContactEdit, NavParam::num(contacts::findByNumber("+918800112233")));
  shot("17_recents", ScreenId::Recents);

  shot("18_settings", ScreenId::Settings);
  shot("19_network", ScreenId::Network);
  shot("20_mobile_data", ScreenId::Data);
  shot("21_hotspot", ScreenId::Hotspot);
  shot("22_text_edit_apn", ScreenId::TextEdit, NavParam::num(0));
  shot("23_bluetooth", ScreenId::Bluetooth);
  g_sim.btOnline = false;
  shot("24_bluetooth_offline", ScreenId::Bluetooth);
  g_sim.btOnline = true;
  shot("25_sound", ScreenId::Sound);
  shot("26_display", ScreenId::Display);
  shot("27_datetime", ScreenId::DateTime);
  txt::copy(g_sim.ussdText,
            "Main Bal: Rs.12.50. Pack: Rs.299 Unlimited, valid till 24-Oct-2026. Data left today: 1.1 GB. "
            "Reply 1 for offers, 2 for last 3 transactions.",
            sizeof g_sim.ussdText);
  g_sim.ussdExpectsReply = true;
  shot("28_ussd", ScreenId::Ussd, NavParam::num(1));
  shot("29_diagnostics", ScreenId::Diagnostics);
  shot("30_log", ScreenId::LogView);
  shot("31_about", ScreenId::About);
  shot("32_calibrate", ScreenId::Calibrate);
  shot("33_test_display", ScreenId::TestDisplay);
  shot("34_confirm", ScreenId::Recents, NavParam(), [] {
    ui.confirm("Clear call history?", "All recent calls will be removed.", "Clear", th::RED, [] {});
  });
  g_sim.call = CallInfo();
  g_sim.call.phase = CallPhase::Incoming;
  g_sim.call.incoming = true;
  txt::copy(g_sim.call.number, "+919876543210", sizeof g_sim.call.number);
  shot("35_sheet", ScreenId::Call, NavParam(), [] {
    ui.sheet("Decline and send", app::kQuickReplies, app::kQuickReplyCount, [](int) {});
  });
  g_sim.call = CallInfo();
  // Problem states
  g_sim.phoneState = PhoneState::NoSim;
  g_sim.net.sim = SimState::Absent;
  g_sim.net.reg = RegState::NotRegistered;
  g_sim.data.state = DataState::WaitNetwork;
  g_sim.hotspotOn = false;
  shot("36_home_no_sim", ScreenId::Home);
  flushSheet();
  printf("%d screenshots written\n", s_count);
  return 0;
}
