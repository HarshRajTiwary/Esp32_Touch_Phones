// Dialer, call (incoming / outgoing / active) and recent calls screens.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../app/phone_app.h"
#include "../bt/bt_manager.h"
#include "../core/text_util.h"
#include "../store/call_log.h"
#include "../store/contacts.h"
#include "keyboard.h"
#include "screens.h"
#include "screens_internal.h"

using lgfx::textdatum_t;

namespace screens {

void displayName(const char* number, char* out, size_t cap) {
  char raw[64];
  contacts::displayName(number, raw, sizeof raw);
  txt::toDisplay(raw, out, cap);
}

bool findPhoneNumber(const char* text, char* out, size_t cap) {
  for (const char* p = text; *p; ++p) {
    if (!((*p >= '0' && *p <= '9') || *p == '+')) continue;
    if (p > text && p[-1] >= '0' && p[-1] <= '9') continue;
    char buf[24];
    size_t n = 0;
    const char* q = p;
    if (*q == '+') buf[n++] = *q++;
    while (*q && n + 1 < sizeof buf) {
      if (*q >= '0' && *q <= '9') buf[n++] = *q;
      else if (*q != ' ' && *q != '-') break;
      else if (!(q[1] >= '0' && q[1] <= '9')) break;
      ++q;
    }
    buf[n] = 0;
    char digits[24];
    txt::digitsOnly(buf, digits, sizeof digits);
    size_t d = strlen(digits);
    if (d >= 10 && d <= 13) {
      txt::copy(out, buf, cap);
      return true;
    }
    p = q > p ? q - 1 : p;
  }
  return false;
}

void infoRow(int y, const char* label, const char* value) {
  ui.text(label, 12, y + 14, th::fSmall(), th::MUTED, textdatum_t::middle_left);
  char v[64];
  txt::toDisplay(value, v, sizeof v);
  int room = 216 - ui.textW(label, th::fSmall()) - 10;  // never run into the label
  const lgfx::IFont* f = ui.textW(v, th::fBody()) <= room ? th::fBody() : th::fSmall();
  ui.textFit(v[0] ? v : "-", 228, y + 14, room, f, th::TEXT, textdatum_t::middle_right);
  ui.g->drawFastHLine(12, y + 29, 216, th::BORDER);
}

}  // namespace screens

// ===========================================================================
class DialerScreen : public Screen {
  char num_[NUMBER_MAX] = "";

  void keypadKey(Gui& ui, const Rect& r, const char* digit, const char* letters) {
    bool pressed = ui.pressingIn(r);
    ui.g->fillSmoothRoundRect(r.x, r.y, r.w, r.h, 12, pressed ? th::PRESSED : th::SURFACE);
    ui.text(digit, r.cx(), r.cy() - (letters[0] ? 5 : 0), th::fLarge(), th::TEXT, textdatum_t::middle_center);
    if (letters[0]) ui.text(letters, r.cx(), r.bottom() - 9, th::fTiny(), th::MUTED, textdatum_t::middle_center);
  }

 public:
  void enter(const NavParam& p) override { txt::copy(num_, p.s, sizeof num_); }
  void resume() override {
    if (pick::done) {
      txt::copy(num_, pick::number, sizeof num_);
      pick::done = false;
    }
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    if (ui.iconButton(Rect(0, 20, 40, 36), Icon::Back, th::TEXT)) {
      nav::back();
      return;
    }

    // number display, largest font that fits
    if (num_[0]) {
      const lgfx::IFont* fonts[] = {th::fLarge(), th::fMedium(), th::fBody()};
      const lgfx::IFont* f = th::fBody();
      for (auto* c : fonts)
        if (ui.textW(num_, c) <= 196) { f = c; break; }
      ui.textFit(num_, 124, 48, 200, f, th::TEXT, textdatum_t::middle_center);
      int ci = contacts::findByNumber(num_);
      if (ci >= 0) {
        char name[40];
        txt::toDisplay(contacts::at(ci).name, name, sizeof name);
        ui.textFit(name, 120, 76, 220, th::fSmall(), th::GREEN, textdatum_t::middle_center);
      } else if (txt::isUssdCode(num_)) {
        ui.text("Service code - press Send", 120, 76, th::fSmall(), th::AMBER, textdatum_t::middle_center);
      }
    } else {
      ui.text("Enter number", 124, 48, th::fMedium(), th::DIM, textdatum_t::middle_center);
    }

    static const char* const kDigits[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"};
    static const char* const kLetters[12] = {"", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ", "", "+", ""};
    for (int k = 0; k < 12; ++k) {
      Rect r(6 + (k % 3) * 78, 88 + (k / 3) * 46, 72, 42);
      keypadKey(ui, r, kDigits[k], kLetters[k]);
      if (k == 10 && ui.longPress(r, 550)) {  // hold 0 for +
        size_t n = strlen(num_);
        if (n + 1 < sizeof num_) { num_[n] = '+'; num_[n + 1] = 0; }
      } else if (ui.clickIn(r)) {
        size_t n = strlen(num_);
        if (n + 1 < sizeof num_) { num_[n] = kDigits[k][0]; num_[n + 1] = 0; }
      }
    }

    // bottom row
    if (ui.iconButton(Rect(12, 274, 56, 42), Icon::Person, th::TEXT)) {
      pick::active = true;
      pick::done = false;
      nav::push(ScreenId::Contacts, NavParam::num(1));
      return;
    }
    bool ussd = txt::isUssdCode(num_);
    if (ui.button(Rect(76, 274, 88, 42), ussd ? "Send" : "", ussd ? th::AMBER : th::GREEN, th::WHITE, th::fBodyB(), 21,
                  ussd ? Icon::None : Icon::Phone)) {
      if (!num_[0]) {
        txt::copy(num_, calllog::lastDialed(), sizeof num_);  // like real phones: call key recalls the last number
      } else {
        app::dial(num_);
        if (!ussd) num_[0] = 0;
      }
      return;
    }
    Rect bs(172, 274, 56, 42);
    if (num_[0]) {
      if (ui.longPress(bs, 500)) num_[0] = 0;
      else if (ui.iconButton(bs, Icon::Backspace, th::TEXT, th::PRESSED, 22)) num_[strlen(num_) - 1] = 0;
    }
  }
};

// ===========================================================================
class CallScreen : public Screen {
  bool keypad_ = false;
  char dtmf_[24] = "";
  bool ended_ = false;
  uint32_t endedAt_ = 0;
  CallInfo endInfo_;
  uint32_t endDuration_ = 0;
  uint32_t idleSince_ = 0;

  const char* endText(CallEndReason r) {
    switch (r) {
      case CallEndReason::Busy: return "Number busy";
      case CallEndReason::NoAnswer: return "No answer";
      case CallEndReason::Rejected: return "Call declined";
      case CallEndReason::Missed: return "Missed call";
      case CallEndReason::Failed: return "Call failed";
      case CallEndReason::NoNetwork: return "No network";
      default: return "Call ended";
    }
  }

 public:
  void enter(const NavParam&) override {
    keypad_ = false;
    dtmf_[0] = 0;
    ended_ = false;
  }
  bool animating() const override { return true; }
  void onEvent(const AppEvent& e) override {
    if (e.type == Ev::CallEnded && e.ptr) {
      endInfo_ = *static_cast<const CallInfo*>(e.ptr);
      endDuration_ = endInfo_.everActive ? (millis() - endInfo_.activeMs) / 1000 : 0;
      ended_ = true;
      endedAt_ = millis();
    } else if (e.type == Ev::CallChanged && app::call().phase != CallPhase::Idle) {
      ended_ = false;  // a new call started while "Call ended" was showing
    }
  }

  void frame(Gui& ui) override {
    auto& g = *ui.g;
    CallInfo c = ended_ ? endInfo_ : app::call();
    if (!ended_ && c.phase == CallPhase::Idle) {
      // The "call ended" event normally arrives right after the idle state;
      // wait briefly for it so the summary can be shown.
      if (!idleSince_) idleSince_ = millis();
      if (millis() - idleSince_ > 600) {
        idleSince_ = 0;
        nav::remove(ScreenId::Call);
      }
      return;
    }
    idleSince_ = 0;
    if (ended_ && millis() - endedAt_ > 1800) {
      nav::remove(ScreenId::Call);
      return;
    }
    g.fillScreen(th::BG);
    ui.statusBar();

    char name[40];
    screens::displayName(c.number, name, sizeof name);
    bool known = contacts::findByNumber(c.number) >= 0;
    bool incoming = !ended_ && c.phase == CallPhase::Incoming;

    char state[40];
    uint32_t stateColor = th::MUTED;
    if (ended_) {
      if (endDuration_) {
        char d[16];
        txt::fmtDuration(endDuration_, d);
        snprintf(state, sizeof state, "%s  %s", endText(endInfo_.endReason), d);
      } else {
        txt::copy(state, endText(endInfo_.endReason), sizeof state);
      }
      stateColor = endInfo_.endReason == CallEndReason::Normal ? th::MUTED : th::RED;
    } else {
      switch (c.phase) {
        case CallPhase::Incoming: txt::copy(state, "Incoming call", sizeof state); break;
        case CallPhase::Dialing: txt::copy(state, "Calling...", sizeof state); break;
        case CallPhase::Alerting: txt::copy(state, "Ringing...", sizeof state); break;
        default: txt::fmtDuration(app::callSeconds(), state); stateColor = th::GREEN; break;
      }
    }

    if (keypad_ && !ended_ && c.phase == CallPhase::Active) {
      ui.text(dtmf_[0] ? dtmf_ : name, 120, 40, th::fMedium(), th::TEXT, textdatum_t::middle_center);
      static const char kKeys[] = "123456789*0#";
      for (int k = 0; k < 12; ++k) {
        Rect r(18 + (k % 3) * 70, 62 + (k / 3) * 46, 64, 42);
        bool pr = ui.pressingIn(r);
        g.fillSmoothRoundRect(r.x, r.y, r.w, r.h, 12, pr ? th::PRESSED : th::SURFACE);
        char s[2] = {kKeys[k], 0};
        ui.text(s, r.cx(), r.cy(), th::fLarge(), th::TEXT, textdatum_t::middle_center);
        if (ui.clickIn(r)) {
          app::dtmf(kKeys[k]);
          size_t n = strlen(dtmf_);
          if (n + 1 >= sizeof dtmf_) memmove(dtmf_, dtmf_ + 1, n--);
          dtmf_[n] = kKeys[k];
          dtmf_[n + 1] = 0;
        }
      }
      if (ui.button(Rect(80, 250, 80, 24), "Hide", th::SURFACE2, th::TEXT, th::fSmall(), 12)) keypad_ = false;
    } else {
      int avY = incoming ? 96 : 86, avR = incoming ? 38 : 34;
      if (incoming) {  // pulsing ring
        float ph = (millis() % 1400) / 1400.0f;
        int rr = avR + 4 + (int)(ph * 18);
        g.fillArc(120, avY, rr - 2, rr, 0, 360, th::mix(th::GREEN, th::BG, (uint8_t)(90 + ph * 165)));
      }
      ui.avatar(120, avY, avR, known ? name : "", th::fTitle());
      int ny = avY + avR + 20;
      ui.textFit(name, 120, ny, 224, th::fTitle(), th::TEXT, textdatum_t::middle_center);
      if (known) ui.textFit(c.number, 120, ny + 22, 224, th::fBody(), th::MUTED, textdatum_t::middle_center);
      ui.text(state, 120, 34, th::fBody(), stateColor, textdatum_t::middle_center);
    }

    if (ended_) return;

    if (incoming) {
      if (ui.roundButton(60, 250, 28, Icon::PhoneEnd, th::RED, th::WHITE, 30)) {
        app::hangup();
        return;
      }
      if (ui.roundButton(180, 250, 28, Icon::Phone, th::GREEN, th::WHITE, 30)) {
        app::answer();
        return;
      }
      if (ui.roundButton(120, 250, 17, Icon::Message, th::SURFACE2, th::TEXT, 18)) {
        ui.sheet("Decline and send", app::kQuickReplies, app::kQuickReplyCount,
                 [](int k) { app::rejectWithSms(app::kQuickReplies[k]); });
      }
      ui.text("Decline", 60, 292, th::fSmall(), th::MUTED, textdatum_t::middle_center);
      ui.text("Answer", 180, 292, th::fSmall(), th::MUTED, textdatum_t::middle_center);
      ui.text("Message", 120, 278, th::fTiny(), th::DIM, textdatum_t::middle_center);
      return;
    }

    if (!keypad_) {
      // audio route chip
      Rect chip(48, 172, 144, 26);
      bool buds = app::earbudsInUse();
      bool pr = ui.pressingIn(chip);
      g.fillSmoothRoundRect(chip.x, chip.y, chip.w, chip.h, 13, pr ? th::PRESSED : th::SURFACE2);
      drawIcon(g, buds ? Icon::Headphones : Icon::VolUp, chip.x + 18, chip.cy(), 14, buds ? th::TEAL : th::TEXT,
               pr ? th::PRESSED : th::SURFACE2);
      ui.text(buds ? "Earbuds" : "Speaker", chip.x + 32, chip.cy(), th::fSmall(), th::TEXT, textdatum_t::middle_left);
      if (ui.clickIn(chip)) {
        if (!bt::connected()) ui.toast("No earbuds connected - using the speaker", th::AMBER);
        else app::setAudioRoute(buds ? AudioRoute::Wired : AudioRoute::Earbuds);
      }

      // controls
      struct Ctl { Icon icon; const char* label; bool on; };
      Ctl ctl[4] = {{c.muted ? Icon::MicOff : Icon::Mic, c.muted ? "Unmute" : "Mute", c.muted},
                    {Icon::Keypad, "Keypad", false},
                    {Icon::VolDown, "Vol -", false},
                    {Icon::VolUp, "Vol +", false}};
      for (int k = 0; k < 4; ++k) {
        Rect r(6 + k * 58, 206, 54, 44);
        bool p = ui.pressingIn(r);
        uint32_t bg = ctl[k].on ? th::TEXT : (p ? th::PRESSED : th::SURFACE);
        g.fillSmoothRoundRect(r.x, r.y, r.w, r.h, 10, bg);
        drawIcon(g, ctl[k].icon, r.cx(), r.y + 16, 18, ctl[k].on ? th::BG : th::TEXT, bg);
        ui.text(ctl[k].label, r.cx(), r.bottom() - 9, th::fTiny(), ctl[k].on ? th::BG : th::MUTED, textdatum_t::middle_center);
        if (ui.clickIn(r)) {
          if (k == 0) app::toggleMute();
          else if (k == 1) keypad_ = c.phase == CallPhase::Active;
          else {
            app::changeCallVolume(k == 2 ? -1 : +1);
            char v[24];
            snprintf(v, sizeof v, "Volume %d%%", app::callVolumePercent());
            ui.toast(v, th::TEAL);
          }
        }
      }
    }
    if (ui.roundButton(120, 286, 26, Icon::PhoneEnd, th::RED, th::WHITE, 28)) app::hangup();
  }
};

// ===========================================================================
class RecentsScreen : public Screen {
  ScrollState scroll_;
  int filter_ = 0;
  int idx_[MAX_CALL_LOG];
  int n_ = 0;

  void rebuild() {
    n_ = 0;
    for (int i = 0; i < calllog::count(); ++i) {
      const CallLogEntry& e = calllog::at(i);
      if (filter_ == 1 && e.type != (uint8_t)CallType::Missed && e.type != (uint8_t)CallType::Rejected) continue;
      idx_[n_++] = i;
    }
  }

 public:
  void enter(const NavParam&) override {
    calllog::markSeen();
    scroll_ = ScrollState();
    rebuild();
  }
  void resume() override { rebuild(); }
  void onEvent(const AppEvent& e) override {
    if (e.type == Ev::CallEnded) {
      rebuild();
      calllog::markSeen();
    }
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    bool clear = false;
    if (ui.header("Recents", calllog::count() ? Icon::Trash : Icon::None, &clear)) {
      nav::back();
      return;
    }
    if (clear) {
      ui.confirm("Clear call history?", "All recent calls will be removed.", "Clear", th::RED, [this] {
        calllog::clearAll();
        rebuild();
      });
    }
    static const char* const kTabs[2] = {"All", "Missed"};
    int f = ui.segmented(Rect(8, 60, 224, 30), kTabs, 2, filter_);
    if (f != filter_) {
      filter_ = f;
      scroll_ = ScrollState();
      rebuild();
    }
    Rect view(0, 96, 240, 224);
    if (!n_) {
      drawIcon(g, Icon::Recents, 120, 170, 40, th::DIM);
      ui.text(filter_ ? "No missed calls" : "No calls yet", 120, 212, th::fBody(), th::MUTED, textdatum_t::middle_center);
      return;
    }
    const int rowH = 50;
    int y0 = ui.beginScroll(scroll_, view, n_ * rowH);
    uint32_t now = app::nowLocal();
    for (int k = 0; k < n_; ++k) {
      int y = y0 + k * rowH;
      if (y + rowH < view.y || y > view.bottom()) continue;
      int li = idx_[k];
      const CallLogEntry& e = calllog::at(li);
      Rect row(0, y, 240, rowH);
      Rect cb(192, y + 6, 44, rowH - 12);
      bool callBack = ui.iconButton(cb, Icon::Phone, th::GREEN, th::PRESSED, 20);
      bool open = ui.listRow(row);
      bool missed = e.type == (uint8_t)CallType::Missed || e.type == (uint8_t)CallType::Rejected;
      Icon ic = e.type == (uint8_t)CallType::Outgoing ? Icon::CallOut : Icon::CallIn;
      uint32_t icCol = e.type == (uint8_t)CallType::Outgoing ? th::GREEN : missed ? th::RED : th::ACCENT;
      drawIcon(g, ic, 20, y + rowH / 2, 16, icCol, th::BG);
      drawIcon(g, Icon::Phone, cb.cx(), cb.cy(), 20, th::GREEN);
      char name[40];
      screens::displayName(e.number, name, sizeof name);
      ui.textFit(name, 40, y + 16, 146, th::fBodyB(), missed ? th::RED : th::TEXT, textdatum_t::middle_left);
      char when[20], detail[48];
      txt::fmtWhen(e.ts, now, when, sizeof when);
      // The arrow icon and colour already say in/out; spell out only the unhappy cases.
      if (e.type == (uint8_t)CallType::Missed) snprintf(detail, sizeof detail, "Missed  %s", when);
      else if (e.type == (uint8_t)CallType::Rejected) snprintf(detail, sizeof detail, "Declined  %s", when);
      else if (e.duration) {
        char d[16];
        txt::fmtDuration(e.duration, d);
        snprintf(detail, sizeof detail, "%s  |  %s", when, d);
      } else {
        snprintf(detail, sizeof detail, "%s  |  no answer", when);
      }
      ui.textFit(detail, 40, y + 35, 148, th::fSmall(), th::MUTED, textdatum_t::middle_left);
      if (callBack) {
        app::dial(e.number);
      } else if (open) {
        char number[NUMBER_MAX];
        txt::copy(number, e.number, sizeof number);
        bool known = contacts::findByNumber(number) >= 0;
        static const char* optsKnown[] = {"Call", "Send message", "View contact", "Delete from history"};
        static const char* optsNew[] = {"Call", "Send message", "Save as contact", "Delete from history"};
        ui.sheet(name, known ? optsKnown : optsNew, 4, [this, number, li, known](int opt) {
          if (opt == 0) app::dial(number);
          else if (opt == 1) nav::push(ScreenId::Thread, NavParam::str(number));
          else if (opt == 2) {
            if (known) nav::push(ScreenId::ContactView, NavParam::num(contacts::findByNumber(number)));
            else nav::push(ScreenId::ContactEdit, NavParam::str(number, -1));
          } else {
            calllog::remove(li);
            rebuild();
          }
        });
      }
    }
    ui.endScroll(scroll_, view);
  }
};

// ===========================================================================
namespace screens {
DialerScreen s_dialer;
CallScreen s_call;
RecentsScreen s_recents;

void registerPhone() {
  nav::add(ScreenId::Dialer, &s_dialer);
  nav::add(ScreenId::Call, &s_call);
  nav::add(ScreenId::Recents, &s_recents);
}
}  // namespace screens
