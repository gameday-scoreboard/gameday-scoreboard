// The weather card's pixel icons (weather.h's WxIcon), drawn into a square of
// side s at any size the card uses (12, 24 or 32), one frame at a time: the
// sun's rays turn, clouds drift, rain falls, snow drifts down, a storm
// flashes. frame counts up at about 10 a second. put(x, y, 0xRRGGBB) sets a
// pixel; the drawer never writes outside the square.
// Pure, with no ESPHome includes, so the host tests render it too.
#pragma once

#include <cmath>
#include <cstdint>

#include "weather.h"

namespace espn {
namespace wxi {

constexpr uint32_t kSun = 0xFFC400, kRay = 0xFF8C00, kCloud = 0xE8E8E8, kCloudShade = 0x9A9AA6,
                   kDark = 0x6E6E80, kDarkShade = 0x4A4A58, kRain = 0x2E7BFF, kSnow = 0xFFFFFF,
                   kBolt = 0xFFF040, kMoon = 0xE8E0A8, kStar = 0xC8C8FF, kFog = 0xA8A8A8, kFogDim = 0x686868;

constexpr float kPi = 3.14159265f;

template<class P> struct Pen {
  P &put;
  int x0, y0, s;
  void px(int x, int y, uint32_t c) const {
    if (x >= 0 && y >= 0 && x < s && y < s)
      put(x0 + x, y0 + y, c);
  }
  void disc(float cx, float cy, float r, uint32_t c) const {
    int a = (int) floorf(cy - r), b = (int) ceilf(cy + r);
    for (int y = a; y <= b; y++)
      for (int x = (int) floorf(cx - r); x <= (int) ceilf(cx + r); x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        if (dx * dx + dy * dy <= r * r)
          px(x, y, c);
      }
  }
  void line(float ax, float ay, float bx, float by, uint32_t c) const {
    float dx = bx - ax, dy = by - ay;
    int n = (int) ceilf(fmaxf(fabsf(dx), fabsf(dy)));
    if (n < 1)
      n = 1;
    for (int i = 0; i <= n; i++)
      px((int) floorf(ax + dx * i / n), (int) floorf(ay + dy * i / n), c);
  }
  void rect(float ax, float ay, float bx, float by, uint32_t c) const {
    for (int y = (int) floorf(ay); y < (int) ceilf(by); y++)
      for (int x = (int) floorf(ax); x < (int) ceilf(bx); x++)
        px(x, y, c);
  }
};

// A cloud w wide, centred on cx, its flat bottom at by. The lower edge is
// shaded so it reads as a shape and not a blob on a small icon.
template<class P> void cloud(const Pen<P> &p, float cx, float by, float w, uint32_t c, uint32_t shade) {
  const float r1 = w * 0.20f, r2 = w * 0.27f, r3 = w * 0.18f;
  for (int pass = 0; pass < 2; pass++) {
    const float d = pass == 0 ? 0.0f : -fmaxf(1.0f, w * 0.06f);  // the lit part sits higher
    const uint32_t col = pass == 0 ? shade : c;
    p.disc(cx - w * 0.27f, by - r1 + d, r1, col);
    p.disc(cx + w * 0.02f, by - r2 * 1.35f + d, r2, col);
    p.disc(cx + w * 0.30f, by - r3 + d, r3, col);
    p.rect(cx - w * 0.27f, by - r1 + d, cx + w * 0.30f, by + d, col);
  }
}

template<class P> void sun(const Pen<P> &p, float cx, float cy, float r, uint32_t frame, bool rays) {
  if (rays) {
    const float turn = frame * 0.035f;
    const float pulse = 1.0f + 0.10f * sinf(frame * 0.25f);
    for (int i = 0; i < 8; i++) {
      float a = turn + i * kPi / 4;
      float r1 = r * 1.35f, r2 = r * 1.85f * pulse;
      p.line(cx + cosf(a) * r1, cy + sinf(a) * r1, cx + cosf(a) * r2, cy + sinf(a) * r2, kRay);
    }
  }
  p.disc(cx, cy, r, kSun);
}

template<class P> void moon(const Pen<P> &p, float cx, float cy, float r) {
  // A crescent: the moon's disc with a black one bitten out of it.
  p.disc(cx, cy, r, kMoon);
  p.disc(cx + r * 0.55f, cy - r * 0.35f, r * 0.85f, 0x000000);
}

template<class P> void stars(const Pen<P> &p, uint32_t frame) {
  const float s = (float) p.s;
  const float at[3][2] = {{0.78f, 0.18f}, {0.88f, 0.52f}, {0.62f, 0.08f}};
  for (int i = 0; i < 3; i++) {
    // Each star dims for a moment on its own beat.
    bool lit = ((frame + i * 7) % 23) > 2;
    p.px((int) (s * at[i][0]), (int) (s * at[i][1]), lit ? kStar : 0x303050);
  }
}

// Things falling from under a cloud: kind 0 rain, 1 drizzle, 2 snow, 3 sleet.
template<class P> void falling(const Pen<P> &p, float top, int kind, uint32_t frame) {
  const float s = (float) p.s;
  const float band = s - top;
  if (band < 2)
    return;
  const int cols = kind == 1 ? 3 : 4;
  const float speed = (kind == 2 ? 0.35f : kind == 1 ? 0.6f : 1.2f) * fmaxf(1.0f, s / 24.0f);
  const int len = s >= 24 ? (kind == 1 ? 1 : 2) : 1;
  for (int i = 0; i < cols; i++) {
    float x = s * (0.22f + 0.56f * i / (cols - 1));
    // Two at a time in each column, half a band apart.
    for (int k = 0; k < 2; k++) {
      float phase = band * (0.5f * k + 0.37f * i);
      float y = top + fmodf(frame * speed + phase, band);
      bool flake = kind == 2 || (kind == 3 && (i + k) % 2 == 0);
      if (flake) {
        float fx = x + sinf(frame * 0.15f + i * 1.7f + k) * s * 0.05f;
        p.px((int) fx, (int) y, kSnow);
        if (s >= 24) {
          p.px((int) fx - 1, (int) y, 0x9090A0);
          p.px((int) fx + 1, (int) y, 0x9090A0);
          p.px((int) fx, (int) y - 1, 0x9090A0);
          p.px((int) fx, (int) y + 1, 0x9090A0);
        }
      } else {
        // Rain slants a little, a pixel over for every three down.
        for (int d = 0; d < len + (s >= 24 ? 1 : 0); d++)
          p.px((int) (x - (y + d - top) / 3.0f), (int) (y + d), kRain);
      }
    }
  }
}

}  // namespace wxi

template<class P> void wx_draw_icon(P &put, WxIcon icon, int x0, int y0, int s, uint32_t frame) {
  using namespace wxi;
  Pen<P> p{put, x0, y0, s};
  const float f = (float) s;
  const float drift = sinf(frame * 0.07f) * f * 0.05f;
  switch (icon) {
    case WxIcon::NONE:
      return;
    case WxIcon::SUN:
      sun(p, f * 0.5f, f * 0.5f, f * 0.22f, frame, true);
      return;
    case WxIcon::MOON:
      stars(p, frame);
      moon(p, f * 0.42f, f * 0.55f, f * 0.32f);
      return;
    case WxIcon::PARTLY_DAY:
      sun(p, f * 0.36f, f * 0.36f, f * 0.16f, frame, true);
      cloud(p, f * 0.58f + drift, f * 0.86f, f * 0.70f, kCloud, kCloudShade);
      return;
    case WxIcon::PARTLY_NIGHT:
      stars(p, frame);
      moon(p, f * 0.34f, f * 0.36f, f * 0.24f);
      cloud(p, f * 0.58f + drift, f * 0.86f, f * 0.70f, kCloud, kCloudShade);
      return;
    case WxIcon::CLOUD:
      cloud(p, f * 0.62f - drift, f * 0.56f, f * 0.58f, kCloudShade, kDarkShade);
      cloud(p, f * 0.42f + drift, f * 0.86f, f * 0.76f, kCloud, kCloudShade);
      return;
    case WxIcon::FOG:
      for (int i = 0; i < 4; i++) {
        float y = f * (0.28f + 0.17f * i);
        float off = sinf(frame * 0.06f + i * 1.3f) * f * 0.10f;
        float h = f >= 24 ? 2.0f : 1.0f;
        p.rect(f * 0.12f + off, y, f * 0.88f + off, y + h, i % 2 ? kFogDim : kFog);
      }
      return;
    case WxIcon::DRIZZLE:
    case WxIcon::RAIN:
    case WxIcon::SNOW:
    case WxIcon::SLEET: {
      const int kind = icon == WxIcon::RAIN ? 0 : icon == WxIcon::DRIZZLE ? 1 : icon == WxIcon::SNOW ? 2 : 3;
      falling(p, f * 0.58f, kind, frame);
      cloud(p, f * 0.5f + drift * 0.5f, f * 0.56f, f * 0.86f, kCloud, kCloudShade);
      return;
    }
    case WxIcon::STORM: {
      // A flash: the bolt and a lit cloud for a few frames out of every 30.
      const uint32_t beat = frame % 30;
      const bool flash = beat < 2 || beat == 5 || beat == 6;
      falling(p, f * 0.58f, 0, frame);
      if (flash) {
        const float pts[4][2] = {{0.56f, 0.52f}, {0.40f, 0.76f}, {0.56f, 0.76f}, {0.42f, 0.99f}};
        for (int i = 0; i < 3; i++) {
          p.line(f * pts[i][0], f * pts[i][1], f * pts[i + 1][0], f * pts[i + 1][1], kBolt);
          if (f >= 24)
            p.line(f * pts[i][0] + 1, f * pts[i][1], f * pts[i + 1][0] + 1, f * pts[i + 1][1], kBolt);
        }
      }
      cloud(p, f * 0.5f + drift * 0.5f, f * 0.56f, f * 0.86f, flash ? kCloud : kDark, flash ? kCloudShade : kDarkShade);
      return;
    }
  }
}

}  // namespace espn
