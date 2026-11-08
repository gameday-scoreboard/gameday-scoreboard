// The opening card on the bar build (brand.h): the pack's logo, title and
// wall name in its colors, drawn into the tt_brand box of
// firmware/pages/gameday-live.yaml when the message card puts it up.
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL
#include <cstring>

#include <lvgl.h>

#include "brand.h"
#include "brand_card.h"

namespace esphome::gameday {

// font12 and font8 are the panel's Spleen fonts. The image points at logo's
// pixels, which stay put until the next pack read replaces them; that read
// redraws the card.
inline void brand_card_draw(lv_obj_t *box, lv_obj_t *img, lv_obj_t *title, lv_obj_t *wall,
                            const ::espn::BrandPack &pack, const BrandLogo &logo, const lv_font_t *font12,
                            const lv_font_t *font8) {
  static lv_image_dsc_t dsc;
  const int32_t W = lv_display_get_horizontal_resolution(lv_display_get_default());
  const int32_t H = 64;
  lv_obj_set_size(box, W, H);
  lv_obj_set_style_bg_color(box, lv_color_hex(pack.background), 0);
  const ::espn::BrandLayout L =
      ::espn::brand_layout(W, H, logo.empty() ? 0 : logo.w, logo.empty() ? 0 : logo.h, pack.title, pack.wall);
  if (logo.empty()) {
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
  } else {
    memset(&dsc, 0, sizeof(dsc));
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc.header.w = logo.w;
    dsc.header.h = logo.h;
    dsc.header.stride = logo.w * 2;
    dsc.data = reinterpret_cast<const uint8_t *>(logo.pixels.data());
    dsc.data_size = (uint32_t) logo.w * logo.h * 2;
    lv_image_set_src(img, &dsc);
    lv_obj_set_size(img, logo.w, logo.h);
    // Scaled about its top left corner, so x and y place the scaled logo.
    lv_image_set_pivot(img, 0, 0);
    lv_image_set_scale(img, L.scale);
    lv_obj_set_pos(img, L.logo_x, L.logo_y);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
  }
  auto line = [W](lv_obj_t *lbl, const std::string &text, int32_t y, const lv_font_t *font, uint32_t color) {
    if (y < 0 || text.empty()) {
      lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_width(lbl, W);
    lv_obj_set_pos(lbl, 0, y);
    lv_label_set_text(lbl, text.c_str());
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_HIDDEN);
  };
  // The title in the rim color, the wall's name in the number color, as on
  // the plates.
  line(title, pack.title, L.title_y, L.title_big ? font12 : font8, pack.rim);
  line(wall, pack.wall, L.wall_y, font8, pack.number);
  lv_obj_move_foreground(box);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace esphome::gameday

#endif  // USE_LVGL
