// Words drawn over a big play's art while it plays: the scorer's name on a
// brace, the minute of a late goal. The frames are drawn ahead of time, so
// anything that changes from game to game is stamped on top here, in the
// panel's own Spleen 5x8 scaled up, white with an outline in the team's
// color. Stands alone (ESPHome includes every header on its own).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

#include "celeb_caption_font.h"

namespace espn {

struct CelebCaption {
  int16_t y;            // top row of the words
  uint16_t from_frame;  // the art's frame they come in on
  uint8_t scale;        // 1, 2 or 3: Spleen 5x8 at that size
  bool blink;           // on and off at about 3 a second
};

// The width of words at this scale: Spleen 5x8 leaves its fifth column
// blank, so the last letter's is trimmed.
inline int celeb_caption_width(const std::string &words, int scale) {
  return words.empty() ? 0 : ((int) words.size() * 5 - 1) * scale;
}

struct CelebCaptionFit {
  std::string words;
  int scale;
};

// The words that fit fw pixels and their size: the whole name at the
// caption's scale, else its last word, then the same a size smaller, and
// only past the smallest size as many letters as fit.
inline CelebCaptionFit celeb_caption_fit(const std::string &words, int scale, int fw) {
  const int room = fw - 2;  // a pixel of outline each side
  std::string last = words;
  size_t sp = last.rfind(' ');
  if (sp != std::string::npos)
    last = last.substr(sp + 1);
  for (int s = scale; s >= 1; s--) {
    if (celeb_caption_width(words, s) <= room)
      return {words, s};
    if (celeb_caption_width(last, s) <= room)
      return {last, s};
  }
  const int fit = (room + 1) / 5;
  return {last.substr(0, fit > 0 ? (size_t) fit : 0), 1};
}

namespace detail {
inline uint16_t caption565(uint32_t rgb, int k) {
  uint32_t r = ((rgb >> 16) & 0xFF) * k / 256, g = ((rgb >> 8) & 0xFF) * k / 256, b = (rgb & 0xFF) * k / 256;
  return (uint16_t) (((r << 8) & 0xF800) | ((g << 3) & 0x07E0) | (b >> 3));
}
inline bool caption_on(const std::string &w, int scale, int x, int y) {
  if (x < 0 || y < 0 || y >= 8 * scale)
    return false;
  const int cell = 5 * scale, n = x / cell;
  if (n >= (int) w.size())
    return false;
  const unsigned char ch = (unsigned char) w[n];
  if (ch < 32 || ch > 126)
    return false;
  const int gx = (x % cell) / scale, gy = y / scale;
  return (kCaptionFont5x8[ch - 32][gy] & (0x80 >> gx)) != 0;
}
}  // namespace detail

// Stamps words on the fw x fh RGB565 frame (stride pixels a row), centered,
// once the art reaches c.from_frame. t is seconds into the show, for the
// blink; fade the show's fade, 0 to 1.
inline void celeb_caption_draw(uint16_t *px, int fw, int fh, int stride, const CelebCaption &c,
                               const std::string &words, int frame, float t, uint32_t team, uint32_t alt, float fade) {
  (void) alt;
  if (words.empty() || frame < (int) c.from_frame || px == nullptr)
    return;
  if (c.blink && ((int) (t * 6)) % 2 == 1)
    return;
  const int want = c.scale < 1 ? 1 : c.scale > 3 ? 3 : c.scale;
  const CelebCaptionFit fit = celeb_caption_fit(words, want, fw);
  const std::string &w = fit.words;
  const int s = fit.scale;
  const int tw = celeb_caption_width(w, s), th = 8 * s;
  // A smaller size sits in the middle of the rows the caption has.
  const int x0 = (fw - tw) / 2, y0 = c.y + (want - s) * 4;
  int k = (int) (fade * 256);
  k = k < 0 ? 0 : k > 256 ? 256 : k;
  const uint16_t fill = detail::caption565(0xFFFFFF, k), edge = detail::caption565(team, k);
  // The outline first: every pixel next to a lit one, the team's color.
  for (int y = -1; y <= th; y++) {
    const int fy = y0 + y;
    if (fy < 0 || fy >= fh)
      continue;
    for (int x = -1; x <= tw; x++) {
      const int fx = x0 + x;
      if (fx < 0 || fx >= fw)
        continue;
      bool lit = detail::caption_on(w, s, x, y);
      if (lit) {
        px[fy * stride + fx] = fill;
        continue;
      }
      bool near = false;
      for (int oy = -1; oy <= 1 && !near; oy++)
        for (int ox = -1; ox <= 1 && !near; ox++)
          near = detail::caption_on(w, s, x + ox, y + oy);
      if (near)
        px[fy * stride + fx] = edge;
    }
  }
}

}  // namespace espn
