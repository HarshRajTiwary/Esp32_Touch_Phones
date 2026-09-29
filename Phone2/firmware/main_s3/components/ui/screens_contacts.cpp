// Contacts list (also used as a picker), contact details and the editor.
#include <stdio.h>
#include <string.h>

#include "phone_app.h"
#include "text_util.h"
#include "contacts.h"
#include "keyboard.h"
#include "screens.h"
#include "screens_internal.h"

using lgfx::textdatum_t;

namespace {
int s_editResult = -1;  // index of the contact the editor just saved
}

// ===========================================================================
class ContactsScreen : public Screen {
  bool pickMode_ = false;
  bool searching_ = false;
  char query_[32] = "";
  ScrollState scroll_;
  int idx_[MAX_CONTACTS];
  int n_ = 0;

  void filter() {
    n_ = 0;
    for (int i = 0; i < contacts::count(); ++i) {
      const Contact& c = contacts::at(i);
      if (query_[0] && !txt::icontains(c.name, query_) && !strstr(c.number, query_)) continue;
      idx_[n_++] = i;
    }
  }

 public:
  void enter(const NavParam& p) override {
    pickMode_ = p.i == 1;
    searching_ = false;
    query_[0] = 0;
    scroll_ = ScrollState();
    filter();
  }
  void resume() override { filter(); }
  void onEvent(const AppEvent& e) override {
    if (e.type == Ev::SimContacts) filter();
  }
  void leave(bool picked) {
    if (pickMode_ && !picked) {
      pick::active = false;
      pick::done = false;
    }
    nav::back();
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    bool add = false;
    if (ui.header(pickMode_ ? "Choose contact" : "Contacts", pickMode_ ? Icon::None : Icon::Plus, &add)) {
      if (searching_) {
        searching_ = false;
        query_[0] = 0;
        filter();
      } else {
        leave(false);
      }
      return;
    }
    if (add) {
      nav::push(ScreenId::ContactEdit, NavParam::num(-1));
      return;
    }

    Rect search(8, 60, 224, 30);
    g.fillSmoothRoundRect(search.x, search.y, search.w, search.h, 15, searching_ ? th::SURFACE2 : th::SURFACE);
    drawIcon(g, Icon::Search, search.x + 16, search.cy(), 14, th::MUTED, searching_ ? th::SURFACE2 : th::SURFACE);
    ui.textFit(query_[0] ? query_ : "Search", search.x + 32, search.cy(), 180, th::fBody(), query_[0] ? th::TEXT : th::DIM,
               textdatum_t::middle_left);
    if (ui.clickIn(search) && !searching_) {
      searching_ = true;
      kb.reset("x");  // lower-case first letter for search
    }

    int bottom = searching_ ? th::H - Keyboard::kHeight : th::H;
    Rect view(0, 96, 240, bottom - 96);
    if (!n_) {
      ui.text(query_[0] ? "No match" : "No contacts yet", 120, view.y + 40, th::fBody(), th::MUTED, textdatum_t::middle_center);
      if (!query_[0]) {
        ui.text("Tap + to add one, or import", 120, view.y + 66, th::fSmall(), th::DIM, textdatum_t::middle_center);
        ui.text("from the SIM in Settings", 120, view.y + 82, th::fSmall(), th::DIM, textdatum_t::middle_center);
      }
    } else {
      const int rowH = 46;
      int y0 = ui.beginScroll(scroll_, view, n_ * rowH);
      for (int k = 0; k < n_; ++k) {
        int y = y0 + k * rowH;
        if (y + rowH < view.y || y > view.bottom()) continue;
        const Contact& c = contacts::at(idx_[k]);
        Rect row(0, y, 240, rowH);
        bool open = ui.listRow(row);
        char name[40];
        txt::toDisplay(c.name, name, sizeof name);
        ui.avatar(26, y + rowH / 2, 16, name, th::fSmall());
        ui.textFit(name, 50, y + 15, 180, th::fBodyB(), th::TEXT, textdatum_t::middle_left);
        ui.textFit(c.number, 50, y + 33, 180, th::fSmall(), th::MUTED, textdatum_t::middle_left);
        if (open) {
          if (pickMode_) {
            txt::copy(pick::number, c.number, sizeof pick::number);
            txt::copy(pick::name, c.name, sizeof pick::name);
            pick::done = true;
            pick::active = false;
            leave(true);
          } else {
            nav::push(ScreenId::ContactView, NavParam::num(idx_[k]));
          }
          ui.endScroll(scroll_, view);
          return;
        }
      }
      ui.endScroll(scroll_, view);
    }
    if (searching_) {
      KeyAction a = kb.draw(ui, th::H - Keyboard::kHeight, query_, sizeof query_, "Done");
      if (a == KeyAction::Typed) {
        scroll_ = ScrollState();
        filter();
      } else if (a == KeyAction::Action) {
        searching_ = false;
      }
    }
  }
};

// ===========================================================================
class ContactViewScreen : public Screen {
  int idx_ = -1;
  Contact c_{};

 public:
  void enter(const NavParam& p) override {
    idx_ = p.i;
    c_ = contacts::at(idx_);
  }
  void resume() override {
    if (s_editResult >= 0) idx_ = s_editResult;
    else idx_ = contacts::indexOf(c_.name, c_.number);
    s_editResult = -1;
    if (idx_ < 0 || idx_ >= contacts::count()) {
      nav::back();
      return;
    }
    c_ = contacts::at(idx_);
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    bool edit = false;
    if (ui.header("Contact", Icon::Edit, &edit)) {
      nav::back();
      return;
    }
    if (edit) {
      nav::push(ScreenId::ContactEdit, NavParam::num(idx_));
      return;
    }
    char name[40];
    txt::toDisplay(c_.name, name, sizeof name);
    ui.avatar(120, 104, 40, name, th::fTitle());
    ui.textFit(name, 120, 164, 224, th::fTitle(), th::TEXT, textdatum_t::middle_center);
    ui.textFit(c_.number, 120, 190, 224, th::fMedium(), th::MUTED, textdatum_t::middle_center);

    struct Act { Icon icon; uint32_t color; const char* label; };
    const Act acts[3] = {{Icon::Phone, th::GREEN, "Call"}, {Icon::Message, th::ACCENT, "Message"}, {Icon::Trash, th::RED, "Delete"}};
    for (int k = 0; k < 3; ++k) {
      int cx = 50 + k * 70;
      bool clicked = ui.roundButton(cx, 244, 24, acts[k].icon, acts[k].color, th::WHITE, 22);
      ui.text(acts[k].label, cx, 282, th::fSmall(), th::MUTED, textdatum_t::middle_center);
      if (!clicked) continue;
      if (k == 0) app::dial(c_.number);
      else if (k == 1) nav::push(ScreenId::Thread, NavParam::str(c_.number));
      else {
        char msg[80];
        snprintf(msg, sizeof msg, "Remove %s from your contacts?", name);
        ui.confirm("Delete contact?", msg, "Delete", th::RED, [this] {
          contacts::remove(idx_);
          nav::back();
        });
      }
    }
  }
};

// ===========================================================================
class ContactEditScreen : public Screen {
  int idx_ = -1;
  char name_[CONTACT_NAME_MAX] = "";
  char number_[NUMBER_MAX] = "";
  int focus_ = 0;  // 0 = name, 1 = number

  void save(Gui& ui) {
    txt::trim(name_);
    char digits[24];
    txt::digitsOnly(number_, digits, sizeof digits);
    if (strlen(digits) < 3) {
      ui.toast("Enter a phone number", th::AMBER);
      focus_ = 1;
      return;
    }
    int r = idx_ >= 0 ? contacts::update(idx_, name_, number_) : contacts::add(name_, number_);
    if (r < 0) {
      ui.toast(idx_ >= 0 ? "Could not save" : "Contact list is full", th::RED);
      return;
    }
    s_editResult = r;
    ui.toast("Contact saved", th::GREEN);
    nav::back();
  }

 public:
  void enter(const NavParam& p) override {
    idx_ = p.i;
    if (idx_ >= 0) {
      const Contact& c = contacts::at(idx_);
      txt::copy(name_, c.name, sizeof name_);
      txt::copy(number_, c.number, sizeof number_);
    } else {
      name_[0] = 0;
      txt::copy(number_, p.s, sizeof number_);
    }
    focus_ = 0;
    kb.reset(name_);
  }
  void frame(Gui& ui) override {
    auto& g = *ui.g;
    g.fillScreen(th::BG);
    ui.statusBar();
    bool ok = false;
    if (ui.header(idx_ >= 0 ? "Edit contact" : "New contact", Icon::Check, &ok)) {
      nav::back();
      return;
    }
    if (ok) {
      save(ui);
      return;
    }
    const char* labels[2] = {"Name", "Phone number"};
    char* values[2] = {name_, number_};
    for (int k = 0; k < 2; ++k) {
      Rect f(8, 62 + k * 50, 224, 44);
      bool active = focus_ == k;
      g.fillSmoothRoundRect(f.x, f.y, f.w, f.h, 10, th::SURFACE);
      if (active) g.drawRoundRect(f.x, f.y, f.w, f.h, 10, th::ACCENT);
      ui.text(labels[k], f.x + 12, f.y + 11, th::fTiny(), active ? th::ACCENT : th::MUTED, textdatum_t::middle_left);
      char disp[48];
      txt::toDisplay(values[k], disp, sizeof disp);
      ui.textFit(disp, f.x + 12, f.y + 29, f.w - 24, th::fBody(), th::TEXT, textdatum_t::middle_left);
      if (ui.clickIn(f) && focus_ != k) {
        focus_ = k;
        if (k == 0) kb.reset(name_);
      }
    }
    if (focus_ == 0) {
      if (kb.draw(ui, th::H - Keyboard::kHeight, name_, sizeof name_, "Next") == KeyAction::Action) focus_ = 1;
    } else {
      if (numpad.draw(ui, th::H - NumPad::kHeight, number_, sizeof number_, "Save", true, false) == KeyAction::Action) save(ui);
    }
  }
};

// ===========================================================================
namespace screens {
ContactsScreen s_contacts;
ContactViewScreen s_contactView;
ContactEditScreen s_contactEdit;

void registerContacts() {
  nav::add(ScreenId::Contacts, &s_contacts);
  nav::add(ScreenId::ContactView, &s_contactView);
  nav::add(ScreenId::ContactEdit, &s_contactEdit);
}
}  // namespace screens
