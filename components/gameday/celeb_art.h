// Frame-by-frame pixel art for the big plays (a dunk, an alley-oop). Each
// play's frames are drawn as PNGs at 128x64 and at 64x64 by a
// tools/celeb_art/<sport>_frames.py script and packed by
// tools/celeb_art/pack.py into celeb_art_<play>_<width>.h: 4-bit palette
// indexes, run-length coded, each frame after the first only the pixels
// that changed. The palette has slots for the team's colors, so one set of
// frames plays in any team's colors. celeb_art_index.h picks the set for
// the board's width. A play that opens like another (the pick six opens as
// the interception) stores only its own frames and names the other as its
// lead: the lead's first lead_frames play first, then its own, which start
// whole. The arrays are const, so they stay in flash; the only
// RAM is the 8 KB index buffer below, inside the Celebration that already
// lives in PSRAM.
#pragma once
#include <cstdint>
#include <cstring>

namespace espn {

// A palette entry: an RGB color, or one of the team's colors when the top
// byte is a role.
constexpr uint32_t kArtTeam = 0x01000000;       // the team's color
constexpr uint32_t kArtTeamShade = 0x02000000;  // the team's color, darker
constexpr uint32_t kArtAlt = 0x03000000;        // the second color
constexpr uint32_t kArtAltShade = 0x04000000;   // the second color, darker
constexpr uint32_t kArtTeamLight = 0x05000000;  // the team's color toward white
constexpr uint8_t kArtKeep = 15;                // index 15: this pixel is as it was

struct CelebArt {
  uint16_t w, h;
  uint8_t fps;
  uint16_t frames;
  const uint32_t *palette;  // 16 entries
  const uint32_t *offsets;  // frames + 1 byte offsets into data
  const uint8_t *data;
  const CelebArt &(*lead)() = nullptr;  // played first, same size, fps and palette
  uint16_t lead_frames = 0;

  int total_frames() const { return lead_frames + frames; }
};

class CelebArtPlayer {
 public:
  static constexpr int kW = 128, kH = 64;

  void reset() {
    decoded_ = -1;
    seg_ = nullptr;
  }

  // Draws the art's frame for t seconds in, centered on the board.
  // px is the fw x fh RGB565 frame, stride pixels a row.
  void render(uint16_t *px, int fw, int fh, int stride, const CelebArt &art, float t, uint32_t team, uint32_t alt,
              float fade) {
    for (int y = 0; y < fh; y++)
      memset(px + y * stride, 0, (size_t) fw * 2);
    if (art.w > kW || art.h > kH || art.frames == 0)
      return;
    int target = (int) (t * art.fps);
    if (target >= art.total_frames())
      target = art.total_frames() - 1;
    if (target < 0)
      target = 0;
    // The lead's frames first, then this art's own.
    const CelebArt *seg = &art;
    if (art.lead != nullptr && target < art.lead_frames) {
      seg = &art.lead();
      if (seg->w != art.w || seg->h != art.h || seg->frames < art.lead_frames)
        return;
    } else {
      target -= art.lead_frames;
    }
    if (seg != seg_ || target < decoded_) {
      seg_ = seg;
      decoded_ = -1;
    }
    while (decoded_ < target)
      decode_(*seg, ++decoded_);
    uint16_t pal[16];
    const int k = (int) (fade * 256);
    for (int i = 0; i < 16; i++)
      pal[i] = to565_(scale_(resolve_(seg->palette[i], team, alt), k));
    const int x0 = (fw - art.w) / 2, y0 = (fh - art.h) / 2;
    for (int y = 0; y < art.h; y++) {
      const int fy = y + y0;
      if (fy < 0 || fy >= fh)
        continue;
      uint16_t *row = px + fy * stride;
      const uint8_t *src = buf_ + y * art.w;
      for (int x = 0; x < art.w; x++) {
        const int fx = x + x0;
        if (fx >= 0 && fx < fw)
          row[fx] = pal[src[x]];
      }
    }
  }

  // The frame last decoded, counted in its own set (the lead's or the art's).
  int frame() const { return decoded_; }

 private:
  // The same arithmetic as celebrate_fx.h's rgb565, rgb_scale and rgb_mix;
  // ESPHome includes every header on its own, so this one stands alone.
  static uint16_t to565_(uint32_t rgb) {
    return (uint16_t) (((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
  }
  static uint32_t scale_(uint32_t rgb, int k) {
    if (k >= 256)
      return rgb;
    if (k <= 0)
      return 0;
    return ((((rgb >> 16) & 0xFF) * k / 256) << 16) | ((((rgb >> 8) & 0xFF) * k / 256) << 8) |
           ((rgb & 0xFF) * k / 256);
  }
  static uint32_t mix_white_(uint32_t a, int k) {
    uint32_t r = (((a >> 16) & 0xFF) * (256 - k) + 255 * k) / 256;
    uint32_t g = (((a >> 8) & 0xFF) * (256 - k) + 255 * k) / 256;
    uint32_t b = ((a & 0xFF) * (256 - k) + 255 * k) / 256;
    return (r << 16) | (g << 8) | b;
  }
  static uint32_t resolve_(uint32_t e, uint32_t team, uint32_t alt) {
    switch (e & 0xFF000000) {
      case kArtTeam:
        return team;
      case kArtTeamShade:
        return scale_(team, 140);
      case kArtAlt:
        return alt;
      case kArtAltShade:
        return scale_(alt, 150);
      case kArtTeamLight:
        return mix_white_(team, 110);
      default:
        return e & 0xFFFFFF;
    }
  }

  // One byte a run: the index in the high nibble, the length - 1 in the low
  // one (1 to 15), or 15 there and the next byte adds 16 (16 to 271).
  void decode_(const CelebArt &art, int n) {
    if (n == 0)
      memset(buf_, 0, sizeof(buf_));
    const uint8_t *p = art.data + art.offsets[n], *end = art.data + art.offsets[n + 1];
    int i = 0;
    const int total = art.w * art.h;
    while (p < end && i < total) {
      uint8_t b = *p++;
      int idx = b >> 4, len = (b & 0x0F) + 1;
      if (len == 16 && p < end)
        len = 16 + *p++;
      if (i + len > total)
        len = total - i;
      if (idx != kArtKeep)
        memset(buf_ + i, idx, (size_t) len);
      i += len;
    }
  }

  uint8_t buf_[kW * kH]{};
  int decoded_{-1};
  const CelebArt *seg_{nullptr};  // the set decoded_ counts in
};

}  // namespace espn
