#include "gui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bt_client.h"
#include "hotspot.h"
#include "modem.h"
#include "phone_app.h"
#include "plog.h"
#include "s3_config.h"
#include "text_util.h"

Gui ui;

namespace th {

uint32_t mix(uint32_t a, uint32_t b, uint8_t t) {
  auto ch = [&](int s) {
    int ca = (a >> s) & 0xFF, cb = (b >> s) & 0xFF;
    return (uint32_t)((ca + ((cb - ca) * t) / 255) & 0xFF) << s;
  };
  return ch(16) | ch(8) | ch(0);
}

uint32_t avatarColor(const char* seed) {
  static const uint32_t kColors[] = {0x2563EB, 0x16A34A, 0xDB2777, 0xEA580C, 0x7C3AED, 0x0891B2, 0xCA8A04, 0xDC2626};
  return kColors[txt::hash(seed ? seed : "") % (sizeof kColors / sizeof kColors[0])];
}

}  // namespace th

namespace {

// Cached advance widths of printable ASCII for a few fonts (fast word wrap).
struct WidthCache {
  const lgfx::IFont* font = nullptr;
  uint8_t w[95];
};
WidthCache s_widths[6];
int s_widthNext = 0;

const uint8_t* charWidths(lgfx::LovyanGFX& g, const lgfx::IFont* f) {
  for (auto& c : s_widths)
    if (c.font == f) return c.w;
  WidthCache& c = s_widths[s_widthNext++ % 6];
  c.font = f;
  char buf[2] = {0, 0};
  for (int k = 0; k < 95; ++k) {
    buf[0] = (char)(32 + k);
    c.w[k] = (uint8_t)g.textWidth(buf, f);
  }
  return c.w;
}

inline int charW(const uint8_t* w, char ch) {
  unsigned char u = (unsigned char)ch;
  return (u >= 32 && u < 127) ? w[u - 32] : w['?' - 32];
}

Rect s_clip;
bool s_clipActive = false;

}  // namespace

// ---------------------------------------------------------------------------
void Gui::beginFrame(lgfx::LovyanGFX* target, const TouchSample& t, uint32_t nowMs, bool inputEnabled) {
  g = target;
  g->clearClipRect();  // in case a screen returned from inside a scroll list
  s_clipActive = false;
  now = nowMs;
  enabled_ = inputEnabled;
  consumed_ = false;
  bool wasDown = in.down;
  in.pressed = t.down && !wasDown;
  in.released = !t.down && wasDown;
  if (in.pressed) {
    in.startX = t.x;
    in.startY = t.y;
    in.dragged = false;
    in.downMs = nowMs;
    longFired_ = false;
    scrollDrag_ = false;
  }
  in.down = t.down;
  if (t.down) {
    in.x = t.x;
    in.y = t.y;
    if (abs(in.x - in.startX) > 8 || abs(in.y - in.startY) > 8) in.dragged = true;
  }

  // A tap on the toast banner wins over whatever is underneath it.
  if (toastVisible() && in.released && !in.dragged) {
    Rect r(8, th::STATUS_H + 4, th::W - 16, 40);
    if (r.contains(in.startX, in.startY) && r.contains(in.x, in.y)) {
      toastUntil_ = 0;
      consumed_ = true;
      if (toastTap_) toastTap_();
    }
  }
}

void Gui::consume() { consumed_ = true; }

bool Gui::clickIn(const Rect& r) {
  if (!enabled_ || consumed_ || !in.released || in.dragged || longFired_) return false;
  if (s_clipActive && !s_clip.contains(in.startX, in.startY)) return false;
  if (!r.contains(in.startX, in.startY) || !r.contains(in.x, in.y)) return false;
  consumed_ = true;
  return true;
}

bool Gui::pressingIn(const Rect& r) const {
  if (!enabled_ || consumed_ || !in.down || in.dragged) return false;
  if (s_clipActive && !s_clip.contains(in.startX, in.startY)) return false;
  return r.contains(in.startX, in.startY) && r.contains(in.x, in.y);
}

uint32_t Gui::holdMs(const Rect& r) const { return pressingIn(r) ? now - in.downMs : 0; }

bool Gui::longPress(const Rect& r, uint32_t ms) {
  if (longFired_ || !pressingIn(r) || now - in.downMs < ms) return false;
  longFired_ = true;
  return true;
}

// ---------------------------------------------------------------------------
void Gui::text(const char* s, int x, int y, const lgfx::IFont* f, uint32_t color, lgfx::textdatum_t datum) {
  g->setFont(f);
  g->setTextColor(color);
  g->setTextDatum(datum);
  g->drawString(s, x, y);
}

int Gui::textW(const char* s, const lgfx::IFont* f) {
  lgfx::LovyanGFX& gg = g ? *g : display::gfx();
  const uint8_t* w = charWidths(gg, f);
  int sum = 0;
  for (; *s; ++s) sum += charW(w, *s);
  return sum;
}

void Gui::textFit(const char* s, int x, int y, int maxW, const lgfx::IFont* f, uint32_t color, lgfx::textdatum_t datum) {
  if (textW(s, f) <= maxW) {
    text(s, x, y, f, color, datum);
    return;
  }
  char buf[128];
  txt::copy(buf, s, sizeof buf);
  int dots = textW("...", f);
  size_t n = strlen(buf);
  while (n > 0 && textW(buf, f) + dots > maxW) buf[--n] = 0;
  while (n > 0 && buf[n - 1] == ' ') buf[--n] = 0;
  strncat(buf, "...", sizeof buf - strlen(buf) - 1);
  text(buf, x, y, f, color, datum);
}

int Gui::wrap(const char* s, int maxW, const lgfx::IFont* f, int* starts, int* lens, int maxLines) {
  lgfx::LovyanGFX& gg = g ? *g : display::gfx();
  const uint8_t* cw = charWidths(gg, f);
  int n = 0, pos = 0, len = (int)strlen(s);
  while (pos < len && n < maxLines) {
    int w = 0, i = pos, lastSpace = -1;
    while (i < len && s[i] != '\n') {
      int c = charW(cw, s[i]);
      if (w + c > maxW) break;
      if (s[i] == ' ') lastSpace = i;
      w += c;
      ++i;
    }
    int end;
    if (i >= len || s[i] == '\n') end = i;
    else if (lastSpace > pos) end = lastSpace;
    else end = i > pos ? i : pos + 1;  // single word longer than the line: hard break
    if (starts) starts[n] = pos;
    if (lens) lens[n] = end - pos;
    ++n;
    pos = end;
    if (pos < len && (s[pos] == ' ' || s[pos] == '\n')) ++pos;
  }
  if (len == 0 && maxLines > 0) {
    if (starts) starts[0] = 0;
    if (lens) lens[0] = 0;
    n = 1;
  }
  return n;
}

// ---------------------------------------------------------------------------
bool Gui::button(const Rect& r, const char* label, uint32_t bg, uint32_t fg, const lgfx::IFont* f, int radius, Icon icon) {
  if (!f) f = th::fBodyB();
  bool pressed = pressingIn(r);
  uint32_t col = pressed ? th::mix(bg, th::WHITE, 45) : bg;
  g->fillSmoothRoundRect(r.x, r.y, r.w, r.h, radius, col);
  int iconSize = r.h > 40 ? 20 : 16;
  int lw = (label && *label) ? textW(label, f) : 0;
  int total = lw + (icon != Icon::None ? iconSize + (lw ? 6 : 0) : 0);
  int x = r.cx() - total / 2;
  if (icon != Icon::None) {
    drawIcon(*g, icon, x + iconSize / 2, r.cy(), iconSize, fg, col);
    x += iconSize + 6;
  }
  if (lw) text(label, x, r.cy(), f, fg, lgfx::textdatum_t::middle_left);
  return clickIn(r);
}

bool Gui::roundButton(int cx, int cy, int r, Icon icon, uint32_t bg, uint32_t fg, int iconSize) {
  Rect hit(cx - r - 4, cy - r - 4, 2 * r + 8, 2 * r + 8);
  bool pressed = pressingIn(hit);
  uint32_t col = pressed ? th::mix(bg, th::WHITE, 50) : bg;
  g->fillSmoothCircle(cx, cy, pressed ? r - 1 : r, col);
  drawIcon(*g, icon, cx, cy, iconSize ? iconSize : r, fg, col);
  return clickIn(hit);
}

bool Gui::iconButton(const Rect& r, Icon icon, uint32_t fg, uint32_t pressedBg, int iconSize) {
  bool pressed = pressingIn(r);
  uint32_t bg = th::BG;
  if (pressed) {
    g->fillSmoothRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 8, pressedBg);
    bg = pressedBg;
  }
  drawIcon(*g, icon, r.cx(), r.cy(), iconSize, fg, bg);
  return clickIn(r);
}

bool Gui::header(const char* title, Icon a1, bool* c1, Icon a2, bool* c2) {
  g->fillRect(0, th::STATUS_H, th::W, th::HEADER_H, th::BG);
  bool back = iconButton(Rect(0, th::STATUS_H, 40, th::HEADER_H), Icon::Back, th::TEXT);
  int actions = (a1 != Icon::None) + (a2 != Icon::None);
  textFit(title, 40, th::STATUS_H + th::HEADER_H / 2, th::W - 44 - actions * 40, th::fTitle(), th::TEXT,
          lgfx::textdatum_t::middle_left);
  if (a1 != Icon::None) {
    bool c = iconButton(Rect(th::W - 40, th::STATUS_H, 40, th::HEADER_H), a1, th::TEXT);
    if (c1) *c1 = c;
  }
  if (a2 != Icon::None) {
    bool c = iconButton(Rect(th::W - 80, th::STATUS_H, 40, th::HEADER_H), a2, th::TEXT);
    if (c2) *c2 = c;
  }
  g->drawFastHLine(0, th::CONTENT_Y - 1, th::W, th::BORDER);
  return back;
}

void Gui::statusBar() {
  g->fillRect(0, 0, th::W, th::STATUS_H, th::BG);
  const NetStatus& st = app::net();
  bool reg = st.reg == RegState::Home || st.reg == RegState::Roaming;
  // ---- left: signal bars, 4G/2G, operator ----
  int bars = modem::signalBars(st);
  for (int k = 0; k < 4; ++k) {
    int h = 4 + k * 3;
    g->fillRect(4 + k * 4, 15 - h, 3, h, k < bars ? th::TEXT : th::DIM);
  }
  int x = 22;
  const char* rat = modem::ratText(st);
  if (rat[0]) {
    bool dataUp = app::data().state == DataState::Connected;
    text(rat, x, 10, th::fTiny(), dataUp ? th::GREEN : th::MUTED, lgfx::textdatum_t::middle_left);
    x += textW(rat, th::fTiny()) + 4;
  }
  const char* label;
  uint32_t lc = th::MUTED;
  if (st.modem == ModemState::NotResponding) { label = "No modem"; lc = th::AMBER; }
  else if (st.modem != ModemState::Ready && st.modem != ModemState::NetworkSearch) label = "";
  else if (st.sim == SimState::Absent) { label = "No SIM"; lc = th::AMBER; }
  else if (st.sim == SimState::PinRequired || st.sim == SimState::PukRequired) { label = "SIM locked"; lc = th::AMBER; }
  else if (!reg) label = st.reg == RegState::Denied ? "SOS only" : "Searching";
  else label = st.op[0] ? st.op : "";
  textFit(label, x, 10, 96 - x, th::fSmall(), lc, lgfx::textdatum_t::middle_left);

  // ---- centre: clock ----
  char clk[8] = "--:--";
  if (app::clockValid()) txt::fmtClock(app::nowLocal(), clk);
  text(clk, th::W / 2, 10, th::fBodyB(), th::TEXT, lgfx::textdatum_t::middle_center);

  // ---- right: battery, percentage, earbuds, hotspot ----
  int pct = app::battery();
  int bx = th::W - 25, by = 5;
  uint32_t bcol = app::charging() ? th::GREEN : (pct >= 0 && pct < 15 ? th::RED : th::TEXT);
  g->drawRoundRect(bx, by, 20, 11, 2, th::MUTED);
  g->fillRect(bx + 20, by + 3, 2, 5, th::MUTED);
  if (pct > 0) g->fillRect(bx + 2, by + 2, (16 * (pct > 100 ? 100 : pct)) / 100, 7, bcol);
  if (app::charging()) drawIcon(*g, Icon::Bolt, bx + 10, by + 5, 10, th::WHITE, th::BG);
  int rx = bx - 3;
  if (pct >= 0) {
    char p[16];
    snprintf(p, sizeof p, "%d%%", pct);
    text(p, rx, 10, th::fTiny(), th::MUTED, lgfx::textdatum_t::middle_right);
    rx -= textW(p, th::fTiny()) + 6;
  }
  if (bt::connected()) {
    drawIcon(*g, Icon::Headphones, rx - 6, 10, 13, bt::audioOpen() ? th::ACCENT : th::TEXT);
    rx -= 16;
  }
  if (hotspot::running()) {
    drawIcon(*g, Icon::Wifi, rx - 6, 8, 12, hotspot::status().clients ? th::ACCENT : th::TEXT);
  }
}

void Gui::badge(int cx, int cy, int count, uint32_t color) {
  if (count <= 0) return;
  char b[6];
  if (count > 99) txt::copy(b, "99+", sizeof b);
  else snprintf(b, sizeof b, "%d", count);
  int w = textW(b, th::fTiny()) + 8;
  if (w < 16) w = 16;
  g->fillSmoothRoundRect(cx - w / 2, cy - 8, w, 16, 8, color);
  text(b, cx, cy, th::fTiny(), th::WHITE, lgfx::textdatum_t::middle_center);
}

void Gui::avatar(int cx, int cy, int r, const char* name, const lgfx::IFont* f) {
  uint32_t c = th::avatarColor(name);
  g->fillSmoothCircle(cx, cy, r, c);
  if (!name || !*name || txt::looksLikeNumber(name) || name[0] == '+') {
    drawIcon(*g, Icon::Person, cx, cy + r / 10, r, th::WHITE, c);
    return;
  }
  char ini[4];
  txt::initials(name, ini);
  text(ini, cx, cy + 1, f ? f : th::fBodyB(), th::WHITE, lgfx::textdatum_t::middle_center);
}

bool Gui::listRow(const Rect& r, bool divider) {
  if (pressingIn(r)) g->fillRect(r.x, r.y, r.w, r.h, th::PRESSED);
  if (divider) g->drawFastHLine(r.x + 12, r.bottom() - 1, r.w - 12, th::BORDER);
  return clickIn(r);
}

bool Gui::stepper(const Rect& r, const char* label, const char* value, bool* minus, bool* plus) {
  g->drawFastHLine(r.x + 12, r.bottom() - 1, r.w - 12, th::BORDER);
  // [label ........ ] [-] value [+]
  Rect m(r.right() - 116, r.y + 6, 30, r.h - 12), p(r.right() - 38, r.y + 6, 30, r.h - 12);
  int avail = m.x - r.x - 16;
  const lgfx::IFont* lf = textW(label, th::fBody()) <= avail ? th::fBody() : th::fSmall();
  textFit(label, r.x + 12, r.cy(), avail, lf, th::TEXT, lgfx::textdatum_t::middle_left);
  bool a = button(m, "", th::SURFACE2, th::TEXT, nullptr, 8, Icon::None);
  g->drawWideLine(m.cx() - 6, m.cy(), m.cx() + 6, m.cy(), 1.5f, th::TEXT);  // minus sign
  bool b = button(p, "", th::SURFACE2, th::TEXT, nullptr, 8, Icon::Plus);
  textFit(value, (m.right() + p.x) / 2, r.cy(), p.x - m.right() - 4, th::fBodyB(), th::TEXT, lgfx::textdatum_t::middle_center);
  if (minus) *minus = a;
  if (plus) *plus = b;
  return a || b;
}

int Gui::segmented(const Rect& r, const char* const* options, int count, int selected) {
  g->fillSmoothRoundRect(r.x, r.y, r.w, r.h, 9, th::SURFACE2);
  int sw = r.w / count;
  int result = selected;
  for (int k = 0; k < count; ++k) {
    Rect s(r.x + k * sw, r.y, k == count - 1 ? r.w - k * sw : sw, r.h);
    bool sel = k == selected;
    if (sel) g->fillSmoothRoundRect(s.x + 2, s.y + 2, s.w - 4, s.h - 4, 7, th::ACCENT);
    else if (pressingIn(s)) g->fillSmoothRoundRect(s.x + 2, s.y + 2, s.w - 4, s.h - 4, 7, th::PRESSED);
    textFit(options[k], s.cx(), s.cy(), s.w - 4, th::fSmall(), sel ? th::WHITE : th::TEXT, lgfx::textdatum_t::middle_center);
    if (clickIn(s)) result = k;
  }
  return result;
}

bool Gui::toggle(const Rect& r, const char* label, bool on) {
  bool clicked = listRow(r);
  textFit(label, r.x + 12, r.cy(), r.w - 70, th::fBody(), th::TEXT, lgfx::textdatum_t::middle_left);
  int sx = r.right() - 52, sy = r.cy() - 10;
  g->fillSmoothRoundRect(sx, sy, 40, 20, 10, on ? th::GREEN : th::DIM);
  g->fillSmoothCircle(on ? sx + 30 : sx + 10, sy + 10, 8, th::WHITE);
  return clicked;
}

void Gui::spinner(int cx, int cy, int r, uint32_t color) {
  float a = (float)((now / 3) % 360);
  g->fillArc(cx, cy, r - 3, r, a, a + 100, color);
  g->fillArc(cx, cy, r - 3, r, a + 180, a + 230, th::mix(color, th::BG, 140));
}

void Gui::meter(const Rect& r, float db, uint32_t color) {
  g->fillSmoothRoundRect(r.x, r.y, r.w, r.h, r.h / 2, th::SURFACE2);
  float t = (db + 60.0f) / 60.0f;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  int w = (int)(r.w * t);
  if (w > r.h) g->fillSmoothRoundRect(r.x, r.y, w, r.h, r.h / 2, db > -3 ? th::RED : color);
}

// ---------------------------------------------------------------------------
int Gui::beginScroll(ScrollState& s, const Rect& view, int contentH) {
  s.contentH = contentH;
  int maxOff = contentH > view.h ? contentH - view.h : 0;
  if (enabled_ && in.pressed && view.contains(in.startX, in.startY)) {
    s.startOffset = s.offset;
    s.dragging = true;
  }
  if (s.dragging) {
    if (in.down) {
      if (in.dragged) {
        s.offset = s.startOffset - (in.y - in.startY);
        scrollDrag_ = true;
      }
    } else {
      s.dragging = false;
    }
  }
  if (s.offset > maxOff) s.offset = maxOff;
  if (s.offset < 0) s.offset = 0;
  g->setClipRect(view.x, view.y, view.w, view.h);
  s_clip = view;
  s_clipActive = true;
  return view.y - s.offset;
}

void Gui::endScroll(ScrollState& s, const Rect& view) {
  g->clearClipRect();
  s_clipActive = false;
  if (s.contentH > view.h) {
    int barH = view.h * view.h / s.contentH;
    if (barH < 18) barH = 18;
    int barY = view.y + (view.h - barH) * s.offset / (s.contentH - view.h);
    g->fillSmoothRoundRect(view.right() - 4, barY, 3, barH, 1, th::DIM);
  }
}

// ---------------------------------------------------------------------------
void Gui::toast(const char* text, uint32_t color, std::function<void()> onTap) {
  txt::toDisplay(text, toast_, sizeof toast_);
  toastColor_ = color;
  toastUntil_ = plogMillis() + TOAST_MS;
  toastTap_ = onTap;
}

bool Gui::toastVisible() const { return toastUntil_ && (int32_t)(toastUntil_ - plogMillis()) > 0; }

void Gui::confirm(const char* title, const char* message, const char* okLabel, uint32_t okColor, std::function<void()> onOk) {
  modal_ = Modal::Confirm;
  txt::copy(modalTitle_, title, sizeof modalTitle_);
  txt::toDisplay(message ? message : "", modalMsg_, sizeof modalMsg_);
  txt::copy(modalOk_, okLabel, sizeof modalOk_);
  modalOkColor_ = okColor;
  onOk_ = onOk;
  consume();
}

void Gui::sheet(const char* title, const char* const* options, int count, std::function<void(int)> onPick) {
  modal_ = Modal::Sheet;
  txt::toDisplay(title ? title : "", modalTitle_, sizeof modalTitle_);
  sheetCount_ = count > 8 ? 8 : count;
  for (int k = 0; k < sheetCount_; ++k) {
    txt::copy(sheetStore_[k], options[k], sizeof sheetStore_[k]);
    sheetOpts_[k] = sheetStore_[k];
  }
  onPick_ = onPick;
  consume();
}

void Gui::drawOverlays() {
  enabled_ = true;
  if (modal_ != Modal::None) g->fillRectAlpha(0, 0, th::W, th::H, 150, th::BLACK);

  if (modal_ == Modal::Confirm) {
    int lines[4], lens[4];
    int n = wrap(modalMsg_, 184, th::fBody(), lines, lens, 4);
    int h = 50 + n * 18 + 58;
    Rect card(16, (th::H - h) / 2, th::W - 32, h);
    g->fillSmoothRoundRect(card.x, card.y, card.w, card.h, 14, th::SURFACE);
    const lgfx::IFont* tf = textW(modalTitle_, th::fTitle()) <= card.w - 32 ? th::fTitle() : th::fBodyB();
    textFit(modalTitle_, card.x + 16, card.y + 26, card.w - 32, tf, th::TEXT, lgfx::textdatum_t::middle_left);
    for (int k = 0; k < n; ++k) {
      char line[80];
      int l = lens[k] < 79 ? lens[k] : 79;
      memcpy(line, modalMsg_ + lines[k], l);
      line[l] = 0;
      text(line, card.x + 16, card.y + 48 + k * 18, th::fBody(), th::MUTED);
    }
    Rect cancel(card.x + 12, card.bottom() - 48, card.w / 2 - 18, 36);
    Rect okr(card.cx() + 6, card.bottom() - 48, card.w / 2 - 18, 36);
    bool c = button(cancel, "Cancel", th::SURFACE2);
    bool o = button(okr, modalOk_, modalOkColor_, th::WHITE);
    bool outside = in.released && !in.dragged && !consumed_ && !card.contains(in.startX, in.startY);
    if (o) {
      modal_ = Modal::None;
      auto cb = onOk_;
      onOk_ = nullptr;
      if (cb) cb();
    } else if (c || outside) {
      modal_ = Modal::None;
      onOk_ = nullptr;
    }
  } else if (modal_ == Modal::Sheet) {
    int rowH = sheetCount_ > 4 ? 36 : 42;  // keep long lists on screen
    bool hasTitle = modalTitle_[0] != 0;
    int h = (hasTitle ? 36 : 8) + sheetCount_ * rowH + rowH + 8;
    Rect card(6, th::H - h - 6, th::W - 12, h);
    g->fillSmoothRoundRect(card.x, card.y, card.w, card.h, 14, th::SURFACE);
    int y = card.y + 4;
    if (hasTitle) {
      textFit(modalTitle_, card.cx(), y + 16, card.w - 24, th::fSmall(), th::MUTED, lgfx::textdatum_t::middle_center);
      y += 32;
    }
    int picked = -1;
    for (int k = 0; k < sheetCount_; ++k) {
      Rect r(card.x, y, card.w, rowH);
      if (listRow(r)) picked = k;
      bool danger = txt::startsWith(sheetOpts_[k], "Delete") || txt::startsWith(sheetOpts_[k], "Remove") ||
                    txt::startsWith(sheetOpts_[k], "Clear");
      char opt[48];
      txt::toDisplay(sheetOpts_[k], opt, sizeof opt);
      const lgfx::IFont* of = textW(opt, th::fBody()) <= r.w - 20 ? th::fBody() : th::fSmall();
      textFit(opt, r.cx(), r.cy(), r.w - 16, of, danger ? th::RED : th::TEXT, lgfx::textdatum_t::middle_center);
      y += rowH;
    }
    Rect cancel(card.x + 8, y + 2, card.w - 16, rowH - 6);
    bool c = button(cancel, "Cancel", th::SURFACE2);
    bool outside = in.released && !in.dragged && !consumed_ && !card.contains(in.startX, in.startY);
    if (picked >= 0) {
      modal_ = Modal::None;
      auto cb = onPick_;
      onPick_ = nullptr;
      if (cb) cb(picked);
    } else if (c || outside) {
      modal_ = Modal::None;
      onPick_ = nullptr;
    }
  }

  if (toastVisible()) {
    Rect r(8, th::STATUS_H + 4, th::W - 16, 40);
    g->fillSmoothRoundRect(r.x, r.y, r.w, r.h, 10, th::SURFACE2);
    g->fillSmoothRoundRect(r.x, r.y, 6, r.h, 3, toastColor_);
    int starts[2], lens[2];
    int n = wrap(toast_, r.w - 24, th::fSmall(), starts, lens, 2);
    for (int k = 0; k < n; ++k) {
      char line[80];
      int l = lens[k] < 79 ? lens[k] : 79;
      memcpy(line, toast_ + starts[k], l);
      line[l] = 0;
      text(line, r.x + 14, r.cy() + (n == 1 ? 0 : (k == 0 ? -8 : 8)), th::fSmall(), th::TEXT,
           lgfx::textdatum_t::middle_left);
    }
  }
}
