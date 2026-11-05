// The brand pack's logo and sprite (brand.h) on the panel: PNG files decoded
// once, on the fetch task, into PSRAM. The logo is RGB565 pixels in the
// pack's colors, which the opening card shows (brand_lvgl.h); the sprite a
// mask the plate wall draws in the rim color (plate_wall_lvgl.cpp). Only the
// bar build reads packs.
#pragma once

#include <cstdint>
#include <string>

#include "brand.h"
#include "psram.h"

namespace esphome::gameday {

struct BrandLogo {
  ::espn::PsVector<uint16_t> pixels;
  uint16_t w{0}, h{0};
  // What it was made from, so a read of the same pack doesn't fetch it again.
  std::string url;
  bool tint{false};
  uint32_t fg{0}, bg{0};

  bool empty() const { return this->w == 0 || this->h == 0; }
  bool same_source(const std::string &u, bool t, uint32_t f, uint32_t b) const {
    return !this->empty() && this->url == u && this->tint == t && (!t || this->fg == f) && this->bg == b;
  }
  void clear() {
    this->pixels.clear();
    this->pixels.shrink_to_fit();
    this->w = this->h = 0;
    this->url.clear();
  }
};

struct BrandSprite {
  ::espn::PsVector<uint8_t> mask;  // w x h, 1 where it is drawn
  uint16_t w{0}, h{0};
  std::string url;  // what it was made from, so the same one isn't read again

  bool empty() const { return this->w == 0 || this->h == 0; }
  bool lit(int x, int y) const { return this->mask[(size_t) y * this->w + x] != 0; }
  void clear() {
    this->mask.clear();
    this->mask.shrink_to_fit();
    this->w = this->h = 0;
    this->url.clear();
  }
};

// A PNG file to out, tinted or in color (::espn::brand_logo_pixels). "" when
// it decoded, else what to tell the owner. Any task.
std::string brand_decode_logo(const uint8_t *data, size_t size, bool tint, uint32_t fg, uint32_t bg, BrandLogo &out);
// The sprite, up to 64 x 32 (::espn::brand_sprite_mask), the same way.
std::string brand_decode_sprite(const uint8_t *data, size_t size, BrandSprite &out);

}  // namespace esphome::gameday
