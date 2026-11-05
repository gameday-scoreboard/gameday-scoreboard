// The brand pack's logo (brand.h) on the panel: a PNG decoded once, on the
// fetch task, into RGB565 pixels in PSRAM in the pack's colors, which the
// opening card shows (brand_lvgl.h). Only the bar build reads packs.
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

// A PNG file to out, tinted or in color (::espn::brand_logo_pixels). "" when
// it decoded, else what to tell the owner. Any task.
std::string brand_decode_logo(const uint8_t *data, size_t size, bool tint, uint32_t fg, uint32_t bg, BrandLogo &out);

}  // namespace esphome::gameday
