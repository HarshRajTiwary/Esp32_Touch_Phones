// Messages: conversation list, conversation view and the composer.
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "phone_app.h"
#include "text_util.h"
#include "sms_pdu.h"
#include "contacts.h"
#include "messages.h"
#include "keyboard.h"
#include "screens.h"
#include "screens_internal.h"

using lgfx::textdatum_t;

namespace {

constexpr int kMaxThreads = 60;
constexpr int kMaxThreadMsgs = 120;
constexpr int kBubbleTextW = 168;
constexpr int kLineH = 18;

bool canReply(const char* number) { return txt::looksLikeNumber(number); }

// Draws `lines` wrapped lines of `text` starting at (x, y).
void drawLines(Gui& ui, const char* text, const int* starts, const int* lens, int n, int x, int y, uint32_t color) {
  char line[128];
  for (int k = 0; k < n; ++k) {
    int l = lens[k] < 127 ? lens[k] : 127;
    memcpy(line, text + starts[k], l);
    line[l] = 0;
    ui.text(line, x, y + k * kLineH, th::fBody(), color);
  }
}

}  // namespace

// ===========================================================================
class MessagesScreen : public Screen {
  messages::Thread threads_[kMaxThreads];
  int n_ = 0;
  ScrollState scroll_;

  void rebuild() { n_ = messages::buildThreads(threads_, kMaxThreads); }

 public:
  void enter(const NavParam&) override {
    scroll_ = ScrollState();
    rebuild();
  }
  void resume() override { rebuild(); }
  void onEvent(const AppEvent& e) override {
    if (e.type == Ev::SmsReceived || e.type == Ev::SmsSent) rebuild();
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    bool add = false;
    if (ui.header("Messages", Icon::Plus, &add)) {
      nav::back();
      return;
    }
    if (add) {
      nav::push(ScreenId::Compose);
      return;
    }
    if (!n_) {
      drawIcon(g, Icon::Message, 120, 140, 44, th::DIM);
      ui.text("No messages yet", 120, 184, th::fBody(), th::MUTED, textdatum_t::middle_center);
      if (ui.button(Rect(60, 206, 120, 36), "New message", th::ACCENT, th::WHITE)) nav::push(ScreenId::Compose);
      return;
    }
    Rect view(0, th::CONTENT_Y, 240, th::H - th::CONTENT_Y);
    const int rowH = 58;
    int y0 = ui.beginScroll(scroll_, view, n_ * rowH);
    uint32_t now = app::nowLocal();
    for (int k = 0; k < n_; ++k) {
      int y = y0 + k * rowH;
      if (y + rowH < view.y || y > view.bottom()) continue;
      const messages::Thread& t = threads_[k];
      const SmsRecord& last = messages::at(t.lastIndex);
      Rect row(0, y, 240, rowH);
      bool open = ui.listRow(row);
      bool hold = ui.longPress(row, 600);
      char name[40];
      screens::displayName(t.number, name, sizeof name);
      ui.avatar(28, y + rowH / 2, 19, contacts::findByNumber(t.number) >= 0 ? name : t.number);
      char when[20];
      txt::fmtWhen(last.ts, now, when, sizeof when);
      ui.text(when, 232, y + 17, th::fSmall(), t.unread ? th::ACCENT : th::MUTED, textdatum_t::middle_right);
      ui.textFit(name, 56, y + 17, 176 - ui.textW(when, th::fSmall()) - 8, th::fBodyB(), th::TEXT, textdatum_t::middle_left);
      char snippet[96], disp[96];
      snprintf(snippet, sizeof snippet, "%s%.88s", (last.flags & SMS_OUT) ? "You: " : "", last.text);
      txt::toDisplay(snippet, disp, sizeof disp);
      for (char* p = disp; *p; ++p)
        if (*p == '\n') *p = ' ';
      ui.textFit(disp, 56, y + 39, t.unread ? 150 : 172, th::fSmall(), t.unread ? th::TEXT : th::MUTED,
                 textdatum_t::middle_left);
      if (t.unread) ui.badge(222, y + 39, t.unread, th::ACCENT);
      if (hold) {
        char number[NUMBER_MAX];
        txt::copy(number, t.number, sizeof number);
        static const char* opts[] = {"Delete conversation"};
        ui.sheet(name, opts, 1, [this, number](int) {
          messages::removeThread(number);
          rebuild();
        });
      } else if (open) {
        nav::push(ScreenId::Thread, NavParam::str(t.number));
      }
    }
    ui.endScroll(scroll_, view);
  }
};

// ===========================================================================
class ThreadScreen : public Screen {
  struct Item {
    uint32_t id;
    int h;
  };
  char number_[NUMBER_MAX] = "";
  std::vector<Item> items_;
  ScrollState scroll_;
  int contentH_ = 0;
  bool stickBottom_ = true;

  void rebuild() {
    static int idx[kMaxThreadMsgs];
    int n = messages::threadIndices(number_, idx, kMaxThreadMsgs);
    items_.clear();
    contentH_ = 8;
    static char disp[SMS_TEXT_MAX + 64];
    for (int k = 0; k < n; ++k) {
      const SmsRecord& r = messages::at(idx[k]);
      txt::toDisplay(r.text, disp, sizeof disp);
      int lines = ui.wrap(disp, kBubbleTextW, th::fBody(), nullptr, nullptr, 40);
      Item it{r.id, lines * kLineH + 14 + 16};
      items_.push_back(it);
      contentH_ += it.h + 6;
    }
    messages::markThreadRead(number_);
    if (stickBottom_) {
      scroll_.contentH = contentH_;
      scroll_.toBottom(viewH());
    }
  }
  static int viewH() { return 282 - th::CONTENT_Y; }

 public:
  void enter(const NavParam& p) override {
    txt::copy(number_, p.s, sizeof number_);
    scroll_ = ScrollState();
    stickBottom_ = true;
    rebuild();
  }
  void resume() override {
    stickBottom_ = true;
    rebuild();
  }
  void onEvent(const AppEvent& e) override {
    if (e.type == Ev::SmsSent) rebuild();
    if (e.type == Ev::SmsReceived && e.ptr && txt::numbersMatch(static_cast<SmsRecord*>(e.ptr)->number, number_)) {
      stickBottom_ = true;
      rebuild();
    }
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    char name[40];
    screens::displayName(number_, name, sizeof name);
    bool call = false, del = false;
    bool back = canReply(number_) ? ui.header(name, Icon::Phone, &call, Icon::Trash, &del)
                                  : ui.header(name, Icon::Trash, &del);
    if (back) {
      nav::back();
      return;
    }
    if (call) app::dial(number_);
    if (del) {
      ui.confirm("Delete conversation?", "All messages with this contact will be removed.", "Delete", th::RED, [this] {
        messages::removeThread(number_);
        nav::back();
      });
    }

    Rect view(0, th::CONTENT_Y, 240, viewH());
    int y0 = ui.beginScroll(scroll_, view, contentH_);
    if (scroll_.dragging) stickBottom_ = false;
    int y = y0 + 8;
    uint32_t now = app::nowLocal();
    static char disp[SMS_TEXT_MAX + 64];
    for (const Item& it : items_) {
      int top = y;
      y += it.h + 6;
      if (top + it.h < view.y || top > view.bottom()) continue;
      SmsRecord* r = messages::byId(it.id);
      if (!r) continue;
      bool out = r->flags & SMS_OUT;
      txt::toDisplay(r->text, disp, sizeof disp);
      int starts[40], lens[40];
      int n = ui.wrap(disp, kBubbleTextW, th::fBody(), starts, lens, 40);
      int maxW = 0;
      for (int k = 0; k < n; ++k) {
        char line[128];
        int l = lens[k] < 127 ? lens[k] : 127;
        memcpy(line, disp + starts[k], l);
        line[l] = 0;
        int w = ui.textW(line, th::fBody());
        if (w > maxW) maxW = w;
      }
      int bw = maxW + 20, bh = n * kLineH + 14;
      if (bw < 44) bw = 44;
      int bx = out ? 232 - bw : 8;
      Rect bubble(bx, top, bw, bh);
      bool pressed = ui.pressingIn(bubble);
      uint32_t bg = out ? ((r->flags & SMS_FAILED) ? th::mix(th::RED, th::BG, 110) : th::BUBBLE_OUT) : th::BUBBLE_IN;
      if (pressed) bg = th::mix(bg, th::WHITE, 40);
      g.fillSmoothRoundRect(bubble.x, bubble.y, bubble.w, bubble.h, 12, bg);
      drawLines(ui, disp, starts, lens, n, bx + 10, top + 7, th::TEXT);

      char meta[48], when[20];
      txt::fmtWhen(r->ts, now, when, sizeof when);
      if (r->ts && now && r->ts / 86400 != now / 86400) {
        char clk[8];
        txt::fmtClock(r->ts, clk);
        snprintf(when + strlen(when), sizeof when - strlen(when), " %s", clk);
      }
      uint32_t metaCol = th::DIM;
      if (!out) snprintf(meta, sizeof meta, "%s%s", when, (r->flags & SMS_PARTIAL) ? "  (part missing)" : "");
      else if (r->flags & SMS_SENDING) snprintf(meta, sizeof meta, "Sending...");
      else if (r->flags & SMS_FAILED) { snprintf(meta, sizeof meta, "Not sent - tap to retry"); metaCol = th::RED; }
      else snprintf(meta, sizeof meta, "%s  Sent", when);
      ui.text(meta, out ? 230 : 10, top + bh + 3, th::fTiny(), metaCol, out ? textdatum_t::top_right : textdatum_t::top_left);

      if (ui.clickIn(bubble)) {
        uint32_t id = r->id;
        char found[NUMBER_MAX] = "";
        bool hasNum = screens::findPhoneNumber(r->text, found, sizeof found);
        static char callLabel[40];
        snprintf(callLabel, sizeof callLabel, "Call %s", found);
        const char* opts[3];
        int nOpts = 0;
        bool failed = r->flags & SMS_FAILED;
        if (failed) opts[nOpts++] = "Resend";
        if (hasNum) opts[nOpts++] = callLabel;
        opts[nOpts++] = "Delete message";
        std::string num(found);
        ui.sheet(nullptr, opts, nOpts, [this, id, failed, hasNum, num](int k) {
          if (failed && k == 0) { app::resendSms(id); rebuild(); return; }
          if (failed) --k;
          if (hasNum && k == 0) { app::dial(num.c_str()); return; }
          messages::remove(id);
          rebuild();
        });
      }
    }
    ui.endScroll(scroll_, view);
    if (items_.empty()) ui.text("No messages", 120, 160, th::fBody(), th::MUTED, textdatum_t::middle_center);

    // reply bar
    g.fillRect(0, 282, 240, 38, th::BG);
    g.drawFastHLine(0, 282, 240, th::BORDER);
    if (!canReply(number_)) {
      ui.text("Replies not possible to this sender", 120, 301, th::fTiny(), th::DIM, textdatum_t::middle_center);
      return;
    }
    Rect field(8, 286, 180, 30);
    bool pf = ui.pressingIn(field);
    g.fillSmoothRoundRect(field.x, field.y, field.w, field.h, 15, pf ? th::PRESSED : th::SURFACE2);
    ui.text("Type a message...", field.x + 14, field.cy(), th::fBody(), th::DIM, textdatum_t::middle_left);
    bool send = ui.roundButton(212, 301, 15, Icon::Send, th::ACCENT, th::WHITE, 16);
    if (ui.clickIn(field) || send) nav::push(ScreenId::Compose, NavParam::str(number_, 1));
  }
};

// ===========================================================================
class ComposeScreen : public Screen {
  char to_[NUMBER_MAX] = "";
  char body_[SMS_TEXT_MAX] = "";
  char draftFor_[NUMBER_MAX] = "";
  bool focusBody_ = false;

 public:
  void enter(const NavParam& p) override {
    if (!txt::numbersMatch(p.s, draftFor_) || !p.s[0]) body_[0] = 0;  // keep an unsent draft for the same person
    txt::copy(to_, p.s, sizeof to_);
    txt::copy(draftFor_, p.s, sizeof draftFor_);
    focusBody_ = to_[0] != 0;
    kb.reset(body_);
  }
  void resume() override {
    if (pick::done) {
      txt::copy(to_, pick::number, sizeof to_);
      pick::done = false;
      focusBody_ = true;
      kb.reset(body_);
    }
  }
  void send(Gui& ui) {
    char digits[24];
    txt::digitsOnly(to_, digits, sizeof digits);
    if (strlen(digits) < 3) {
      ui.toast("Enter a phone number first", th::AMBER);
      focusBody_ = false;
      return;
    }
    txt::trim(body_);
    if (!body_[0]) {
      ui.toast("Type a message", th::AMBER);
      return;
    }
    int used, per;
    bool uni;
    if (smsMeasure(body_, &used, &per, &uni) == 0) {
      ui.toast("Message is too long", th::RED);
      return;
    }
    char num[NUMBER_MAX];
    txt::copy(num, to_, sizeof num);
    if (app::sendSms(num, body_)) {
      body_[0] = 0;
      draftFor_[0] = 0;
      if (nav::previous() == ScreenId::Thread) nav::back();
      else nav::replace(ScreenId::Thread, NavParam::str(num));
    } else {
      ui.toast("Could not queue the message", th::RED);
    }
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    char title[40] = "New message";
    if (to_[0] && contacts::findByNumber(to_) >= 0) screens::displayName(to_, title, sizeof title);
    if (ui.header(title)) {
      nav::back();
      return;
    }
    // recipient row
    Rect toRow(0, th::CONTENT_Y, 196, 34);
    if (!focusBody_) g.fillRect(toRow.x, toRow.y, 240, toRow.h, th::SURFACE);
    ui.text("To:", 10, toRow.cy(), th::fSmall(), th::MUTED, textdatum_t::middle_left);
    ui.textFit(to_[0] ? to_ : "Number", 38, toRow.cy(), 150, th::fBody(), to_[0] ? th::TEXT : th::DIM, textdatum_t::middle_left);
    if (ui.clickIn(toRow)) focusBody_ = false;
    if (ui.iconButton(Rect(200, th::CONTENT_Y, 40, 34), Icon::Person, th::ACCENT)) {
      pick::active = true;
      pick::done = false;
      nav::push(ScreenId::Contacts, NavParam::num(1));
      return;
    }
    g.drawFastHLine(0, th::CONTENT_Y + 34, 240, th::BORDER);

    int kbTop = focusBody_ ? th::H - Keyboard::kHeight : th::H - NumPad::kHeight;
    Rect area(0, th::CONTENT_Y + 35, 240, kbTop - th::CONTENT_Y - 35);
    // counter
    int used, per;
    bool uni;
    int parts = smsMeasure(body_, &used, &per, &uni);
    char counter[24];
    if (parts == 0) txt::copy(counter, "too long", sizeof counter);
    else if (parts > 1) snprintf(counter, sizeof counter, "%d/%d (%d SMS)", used, per * parts, parts);
    else snprintf(counter, sizeof counter, "%d/%d", used, per);
    ui.text(counter, 232, area.y + 4, th::fTiny(), parts == 0 ? th::RED : th::DIM, textdatum_t::top_right);

    // body text: show the last lines that fit, with a cursor
    static char disp[SMS_TEXT_MAX + 64];
    txt::toDisplay(body_, disp, sizeof disp);
    int starts[40], lens[40];
    int n = ui.wrap(disp, 216, th::fBody(), starts, lens, 40);
    int visible = (area.h - 18) / kLineH;
    int first = n > visible ? n - visible : 0;
    if (!body_[0]) {
      ui.text("Message", 12, area.y + 14, th::fBody(), th::DIM);
    } else {
      drawLines(ui, disp, starts + first, lens + first, n - first, 12, area.y + 14, th::TEXT);
    }
    if (focusBody_ && (ui.now / 500) % 2 == 0) {
      int lastLine = n - 1 - first;
      char line[128];
      int l = lens[n - 1] < 127 ? lens[n - 1] : 127;
      memcpy(line, disp + starts[n - 1], l);
      line[l] = 0;
      int cx = 12 + (body_[0] ? ui.textW(line, th::fBody()) : 0) + 1;
      g.fillRect(cx, area.y + 12 + lastLine * kLineH, 2, 16, th::ACCENT);
    }
    // quick replies
    if (ui.roundButton(222, area.bottom() - 16, 12, Icon::Bolt, th::SURFACE2, th::AMBER, 14)) {
      ui.sheet("Quick reply", app::kQuickReplies, app::kQuickReplyCount, [this](int k) {
        txt::copy(body_, app::kQuickReplies[k], sizeof body_);
        focusBody_ = true;
      });
    }
    if (ui.clickIn(area)) {  // after the quick-reply button so it gets its taps first
      if (!focusBody_) kb.reset(body_);
      focusBody_ = true;
    }

    if (focusBody_) {
      if (kb.draw(ui, kbTop, body_, sizeof body_, "Send") == KeyAction::Action) send(ui);
    } else {
      KeyAction a = numpad.draw(ui, kbTop, to_, sizeof to_, "Next", true, false);
      if (a == KeyAction::Action) {
        focusBody_ = true;
        kb.reset(body_);
      }
    }
  }
  bool animating() const override { return true; }  // cursor blink
};

// ===========================================================================
namespace screens {
MessagesScreen s_messages;
ThreadScreen s_thread;
ComposeScreen s_compose;

void registerSms() {
  nav::add(ScreenId::Messages, &s_messages);
  nav::add(ScreenId::Thread, &s_thread);
  nav::add(ScreenId::Compose, &s_compose);
}
}  // namespace screens
