// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

// The live game picture on a wide board's situation row
// (firmware/pages/gameday-live.yaml): football's field strip. The geometry
// lives here so the host tests can check it; the page only puts pixels where
// this says. Pure, with no ESPHome includes.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "espn_parse.h"

namespace espn {

// What the page draws, as bits in UpdateFields::viz.
static constexpr uint8_t VIZ_FIELD = 1;    // the field strip has a ball to place

// The field strip, `width` px wide with an end zone `ez` px wide at each end
// (our side's on the left, under our logo). Columns are canvas x positions.
struct FieldStrip {
  bool valid{false};
  uint8_t ez{6};
  uint8_t ball_x{0};     // the ball marker's left column; it is two columns wide
  bool to_right{true};   // the offense attacks the right end zone
  bool has_offense{false};  // ESPN said who has the ball; without it no tip is drawn
  int16_t gain_x{-1};    // the line to gain's column, -1 when it is the goal line or unknown
  bool red_zone{false};
  uint8_t red_from{0}, red_to{0};  // the columns (inclusive) of the last 20 yards, the 20 to the goal line
  // The row's text ("3rd & 4", "ND 84% win") goes in the clear part of the
  // strip: between an end zone and the nearest marker (the ball, its tip or
  // the line to gain), so it never runs into one. text_left puts it against
  // our end zone, left-aligned; else against theirs, right-aligned.
  bool text_left{false};
  uint8_t text_x{0};  // its first column
  uint8_t text_w{0};  // its width in px, 0 when no room is clear
};

// The field column of a yard position measured from the left end zone (0 is
// our goal line, 100 the opponent's).
inline int field_col(int pos, int width, int ez) {
  int span = width - 2 * ez - 1;
  pos = std::max(0, std::min(100, pos));
  return ez + (pos * span + 50) / 100;
}

inline FieldStrip field_strip(const GameSnapshot &g, int width, int ez = 6) {
  FieldStrip f;
  if (!g.valid || g.state != GameState::IN || g.sport != Sport::FOOTBALL || g.yard_line > 100 || width < 2 * ez + 20)
    return f;
  f.ez = (uint8_t) ez;
  // ESPN counts from the home goal line; our end zone is on the left.
  int pos = g.team_home ? g.yard_line : 100 - g.yard_line;
  f.has_offense = g.possession == 1 || g.possession == 2;
  f.to_right = g.possession != 2;
  int bx = field_col(pos, width, ez);
  if (bx + 1 > width - ez - 1)
    bx = width - ez - 2;
  f.ball_x = (uint8_t) bx;
  if (f.has_offense && g.distance > 0) {
    int gain = f.to_right ? pos + g.distance : pos - g.distance;
    if (gain > 0 && gain < 100) {
      int gx = field_col(gain, width, ez);
      // Never on the marker itself; a yard past the ball is still one column.
      if (f.to_right && gx <= bx + 1)
        gx = bx + 2;
      if (!f.to_right && gx >= bx)
        gx = bx - 1;
      if (gx >= ez && gx <= width - ez - 1)
        f.gain_x = (int16_t) gx;
    }
  }
  f.red_zone = g.is_red_zone && f.has_offense;
  // The whole red zone glows, not only the yards left to go: at the 1 that
  // would be a column or two, a blip from across a room. The page draws the
  // ball and the line to gain on top.
  if (f.red_zone) {
    if (f.to_right) {
      f.red_from = (uint8_t) field_col(80, width, ez);
      f.red_to = (uint8_t) (width - ez - 1);
    } else {
      f.red_from = (uint8_t) ez;
      f.red_to = (uint8_t) field_col(20, width, ez);
    }
  }
  // The markers' columns: the ball (two wide), its tip, the line to gain.
  int lo = bx, hi = bx + 1;
  if (f.has_offense) {
    if (f.to_right)
      hi = bx + 2;
    else
      lo = bx - 1;
  }
  if (f.gain_x >= 0) {
    lo = std::min(lo, (int) f.gain_x);
    hi = std::max(hi, (int) f.gain_x);
  }
  // Two columns clear of each end zone (as before) and one clear of a marker.
  const int lx = ez + 2, rx = width - ez - 3;  // the text's outermost columns
  const int left_w = std::max(0, (lo - 2) - lx + 1);
  const int right_w = std::max(0, rx - (hi + 2) + 1);
  // The roomier side; on a tie, the half away from the ball.
  f.text_left = left_w != right_w ? left_w > right_w : bx >= width / 2;
  f.text_x = (uint8_t) (f.text_left ? lx : hi + 2);
  f.text_w = (uint8_t) (f.text_left ? left_w : right_w);
  f.valid = true;
  return f;
}

}  // namespace espn
