#pragma once

// The live game pictures on a wide board's situation row
// (firmware/pages/gameday-live.yaml): football's field strip, the win bar and
// the score-margin sparkline for basketball and hockey. The geometry lives
// here so the host tests can check it; the page only puts pixels where this
// says. Pure, with no ESPHome includes.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "espn_parse.h"

namespace espn {

// What the page draws, as bits in UpdateFields::viz.
static constexpr uint8_t VIZ_FIELD = 1;    // the field strip has a ball to place
static constexpr uint8_t VIZ_WINBAR = 2;   // ESPN gave a win chance for this read
static constexpr uint8_t VIZ_SPARK = 4;    // the margin sparkline has a sample

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
  uint8_t red_from{0}, red_to{0};  // the columns (inclusive) between the ball and the goal line
  uint8_t left_timeouts{0}, right_timeouts{0};
  bool text_left{false};  // "3rd & 4" goes on the left half, away from the ball
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
  if (f.red_zone) {
    if (f.to_right) {
      f.red_from = (uint8_t) std::min(bx + 2, width - ez - 1);
      f.red_to = (uint8_t) (width - ez - 1);
    } else {
      f.red_from = (uint8_t) ez;
      f.red_to = (uint8_t) std::max(bx - 1, ez);
    }
  }
  f.left_timeouts = (uint8_t) std::max(0, std::min(3, g.team_timeouts));
  f.right_timeouts = (uint8_t) std::max(0, std::min(3, g.opp_timeouts));
  f.text_left = bx >= width / 2;
  f.valid = true;
  return f;
}

// How many columns of a `width` px bar are ours for a win chance of pct
// (1 to 99). Both sides always keep at least one column.
inline int win_bar_split(int pct, int width) {
  if (pct < 1 || pct > 99 || width < 2)
    return -1;
  int cols = (pct * width + 50) / 100;
  return std::max(1, std::min(width - 1, cols));
}

// The score margin over one game, one sample per read while the game is on.
// When the 128 slots are full every pair merges into one and later reads
// count double, so a whole game always fits in the same width.
struct MarginTrack {
  static constexpr uint8_t kMax = 128;
  int8_t margin[kMax]{};  // our score minus theirs
  uint8_t win[kMax]{};    // our win chance that read, 0 when ESPN gave none
  uint8_t n{0};
  uint16_t stride{1};     // reads per sample
  uint16_t pending{0};    // reads toward the next sample
  std::string event_id;
  int last_margin{0};
  int last_win{0};
  // The last read recorded, so a redraw of the same read adds nothing.
  std::string last_clock;
  int last_period{-1};

  void reset(const std::string &event) {
    n = 0;
    stride = 1;
    pending = 0;
    event_id = event;
    last_margin = 0;
    last_win = 0;
    last_clock.clear();
    last_period = -1;
  }

  // One emit of the board: records a read when something about the game
  // moved (the clock, the period or the score), and ignores a redraw of the
  // read already recorded. Returns true when it recorded one.
  bool read(const std::string &event, int score_margin, int win_pct, const std::string &clock, int period) {
    if (event == event_id && score_margin == last_margin && clock == last_clock && period == last_period && n > 0)
      return false;
    add(event, score_margin, win_pct);
    last_clock = clock;
    last_period = period;
    return true;
  }

  // One read of the game. A read of another game starts over.
  void add(const std::string &event, int score_margin, int win_pct) {
    if (event != event_id)
      reset(event);
    last_margin = score_margin;
    last_win = win_pct > 0 ? win_pct : 0;
    if (++pending < stride)
      return;
    pending = 0;
    if (n == kMax) {
      for (uint8_t i = 0; i < kMax / 2; i++) {
        margin[i] = margin[2 * i + 1];
        win[i] = win[2 * i + 1];
      }
      n = kMax / 2;
      stride = (uint16_t) std::min(stride * 2, 32768);
    }
    margin[n] = (int8_t) std::max(-127, std::min(127, score_margin));
    win[n] = (uint8_t) std::max(0, std::min(99, win_pct));
    n++;
  }
};

// The sparkline's columns: out[x] is the margin's height in pixels from the
// baseline for each of `width` columns, positive up to `up`, negative down to
// `down`. The samples, with the latest read as the last point, are stretched
// across the width and joined with straight lines, and the tallest lead or
// deficit of the game sets the scale. Returns the number of points drawn (0
// leaves out untouched and means nothing to draw).
inline int margin_columns(const MarginTrack &t, int width, int up, int down, int8_t *out) {
  if (width <= 0 || t.n == 0)
    return 0;
  // Points: the samples, then the latest read when it is newer than the last sample.
  int pts = t.n + (t.pending > 0 ? 1 : 0);
  auto at = [&](int i) -> int { return i < t.n ? t.margin[i] : t.last_margin; };
  int peak = 1;
  for (int i = 0; i < pts; i++)
    peak = std::max(peak, std::abs(at(i)));
  auto px = [&](int v) -> int8_t {
    if (v == 0)
      return 0;
    int lim = v > 0 ? up : down;
    int h = (std::abs(v) * lim + peak - 1) / peak;  // ceil: a 1-point lead shows
    return (int8_t) (v > 0 ? std::min(h, lim) : -std::min(h, lim));
  };
  if (pts == 1) {
    for (int x = 0; x < width; x++)
      out[x] = px(at(0));
    return 1;
  }
  for (int x = 0; x < width; x++) {
    // Which two points straddle this column, and how far between them.
    int num = x * (pts - 1);
    int i = num / (width - 1);
    int rem = num - i * (width - 1);
    if (i >= pts - 1) {
      out[x] = px(at(pts - 1));
      continue;
    }
    int a = at(i), b = at(i + 1);
    int v = a + ((b - a) * rem) / (width - 1);
    out[x] = px(v);
  }
  return pts;
}

}  // namespace espn
