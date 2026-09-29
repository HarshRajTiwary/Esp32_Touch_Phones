#pragma once
// ILI9341 panel + XPT2046 touch through LovyanGFX, with an off-screen
// frame buffer in PSRAM so every screen is drawn without flicker.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace display {

bool begin(uint8_t brightness);
lgfx::LovyanGFX& gfx();          // draw here (frame buffer, or the panel if no PSRAM)
lgfx::LGFX_Device& panel();      // the physical panel (touch access)
void present();                  // copies the frame buffer to the panel
bool buffered();
void setBrightness(uint8_t level);
void sleep(bool on);             // backlight off + panel sleep
bool asleep();

}  // namespace display
