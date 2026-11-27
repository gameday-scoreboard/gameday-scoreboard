// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

// Shrinks a square team logo to the 32 px the panel shows. Picking one source
// pixel per LED, as the image decoder does, breaks thin outlines into specks,
// and averaging turns a white line on a dark field gray. Instead each LED
// takes one of the logo's own solid colors:
//
// 1. The palette is black plus the logo's solid colors (alpha 250 and up),
//    most used first, each covering at least 0.8% of the solid pixels and not
//    close to one already kept.
// 2. Every source pixel, over black, snaps to its nearest palette color.
// 3. Each LED takes the color that covers most of its block of source
//    pixels, less 0.35 times that color's share of the 3x3 LEDs around it, so
//    a thin line wins over the wide field it runs through.
//
// From a 128 px source each LED has a 4x4 block. Pure, with no ESPHome, so
// the host tests build it.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace espn::shrink {

static constexpr int OUT = 32;
// Colors in the palette, black included. Logos use a handful.
static constexpr int MAX_COLORS = 16;
// Kept colors cover at least 0.8% of the solid pixels, so no more than 125
// can qualify.
static constexpr int MAX_CANDIDATES = 128;
static constexpr float RARE_WEIGHT = 0.35f;

// The smallest and largest source sizes shrunk this way; others are left to
// the decoder.
static constexpr int MIN_SOURCE = 64;
static constexpr int MAX_SOURCE = 128;

inline bool fits(int w, int h) { return w == h && w >= MIN_SOURCE && w <= MAX_SOURCE && w % OUT == 0; }

// Width and height from a PNG's header, or false when the bytes are not a PNG.
inline bool png_size(const uint8_t *d, size_t len, int &w, int &h) {
  static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (d == nullptr || len < 24)
    return false;
  for (int i = 0; i < 8; i++) {
    if (d[i] != SIG[i])
      return false;
  }
  if (d[12] != 'I' || d[13] != 'H' || d[14] != 'D' || d[15] != 'R')
    return false;
  auto be32 = [&](int at) {
    return (uint32_t) d[at] << 24 | (uint32_t) d[at + 1] << 16 | (uint32_t) d[at + 2] << 8 | (uint32_t) d[at + 3];
  };
  uint32_t pw = be32(16), ph = be32(20);
  if (pw == 0 || ph == 0 || pw > 32767 || ph > 32767)
    return false;
  w = (int) pw;
  h = (int) ph;
  return true;
}

// 32-bit words of scratch rare_vote needs for an n px source: a copy of the
// solid pixels to sort, later the coverage counts.
inline size_t scratch_words(int n) {
  size_t copy = (size_t) n * n;
  size_t counts = ((size_t) OUT * OUT * MAX_COLORS + OUT * OUT) / 2;
  return std::max(copy, counts);
}

inline uint32_t argb(int a, int r, int g, int b) {
  return (uint32_t) a << 24 | (uint32_t) r << 16 | (uint32_t) g << 8 | (uint32_t) b;
}
inline int ch_r(uint32_t p) { return (p >> 16) & 0xFF; }
inline int ch_g(uint32_t p) { return (p >> 8) & 0xFF; }
inline int ch_b(uint32_t p) { return p & 0xFF; }
inline float lum(uint32_t p) { return 0.3f * ch_r(p) + 0.59f * ch_g(p) + 0.11f * ch_b(p); }
// Weighted squared distance, green counting most.
inline int dist(uint32_t a, uint32_t b) {
  int dr = ch_r(a) - ch_r(b), dg = ch_g(a) - ch_g(b), db = ch_b(a) - ch_b(b);
  return 2 * dr * dr + 4 * dg * dg + 3 * db * db;
}
// Closer than this to a kept color, a color adds nothing to the palette.
static constexpr int SAME_COLOR = 1500;

// Shrinks in three steps, so a caller on the main loop can spread the work
// over three passes: sorting the solid pixels, then the palette and each
// block's coverage, then the vote. Each step is a few to 20 ms for a 128 px
// logo on the ESP32-S3.
//
// src is n x n pixels of 4 bytes, blue, green, red, alpha (ESPHome's RGB image
// with an alpha channel), n a multiple of 32. scratch holds scratch_words(n).
// out gets 32 x 32 pixels as 0xAARRGGBB: opaque, or clear where black won
// over a block that is mostly see-through. All three stay the caller's and
// must outlive the steps.
class RareVote {
 public:
  RareVote(const uint8_t *src, int n, uint32_t *scratch, uint32_t *out)
      : src_(src), n_(n), s_(n / OUT), scratch_(scratch), out_(out) {}

  // Runs the next step. True once out is filled.
  bool step() {
    switch (this->next_++) {
      case 0:
        this->sort_solid_();
        return false;
      case 1:
        this->palette_();
        this->coverage_();
        return false;
      default:
        this->vote_();
        return true;
    }
  }

 protected:
  uint32_t pixel_(size_t i) const {
    const uint8_t *p = this->src_ + i * 4;
    return argb(p[3], p[2], p[1], p[0]);
  }
  static uint32_t over_black(uint32_t p) {
    int a = p >> 24;
    return argb(255, ch_r(p) * a / 255, ch_g(p) * a / 255, ch_b(p) * a / 255);
  }

  // The solid pixels, sorted, at the start of scratch.
  void sort_solid_() {
    const size_t total = (size_t) this->n_ * this->n_;
    size_t solid = 0;
    for (size_t i = 0; i < total; i++) {
      uint32_t p = this->pixel_(i);
      if ((p >> 24) >= 250)
        this->scratch_[solid++] = p | 0xFF000000u;
    }
    std::sort(this->scratch_, this->scratch_ + solid);
    this->solid_ = solid;
  }

  // 1. Black, then the solid colors by how much they cover.
  void palette_() {
    const uint32_t *sorted = this->scratch_;
    const size_t solid = this->solid_;
    this->pal_[0] = argb(255, 0, 0, 0);
    this->k_ = 1;
    // Only a color covering 0.8% of the solid pixels can join, so only those
    // runs are kept, most used first.
    struct Run {
      uint32_t count, color;
    };
    std::array<Run, MAX_CANDIDATES> runs{};
    int nruns = 0;
    for (size_t i = 0; i < solid;) {
      size_t j = i;
      while (j < solid && sorted[j] == sorted[i])
        j++;
      if ((j - i) / (float) solid >= 0.008f && nruns < MAX_CANDIDATES)
        runs[nruns++] = {(uint32_t) (j - i), sorted[i]};
      i = j;
    }
    std::sort(runs.begin(), runs.begin() + nruns, [](const Run &a, const Run &b) {
      return a.count != b.count ? a.count > b.count : a.color > b.color;
    });
    for (int r = 0; r < nruns && this->k_ < MAX_COLORS; r++) {
      bool near = false;
      for (int i = 0; i < this->k_; i++)
        near |= dist(runs[r].color, this->pal_[i]) <= SAME_COLOR;
      if (!near)
        this->pal_[this->k_++] = runs[r].color;
    }
  }

  // 2. How many source pixels of each LED's block snap to each color, and
  // how many of them are see-through. Written over the sorted pixels.
  void coverage_() {
    const int n = this->n_, s = this->s_, k = this->k_;
    auto *cov = reinterpret_cast<uint16_t *>(this->scratch_);
    uint16_t *clear = cov + (size_t) OUT * OUT * k;
    std::fill(cov, clear + OUT * OUT, (uint16_t) 0);
    uint32_t last = 0;
    int last_i = -1;
    for (int y = 0; y < n; y++) {
      for (int x = 0; x < n; x++) {
        uint32_t raw = this->pixel_((size_t) y * n + x);
        uint32_t c = over_black(raw);
        if (last_i < 0 || c != last) {
          int bi = 0, bd = dist(c, this->pal_[0]);
          for (int i = 1; i < k; i++) {
            int d = dist(c, this->pal_[i]);
            if (d < bd) {
              bd = d;
              bi = i;
            }
          }
          last = c;
          last_i = bi;
        }
        int cell = (y / s) * OUT + x / s;
        cov[(size_t) cell * k + last_i]++;
        clear[cell] += (raw >> 24) < 128;
      }
    }
  }

  // 3. Each LED takes the color covering most of its block, less
  // RARE_WEIGHT times that color's share of the 3x3 LEDs around it.
  void vote_() {
    const int s = this->s_, k = this->k_;
    const auto *cov = reinterpret_cast<const uint16_t *>(this->scratch_);
    const uint16_t *clear = cov + (size_t) OUT * OUT * k;
    const float area = (float) (s * s);
    for (int y = 0; y < OUT; y++) {
      for (int x = 0; x < OUT; x++) {
        std::array<float, MAX_COLORS> nb{};
        int cells = 0;
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= OUT || yy >= OUT)
              continue;
            cells++;
            for (int i = 0; i < k; i++)
              nb[i] += cov[(size_t) (yy * OUT + xx) * k + i] / area;
          }
        }
        const uint16_t *h = &cov[(size_t) (y * OUT + x) * k];
        int best = -1;
        float bscore = 0, blum = 0;
        for (int i = 0; i < k; i++) {
          float sc = h[i] > 0 ? h[i] / area - RARE_WEIGHT * nb[i] / cells : -9.0f;
          float li = lum(this->pal_[i]);
          if (best < 0 || sc > bscore || (sc == bscore && li > blum)) {
            best = i;
            bscore = sc;
            blum = li;
          }
        }
        uint32_t c = this->pal_[best];
        if (best == 0 && clear[y * OUT + x] * 2 > s * s)
          c &= 0x00FFFFFFu;
        this->out_[y * OUT + x] = c;
      }
    }
  }

  const uint8_t *src_;
  int n_, s_;
  uint32_t *scratch_;
  uint32_t *out_;
  size_t solid_{0};
  std::array<uint32_t, MAX_COLORS> pal_{};
  int k_{0};
  uint8_t next_{0};
};

// All three steps at once.
inline void rare_vote(const uint8_t *src, int n, uint32_t *scratch, uint32_t *out) {
  RareVote v(src, n, scratch, out);
  while (!v.step()) {
  }
}

}  // namespace espn::shrink
