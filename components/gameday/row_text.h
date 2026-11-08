#pragma once

// The text on a wide board's situation row while a game is on
// (firmware/pages/gameday-live.yaml, gd_row_show): the lines the page takes
// turns showing under the clock. Each line comes in up to three forms, the
// longest first, and the page draws the longest that fits the room the row
// has (half the field strip in football, the space between the logos'
// marks in soccer, the full width in basketball). Pure, with no ESPHome
// includes, so the host tests build it.
//
// Football: down and distance, then the win chance.
// Basketball: the last basket, each side's points leader, a timeout, the
// playoff series, the win chance.
// Soccer: the latest goal.

#include <cctype>
#include <cstdint>
#include <string>

#include "espn_parse.h"
#include "game_leaders.h"
#include "panel_text.h"

namespace espn {

struct RowLine {
  std::string forms[3];  // longest first; an empty form is skipped
  uint32_t rgb{0xFFFFFF};
  uint8_t secs{4};  // how long its turn lasts
};

struct RowLines {
  static constexpr uint8_t kMax = 6;
  RowLine line[kMax];
  uint8_t n{0};
  // Bumped when the lead line has news (a new basket, a new down), so the
  // page shows it at once instead of waiting for its turn.
  uint32_t news{0};

  RowLine *add(const std::string &text, uint32_t rgb, uint8_t secs = 4) {
    if (n >= kMax || text.empty())
      return nullptr;
    RowLine &l = line[n++];
    l = RowLine();
    l.forms[0] = ::esphome::gameday::panel_text(text);
    l.rgb = rgb;
    l.secs = secs;
    return &l;
  }
};

// A team color as the board draws it on black: ESPN's black turns white and
// a dark color is lifted until its brightest channel is 0xC0, so navy still
// reads across a room.
inline uint32_t row_rgb(uint32_t c) {
  uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  uint32_t m = r > g ? (r > b ? r : b) : (g > b ? g : b);
  if (m == 0)
    return 0xFFFFFF;
  if (m < 0xC0) {
    r = r * 0xC0 / m;
    g = g * 0xC0 / m;
    b = b * 0xC0 / m;
  }
  return (r << 16) | (g << 8) | b;
}

// The latest basket the board has seen, held until the next one: ESPN's
// last play moves on with every rebound and foul, so the panel keeps the
// last one that scored. Follows one game; another game starts it over.
struct RowHold {
  std::string event_id;
  std::string basket;  // "McDaniels 3", "Burries FT"
  bool ours{false};

  // One read of the board game.
  void read(const GameSnapshot &g) {
    if (g.event_id != this->event_id) {
      this->event_id = g.event_id;
      this->basket.clear();
    }
    if (g.sport != Sport::BASKETBALL || g.state != GameState::IN || g.play_value <= 0 || g.play_athlete.empty())
      return;
    std::string who = detail::leader_short_surname(g.play_athlete);
    bool ft = g.play_type.find("Free Throw") != std::string::npos;
    this->basket = who + " " + (ft ? std::string("FT") : std::to_string(g.play_value));
    this->ours = g.play_team == g.team_id;
  }
};

struct RowOptions {
  bool win_chance{true};  // the Win chance switch (winbar)
  bool last_play{true};   // the Last play switch: the last basket
};

// The win chance as words, "ND 84% to win", in the color of the side ESPN
// favors. Nothing without a number.
inline void add_win_chance(RowLines &r, const GameSnapshot &g, uint8_t secs) {
  if (g.team_win_pct < 1 || g.team_win_pct > 99)
    return;
  bool ours = g.team_win_pct >= 50;
  int pct = ours ? g.team_win_pct : 100 - g.team_win_pct;
  const std::string &abbr = ours ? g.team_abbr : g.opp_abbr;
  std::string num = abbr + " " + std::to_string(pct) + "%";
  RowLine *l = r.add(num + " to win", row_rgb(parse_color(ours ? g.team_color : g.opp_color)), secs);
  if (l == nullptr)
    return;
  l->forms[1] = ::esphome::gameday::panel_text(num + " win");
  l->forms[2] = ::esphome::gameday::panel_text(num);
}

// A timeout as the last play: "Official TV Timeout" as ESPN words it when
// it fits, else "TV timeout" or "MIN timeout".
inline void add_timeout(RowLines &r, const GameSnapshot &g) {
  std::string low = g.last_play;
  for (char &c : low)
    c = (char) tolower((unsigned char) c);
  if (low.find("timeout") == std::string::npos)
    return;
  RowLine *l = r.add(ascii_fold(g.last_play), 0xFFFFFF);
  if (l == nullptr)
    return;
  const std::string &side = g.play_team == g.team_id ? g.team_abbr : g.play_team == g.opp_id ? g.opp_abbr : "";
  if (low.find("official") != std::string::npos || low.find("tv timeout") != std::string::npos)
    l->forms[1] = "TV timeout";
  else if (!side.empty())
    l->forms[1] = ::esphome::gameday::panel_text(side + " timeout");
  l->forms[2] = "Timeout";
}

// The row's lines for the board game, into r (filled in place: the
// component's loop stack has no room to spare for a copy). `playing` is a
// game on and not at halftime (gameday.cpp emit_); `hold` has already read
// this game.
inline void fill_row_lines(RowLines &r, const GameSnapshot &g, const RowHold &hold, const RowOptions &o,
                           bool playing) {
  r.n = 0;
  if (!g.valid || g.state != GameState::IN)
    return;
  const uint32_t white = 0xFFFFFF;
  switch (g.sport) {
    case Sport::FOOTBALL:
      if (!playing)
        break;
      // Down and distance keeps most of the time; red in the red zone.
      r.add(g.short_down_distance, g.is_red_zone ? 0xFF3030 : white, 8);
      if (o.win_chance)
        add_win_chance(r, g, 4);
      break;
    case Sport::BASKETBALL: {
      const std::string series = basketball_series(g);
      if (!playing) {
        r.add(series, white);
        break;
      }
      if (o.last_play && hold.event_id == g.event_id)
        r.add(hold.basket, row_rgb(parse_color(hold.ours ? g.team_color : g.opp_color)));
      r.add(g.nba.team_scorer, row_rgb(parse_color(g.team_color)));
      r.add(g.nba.opp_scorer, row_rgb(parse_color(g.opp_color)));
      add_timeout(r, g);
      r.add(series, white);
      if (o.win_chance)
        add_win_chance(r, g, 4);
      break;
    }
    case Sport::SOCCER:
      r.add(soccer_situation(g), white);
      break;
    default:
      break;
  }
}

inline RowLines row_lines(const GameSnapshot &g, const RowHold &hold, const RowOptions &o, bool playing) {
  RowLines r;
  fill_row_lines(r, g, hold, o, playing);
  return r;
}

// What counts as news on the row: the held basket, football's down and
// distance, soccer's latest goal. The component bumps RowLines::news when
// it changes.
inline std::string row_news(const GameSnapshot &g, const RowHold &hold) {
  if (g.sport == Sport::BASKETBALL)
    return hold.basket;
  if (g.sport == Sport::FOOTBALL)
    return g.short_down_distance;
  return "";
}

}  // namespace espn
