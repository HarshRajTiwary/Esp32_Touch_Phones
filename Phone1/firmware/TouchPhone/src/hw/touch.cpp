#include "touch.h"

#include <math.h>
#include <string.h>

#include "../config/board_config.h"
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

void mapRaw(const float c[6], int16_t rx, int16_t ry, int16_t* x, int16_t* y) {
  float fx = c[0] * rx + c[1] * ry + c[2];
  float fy = c[3] * rx + c[4] * ry + c[5];
  if (fx < 0) fx = 0;
  if (fy < 0) fy = 0;
  if (fx > TFT_WIDTH - 1) fx = TFT_WIDTH - 1;
  if (fy > TFT_HEIGHT - 1) fy = TFT_HEIGHT - 1;
  *x = (int16_t)lrintf(fx);
  *y = (int16_t)lrintf(fy);
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

bool computeCalibration(const int16_t raw[][2], const int16_t scr[][2], int n, float out[6]) {
  if (n < 3) return false;
  double sxx = 0, sxy = 0, syy = 0, sx = 0, sy = 0;
  double bx[3] = {0}, by[3] = {0};
  for (int k = 0; k < n; ++k) {
    double x = raw[k][0], y = raw[k][1];
    sxx += x * x; sxy += x * y; syy += y * y; sx += x; sy += y;
    bx[0] += x * scr[k][0]; bx[1] += y * scr[k][0]; bx[2] += scr[k][0];
    by[0] += x * scr[k][1]; by[1] += y * scr[k][1]; by[2] += scr[k][1];
  }
  double m[3][3] = {{sxx, sxy, sx}, {sxy, syy, sy}, {sx, sy, (double)n}};
  auto det3 = [](double a[3][3]) {
    return a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
  };
  double d = det3(m);
  if (fabs(d) < 1e-6) return false;
  auto solve = [&](const double b[3], float* res) {
    for (int col = 0; col < 3; ++col) {
      double t[3][3];
      memcpy(t, m, sizeof t);
      for (int r = 0; r < 3; ++r) t[r][col] = b[r];
      res[col] = (float)(det3(t) / d);
    }
  };
  solve(bx, out);
  solve(by, out + 3);
  return true;
}

}  // namespace touch
