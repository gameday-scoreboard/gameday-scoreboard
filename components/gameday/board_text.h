// Owner text on a full-screen box: the banner (firmware/pages/message.yaml)
// and the message card (firmware/pages/gameday-live.yaml). The text takes
// the biggest font it fits in, centered; past the smallest it rolls up
// slowly, holding at the top and the bottom.
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL
#include <cstdint>

#include <lvgl.h>

namespace esphome::gameday {

static constexpr int32_t kBoardRollPxPerS = 12;  // the boot screen's scroll speed
static constexpr uint32_t kBoardRollHoldMs = 2000;

inline void board_roll_y_(void *label, int32_t v) { lv_obj_set_y((lv_obj_t *) label, v); }

// fonts: biggest first. Returns how long one pass of a rolling text takes in
// ms, 0 when it stands still.
inline uint32_t board_text(lv_obj_t *box, lv_obj_t *label, const char *text, const lv_font_t *const *fonts,
                           size_t n_fonts) {
  const int32_t W = lv_display_get_horizontal_resolution(lv_display_get_default());
  const int32_t H = 64;
  lv_obj_set_size(box, W, H);
  lv_anim_delete(label, board_roll_y_);
  const lv_font_t *font = fonts[n_fonts - 1];
  lv_point_t sz{};
  for (size_t i = 0; i < n_fonts; i++) {
    lv_text_get_size(&sz, text, fonts[i], 0, 0, W - 2, LV_TEXT_FLAG_NONE);
    font = fonts[i];
    if (sz.y <= H)
      break;
  }
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_width(label, W - 2);
  lv_label_set_text(label, text);
  lv_obj_set_x(label, 1);
  if (sz.y <= H) {
    lv_obj_set_y(label, (H - sz.y) / 2);
    return 0;
  }
  // Top lines first, then up to the last line and back.
  const int32_t travel = sz.y - H;
  const uint32_t ms = (uint32_t) travel * 1000 / kBoardRollPxPerS;
  lv_obj_set_y(label, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, label);
  lv_anim_set_exec_cb(&a, board_roll_y_);
  lv_anim_set_values(&a, 0, -travel);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_delay(&a, kBoardRollHoldMs);
  lv_anim_set_repeat_delay(&a, kBoardRollHoldMs);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
  return kBoardRollHoldMs + ms + kBoardRollHoldMs;
}

}  // namespace esphome::gameday
#endif
