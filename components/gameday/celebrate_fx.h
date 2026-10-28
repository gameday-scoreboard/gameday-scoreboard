// The celebration animations: fireworks, confetti, a light wave, a ball out
// of the park, a rippling net, a spinning goal lamp, a three through the
// hoop, drawn into an RGB565 frame. Pure, no ESPHome or LVGL includes: the
// panel hands it the LVGL canvas buffer (celebrate_lvgl.h), the host tests
// and tests/render_celebrations.cpp hand it a plain array, so the GIFs in a
// review are the firmware's own frames. Fixed-size, no allocation after
// start(): a particle pool of kCelebParticles and nothing else.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

#include "celebrate.h"

namespace espn {

constexpr int kCelebParticles = 256;

inline uint16_t rgb565(uint32_t rgb) {
  return (uint16_t) (((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}
// rgb scaled by k/256.
inline uint32_t rgb_scale(uint32_t rgb, int k) {
  if (k >= 256)
    return rgb;
  if (k <= 0)
    return 0;
  uint32_t r = ((rgb >> 16) & 0xFF) * k / 256, g = ((rgb >> 8) & 0xFF) * k / 256, b = (rgb & 0xFF) * k / 256;
  return (r << 16) | (g << 8) | b;
}
inline uint32_t rgb_mix(uint32_t a, uint32_t b, int k) {  // k/256 of b
  uint32_t r = (((a >> 16) & 0xFF) * (256 - k) + ((b >> 16) & 0xFF) * k) / 256;
  uint32_t g = (((a >> 8) & 0xFF) * (256 - k) + ((b >> 8) & 0xFF) * k) / 256;
  uint32_t bl = ((a & 0xFF) * (256 - k) + (b & 0xFF) * k) / 256;
  return (r << 16) | (g << 8) | bl;
}

// A frame to draw into: w x h RGB565 pixels, row stride in pixels. Every
// write is bounds checked, so a particle off the edge costs nothing.
struct CelebFrame {
  uint16_t *px{nullptr};
  int w{0}, h{0}, stride{0};
  void clear(uint32_t rgb = 0) {
    const uint16_t c = rgb565(rgb);
    for (int y = 0; y < h; y++) {
      uint16_t *row = px + y * stride;
      if (c == 0)
        memset(row, 0, (size_t) w * 2);
      else
        for (int x = 0; x < w; x++)
          row[x] = c;
    }
  }
  void put(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || x >= w || y >= h)
      return;
    px[y * stride + x] = rgb565(rgb);
  }
  // Adds light: the brighter of what is there and the new color, per channel.
  void glow(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || x >= w || y >= h)
      return;
    uint16_t &p = px[y * stride + x];
    uint16_t c = rgb565(rgb);
    uint16_t r = (p & 0xF800) > (c & 0xF800) ? (p & 0xF800) : (c & 0xF800);
    uint16_t g = (p & 0x07E0) > (c & 0x07E0) ? (p & 0x07E0) : (c & 0x07E0);
    uint16_t b = (p & 0x001F) > (c & 0x001F) ? (p & 0x001F) : (c & 0x001F);
    p = r | g | b;
  }
  void hline(int x0, int x1, int y, uint32_t rgb) {
    if (x0 > x1) {
      int t = x0;
      x0 = x1;
      x1 = t;
    }
    for (int x = x0; x <= x1; x++)
      put(x, y, rgb);
  }
  void vline(int x, int y0, int y1, uint32_t rgb) {
    if (y0 > y1) {
      int t = y0;
      y0 = y1;
      y1 = t;
    }
    for (int y = y0; y <= y1; y++)
      put(x, y, rgb);
  }
  void rect(int x0, int y0, int x1, int y1, uint32_t rgb) {
    for (int y = y0; y <= y1; y++)
      hline(x0, x1, y, rgb);
  }
  void line(int x0, int y0, int x1, int y1, uint32_t rgb) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (int n = 0; n < 512; n++) {
      put(x0, y0, rgb);
      if (x0 == x1 && y0 == y1)
        break;
      int e2 = 2 * err;
      if (e2 > -dy) {
        err -= dy;
        x0 += sx;
      }
      if (e2 < dx) {
        err += dx;
        y0 += sy;
      }
    }
  }
  void disc(int cx, int cy, int r, uint32_t rgb) {
    for (int y = -r; y <= r; y++)
      for (int x = -r; x <= r; x++)
        if (x * x + y * y <= r * r)
          put(cx + x, cy + y, rgb);
  }
  // Pixel count that is not black, for the tests.
  int lit() const {
    int n = 0;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
        n += px[y * stride + x] != 0;
    return n;
  }
};

// xorshift32: the same seed gives the same show, on the panel and the host.
struct CelebRng {
  uint32_t s{0x9E3779B9};
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  float unit() { return (float) (next() & 0xFFFF) / 65536.0f; }  // [0, 1)
  float range(float a, float b) { return a + (b - a) * unit(); }
  int below(int n) { return n <= 0 ? 0 : (int) (next() % (uint32_t) n); }
};

struct CelebParticle {
  float x, y, vx, vy;
  float life, max_life;  // seconds left, seconds at birth
  uint32_t color;
  uint8_t kind;  // 0 spark (gravity, fades), 1 confetti (drifts, wraps), 2 rocket (rises, bursts)
  uint8_t size;  // 1 or 2 pixels
};

class Celebration {
 public:
  void start(const CelebPlan &plan, int w, int h, uint32_t seed = 0x9E3779B9) {
    plan_ = plan;
    w_ = w;
    h_ = h;
    t_ = 0;
    count_ = 0;
    rng_.s = seed ? seed : 1;
    next_rocket_ = 0;
    ball_done_ = false;
    switch (plan.kind) {
      case CelebKind::TOUCHDOWN:
        // Three bursts at once for the punch, rockets after.
        for (int i = 0; i < 4; i++)
          burst_(rng_.range(w * 0.15f, w * 0.85f), rng_.range(4, h * 0.45f), 44, i == 1 ? plan.alt : plan.color);
        next_rocket_ = 0.35f;
        break;
      case CelebKind::WIN:
        // Confetti for the board's area: a single panel gets half as much.
        for (int i = 0, n = kCelebParticles * 5 / 6 * (w < 128 ? w : 128) / 128; i < n; i++)
          confetti_(rng_.range(0, (float) h));
        break;
      default:
        break;
    }
  }

  bool running() const { return plan_.kind != CelebKind::NONE && t_ * 1000.0f < (float) plan_.duration_ms; }
  float elapsed() const { return t_; }
  const CelebPlan &plan() const { return plan_; }
  int particles() const { return count_; }

  // Advances dt seconds. False once the show is over.
  bool step(float dt) {
    if (plan_.kind == CelebKind::NONE)
      return false;
    if (dt > 0.1f)
      dt = 0.1f;
    t_ += dt;
    switch (plan_.kind) {
      case CelebKind::TOUCHDOWN:
        if (t_ >= next_rocket_ && t_ < plan_.duration_ms / 1000.0f - 1.2f) {
          rocket_();
          if (rng_.below(3) == 0)
            rocket_();
          next_rocket_ = t_ + rng_.range(0.2f, 0.4f);
        }
        break;
      case CelebKind::HOME_RUN:
        home_run_step_();
        break;
      case CelebKind::THREE:
        three_step_();
        break;
      case CelebKind::GOAL_NET:
        if (t_ < 0.05f)
          burst_(net_hit_x_(), net_hit_y_(), 30, plan_.color);
        break;
      default:
        break;
    }
    step_particles_(dt);
    return running();
  }

  void render(CelebFrame &f) const {
    switch (plan_.kind) {
      case CelebKind::NONE:
        f.clear();
        return;
      case CelebKind::FLASH:
      case CelebKind::MUTED:
        f.clear(plan_.color);
        return;
      case CelebKind::WAVE:
        render_wave_(f);
        break;
      case CelebKind::TOUCHDOWN:
        f.clear();
        render_goalposts_(f);
        break;
      case CelebKind::HOME_RUN:
        f.clear();
        render_park_(f);
        break;
      case CelebKind::GOAL_NET:
        f.clear();
        render_net_(f);
        break;
      case CelebKind::GOAL_LAMP:
        render_lamp_(f);
        break;
      case CelebKind::THREE:
        f.clear();
        render_hoop_(f);
        break;
      case CelebKind::WIN:
        f.clear();
        break;
    }
    render_particles_(f);
  }

 private:
  CelebPlan plan_;
  int w_{128}, h_{64};
  float t_{0};
  float next_rocket_{0};
  bool ball_done_{false};
  CelebRng rng_;
  CelebParticle p_[kCelebParticles];
  int count_{0};

  float fade_() const {
    // The last 0.6 s dims everything out so the board comes back softly.
    float left = plan_.duration_ms / 1000.0f - t_;
    return left < 0.6f ? (left < 0 ? 0 : left / 0.6f) : 1.0f;
  }

  CelebParticle *alloc_() {
    if (count_ < kCelebParticles)
      return &p_[count_++];
    // Full: recycle the oldest spark so a late burst still shows.
    int oldest = 0;
    for (int i = 1; i < count_; i++)
      if (p_[i].life < p_[oldest].life)
        oldest = i;
    return &p_[oldest];
  }

  void spark_(float x, float y, float vx, float vy, float life, uint32_t color, uint8_t size = 1) {
    CelebParticle *p = alloc_();
    *p = CelebParticle{x, y, vx, vy, life, life, color, 0, size};
  }

  void burst_(float cx, float cy, int n, uint32_t color) {
    for (int i = 0; i < n; i++) {
      float a = rng_.range(0, 6.2832f);
      float sp = rng_.range(8, 38);
      uint32_t c = (i % 4 == 0) ? plan_.alt : color;
      spark_(cx, cy, cosf(a) * sp, sinf(a) * sp, rng_.range(0.9f, 1.8f), c, (uint8_t) (i % 5 == 0 ? 2 : 1));
    }
  }

  void rocket_() {
    CelebParticle *p = alloc_();
    float x = rng_.range(w_ * 0.15f, w_ * 0.85f);
    *p = CelebParticle{x, (float) h_, rng_.range(-6, 6), -rng_.range(h_ * 1.1f, h_ * 1.6f), rng_.range(0.45f, 0.7f),
                       0.6f, plan_.alt, 2, 1};
  }

  void confetti_(float y) {
    CelebParticle *p = alloc_();
    static const uint32_t extra[2] = {0xFFFFFF, 0xFFD000};
    int pick = rng_.below(4);
    uint32_t c = pick == 0 ? plan_.alt : pick == 1 ? extra[rng_.below(2)] : plan_.color;
    *p = CelebParticle{rng_.range(0, (float) w_), y, rng_.range(-8, 8), rng_.range(10, 26), 1e9f, 1e9f, c, 1,
                       (uint8_t) (1 + rng_.below(2))};
  }

  void step_particles_(float dt) {
    const float g = (float) h_ * 1.1f;  // gravity, so a burst falls in about a second
    for (int i = 0; i < count_;) {
      CelebParticle &p = p_[i];
      p.life -= dt;
      switch (p.kind) {
        case 0:
          p.vy += g * dt;
          p.vx *= 0.985f;
          p.x += p.vx * dt;
          p.y += p.vy * dt;
          break;
        case 1:
          // Confetti sways as it falls and comes back in at the top.
          p.x += (p.vx + 6.0f * sinf(t_ * 3.0f + p.y * 0.2f)) * dt;
          p.y += p.vy * dt;
          if (p.y > h_ + 2) {
            p.y = -2;
            p.x = rng_.range(0, (float) w_);
          }
          if (p.x < -2)
            p.x = (float) w_ + 1;
          if (p.x > w_ + 2)
            p.x = -1;
          break;
        case 2:
          p.vy += g * 0.6f * dt;
          p.x += p.vx * dt;
          p.y += p.vy * dt;
          if (p.life <= 0 || p.vy >= -4) {
            // The rocket has spent its climb: burst where it is.
            float bx = p.x, by = p.y;
            p.life = 0;
            burst_(bx, by, 36, plan_.color);
          }
          break;
      }
      if (p.life <= 0 && p.kind != 1) {
        p_[i] = p_[--count_];  // swap-remove
        continue;
      }
      i++;
    }
  }

  void render_particles_(CelebFrame &f) const {
    const float fade = fade_();
    for (int i = 0; i < count_; i++) {
      const CelebParticle &p = p_[i];
      int k = 256;
      if (p.kind == 0 && p.max_life > 0) {
        k = (int) (400.0f * (p.life / p.max_life));
        if (k > 256)
          k = 256;
      }
      uint32_t c = rgb_scale(p.color, (int) (k * fade));
      int x = (int) lroundf(p.x), y = (int) lroundf(p.y);
      f.glow(x, y, c);
      if (p.size > 1) {
        f.glow(x + 1, y, c);
        if (p.kind == 1)
          f.glow(x, y + 1, c);
      }
      if (p.kind == 2)  // a rocket's tail
        f.glow(x, y + 1, rgb_scale(c, 128));
    }
  }

  // ---- the sweep ------------------------------------------------------------
  void render_wave_(CelebFrame &f) const {
    // A bright bar sweeps left to right twice over 1.4 s, leaving the team's
    // color behind it at half strength so the white words read.
    const float fade = fade_();
    const float sweep = 0.7f;
    float u = t_ / sweep;  // 0..2 are the two sweeps
    // The head starts a few pixels in, so the first frame already shows it.
    int head = u < 2.0f ? (int) ((u - floorf(u)) * (w_ + 12)) + 3 : w_ + 16;
    uint32_t base = rgb_scale(plan_.color, 110);
    for (int x = 0; x < w_; x++) {
      int d = head - x;
      uint32_t c = base;
      if (u < 2.0f && d >= 0 && d < 10)
        c = rgb_mix(plan_.color, 0xFFFFFF, (10 - d) * 20 > 160 ? 160 : (10 - d) * 20);
      else if (u < 2.0f && x > head)
        c = u < 1.0f ? 0 : base;
      c = rgb_scale(c, (int) (256 * fade));
      f.vline(x, 0, h_ - 1, c);
    }
  }

  // ---- football -------------------------------------------------------------
  void render_goalposts_(CelebFrame &f) const {
    // A small goalpost on the ground under the words: a pole, the crossbar,
    // two uprights. Gold, as the pips are.
    const uint32_t gold = rgb_scale(0xFFD000, (int) (256 * fade_()));
    const int cx = w_ / 2, ground = h_ - 1, bar = h_ - 11, top = h_ - 22, half = w_ >= 128 ? 12 : 9;
    f.vline(cx, ground, bar, gold);
    f.hline(cx - half, cx + half, bar, gold);
    f.vline(cx - half, bar, top, gold);
    f.vline(cx + half, bar, top, gold);
    // The ball sails through the uprights for the first second.
    if (t_ < 1.1f) {
      float u = t_ / 1.1f;
      float x = -4 + u * (w_ + 8);
      float y = bar - 2 - sinf(u * 3.1416f) * (h_ * 0.45f);
      f.put((int) x, (int) y, 0xC06020);
      f.put((int) x + 1, (int) y, 0xC06020);
    }
  }

  // ---- baseball -------------------------------------------------------------
  float ball_u_() const { return t_ / 2.0f; }  // the flight takes 2 s
  void ball_at_(float u, float &x, float &y) const {
    x = 4 + u * (w_ + 4);
    // Up and out: it peaks under the words and leaves the right edge high.
    y = (h_ - 8) - sinf(u * 2.2f) * (h_ * 0.5f);
  }
  void home_run_step_() {
    float u = ball_u_();
    if (!ball_done_ && u >= 0.78f) {
      ball_done_ = true;
      float x, y;
      ball_at_(u, x, y);
      burst_(x, y, 56, plan_.color);
    }
  }
  void render_park_(CelebFrame &f) const {
    const float fade = fade_();
    // The outfield wall and a strip of grass; the crowd sparkles over it.
    const uint32_t wall = rgb_scale(0x1E6E2A, (int) (256 * fade)), grass = rgb_scale(0x0E3A14, (int) (256 * fade));
    f.rect(0, h_ - 5, w_ - 1, h_ - 1, grass);
    f.hline(0, w_ - 1, h_ - 6, wall);
    f.hline(0, w_ - 1, h_ - 7, wall);
    // Stadium lights: two masts with a flicker.
    for (int m = 0; m < 2; m++) {
      int lx = m == 0 ? w_ / 5 : w_ * 4 / 5;
      f.vline(lx, h_ - 7, h_ - 20, rgb_scale(0x606060, (int) (256 * fade)));
      bool on = ((int) (t_ * 10) + m) % 3 != 0;
      f.rect(lx - 2, h_ - 23, lx + 2, h_ - 21, rgb_scale(on ? 0xFFFFE0 : 0x806020, (int) (256 * fade)));
    }
    float u = ball_u_();
    if (u <= 1.0f) {
      // The ball, 2x2 and white, with a trail of six fading dots behind it.
      for (int k = 6; k >= 0; k--) {
        float uu = u - k * 0.025f;
        if (uu < 0)
          continue;
        float x, y;
        ball_at_(uu, x, y);
        if (k > 0) {
          f.glow((int) x, (int) y, rgb_scale(plan_.alt, (int) ((220 - k * 30) * fade)));
          continue;
        }
        uint32_t c = rgb_scale(0xFFFFFF, (int) (256 * fade));
        f.glow((int) x, (int) y, c);
        f.glow((int) x + 1, (int) y, c);
        f.glow((int) x, (int) y - 1, c);
        f.glow((int) x + 1, (int) y - 1, c);
      }
    }
  }

  // ---- soccer ---------------------------------------------------------------
  float net_hit_x_() const { return w_ * 0.5f; }
  float net_hit_y_() const { return h_ * 0.45f; }
  void render_net_(CelebFrame &f) const {
    // A net of 6 px squares across the whole board, rippling out from where
    // the ball hit: each strand is pushed along the ring that passes it.
    const float fade = fade_();
    const uint32_t strand = rgb_scale(0x606060, (int) (256 * fade));
    const float hx = net_hit_x_(), hy = net_hit_y_();
    const float speed = w_ * 0.45f, wavelen = 16.0f;
    const float amp = t_ < 3.5f ? 3.0f * (1.0f - t_ / 3.5f) : 0.0f;
    const float ring = t_ * speed;
    auto shift = [&](float x, float y, float &dx, float &dy) {
      float ddx = x - hx, ddy = y - hy;
      float d = sqrtf(ddx * ddx + ddy * ddy) + 0.01f;
      float ph = (ring - d) / wavelen * 6.2832f;
      float a = d < ring ? amp * sinf(ph) * expf(-(ring - d) / (w_ * 1.2f)) : 0;
      dx = ddx / d * a;
      dy = ddy / d * a;
    };
    for (int x = 1; x < w_; x += 6)
      for (int y = 0; y < h_ - 1; y++) {
        float dx, dy;
        shift((float) x, (float) y, dx, dy);
        f.put(x + (int) lroundf(dx), y + (int) lroundf(dy), strand);
      }
    for (int y = 1; y < h_; y += 6)
      for (int x = 0; x < w_ - 1; x++) {
        float dx, dy;
        shift((float) x, (float) y, dx, dy);
        f.put(x + (int) lroundf(dx), y + (int) lroundf(dy), strand);
      }
    // The ball sits where it hit for the first moment, in the team's color.
    if (t_ < 0.4f)
      f.disc((int) hx, (int) hy, 2, rgb_scale(plan_.color, (int) (256 * fade)));
  }

  // ---- hockey ---------------------------------------------------------------
  void render_lamp_(CelebFrame &f) const {
    // The goal lamp: a red beam spinning from the middle, a red glow that
    // pulses, and the frame of the board flashing red. White words go on top.
    const float fade = fade_();
    const float pulse = 0.55f + 0.45f * sinf(t_ * 9.0f);
    f.clear(rgb_scale(0x500000, (int) (pulse * 256 * fade)));
    const float cx = w_ / 2.0f, cy = h_ / 2.0f, R = (float) w_;
    const float th = t_ * 7.0f;  // rad/s, a lamp's spin
    for (int tail = 0; tail < 12; tail++) {
      float a = th - tail * 0.045f;
      uint32_t c = rgb_scale(tail == 0 ? 0xFF6060 : 0xFF0000, (int) ((256 - tail * 18) * fade));
      for (int side = 0; side < 2; side++) {
        float aa = a + side * 3.1416f;
        f.line((int) cx, (int) cy, (int) (cx + cosf(aa) * R), (int) (cy + sinf(aa) * R), c);
      }
    }
    // The flashing frame.
    if (((int) (t_ * 6)) % 2 == 0) {
      uint32_t c = rgb_scale(0xFF2020, (int) (256 * fade));
      f.hline(0, w_ - 1, 0, c);
      f.hline(0, w_ - 1, h_ - 1, c);
      f.vline(0, 0, h_ - 1, c);
      f.vline(w_ - 1, 0, h_ - 1, c);
    }
  }

  // ---- basketball -----------------------------------------------------------
  int hoop_x_() const { return w_ - (w_ >= 128 ? 22 : 16); }
  int hoop_y_() const { return h_ * 0.3f; }
  void three_step_() {
    // The ball drops through at 1.1 s: a pop of sparks under the rim.
    if (!ball_done_ && t_ >= 1.1f) {
      ball_done_ = true;
      burst_(hoop_x_(), hoop_y_() + 4, 24, plan_.color);
    }
  }
  void render_hoop_(CelebFrame &f) const {
    const float fade = fade_();
    const int hx = hoop_x_(), hy = hoop_y_();
    const uint32_t rim = rgb_scale(0xFF6A00, (int) (256 * fade)), board = rgb_scale(0xA0A0A0, (int) (256 * fade));
    const uint32_t net = rgb_scale(0xC0C0C0, (int) (200 * fade));
    // Backboard, rim, net.
    f.vline(hx + 7, hy - 14, hy + 2, board);
    f.vline(hx + 8, hy - 14, hy + 2, board);
    f.hline(hx - 6, hx + 6, hy, rim);
    f.hline(hx - 6, hx + 6, hy + 1, rgb_scale(rim, 160));
    const float swing = t_ > 1.1f && t_ < 2.1f ? sinf((t_ - 1.1f) * 12.0f) * (2.1f - t_) * 2.5f : 0.0f;
    for (int k = -3; k <= 3; k++) {
      int x0 = hx + k * 2, x1 = hx + k + (int) lroundf(swing);
      f.line(x0, hy + 2, x1, hy + 9, net);
    }
    // The ball: from the lower left, up over the words and down through the rim.
    if (t_ < 1.1f) {
      float u = t_ / 1.1f;
      float x = 6 + u * (hx - 6);
      float y = (h_ - 8) - sinf(u * 3.1416f * 0.8f) * (h_ * 0.78f);
      if (y > hy - 2 && u > 0.6f)
        y = hy - 2 + (u - 0.6f) * 4;  // dropping in
      // A short trail, then the ball.
      for (int k = 3; k >= 1; k--) {
        float uu = u - k * 0.03f;
        if (uu < 0)
          continue;
        float tx = 6 + uu * (hx - 6), ty = (h_ - 8) - sinf(uu * 3.1416f * 0.8f) * (h_ * 0.78f);
        f.glow((int) tx, (int) ty, rgb_scale(0xFF8C1A, (int) ((200 - k * 50) * fade)));
      }
      f.disc((int) x, (int) y, 1, rgb_scale(0xFF8C1A, (int) (256 * fade)));
    } else if (t_ < 1.5f) {
      float d = (t_ - 1.1f) / 0.4f;
      f.disc(hx, hy + 2 + (int) (d * 10), 1, rgb_scale(0xFF8C1A, (int) ((1 - d) * 256 * fade)));
    }
  }
};

}  // namespace espn
