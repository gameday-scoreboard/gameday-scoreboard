// Two games at once: the board split into two rows, one game a row (128x32
// on a wide board, 64x32 on a single panel). Each row shows the two
// abbreviations on chips in the team colors, the scores, the clock and a
// gold mark on the side with the ball; a side that just scored flashes.
// Built at runtime like the rest of the scoreboard, so one function serves
// every width.
#pragma once

#include <lvgl.h>
#include <cstdint>
#include <string>

#include "badge.h"

namespace esphome::gameday {

struct BoardSide {
  std::string abbr;
  std::string score;  // empty before the game
  uint32_t color = 0x808080;
  uint32_t alt = 0x808080;
  bool poss = false;
  bool dim = false;  // the losing side of a final
};

struct BoardGame {
  BoardSide away, home;
  std::string clock;  // "Q3 4:12", "FINAL", "7:20 PM"
  std::string sub;    // "2ND & 7", "TOP 7  1 OUT"; wide boards only
};

class MultiBoard {
 public:
  static constexpr int kRows = 2;
  static constexpr uint32_t kPossColor = 0xFFD000;
  static constexpr int kDimLum = 250;  // badge_luminance below this reads dim on the panel

  void build(lv_obj_t *parent, int width, const lv_font_t *f8, const lv_font_t *f12) {
    width_ = width;
    const bool wide = width >= 128;
    root_ = lv_obj_create(parent);
    plain(root_);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, width, 64);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    for (int r = 0; r < kRows; r++) {
      Row &row = rows_[r];
      row.box = lv_obj_create(root_);
      plain(row.box);
      lv_obj_set_pos(row.box, 0, r * 32);
      lv_obj_set_size(row.box, width, 32);
      for (int s = 0; s < 2; s++) {
        Side &side = row.side[s];
        // Wide: two 13 px lines in the left half. Narrow: two 9 px lines
        // over the clock line.
        const int y = wide ? (s == 0 ? 2 : 17) : (s == 0 ? 1 : 11);
        const int chip_w = wide ? 28 : 22, chip_h = wide ? 13 : 9;
        side.chip = lv_obj_create(row.box);
        plain(side.chip);
        lv_obj_set_pos(side.chip, 0, y);
        lv_obj_set_size(side.chip, chip_w, chip_h);
        lv_obj_set_style_radius(side.chip, 2, 0);
        lv_obj_set_style_bg_opa(side.chip, LV_OPA_COVER, 0);
        side.abbr = label(side.chip, wide ? f12 : f8);
        lv_obj_set_width(side.abbr, chip_w);
        lv_obj_set_style_text_align(side.abbr, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(side.abbr, LV_ALIGN_CENTER, 0, wide ? 0 : 1);
        side.poss = lv_obj_create(row.box);
        plain(side.poss);
        lv_obj_set_style_bg_color(side.poss, lv_color_hex(kPossColor), 0);
        lv_obj_set_style_bg_opa(side.poss, LV_OPA_COVER, 0);
        if (wide) {
          lv_obj_set_pos(side.poss, chip_w + 2, y + 4);
          lv_obj_set_size(side.poss, 3, 5);
        } else {
          lv_obj_set_pos(side.poss, chip_w + 2, y + 3);
          lv_obj_set_size(side.poss, 3, 3);
        }
        side.score = label(row.box, wide ? f12 : f8);
        lv_obj_set_style_text_align(side.score, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_pad_right(side.score, 1, 0);
        lv_obj_set_style_radius(side.score, 2, 0);
        if (wide) {
          lv_obj_set_pos(side.score, 34, y);
          lv_obj_set_size(side.score, 24, 13);
        } else {
          lv_obj_set_pos(side.score, 40, y);
          lv_obj_set_size(side.score, 24, 9);
        }
      }
      row.clock = label(row.box, wide ? f12 : f8);
      row.sub = label(row.box, f8);
      lv_obj_set_style_text_align(row.clock, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_align(row.sub, LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(row.clock, LV_LABEL_LONG_CLIP);
      lv_label_set_long_mode(row.sub, LV_LABEL_LONG_CLIP);
      if (wide) {
        lv_obj_set_pos(row.clock, 60, 2);
        lv_obj_set_width(row.clock, width - 60);
        lv_obj_set_pos(row.sub, 60, 19);
        lv_obj_set_width(row.sub, width - 60);
        lv_obj_set_style_text_opa(row.sub, LV_OPA_80, 0);
      } else {
        lv_obj_set_pos(row.clock, 0, 22);
        lv_obj_set_width(row.clock, width);
        lv_obj_add_flag(row.sub, LV_OBJ_FLAG_HIDDEN);
      }
    }
    flash_timer_ = lv_timer_create(&MultiBoard::flash_cb, 250, this);
    lv_timer_pause(flash_timer_);
  }

  lv_obj_t *root() const { return root_; }

  void show(int r, const BoardGame &g) {
    if (r < 0 || r >= kRows)
      return;
    Row &row = rows_[r];
    const BoardSide *sides[2] = {&g.away, &g.home};
    for (int s = 0; s < 2; s++) {
      const BoardSide &in = *sides[s];
      Side &side = row.side[s];
      const espn::BadgeStyle st = espn::badge_style(in.color, in.alt);
      // The flash needs a color that stands out on LEDs: a dark team flashes
      // in its second color when that is brighter.
      side.color = espn::badge_luminance(in.color) < kDimLum && espn::badge_luminance(in.alt) > espn::badge_luminance(in.color)
                       ? in.alt
                       : in.color;
      lv_obj_set_style_bg_color(side.chip, lv_color_hex(st.fill), 0);
      // The badge's ring only where the fill would vanish on the black panel.
      const bool ring = st.has_ring && espn::badge_luminance(st.fill) < espn::kBadgeNearBlackLum;
      lv_obj_set_style_border_width(side.chip, ring ? 1 : 0, 0);
      lv_obj_set_style_border_color(side.chip, lv_color_hex(st.ring), 0);
      lv_obj_set_style_border_opa(side.chip, LV_OPA_COVER, 0);
      lv_obj_set_style_text_color(side.abbr, lv_color_hex(st.dark_text ? 0x000000 : 0xFFFFFF), 0);
      lv_label_set_text(side.abbr, in.abbr.c_str());
      lv_label_set_text(side.score, in.score.c_str());
      lv_obj_set_style_text_opa(side.score, in.dim ? LV_OPA_50 : LV_OPA_COVER, 0);
      paint_score(side, false);
      if (in.poss)
        lv_obj_clear_flag(side.poss, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_add_flag(side.poss, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(row.clock, g.clock.c_str());
    lv_label_set_text(row.sub, g.sub.c_str());
  }

  // A score change: the scoring side's score blinks in its team color (the
  // second color for a dark team) for about five seconds (side 0 away, 1 home).
  void flash(int r, int s) {
    if (r < 0 || r >= kRows || s < 0 || s > 1)
      return;
    rows_[r].side[s].flash_left = 20;
    lv_timer_resume(flash_timer_);
  }

  void set_visible(bool on) {
    if (on)
      lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
  }

 private:
  struct Side {
    lv_obj_t *chip = nullptr, *abbr = nullptr, *poss = nullptr, *score = nullptr;
    uint32_t color = 0;
    int flash_left = 0;
  };
  struct Row {
    lv_obj_t *box = nullptr, *clock = nullptr, *sub = nullptr;
    Side side[2];
  };

  static void plain(lv_obj_t *o) {
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
  }

  static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(l, "");
    return l;
  }

  // Lit: white on the team color (black on a light one). Unlit: plain.
  static void paint_score(Side &side, bool lit) {
    if (lit) {
      const bool light = espn::badge_luminance(side.color) > espn::kBadgeDarkTextLum;
      lv_obj_set_style_bg_color(side.score, lv_color_hex(side.color), 0);
      lv_obj_set_style_bg_opa(side.score, LV_OPA_COVER, 0);
      lv_obj_set_style_text_color(side.score, lv_color_hex(light ? 0x000000 : 0xFFFFFF), 0);
    } else {
      lv_obj_set_style_bg_opa(side.score, LV_OPA_TRANSP, 0);
      lv_obj_set_style_text_color(side.score, lv_color_hex(0xFFFFFF), 0);
    }
  }

  static void flash_cb(lv_timer_t *t) {
    auto *self = static_cast<MultiBoard *>(lv_timer_get_user_data(t));
    bool any = false;
    for (Row &row : self->rows_) {
      for (Side &side : row.side) {
        if (side.flash_left <= 0)
          continue;
        side.flash_left--;
        paint_score(side, side.flash_left > 0 && (side.flash_left & 2));
        any = any || side.flash_left > 0;
      }
    }
    if (!any)
      lv_timer_pause(t);
  }

  int width_ = 0;
  lv_obj_t *root_ = nullptr;
  lv_timer_t *flash_timer_ = nullptr;
  Row rows_[kRows];
};

}  // namespace esphome::gameday
