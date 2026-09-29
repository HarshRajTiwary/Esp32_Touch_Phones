#include "keyboard.h"

#include <ctype.h>
#include <string.h>

#include "text_util.h"
#include "settings.h"

Keyboard kb;
NumPad numpad;

namespace {

const char* const kAlpha[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
const char* const kSym1[3] = {"1234567890", "@#$%&-+()", "*\"':;!?"};
const char* const kSym2[3] = {"[]{}<>^~|\\", "_=/.,:;!?", "@#%&*+-"};

void append(char* buf, size_t cap, char c) {
  size_t n = strlen(buf);
  if (n + 1 >= cap) return;
  buf[n] = c;
  buf[n + 1] = 0;
}

// Draws one key and returns true if it was tapped.
bool key(Gui& ui, const Rect& r, const char* label, uint32_t bg, bool* pressedOut = nullptr, uint32_t fg = th::TEXT,
         const lgfx::IFont* f = nullptr) {
  Rect k = r.inset(2);
  bool pressed = ui.pressingIn(r);
  if (pressedOut) *pressedOut = pressed;
  ui.g->fillSmoothRoundRect(k.x, k.y, k.w, k.h, 5, pressed ? th::PRESSED : bg);
  if (label && *label) ui.text(label, k.cx(), k.cy(), f ? f : th::fBody(), fg, lgfx::textdatum_t::middle_center);
  return ui.clickIn(r);
}

void preview(Gui& ui, const Rect& r, char c) {
  int w = 40, h = 48;
  int x = r.cx() - w / 2;
  if (x < 2) x = 2;
  if (x + w > th::W - 2) x = th::W - 2 - w;
  int y = r.y - h - 2;
  ui.g->fillSmoothRoundRect(x, y, w, h, 8, th::ACCENT);
  char s[2] = {c, 0};
  ui.text(s, x + w / 2, y + h / 2, th::fLargeB(), th::WHITE, lgfx::textdatum_t::middle_center);
}

bool backspaceKey(Gui& ui, const Rect& r, char* buf, uint32_t& repeatAt) {
  bool dummy;
  Rect k = r.inset(2);
  bool tapped = key(ui, r, "", th::SURFACE2, &dummy);
  drawIcon(*ui.g, Icon::Backspace, k.cx(), k.cy(), 18, th::TEXT, ui.pressingIn(r) ? th::PRESSED : th::SURFACE2);
  bool changed = false;
  if (ui.longPress(r, 450)) {
    txt::utf8PopBack(buf);
    repeatAt = ui.now + 70;
    changed = true;
  } else if (ui.holdMs(r) > 450 && (int32_t)(ui.now - repeatAt) >= 0) {
    txt::utf8PopBack(buf);
    repeatAt = ui.now + 70;
    changed = true;
  } else if (tapped) {
    txt::utf8PopBack(buf);
    changed = true;
  }
  return changed;
}

}  // namespace

// ---------------------------------------------------------------------------
void Keyboard::reset(const char* buf) {
  mode_ = Mode::Alpha;
  caps_ = false;
  shift_ = settings::get().autoCaps && (!buf || !*buf);
}

void Keyboard::typed(char* buf, size_t cap, char c) {
  if (mode_ == Mode::Alpha && (shift_ || caps_)) c = (char)toupper((unsigned char)c);
  append(buf, cap, c);
  if (!caps_) shift_ = false;
  // Auto-capitalise after ". " "! " "? "
  size_t n = strlen(buf);
  if (settings::get().autoCaps && c == ' ' && n >= 2 && (buf[n - 2] == '.' || buf[n - 2] == '!' || buf[n - 2] == '?'))
    shift_ = true;
}

KeyAction Keyboard::draw(Gui& ui, int y, char* buf, size_t cap, const char* actionLabel) {
  ui.g->fillRect(0, y, th::W, kHeight, th::SURFACE);
  const char* const* rows = mode_ == Mode::Alpha ? kAlpha : mode_ == Mode::Sym1 ? kSym1 : kSym2;
  KeyAction result = KeyAction::None;
  const int kw = 24;
  Rect previewRect;
  char previewChar = 0;

  for (int row = 0; row < 3; ++row) {
    const char* keys = rows[row];
    int n = (int)strlen(keys);
    int ry = y + row * kRowH;
    int x0 = (th::W - n * kw) / 2;
    if (row == 2) {
      x0 = 36;
      // left special key
      Rect l(0, ry, 36, kRowH);
      if (mode_ == Mode::Alpha) {
        bool on = shift_ || caps_;
        bool t = key(ui, l, "", on ? th::ACCENT : th::SURFACE2);
        Rect k = l.inset(2);
        int cx = k.cx(), cy = k.cy();
        uint32_t c = on ? th::WHITE : th::TEXT;
        ui.g->fillTriangle(cx - 8, cy + 1, cx + 8, cy + 1, cx, cy - 8, c);
        ui.g->fillRect(cx - 4, cy + 1, 8, 6, c);
        if (caps_) ui.g->fillRect(cx - 7, cy + 9, 14, 2, c);
        if (t) {
          if (caps_) { caps_ = false; shift_ = false; }
          else if (shift_ && ui.now - lastShiftMs_ < 400) caps_ = true;  // double tap = caps lock
          else shift_ = !shift_;
          lastShiftMs_ = ui.now;
        }
      } else {
        if (key(ui, l, mode_ == Mode::Sym1 ? "=\\<" : "123", th::SURFACE2, nullptr, th::TEXT, th::fSmall()))
          mode_ = mode_ == Mode::Sym1 ? Mode::Sym2 : Mode::Sym1;
      }
      if (backspaceKey(ui, Rect(th::W - 36, ry, 36, kRowH), buf, repeatAt_)) result = KeyAction::Typed;
    }
    for (int k = 0; k < n; ++k) {
      Rect r(x0 + k * kw, ry, kw, kRowH);
      char c = keys[k];
      char label[2] = {c, 0};
      if (mode_ == Mode::Alpha && (shift_ || caps_)) label[0] = (char)toupper((unsigned char)c);
      bool pressed;
      if (key(ui, r, label, th::SURFACE2, &pressed)) {
        typed(buf, cap, c);
        result = KeyAction::Typed;
      }
      if (pressed) {
        previewRect = r;
        previewChar = label[0];
      }
    }
  }

  // bottom row: mode | , | space | . | action
  int ry = y + 3 * kRowH;
  if (key(ui, Rect(0, ry, 40, kRowH), mode_ == Mode::Alpha ? "?123" : "ABC", th::SURFACE2, nullptr, th::TEXT, th::fSmall()))
    mode_ = mode_ == Mode::Alpha ? Mode::Sym1 : Mode::Alpha;
  bool pc;
  if (key(ui, Rect(40, ry, 24, kRowH), ",", th::SURFACE2, &pc)) { typed(buf, cap, ','); result = KeyAction::Typed; }
  if (pc) { previewRect = Rect(40, ry, 24, kRowH); previewChar = ','; }
  if (key(ui, Rect(64, ry, 104, kRowH), "space", th::SURFACE2, nullptr, th::MUTED, th::fSmall())) {
    typed(buf, cap, ' ');
    result = KeyAction::Typed;
  }
  if (key(ui, Rect(168, ry, 24, kRowH), ".", th::SURFACE2, &pc)) { typed(buf, cap, '.'); result = KeyAction::Typed; }
  if (pc) { previewRect = Rect(168, ry, 24, kRowH); previewChar = '.'; }
  if (key(ui, Rect(192, ry, 48, kRowH), actionLabel, th::ACCENT, nullptr, th::WHITE, th::fBodyB())) result = KeyAction::Action;

  if (previewChar) preview(ui, previewRect, previewChar);
  return result;
}

// ---------------------------------------------------------------------------
KeyAction NumPad::draw(Gui& ui, int y, char* buf, size_t cap, const char* actionLabel, bool dialChars, bool allowAlpha) {
  ui.g->fillRect(0, y, th::W, kHeight, th::SURFACE);
  KeyAction result = KeyAction::None;
  static const char* const kKeys[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {"", "0", "#"}};
  const int kw = 60;
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 3; ++col) {
      const char* label = kKeys[row][col];
      if (row == 3 && col == 0) label = allowAlpha ? "ABC" : "";
      if (row == 3 && col == 2 && !dialChars) label = "";
      Rect r(col * kw, y + row * kRowH, kw, kRowH);
      if (!*label) {
        ui.g->fillSmoothRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 5, th::SURFACE);
        continue;
      }
      bool isAbc = row == 3 && col == 0;
      if (key(ui, r, label, th::SURFACE2, nullptr, th::TEXT, isAbc ? th::fSmall() : th::fMedium())) {
        if (isAbc) result = KeyAction::SwitchToAlpha;
        else {
          append(buf, cap, label[0]);
          result = KeyAction::Typed;
        }
      }
    }
  }
  // right column: backspace, +, *, action
  if (backspaceKey(ui, Rect(180, y, 60, kRowH), buf, repeatAt_)) result = KeyAction::Typed;
  if (dialChars) {
    if (key(ui, Rect(180, y + kRowH, 60, kRowH), "+", th::SURFACE2, nullptr, th::TEXT, th::fMedium())) {
      append(buf, cap, '+');
      result = KeyAction::Typed;
    }
    if (key(ui, Rect(180, y + 2 * kRowH, 60, kRowH), "*", th::SURFACE2, nullptr, th::TEXT, th::fMedium())) {
      append(buf, cap, '*');
      result = KeyAction::Typed;
    }
  } else {
    ui.g->fillRect(180, y + kRowH, 60, 2 * kRowH, th::SURFACE);
  }
  if (key(ui, Rect(180, y + 3 * kRowH, 60, kRowH), actionLabel, th::ACCENT, nullptr, th::WHITE, th::fBodyB()))
    result = KeyAction::Action;
  return result;
}
