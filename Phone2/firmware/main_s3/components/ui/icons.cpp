#include "icons.h"

#include <math.h>

namespace {

void handset(lgfx::LovyanGFX& g, int cx, int cy, int s, uint32_t c, float rot) {
  // C-shaped receiver: a thick quarter ring with rounded ear and mouth pieces.
  float r0 = s * 0.30f, r1 = s * 0.52f, rm = (r0 + r1) / 2;
  float ox = cx + s * 0.22f, oy = cy - s * 0.22f;  // ring centre, top-right of the glyph
  float a0 = 95 + rot, a1 = 175 + rot;
  // rotate the ring centre around the glyph centre
  if (rot != 0) {
    float rad = rot * 3.14159265f / 180.0f;
    float dx = ox - cx, dy = oy - cy;
    ox = cx + dx * cosf(rad) - dy * sinf(rad);
    oy = cy + dx * sinf(rad) + dy * cosf(rad);
  }
  g.fillArc((int)ox, (int)oy, (int)r0, (int)r1, a0, a1, c);
  float e0 = a0 * 3.14159265f / 180.0f, e1 = a1 * 3.14159265f / 180.0f;
  float pr = s * 0.15f;
  g.fillSmoothCircle((int)(ox + rm * cosf(e0)), (int)(oy + rm * sinf(e0)), (int)pr, c);
  g.fillSmoothCircle((int)(ox + rm * cosf(e1)), (int)(oy + rm * sinf(e1)), (int)pr, c);
}

void arrow(lgfx::LovyanGFX& g, int x0, int y0, int x1, int y1, float w, uint32_t c) {
  g.drawWideLine(x0, y0, x1, y1, w, c);
  float ang = atan2f((float)(y1 - y0), (float)(x1 - x0));
  float len = 5.0f + w * 2;
  for (float d : {2.5f, -2.5f}) {
    float a = ang + 3.14159265f + d * 0.3f;
    g.drawWideLine(x1, y1, (int)(x1 + len * cosf(a)), (int)(y1 + len * sinf(a)), w, c);
  }
}

}  // namespace

void drawIcon(lgfx::LovyanGFX& g, Icon icon, int cx, int cy, int s, uint32_t c, uint32_t bg) {
  float w = s >= 24 ? 2.0f : 1.4f;  // stroke half-width
  int h = s / 2;
  switch (icon) {
    case Icon::None:
      break;
    case Icon::Phone:
      handset(g, cx, cy, s, c, 0);
      break;
    case Icon::PhoneEnd:
      handset(g, cx, cy + s / 10, s, c, 135);
      break;
    case Icon::Message:
      g.fillSmoothRoundRect(cx - h, cy - s * 0.38f, s, (int)(s * 0.68f), s / 5, c);
      g.fillTriangle(cx - h + s / 5, cy + s * 0.25f, cx - h + s / 5, cy + s * 0.48f, cx - h + s * 0.48f, cy + s * 0.25f, c);
      g.fillSmoothCircle(cx - s / 4, cy - s / 20, s / 14 + 1, bg);
      g.fillSmoothCircle(cx, cy - s / 20, s / 14 + 1, bg);
      g.fillSmoothCircle(cx + s / 4, cy - s / 20, s / 14 + 1, bg);
      break;
    case Icon::Person:
      g.fillSmoothCircle(cx, cy - s * 0.2f, s * 0.22f, c);
      g.fillArc(cx, cy + s * 0.48f, 0, (int)(s * 0.42f), 180, 360, c);
      break;
    case Icon::Recents:
    case Icon::Clock:
      g.fillArc(cx, cy, (int)(s * 0.36f), (int)(s * 0.48f), 0, 360, c);
      g.drawWideLine(cx, cy, cx, cy - s * 0.24f, w, c);
      g.drawWideLine(cx, cy, cx + s * 0.18f, cy + s * 0.08f, w, c);
      break;
    case Icon::Gear:
      for (int k = 0; k < 8; ++k) {
        float a = k * 3.14159265f / 4;
        g.drawWideLine(cx, cy, cx + s * 0.44f * cosf(a), cy + s * 0.44f * sinf(a), s * 0.09f, c);
      }
      g.fillSmoothCircle(cx, cy, s * 0.33f, c);
      g.fillSmoothCircle(cx, cy, s * 0.13f, bg);
      break;
    case Icon::Bluetooth: {
      int t = cy - h + 1, b = cy + h - 1, r = cx + s / 4, l = cx - s / 4;
      int q1 = cy - s / 4, q3 = cy + s / 4;
      g.drawWideLine(cx, t, cx, b, w, c);
      g.drawWideLine(cx, t, r, q1, w, c);
      g.drawWideLine(r, q1, l, q3, w, c);
      g.drawWideLine(cx, b, r, q3, w, c);
      g.drawWideLine(r, q3, l, q1, w, c);
      break;
    }
    case Icon::Headphones:
      g.fillArc(cx, cy + s / 10, (int)(s * 0.36f), (int)(s * 0.46f), 180, 360, c);
      g.fillSmoothRoundRect(cx - s * 0.48f, cy + s * 0.02f, s * 0.22f, s * 0.42f, s / 10, c);
      g.fillSmoothRoundRect(cx + s * 0.26f, cy + s * 0.02f, s * 0.22f, s * 0.42f, s / 10, c);
      break;
    case Icon::Back:
      g.drawWideLine(cx + s / 8, cy - s / 3, cx - s / 5, cy, w + 0.4f, c);
      g.drawWideLine(cx - s / 5, cy, cx + s / 8, cy + s / 3, w + 0.4f, c);
      break;
    case Icon::Chevron:
      g.drawWideLine(cx - s / 8, cy - s / 4, cx + s / 8, cy, w, c);
      g.drawWideLine(cx + s / 8, cy, cx - s / 8, cy + s / 4, w, c);
      break;
    case Icon::Plus:
      g.drawWideLine(cx - s * 0.35f, cy, cx + s * 0.35f, cy, w + 0.3f, c);
      g.drawWideLine(cx, cy - s * 0.35f, cx, cy + s * 0.35f, w + 0.3f, c);
      break;
    case Icon::Close:
      g.drawWideLine(cx - s * 0.3f, cy - s * 0.3f, cx + s * 0.3f, cy + s * 0.3f, w, c);
      g.drawWideLine(cx - s * 0.3f, cy + s * 0.3f, cx + s * 0.3f, cy - s * 0.3f, w, c);
      break;
    case Icon::Check:
      g.drawWideLine(cx - s * 0.35f, cy, cx - s * 0.1f, cy + s * 0.25f, w + 0.3f, c);
      g.drawWideLine(cx - s * 0.1f, cy + s * 0.25f, cx + s * 0.38f, cy - s * 0.28f, w + 0.3f, c);
      break;
    case Icon::Backspace: {
      int l = cx - h, r = cx + h, t = cy - s / 3, b = cy + s / 3;
      g.drawWideLine(l, cy, l + s / 4, t, w, c);
      g.drawWideLine(l + s / 4, t, r, t, w, c);
      g.drawWideLine(r, t, r, b, w, c);
      g.drawWideLine(r, b, l + s / 4, b, w, c);
      g.drawWideLine(l + s / 4, b, l, cy, w, c);
      int xc = cx + s / 8;
      g.drawWideLine(xc - s / 8, cy - s / 8, xc + s / 8, cy + s / 8, w * 0.8f, c);
      g.drawWideLine(xc - s / 8, cy + s / 8, xc + s / 8, cy - s / 8, w * 0.8f, c);
      break;
    }
    case Icon::Mic:
    case Icon::MicOff:
      g.fillSmoothRoundRect(cx - s * 0.14f, cy - s * 0.45f, s * 0.28f, s * 0.55f, s * 0.14f, c);
      g.fillArc(cx, cy - s * 0.05f, (int)(s * 0.24f), (int)(s * 0.24f + w * 1.6f), 0, 180, c);
      g.drawWideLine(cx, cy + s * 0.22f, cx, cy + s * 0.4f, w, c);
      g.drawWideLine(cx - s * 0.16f, cy + s * 0.42f, cx + s * 0.16f, cy + s * 0.42f, w, c);
      if (icon == Icon::MicOff) {
        g.drawWideLine(cx - s * 0.4f, cy - s * 0.42f, cx + s * 0.4f, cy + s * 0.42f, w + 1.2f, bg);
        g.drawWideLine(cx - s * 0.4f, cy - s * 0.42f, cx + s * 0.4f, cy + s * 0.42f, w, th::RED);
      }
      break;
    case Icon::Keypad:
      for (int r = -1; r <= 1; ++r)
        for (int k = -1; k <= 1; ++k) g.fillSmoothCircle(cx + k * s / 3, cy + r * s / 3 - s / 12, s / 11 + 1, c);
      g.fillSmoothCircle(cx, cy + s * 0.5f - s / 12, s / 11 + 1, c);
      break;
    case Icon::VolUp:
    case Icon::VolDown: {
      int l = cx - h;
      g.fillRect(l, cy - s / 8, s / 5, s / 4, c);
      g.fillTriangle(l + s / 6, cy - s / 8, l + s / 6, cy + s / 8, l + s * 0.45f, cy - s * 0.35f, c);
      g.fillTriangle(l + s / 6, cy + s / 8, l + s * 0.45f, cy + s * 0.35f, l + s * 0.45f, cy - s * 0.35f, c);
      int px = cx + s / 4;
      g.drawWideLine(px - s / 7, cy, px + s / 7, cy, w, c);
      if (icon == Icon::VolUp) g.drawWideLine(px, cy - s / 7, px, cy + s / 7, w, c);
      break;
    }
    case Icon::Trash:
      g.fillSmoothRoundRect(cx - s * 0.3f, cy - s * 0.22f, s * 0.6f, s * 0.66f, s / 10, c);
      g.fillRect(cx - s * 0.4f, cy - s * 0.36f, s * 0.8f, s * 0.08f + 1, c);
      g.fillRect(cx - s * 0.12f, cy - s * 0.46f, s * 0.24f, s * 0.12f, c);
      g.drawWideLine(cx - s / 9, cy - s / 10, cx - s / 9, cy + s * 0.32f, w * 0.7f, bg);
      g.drawWideLine(cx + s / 9, cy - s / 10, cx + s / 9, cy + s * 0.32f, w * 0.7f, bg);
      break;
    case Icon::Search:
      g.fillArc(cx - s / 10, cy - s / 10, (int)(s * 0.24f), (int)(s * 0.24f + w * 1.6f), 0, 360, c);
      g.drawWideLine(cx + s * 0.14f, cy + s * 0.14f, cx + s * 0.42f, cy + s * 0.42f, w + 0.5f, c);
      break;
    case Icon::CallOut:
      arrow(g, cx - s / 3, cy + s / 3, cx + s / 3, cy - s / 3, w, c);
      break;
    case Icon::CallIn:
    case Icon::CallMissed:
      arrow(g, cx + s / 3, cy - s / 3, cx - s / 3, cy + s / 3, w, c);
      break;
    case Icon::Lock:
      g.fillArc(cx, cy - s / 12, (int)(s * 0.22f), (int)(s * 0.22f + w * 1.8f), 180, 360, c);
      g.fillRect(cx - s * 0.22f - w * 1.8f, cy - s / 12, w * 1.8f, s / 8, c);
      g.fillRect(cx + s * 0.22f, cy - s / 12, w * 1.8f, s / 8, c);
      g.fillSmoothRoundRect(cx - s * 0.36f, cy, s * 0.72f, s * 0.46f, s / 10, c);
      g.fillSmoothCircle(cx, cy + s * 0.2f, s / 14 + 1, bg);
      break;
    case Icon::Send:
      g.fillTriangle(cx - s * 0.42f, cy - s * 0.4f, cx + s * 0.45f, cy, cx - s * 0.42f, cy + s * 0.4f, c);
      g.fillTriangle(cx - s * 0.42f, cy - s * 0.4f, cx - s * 0.18f, cy, cx - s * 0.42f, cy + s * 0.4f, bg);
      break;
    case Icon::Sim:
      g.fillSmoothRoundRect(cx - s * 0.3f, cy - s * 0.42f, s * 0.6f, s * 0.84f, s / 10, c);
      g.fillTriangle(cx + s * 0.12f, cy - s * 0.43f, cx + s * 0.31f, cy - s * 0.43f, cx + s * 0.31f, cy - s * 0.24f, bg);
      g.fillSmoothRoundRect(cx - s * 0.16f, cy - s * 0.05f, s * 0.32f, s * 0.3f, s / 16, bg);
      break;
    case Icon::Info:
      g.fillSmoothCircle(cx, cy, h, c);
      g.fillRect(cx - s / 16, cy - s / 12, s / 8 + 1, s / 3, bg);
      g.fillSmoothCircle(cx, cy - s / 4, s / 14 + 1, bg);
      break;
    case Icon::Sun:
      for (int k = 0; k < 8; ++k) {
        float a = k * 3.14159265f / 4;
        g.drawWideLine(cx + s * 0.3f * cosf(a), cy + s * 0.3f * sinf(a), cx + s * 0.46f * cosf(a), cy + s * 0.46f * sinf(a), w * 0.8f, c);
      }
      g.fillSmoothCircle(cx, cy, s * 0.2f, c);
      break;
    case Icon::Antenna:
      g.drawWideLine(cx, cy - s / 8, cx, cy + s * 0.45f, w, c);
      g.fillSmoothCircle(cx, cy - s / 8, s / 10 + 1, c);
      g.fillArc(cx, cy - s / 8, (int)(s * 0.24f), (int)(s * 0.24f + w * 1.4f), 200, 340, c);
      g.fillArc(cx, cy - s / 8, (int)(s * 0.42f), (int)(s * 0.42f + w * 1.4f), 210, 330, c);
      break;
    case Icon::Balance:
      g.fillSmoothRoundRect(cx - s * 0.42f, cy - s * 0.3f, s * 0.84f, s * 0.6f, s / 8, c);
      g.fillRect(cx - s * 0.42f, cy - s * 0.14f, s * 0.84f, s / 9 + 1, bg);
      g.fillSmoothRoundRect(cx + s * 0.08f, cy + s * 0.06f, s * 0.24f, s * 0.12f, 2, bg);
      break;
    case Icon::Touch:
      g.drawWideLine(cx - s / 3, cy + s / 3, cx + s / 5, cy - s / 5, w + 0.6f, c);
      g.fillSmoothCircle(cx + s / 4, cy - s / 4, s / 8, c);
      break;
    case Icon::Reset:
      g.fillArc(cx, cy, (int)(s * 0.3f), (int)(s * 0.3f + w * 1.6f), 45, 330, c);
      g.fillTriangle(cx + s * 0.18f, cy - s * 0.42f, cx + s * 0.46f, cy - s * 0.2f, cx + s * 0.14f, cy - s * 0.1f, c);
      break;
    case Icon::Edit:
      g.drawWideLine(cx - s / 3, cy + s / 3, cx + s / 4, cy - s / 4, w + 1.2f, c);
      g.fillTriangle(cx - s * 0.4f, cy + s * 0.4f, cx - s * 0.36f, cy + s * 0.2f, cx - s * 0.2f, cy + s * 0.36f, c);
      break;
    case Icon::Bolt:
      g.fillTriangle(cx + s * 0.1f, cy - s * 0.48f, cx - s * 0.3f, cy + s * 0.08f, cx + s * 0.02f, cy + s * 0.08f, c);
      g.fillTriangle(cx - s * 0.1f, cy + s * 0.48f, cx + s * 0.3f, cy - s * 0.08f, cx - s * 0.02f, cy - s * 0.08f, c);
      break;
    case Icon::Wifi:
      // three arcs above a dot (Wi-Fi / hotspot)
      g.fillSmoothCircle(cx, cy + s * 0.3f, s / 10 + 1, c);
      g.fillArc(cx, cy + s * 0.3f, (int)(s * 0.22f), (int)(s * 0.22f + w * 1.5f), 225, 315, c);
      g.fillArc(cx, cy + s * 0.3f, (int)(s * 0.40f), (int)(s * 0.40f + w * 1.5f), 225, 315, c);
      g.fillArc(cx, cy + s * 0.3f, (int)(s * 0.58f), (int)(s * 0.58f + w * 1.5f), 225, 315, c);
      break;
    case Icon::Data:
      // up and down arrows (mobile data)
      arrow(g, cx - s / 6, cy + s * 0.38f, cx - s / 6, cy - s * 0.38f, w, c);
      arrow(g, cx + s / 6, cy - s * 0.38f, cx + s / 6, cy + s * 0.38f, w, c);
      break;
    case Icon::Chip: {
      // integrated circuit with pins (diagnostics)
      int b = (int)(s * 0.26f);
      g.fillSmoothRoundRect(cx - b, cy - b, 2 * b, 2 * b, s / 12, c);
      for (int k = -1; k <= 1; ++k) {
        int o = k * b * 2 / 3;
        g.drawWideLine(cx + o, cy - b, cx + o, cy - s * 0.44f, w * 0.7f, c);
        g.drawWideLine(cx + o, cy + b, cx + o, cy + s * 0.44f, w * 0.7f, c);
        g.drawWideLine(cx - b, cy + o, cx - s * 0.44f, cy + o, w * 0.7f, c);
        g.drawWideLine(cx + b, cy + o, cx + s * 0.44f, cy + o, w * 0.7f, c);
      }
      g.fillSmoothCircle(cx - b / 2, cy - b / 2, s / 16 + 1, bg);
      break;
    }
    case Icon::Speaker:
      // loudspeaker cone + sound waves
      g.fillRect(cx - s * 0.42f, cy - s * 0.14f, s * 0.18f, s * 0.28f, c);
      g.fillTriangle(cx - s * 0.26f, cy - s * 0.14f, cx - s * 0.02f, cy - s * 0.38f, cx - s * 0.02f, cy + s * 0.38f, c);
      g.fillTriangle(cx - s * 0.26f, cy - s * 0.14f, cx - s * 0.26f, cy + s * 0.14f, cx - s * 0.02f, cy + s * 0.38f, c);
      g.fillArc(cx, cy, (int)(s * 0.18f), (int)(s * 0.18f + w * 1.3f), 300, 60, c);
      g.fillArc(cx, cy, (int)(s * 0.34f), (int)(s * 0.34f + w * 1.3f), 300, 60, c);
      break;
    case Icon::Globe:
      g.fillArc(cx, cy, (int)(s * 0.40f), (int)(s * 0.40f + w * 1.4f), 0, 360, c);
      g.drawWideLine(cx - s * 0.4f, cy, cx + s * 0.4f, cy, w * 0.8f, c);
      g.drawEllipse(cx, cy, (int)(s * 0.16f), (int)(s * 0.40f), c);
      g.drawEllipse(cx, cy, (int)(s * 0.16f) + 1, (int)(s * 0.40f), c);
      break;
  }
}
