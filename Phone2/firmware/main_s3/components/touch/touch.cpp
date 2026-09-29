#include "touch.h"

#include <math.h>
#include <string.h>

#include "board_config.h"
#include "display.h"

namespace touch {

static float s_cal[6] = {0.0625f, 0, -12.0f, 0, 0.085f, -16.0f};
static int s_downCount = 0, s_upCount = 0;
static bool s_down = false;
static int16_t s_x = 0, s_y = 0;

void setCalibration(const float c[6]) { memcpy(s_cal, c, sizeof s_cal); }

bool readRaw(int16_t* rx, int16_t* ry) {
  lgfx::touch_point_t tp;
  if (display::panel().getTouchRaw(&tp, 1) == 0) return false;
  // A missing / unwired XPT2046 leaves MISO floating and reads saturated values;
  // never treat those as a press (it could otherwise "tap" random buttons).
  if (tp.x <= 0 || tp.y <= 0 || tp.x >= 4095 || tp.y >= 4095) return false;
  *rx = tp.x;
  *ry = tp.y;
  return true;
}

TouchSample read() {
  int16_t rx, ry;
  bool pressed = readRaw(&rx, &ry);
  if (pressed) {
    s_upCount = 0;
    int16_t x, y;
    mapRaw(s_cal, rx, ry, &x, &y);
    if (!s_down) {
      // Ignore the very first sample of a press: resistive panels report a
      // bogus position while the layers are still settling.
      if (++s_downCount >= 2) {
        s_down = true;
        s_x = x;
        s_y = y;
      }
    } else {
      int dx = x - s_x, dy = y - s_y;
      if (dx * dx + dy * dy < 36) {  // small jitter: smooth it
        s_x = (int16_t)((s_x + x) / 2);
        s_y = (int16_t)((s_y + y) / 2);
      } else {
        s_x = x;
        s_y = y;
      }
    }
  } else {
    s_downCount = 0;
    if (s_down && ++s_upCount >= 3) s_down = false;  // ~45 ms of no contact = released
  }
  TouchSample t;
  t.down = s_down;
  t.x = s_x;
  t.y = s_y;
  return t;
}

}  // namespace touch
