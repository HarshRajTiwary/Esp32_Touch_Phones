#pragma once
// Colours, fonts and layout metrics for the 240x320 portrait UI.
#include "display.h"

namespace th {

// ---- colours (RGB888) ----
constexpr uint32_t BG        = 0x0B0F14;
constexpr uint32_t SURFACE   = 0x161C24;
constexpr uint32_t SURFACE2  = 0x222A36;
constexpr uint32_t PRESSED   = 0x33404F;
constexpr uint32_t BORDER    = 0x2E3947;
constexpr uint32_t TEXT      = 0xE8EDF2;
constexpr uint32_t MUTED     = 0x8C98A5;
constexpr uint32_t DIM       = 0x566170;
constexpr uint32_t ACCENT    = 0x3B82F6;
constexpr uint32_t GREEN     = 0x22C55E;
constexpr uint32_t RED       = 0xEF4444;
constexpr uint32_t AMBER     = 0xF59E0B;
constexpr uint32_t PURPLE    = 0x8B5CF6;
constexpr uint32_t ORANGE    = 0xF97316;
constexpr uint32_t TEAL      = 0x14B8A6;
constexpr uint32_t INDIGO    = 0x6366F1;
constexpr uint32_t SLATE     = 0x64748B;
constexpr uint32_t PINK      = 0xEC4899;
constexpr uint32_t BUBBLE_IN = 0x232C38;
constexpr uint32_t BUBBLE_OUT= 0x2563EB;
constexpr uint32_t WHITE     = 0xFFFFFF;
constexpr uint32_t BLACK     = 0x000000;

// ---- fonts ----
inline const lgfx::IFont* fTiny()   { return &fonts::DejaVu9; }
inline const lgfx::IFont* fSmall()  { return &fonts::DejaVu12; }
inline const lgfx::IFont* fBody()   { return &fonts::FreeSans9pt7b; }
inline const lgfx::IFont* fBodyB()  { return &fonts::FreeSansBold9pt7b; }
inline const lgfx::IFont* fTitle()  { return &fonts::FreeSansBold12pt7b; }
inline const lgfx::IFont* fMedium() { return &fonts::FreeSans12pt7b; }
inline const lgfx::IFont* fLarge()  { return &fonts::FreeSans18pt7b; }
inline const lgfx::IFont* fLargeB() { return &fonts::FreeSansBold18pt7b; }
inline const lgfx::IFont* fClock()  { return &fonts::DejaVu56; }
inline const lgfx::IFont* fHuge()   { return &fonts::DejaVu72; }

// ---- layout ----
constexpr int W = 240;
constexpr int H = 320;
constexpr int STATUS_H = 20;
constexpr int HEADER_H = 36;
constexpr int CONTENT_Y = STATUS_H + HEADER_H;  // 56

uint32_t mix(uint32_t a, uint32_t b, uint8_t t);  // t=0 -> a, 255 -> b
uint32_t avatarColor(const char* seed);

}  // namespace th
