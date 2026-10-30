// The weather card and the next game's forecast badge, drawn whole onto an
// LVGL canvas: the icons pixel by pixel (wx_icons.h), the text with LVGL's
// label drawing on top. firmware/pages/gameday-live.yaml redraws them about
// ten times a second while they are up, which is what moves the icons.
#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL
#include <cstdint>
#include <string>

#include <lvgl.h>

#include "weather.h"
#include "wx_icons.h"

namespace esphome::gameday {

struct WxFonts {
  const lv_font_t *f32, *f16, *f12, *f8;
};

// Pixels straight into the canvas's buffer, clipped to w x h.
struct WxPx {
  lv_draw_buf_t *buf;
  int w, h;
  void operator()(int x, int y, uint32_t c) const {
    if (x < 0 || y < 0 || x >= w || y >= h)
      return;
    uint8_t *row = buf->data + (size_t) y * buf->header.stride;
#if LV_COLOR_DEPTH == 16
    reinterpret_cast<uint16_t *>(row)[x] =
        (uint16_t) (((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
#elif LV_COLOR_DEPTH == 32
    reinterpret_cast<uint32_t *>(row)[x] = 0xFF000000u | c;
#else
    uint8_t *p = row + x * 3;
    p[0] = c & 0xFF;
    p[1] = (c >> 8) & 0xFF;
    p[2] = (c >> 16) & 0xFF;
#endif
  }
};

inline void wx_text_(lv_layer_t *layer, const lv_font_t *font, const std::string &text, int x, int y, int w,
                     uint32_t color, lv_text_align_t align) {
  if (text.empty())
    return;
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.font = font;
  d.color = lv_color_hex(color);
  d.align = align;
  d.text = text.c_str();
  d.text_local = 1;  // copied: the string is gone by the time LVGL draws
  lv_area_t a{x, y, x + w - 1, y + lv_font_get_line_height(font) - 1};
  lv_draw_label(layer, &d, &a);
}

inline int wx_text_w_(const lv_font_t *font, const std::string &text) {
  lv_point_t sz{};
  lv_text_get_size(&sz, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return sz.x;
}

// The card on a canvas W wide (the display's width) and 64 high. Page 1:
// the icon, the temperature, the words, today's high and low, and the place.
// Page 2: a column a day.
inline void wx_card_draw(lv_obj_t *canvas, const ::espn::WxCard &card, uint32_t frame, int W, const WxFonts &f) {
  lv_draw_buf_t *buf = lv_canvas_get_draw_buf(canvas);
  if (buf == nullptr || buf->data == nullptr || card.page == 0)
    return;
  const int cw = (int) buf->header.w, ch = (int) buf->header.h;
  if (W > cw)
    W = cw;
  lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_COVER);
  WxPx px{buf, W, ch};
  const bool wide = W >= 128;
  const uint32_t kDim = 0x8A8A8A, kHi = 0xFFB060, kLo = 0x70A8FF;
  if (card.page == 1) {
    const int is = wide ? 32 : 24;
    ::espn::wx_draw_icon(px, card.icon, wide ? 6 : 2, wide ? 3 : 2, is, frame);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    if (wide) {
      // The temperature in the big digits beside the icon, the words under
      // them, and a bottom row with today's high and low and the place.
      wx_text_(&layer, f.f32, card.temp, 44, 1, W - 44, 0xFFFFFF, LV_TEXT_ALIGN_LEFT);
      const lv_font_t *wf = wx_text_w_(f.f12, card.words) <= W ? f.f12 : f.f8;
      wx_text_(&layer, wf, card.words, 0, 38, W, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);
      const int hw = wx_text_w_(f.f8, card.hilo);
      wx_text_(&layer, f.f8, card.hilo, 2, 55, hw + 1, 0xC0C0C0, LV_TEXT_ALIGN_LEFT);
      wx_text_(&layer, f.f8, card.place, hw + 8, 55, W - hw - 10, kDim, LV_TEXT_ALIGN_RIGHT);
    } else {
      wx_text_(&layer, f.f16, card.temp, 28, 6, W - 28, 0xFFFFFF, LV_TEXT_ALIGN_LEFT);
      // Up to two lines of words, then the high and low, then the place.
      lv_draw_label_dsc_t d;
      lv_draw_label_dsc_init(&d);
      d.font = f.f8;
      d.color = lv_color_hex(0xFFFFFF);
      d.align = LV_TEXT_ALIGN_CENTER;
      d.text = card.words.c_str();
      d.text_local = 1;
      lv_area_t a{0, 28, W - 1, 45};
      lv_draw_label(&layer, &d, &a);
      wx_text_(&layer, f.f8, card.hilo, 0, 46, W, 0xC0C0C0, LV_TEXT_ALIGN_CENTER);
      wx_text_(&layer, f.f8, card.place, 0, 55, W, kDim, LV_TEXT_ALIGN_CENTER);
    }
    lv_canvas_finish_layer(canvas, &layer);
  } else {
    const int n = wide ? (card.ndays < ::espn::kWxDays ? card.ndays : ::espn::kWxDays) : (card.ndays < 3 ? card.ndays : 3);
    if (n == 0)
      return;
    const int colw = W / n;
    const int is = colw >= 40 ? 24 : 16;
    for (int i = 0; i < n; i++)
      ::espn::wx_draw_icon(px, card.days[i].icon, i * colw + (colw - is) / 2, 10, is, frame + (uint32_t) i * 11);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    const int y_hi = 12 + is, y_lo = y_hi + 10;
    for (int i = 0; i < n; i++) {
      // "Today" where it fits, the weekday elsewhere.
      std::string name = i == 0 && colw >= 26 ? std::string("Today") : card.days[i].name;
      wx_text_(&layer, f.f8, name, i * colw, 1, colw, i == 0 ? 0xFFFFFF : 0xC0C0C0, LV_TEXT_ALIGN_CENTER);
      wx_text_(&layer, f.f8, card.days[i].hi, i * colw, y_hi, colw, kHi, LV_TEXT_ALIGN_CENTER);
      wx_text_(&layer, f.f8, card.days[i].lo, i * colw, y_lo, colw, kLo, LV_TEXT_ALIGN_CENTER);
    }
    if (y_lo + 9 <= 55)
      wx_text_(&layer, f.f8, card.place, 0, 55, W, kDim, LV_TEXT_ALIGN_CENTER);
    lv_canvas_finish_layer(canvas, &layer);
  }
  lv_obj_invalidate(canvas);
}

// The next game's forecast: a 12 px icon and "54°F", centred on a canvas
// 64 wide and 13 high.
inline void wx_badge_draw(lv_obj_t *canvas, const ::espn::WxBadge &badge, uint32_t frame, const WxFonts &f) {
  lv_draw_buf_t *buf = lv_canvas_get_draw_buf(canvas);
  if (buf == nullptr || buf->data == nullptr)
    return;
  const int cw = (int) buf->header.w, ch = (int) buf->header.h;
  lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_COVER);
  if (badge.text.empty()) {
    lv_obj_invalidate(canvas);
    return;
  }
  const int tw = wx_text_w_(f.f12, badge.text);
  const int x0 = (cw - (12 + 3 + tw)) / 2;
  WxPx px{buf, cw, ch};
  ::espn::wx_draw_icon(px, badge.icon, x0, 0, 12, frame);
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  wx_text_(&layer, f.f12, badge.text, x0 + 15, 0, tw + 1, 0xE0E0E0, LV_TEXT_ALIGN_LEFT);
  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_invalidate(canvas);
}

}  // namespace esphome::gameday
#endif
