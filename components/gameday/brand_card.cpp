#include "brand_card.h"

#include <cstring>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "picture.h"
#include "pngle.h"

namespace esphome::gameday {

static const char *const TAG = "gameday.brand";

namespace {

struct RgbaTarget {
  uint8_t *rgba;
  uint32_t w, h;
  uint32_t drawn;
};

void rgba_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  auto *t = static_cast<RgbaTarget *>(pngle_get_user_data(pngle));
  for (uint32_t yy = y; yy < y + h && yy < t->h; yy++)
    for (uint32_t xx = x; xx < x + w && xx < t->w; xx++) {
      memcpy(t->rgba + (yy * t->w + xx) * 4, rgba, 4);
      t->drawn++;
    }
}

}  // namespace

std::string brand_decode_logo(const uint8_t *data, size_t size, bool tint, uint32_t fg, uint32_t bg, BrandLogo &out) {
  if (size == 0)
    return "The logo file is empty.";
  if (size > ::espn::kBrandLogoMaxBytes)
    return "The logo file is too big; it can be up to 32 KB.";
  if (::espn::pic_sniff(data, size) != ::espn::PicKind::PNG)
    return "The logo has to be a PNG file.";
  // pic_check reads the size from the PNG's header.
  const ::espn::PicCheck c = ::espn::pic_check(data, size);
  if (!c.error.empty())
    return c.error == ::espn::pic_too_big_text() ? "The logo can be up to 128 by 64 pixels." : "The logo file is damaged.";
  const uint32_t w = c.info.w, h = c.info.h;
  ::espn::PsVector<uint8_t> rgba((size_t) w * h * 4, 0);
  RAMAllocator<pngle_t> allocator;
  pngle_t *pngle = allocator.allocate(1, PNGLE_T_SIZE);
  if (pngle == nullptr)
    return "No room to read the logo.";
  memset(pngle, 0, PNGLE_T_SIZE);
  pngle_reset(pngle);
  RgbaTarget target{rgba.data(), w, h, 0};
  pngle_set_user_data(pngle, &target);
  pngle_set_draw_callback(pngle, rgba_draw);
  const int fed = pngle_feed(pngle, const_cast<uint8_t *>(data), size);
  if (fed < 0)
    ESP_LOGW(TAG, "Logo PNG: %s", pngle_error(pngle));
  const bool ok = fed >= 0 && target.drawn >= w * h && pngle_get_width(pngle) == w && pngle_get_height(pngle) == h;
  pngle_reset(pngle);
  allocator.deallocate(pngle, PNGLE_T_SIZE);
  if (!ok)
    return "The logo file is damaged.";
  out.pixels.assign((size_t) w * h, 0);
  ::espn::brand_logo_pixels(rgba.data(), w, h, tint, fg, bg, out.pixels.data());
  out.w = (uint16_t) w;
  out.h = (uint16_t) h;
  out.tint = tint;
  out.fg = fg;
  out.bg = bg;
  return "";
}

}  // namespace esphome::gameday
