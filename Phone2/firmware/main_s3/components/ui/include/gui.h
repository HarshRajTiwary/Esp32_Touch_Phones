#pragma once
// Minimal immediate-mode GUI toolkit for a resistive touchscreen + stylus.
//
// Every frame the active screen redraws itself and asks the toolkit
// "was this rectangle clicked?". A click = press and release inside the same
// rectangle without dragging, which is robust against resistive-panel jitter.
#include <stdint.h>

#include <functional>

#include "touch.h"
#include "icons.h"
#include "theme.h"

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
  Rect() {}
  Rect(int x_, int y_, int w_, int h_) : x((int16_t)x_), y((int16_t)y_), w((int16_t)w_), h((int16_t)h_) {}
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  Rect inset(int d) const { return Rect(x + d, y + d, w - 2 * d, h - 2 * d); }
  int cx() const { return x + w / 2; }
  int cy() const { return y + h / 2; }
  int right() const { return x + w; }
  int bottom() const { return y + h; }
};

struct ScrollState {
  int offset = 0;
  int startOffset = 0;
  int contentH = 0;
  bool dragging = false;
  void toBottom(int viewH) { offset = contentH > viewH ? contentH - viewH : 0; }
};

class Gui {
 public:
  struct Input {
    bool down = false;
    int16_t x = 0, y = 0;
    bool pressed = false;    // went down this frame
    bool released = false;   // went up this frame
    int16_t startX = 0, startY = 0;
    bool dragged = false;    // moved > 8 px since the press
    uint32_t downMs = 0;
  };

  lgfx::LovyanGFX* g = nullptr;
  Input in;
  uint32_t now = 0;

  void beginFrame(lgfx::LovyanGFX* target, const TouchSample& t, uint32_t nowMs, bool inputEnabled);
  void consume();                 // swallow this frame's click (after navigating away)
  bool inputEnabled() const { return enabled_; }

  // ---- hit testing ----
  bool clickIn(const Rect& r);
  bool pressingIn(const Rect& r) const;
  uint32_t holdMs(const Rect& r) const;     // how long the current press inside r has lasted
  bool longPress(const Rect& r, uint32_t ms);  // fires once when held long enough

  // ---- text ----
  void text(const char* s, int x, int y, const lgfx::IFont* f, uint32_t color,
            lgfx::textdatum_t datum = lgfx::textdatum_t::top_left);
  // Draws text shrunk with "..." to fit maxW.
  void textFit(const char* s, int x, int y, int maxW, const lgfx::IFont* f, uint32_t color,
               lgfx::textdatum_t datum = lgfx::textdatum_t::top_left);
  int textW(const char* s, const lgfx::IFont* f);
  // Word-wraps into lines of at most maxW pixels. Returns the number of lines;
  // `starts` receives the byte offset of each line (up to maxLines).
  int wrap(const char* s, int maxW, const lgfx::IFont* f, int* starts, int* lens, int maxLines);

  // ---- widgets ----
  bool button(const Rect& r, const char* label, uint32_t bg, uint32_t fg = th::TEXT,
              const lgfx::IFont* f = nullptr, int radius = 10, Icon icon = Icon::None);
  bool roundButton(int cx, int cy, int r, Icon icon, uint32_t bg, uint32_t fg, int iconSize = 0);
  bool iconButton(const Rect& r, Icon icon, uint32_t fg, uint32_t pressedBg = th::PRESSED, int iconSize = 18);
  // Header with back arrow and title; returns true if back was clicked.
  bool header(const char* title, Icon action1 = Icon::None, bool* action1Clicked = nullptr,
              Icon action2 = Icon::None, bool* action2Clicked = nullptr);
  void statusBar();
  void badge(int cx, int cy, int count, uint32_t color = th::RED);
  void avatar(int cx, int cy, int r, const char* name, const lgfx::IFont* f = nullptr);
  bool listRow(const Rect& r, bool divider = true);          // background + click
  bool stepper(const Rect& r, const char* label, const char* value, bool* minus, bool* plus);
  int segmented(const Rect& r, const char* const* options, int count, int selected);  // returns new selection
  bool toggle(const Rect& r, const char* label, bool on);     // returns true when flipped
  void spinner(int cx, int cy, int r, uint32_t color);
  void meter(const Rect& r, float db, uint32_t color);

  // ---- scrolling (vertical lists) ----
  // Call before drawing the list; sets the clip rectangle. Returns the y
  // offset to add to item positions.
  int beginScroll(ScrollState& s, const Rect& view, int contentH);
  void endScroll(ScrollState& s, const Rect& view);
  bool scrolling() const { return scrollDrag_; }

  // ---- overlays (drawn by the root after the screen) ----
  void toast(const char* text, uint32_t color = th::ACCENT, std::function<void()> onTap = nullptr);
  void confirm(const char* title, const char* message, const char* okLabel, uint32_t okColor, std::function<void()> onOk);
  void sheet(const char* title, const char* const* options, int count, std::function<void(int)> onPick);
  bool modalOpen() const { return modal_ != Modal::None; }
  bool toastVisible() const;
  void drawOverlays();            // uses real input; call after the screen frame
  void closeModal() { modal_ = Modal::None; }

 private:
  bool enabled_ = true;
  bool consumed_ = false;
  bool longFired_ = false;
  bool scrollDrag_ = false;

  enum class Modal : uint8_t { None, Confirm, Sheet };
  Modal modal_ = Modal::None;
  char modalTitle_[48] = "";
  char modalMsg_[160] = "";
  char modalOk_[20] = "";
  uint32_t modalOkColor_ = th::ACCENT;
  const char* sheetOpts_[8] = {};
  char sheetStore_[8][40] = {};
  int sheetCount_ = 0;
  std::function<void()> onOk_;
  std::function<void(int)> onPick_;

  char toast_[120] = "";
  uint32_t toastColor_ = th::ACCENT;
  uint32_t toastUntil_ = 0;
  std::function<void()> toastTap_;
};

extern Gui ui;
