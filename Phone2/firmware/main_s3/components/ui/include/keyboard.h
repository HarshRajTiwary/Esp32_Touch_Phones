#pragma once
// On-screen QWERTY keyboard and numeric keypad, sized for a stylus on 240 px.
#include <stddef.h>

#include "gui.h"

enum class KeyAction : uint8_t { None, Typed, Action, SwitchToAlpha, SwitchToNumbers };

class Keyboard {
 public:
  static constexpr int kRowH = 38;
  static constexpr int kHeight = kRowH * 4;
  void reset(const char* buf);  // resets mode and auto-capitalisation state
  // Draws the keyboard with its top edge at y and edits buf (UTF-8, cap bytes).
  KeyAction draw(Gui& ui, int y, char* buf, size_t cap, const char* actionLabel);

 private:
  enum class Mode : uint8_t { Alpha, Sym1, Sym2 };
  Mode mode_ = Mode::Alpha;
  bool shift_ = true, caps_ = false;
  uint32_t lastShiftMs_ = 0, repeatAt_ = 0;
  void typed(char* buf, size_t cap, char c);
};

class NumPad {
 public:
  static constexpr int kRowH = 40;
  static constexpr int kHeight = kRowH * 4;
  // dialChars adds + * # keys; allowAlpha shows an "ABC" key.
  KeyAction draw(Gui& ui, int y, char* buf, size_t cap, const char* actionLabel, bool dialChars, bool allowAlpha);

 private:
  uint32_t repeatAt_ = 0;
};

extern Keyboard kb;
extern NumPad numpad;
