// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#include "plate_wall_lvgl.h"

#if defined(USE_LVGL) && defined(GAMEDAY_BAR)
#include <cmath>
#include <vector>

#include <esp_heap_caps.h>

#include "plate_wall.h"

namespace esphome::gameday {

namespace {

using ::espn::PlateLine;

enum class Kind : uint8_t { NONE, PLATE, PARTY, EVENT };

struct Text {
  int x, y, px;
  uint32_t color;
  std::string s;
};

// One canvas, made the first time a plate page goes up and kept: the
// pixels are written straight into its buffer, the words through a layer.
struct Wall {
  lv_obj_t *canvas{nullptr};
  uint16_t *buf{nullptr};
  int W{0}, H{64};
  lv_timer_t *timer{nullptr};
  PlateWallFonts fonts;
  const ::espn::BrandPack *pack{nullptr};
  const BrandSprite *sprite{nullptr};
  Kind kind{Kind::NONE};
  PlateLine e;
  std::vector<std::string> quote;
  std::string day, time;  // the event page's
  uint32_t start{0};
  int64_t phase{-1};    // what the last frame showed, so a still page draws once
  uint32_t members{0};  // plate pages shown, so each member trades sides with the last
  bool plate_left{true};
  std::vector<Text> texts;
};

Wall w_;

constexpr uint32_t kWhite = 0xFFFFFF;

bool two() { return w_.W >= 128; }

uint32_t mix(uint32_t a, uint32_t b, float k) {
  auto ch = [&](int s) {
    const float v = ((a >> s) & 0xFF) * k + ((b >> s) & 0xFF) * (1 - k);
    return (uint32_t) (v < 0 ? 0 : v > 255 ? 255 : v) << s;
  };
  return ch(16) | ch(8) | ch(0);
}

uint32_t bg() { return w_.pack->background; }
// The inside of a plate: the rim's color, deep down.
uint32_t plate_fill() { return mix(w_.pack->rim, bg(), 0.12f); }

void px(int x, int y, uint16_t v) {
  if (x >= 0 && x < w_.W && y >= 0 && y < w_.H)
    w_.buf[y * w_.W + x] = v;
}

const lv_font_t *font(int px) { return px >= 16 ? w_.fonts.f16 : px >= 12 ? w_.fonts.f12 : w_.fonts.f8; }

void text(int x, int y, int size, uint32_t color, const std::string &s) {
  if (!s.empty())
    w_.texts.push_back({x, y, size, color, s});
}

void text_c(float cx, int y, int size, uint32_t color, const std::string &s) {
  text(::espn::plate_left(cx, ::espn::plate_text_w(::espn::plate_letters(s), size)), y, size, color, s);
}

void flush_texts() {
  if (w_.texts.empty())
    return;
  lv_layer_t layer;
  lv_canvas_init_layer(w_.canvas, &layer);
  for (const Text &t : w_.texts) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = font(t.px);
    d.color = lv_color_hex(t.color);
    d.text = t.s.c_str();
    const int wpx = ::espn::plate_text_w(::espn::plate_letters(t.s), t.px);
    lv_area_t a = {t.x, t.y, t.x + wpx + 8, t.y + t.px};
    lv_draw_label(&layer, &d, &a);
  }
  lv_canvas_finish_layer(w_.canvas, &layer);
  w_.texts.clear();
}

// A plate: the rim (a second-round plate's in the quote color with a ring
// of the rim color inside it), the dark inside, the tag and the name.
void plate(float cx, float cy, float rx, float ry, const PlateLine &e, bool big) {
  const uint16_t rim = ::espn::brand_565(w_.pack->rim), fill = ::espn::brand_565(plate_fill());
  const uint16_t outer = e.round2 ? ::espn::brand_565(w_.pack->quote) : rim;
  const int x0 = (int) floorf(cx - rx), x1 = (int) ceilf(cx + rx);
  const int y0 = (int) floorf(cy - ry), y1 = (int) ceilf(cy + ry);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      if (!::espn::plate_inside(cx, cy, rx, ry, x, y))
        continue;
      uint16_t v = ::espn::plate_inside(cx, cy, rx - 1.2f, ry - 1.2f, x, y) ? fill : outer;
      if (e.round2 && ::espn::plate_inside(cx, cy, rx - 2.4f, ry - 2.4f, x, y) &&
          !::espn::plate_inside(cx, cy, rx - 3.6f, ry - 3.6f, x, y))
        v = rim;
      px(x, y, v);
    }
  const float inset = e.round2 ? 5.0f : 3.0f;
  const std::string tag = ::espn::plate_tag(e, w_.pack->party_round2);
  const ::espn::PlateFit f = ::espn::plate_fit(e.name, tag, cx, cy, rx - inset, ry - inset, big);
  if (!tag.empty())
    text_c(cx, f.tag_y, f.tag_px, e.round2 ? w_.pack->rim : w_.pack->number, tag);
  for (int i = 0; i < f.lines; i++)
    text_c(cx, f.name_y + i * f.px, f.px, kWhite, f.line[i]);
}

void big_oval(const PlateLine &e) {
  if (two())
    plate(w_.W / 2.0f, 32, w_.W / 2.0f - 0.5f, 31.5f, e, true);
  else
    plate(32, 32, 31.5f, 31.5f, e, false);
}

// The sprite at x, y in the rim color on a patch of the background, with
// lights along its bottom that blink.
void sprite_at(int x, int y, uint32_t t, int lights) {
  const BrandSprite &s = *w_.sprite;
  const uint16_t lit = ::espn::brand_565(w_.pack->rim), back = ::espn::brand_565(bg());
  for (int yy = 0; yy < s.h; yy++)
    for (int xx = 0; xx < s.w; xx++)
      px(x + xx, y + yy, s.lit(xx, yy) ? lit : back);
  const uint16_t lamp = ::espn::brand_565(w_.pack->number);
  for (int k = 0; k < lights; k++)
    if ((t / 120 + k) % 2 == 0)
      px(x + (k + 1) * s.w / (lights + 1), y + s.h, lamp);
}

// The quote rising on the panel centered on cx, t ms after it starts.
void rise(uint32_t t, float cx) {
  const int h = (int) w_.quote.size() * ::espn::kQuotePx;
  const int y = ::espn::plate_rise_y(t, h, w_.H);
  for (size_t i = 0; i < w_.quote.size(); i++) {
    const int ly = y + (int) i * ::espn::kQuotePx;
    if (ly > -::espn::kQuotePx && ly < w_.H)
      text_c(cx, ly, ::espn::kQuotePx, w_.pack->quote, w_.quote[i]);
  }
}

// A member's page: the big oval, then (with a quote) the plate on one panel
// and the quote rising on the other. One panel: the plate, then the quote.
void showcase(uint32_t t) {
  const bool quote = !w_.quote.empty();
  if (t < ::espn::kPlateBigMs || !quote) {
    big_oval(w_.e);
    return;
  }
  if (!two()) {
    rise(t - ::espn::kPlateBigMs, 32);
    return;
  }
  const float mid = w_.W / 2.0f, to = w_.plate_left ? 32.0f : w_.W - 32.0f;
  if (t < ::espn::kPlateBigMs + ::espn::kPlateFoldMs) {
    const float k = ::espn::plate_ease((float) (t - ::espn::kPlateBigMs) / ::espn::kPlateFoldMs);
    plate(mid + (to - mid) * k, 32, (mid - 0.5f) + (31.5f - (mid - 0.5f)) * k, 31.5f, w_.e, false);
    return;
  }
  plate(to, 32, 31.5f, 31.5f, w_.e, false);
  rise(t - ::espn::kPlateBigMs - ::espn::kPlateFoldMs, w_.plate_left ? w_.W - 32.0f : 32.0f);
}

// The sprite crossing left to right: the page shows behind it, the board is
// dark ahead of it. With no sprite, a plain edge does the same.
void wipe(uint32_t t) {
  const bool has = !w_.sprite->empty();
  const int sw = has ? w_.sprite->w : 0, sh = has ? w_.sprite->h : 0;
  const int sx = -sw + (int) ((w_.W + sw) * t / ::espn::kPlateWipeMs);
  const int edge = has ? sx + sw / 2 : (int) (w_.W * t / ::espn::kPlateWipeMs);
  const uint16_t back = ::espn::brand_565(bg());
  for (int y = 0; y < w_.H; y++)
    for (int x = edge < 0 ? 0 : edge; x < w_.W; x++)
      w_.buf[y * w_.W + x] = back;
  if (has)
    sprite_at(sx, (w_.H - sh) / 2 - 1 + (int) ((t / 160) % 2), t, 3);
}

uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352d;
  x ^= x >> 15;
  x *= 0x846ca68b;
  x ^= x >> 16;
  return x;
}

// Bursts at fixed, scattered spots and times, each a ring of sparks that
// spreads, falls a little and fades into the background. Worked out from t
// alone, so any frame draws the same.
void fireworks(uint32_t t) {
  const uint32_t cols[3] = {w_.pack->rim, w_.pack->number, w_.pack->quote};
  for (int k = 0; k < 12; k++) {
    const uint32_t from = ::espn::kPartyFireFromMs + (uint32_t) k * 280;
    if (from > ::espn::kPartyFireToMs || t < from || t > from + 1300)
      continue;
    const uint32_t r = hash32((uint32_t) k * 2654435761u + 7);
    const float bx = 8 + (float) (r % (uint32_t) (w_.W - 16));
    const float by = 6 + (float) ((r >> 8) % 26);
    const float age = (t - from) / 1000.0f;
    const float fade = age < 0.5f ? 1.0f : 1.0f - (age - 0.5f) / 0.8f;
    const uint16_t v = ::espn::brand_565(mix(cols[(r >> 16) % 3], bg(), fade));
    const uint16_t dim = ::espn::brand_565(mix(cols[(r >> 16) % 3], bg(), fade * 0.25f));
    const float spin = (r % 100) / 100.0f, d = 22.0f * age, g = 6.0f * age * age;
    for (int i = 0; i < 18; i++) {
      const float a = 6.2831853f * i / 18 + spin, ca = cosf(a), sa = sinf(a);
      px((int) lroundf(bx + ca * d), (int) lroundf(by + sa * d + g), v);
      px((int) lroundf(bx + ca * d * 0.75f), (int) lroundf(by + sa * d * 0.75f + g), v);
      px((int) lroundf(bx + ca * d * 0.5f), (int) lroundf(by + sa * d * 0.5f + g * 0.5f), dim);
    }
  }
}

// A line of words in 8 px, on two lines when it is wider than the board.
int small_lines(const std::string &s, std::string out[2]) {
  if (s.empty())
    return 0;
  if (::espn::plate_text_w(::espn::plate_letters(s), 8) <= w_.W - 2) {
    out[0] = s;
    return 1;
  }
  ::espn::plate_split(s, out[0], out[1]);
  return out[1].empty() ? 1 : 2;
}

// The plate party: the sprite flies in and hovers, the party words flash
// with the wall's name under them and fireworks behind, then the new
// member's big oval.
void party(uint32_t t) {
  if (t >= ::espn::kPartyHoverMs) {
    big_oval(w_.e);
    return;
  }
  const bool has = !w_.sprite->empty();
  if (t >= ::espn::kPartyFireFromMs)
    fireworks(t);
  int top = 0;
  if (has) {
    const int sw = w_.sprite->w, sh = w_.sprite->h, home = (w_.W - sw) / 2;
    const int sx = t < ::espn::kPartyFlyMs ? -sw + (int) ((home + sw) * t / ::espn::kPartyFlyMs) : home;
    sprite_at(sx, 1 + (int) ((t / 250) % 2), t, 5);
    top = sh + 3;
  }
  if (has && t < ::espn::kPartyFlyMs)
    return;
  const std::string &word = w_.e.round2 ? w_.pack->party_round2 : w_.pack->party_new;
  const int wpx = ::espn::plate_fit_px(word, w_.W - 2, two() ? 16 : 12);
  std::string wall[2];
  const int wl = small_lines(w_.pack->wall, wall);
  const int block = wpx + (wl > 0 ? 3 + 8 * wl : 0);
  int y = top + (w_.H - top - block) / 2;
  if (y + block > w_.H)
    y = w_.H - block;
  if ((t / 350) % 3 != 2)
    text_c(w_.W / 2.0f, y, wpx, w_.pack->rim, word);
  for (int i = 0; i < wl; i++)
    text_c(w_.W / 2.0f, y + wpx + 3 + 8 * i, 8, w_.pack->number, wall[i]);
}

// The event: its name big in the rim color, the day and time, the wall's
// name small, and the sprite over them when there is room.
void event(uint32_t t) {
  const ::espn::BrandPack &p = *w_.pack;
  std::string name[2];
  int npx = ::espn::plate_fit_px(p.event_name, w_.W - 2);
  int nl = 1;
  name[0] = p.event_name;
  if (::espn::plate_text_w(::espn::plate_letters(p.event_name), npx) > w_.W - 2) {
    npx = 8;
    nl = small_lines(p.event_name, name);
  }
  // Two panels: "THURSDAY 6 PM" on one line; one panel: the day, then the time.
  std::string when[2];
  int wn = 0;
  const std::string both = w_.time.empty() ? w_.day : w_.day + " " + w_.time;
  if (two() || w_.time.empty()) {
    when[wn++] = both;
  } else {
    when[wn++] = w_.day;
    when[wn++] = w_.time;
  }
  int wpx = 12;
  for (int i = 0; i < wn; i++)
    if (::espn::plate_text_w(::espn::plate_letters(when[i]), 12) > w_.W - 2)
      wpx = 8;
  std::string wall[2];
  const int wl = small_lines(p.wall, wall);
  const int block = npx * nl + 3 + wpx * wn + (wl > 0 ? 3 + 8 * wl : 0);
  int y = (w_.H - block) / 2;
  if (!w_.sprite->empty() && w_.sprite->h + 2 + block <= w_.H) {
    const int sh = w_.sprite->h;
    y = sh + 2 + (w_.H - sh - 2 - block) / 2;
    sprite_at((w_.W - w_.sprite->w) / 2, (y - sh) / 2 - 1 + (int) ((t / 300) % 2), t, 3);
  }
  for (int i = 0; i < nl; i++, y += npx)
    text_c(w_.W / 2.0f, y, npx, p.rim, name[i]);
  y += 3;
  for (int i = 0; i < wn; i++, y += wpx)
    text_c(w_.W / 2.0f, y, wpx, p.quote, when[i]);
  y += 3;
  for (int i = 0; i < wl; i++, y += 8)
    text_c(w_.W / 2.0f, y, 8, p.number, wall[i]);
}

void draw(uint32_t t) {
  const uint16_t back = ::espn::brand_565(bg());
  for (int i = 0; i < w_.W * w_.H; i++)
    w_.buf[i] = back;
  w_.texts.clear();
  switch (w_.kind) {
    case Kind::PLATE:
      showcase(t);
      break;
    case Kind::PARTY:
      party(t);
      break;
    case Kind::EVENT:
      event(t);
      break;
    default:
      break;
  }
  flush_texts();
  if (w_.kind == Kind::PLATE && t < ::espn::kPlateWipeMs)
    wipe(t);
  lv_obj_invalidate(w_.canvas);
}

// What a frame at t shows, as a number: the timer draws only when it moves.
int64_t phase_of(uint32_t t) {
  switch (w_.kind) {
    case Kind::PLATE: {
      if (t < ::espn::kPlateWipeMs)
        return 1000000 + t / 33;
      if (w_.quote.empty() || t < ::espn::kPlateBigMs)
        return 0;
      uint32_t tr = t - ::espn::kPlateBigMs;
      if (two()) {
        if (tr < ::espn::kPlateFoldMs)
          return 2000000 + tr / 33;
        tr -= ::espn::kPlateFoldMs;
      }
      const int h = (int) w_.quote.size() * ::espn::kQuotePx;
      return ::espn::plate_rise_moving(tr, h, w_.H) ? 3000000 + ::espn::plate_rise_y(tr, h, w_.H) : 4;
    }
    case Kind::PARTY:
      return t < ::espn::kPartyHoverMs ? 5000000 + t / 33 : 6;
    case Kind::EVENT:
      return w_.sprite->empty() ? 7 : 8 + (t / 300) % 2;
    default:
      return 0;
  }
}

void tick(lv_timer_t *) {
  if (w_.canvas == nullptr || w_.kind == Kind::NONE || lv_obj_has_flag(w_.canvas, LV_OBJ_FLAG_HIDDEN) ||
      lv_obj_has_flag(lv_obj_get_parent(w_.canvas), LV_OBJ_FLAG_HIDDEN))
    return;
  const uint32_t t = lv_tick_elaps(w_.start);
  const int64_t ph = phase_of(t);
  if (ph != w_.phase) {
    w_.phase = ph;
    draw(t);
  }
}

}  // namespace

uint32_t plate_wall_show(lv_obj_t *parent, const std::string &page, const ::espn::BrandPack &pack,
                         const BrandSprite &sprite, const PlateWallFonts &fonts) {
  const int W = lv_display_get_horizontal_resolution(lv_display_get_default());
  if (w_.buf == nullptr || W != w_.W) {
    if (w_.buf != nullptr)
      heap_caps_free(w_.buf);
    w_.buf = (uint16_t *) heap_caps_calloc((size_t) W * w_.H, 2, MALLOC_CAP_SPIRAM);
    w_.W = W;
    if (w_.buf == nullptr) {
      plate_wall_hide();
      return 0;
    }
    if (w_.canvas == nullptr)
      w_.canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(w_.canvas, w_.buf, W, w_.H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(w_.canvas, 0, 0);
  }
  if (w_.timer == nullptr)
    w_.timer = lv_timer_create(tick, 25, nullptr);
  w_.pack = &pack;
  w_.sprite = &sprite;
  w_.fonts = fonts;
  const char mark = page.empty() ? 0 : page[0];
  const std::string body = page.empty() ? "" : page.substr(1);
  w_.quote.clear();
  w_.day.clear();
  w_.time.clear();
  uint32_t ms = 0;
  if (mark == ::espn::kPlatePageMark) {
    w_.kind = Kind::PLATE;
    w_.e = ::espn::plate_line(body);
    w_.quote = ::espn::plate_quote_lines(w_.e.quote);
    w_.plate_left = w_.members++ % 2 == 0;
    ms = ::espn::plate_page_ms(two(), w_.quote.size(), w_.H);
  } else if (mark == ::espn::kPartyPageMark) {
    w_.kind = Kind::PARTY;
    w_.e = ::espn::plate_line(body);
    ms = ::espn::kPartyMs;
  } else if (mark == ::espn::kEventPageMark) {
    w_.kind = Kind::EVENT;
    const size_t nl = body.find('\n');
    w_.day = body.substr(0, nl);
    w_.time = nl == std::string::npos ? "" : body.substr(nl + 1);
  } else {
    plate_wall_hide();
    return 0;
  }
  w_.start = lv_tick_get();
  draw(0);
  w_.phase = phase_of(0);
  lv_timer_resume(w_.timer);
  lv_obj_clear_flag(w_.canvas, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(w_.canvas);
  return ms;
}

void plate_wall_hide() {
  w_.kind = Kind::NONE;
  if (w_.canvas != nullptr)
    lv_obj_add_flag(w_.canvas, LV_OBJ_FLAG_HIDDEN);
  if (w_.timer != nullptr)
    lv_timer_pause(w_.timer);
}

}  // namespace esphome::gameday

#endif
