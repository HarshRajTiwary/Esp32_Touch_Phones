// Touch calibration math (pure C++, covered by tests/host).
#include <math.h>
#include <string.h>

#include "touch.h"

#ifndef TFT_WIDTH
#include "board_config.h"
#endif

namespace touch {

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
