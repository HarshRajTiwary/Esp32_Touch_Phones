#pragma once
// Vector icons drawn from anti-aliased primitives (no bitmaps to store).
#include "theme.h"

enum class Icon : uint8_t {
  None, Phone, PhoneEnd, Message, Person, Recents, Gear, Bluetooth, Headphones,
  Back, Plus, Backspace, Mic, MicOff, Keypad, VolUp, VolDown, Trash, Search,
  CallOut, CallIn, CallMissed, Lock, Send, Sim, Info, Sun, Antenna, Clock, Check,
  Close, Chevron, Balance, Touch, Reset, Edit, Bolt,
  Wifi, Data, Chip, Speaker, Globe,
};

void drawIcon(lgfx::LovyanGFX& g, Icon icon, int cx, int cy, int size, uint32_t color, uint32_t bg = th::BG);
