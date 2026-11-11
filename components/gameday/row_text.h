// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

// The text on a wide board's situation row while a game is on
// (firmware/pages/gameday-live.yaml, gd_row_show): the lines the page takes
// turns showing under the clock. Each line comes in up to three forms, the
// longest first, and the page draws the longest that fits the room the row
// has (the clear part of the field strip in football, the space between the logos'
// marks in soccer, the full width in basketball). Pure, with no ESPHome
// includes, so the host tests build it.
//
// Football: down and distance, then the win chance.
// Basketball: the last basket, each side's points leader, the playoff
// series, the win chance. No timeout line: the stopped clock already shows
// the break, and ESPN gives no count of basketball timeouts left.
// Soccer: the latest goal.
//
// Every line is white (football's down turns red in the red zone): a team
// color, even lifted, was hard to read across a room, and the logos and
// abbreviations above already carry the colors.

#include <cctype>
#include <cstdint>
#include <string>

#include "espn_parse.h"
#include "game_leaders.h"
#include "panel_text.h"

namespace espn {

struct RowLine {
  // Its forms, longest first, one per '\n'-separated piece. One string, not
  // three: the component keeps the last update, these lines included, in
  // internal RAM.
  std::string forms;
  uint32_t rgb{0xFFFFFF};
  uint8_t secs{4};  // how long its turn lasts

  // A shorter form, for when the longer ones don't fit.
  void add_form(const std::string &f) {
    if (f.empty())
      return;
    this->forms += '\n';
    this->forms += f;
  }
  // Form i, "" past the last.
  std::string form(int i) const {
    size_t at = 0;
    for (; i > 0; i--) {
      at = this->forms.find('\n', at);
      if (at == std::string::npos)
        return "";
      at++;
    }
    return this->forms.substr(at, this->forms.find('\n', at) - at);
  }
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
    l.forms = ::esphome::gameday::panel_text(text);
    l.rgb = rgb;
    l.secs = secs;
    return &l;
  }
};

// The latest basket the board has seen, held until the next one: ESPN's
// last play moves on with every rebound and foul, so the panel keeps the
// last one that scored. Follows one game; another game starts it over.
struct RowHold {
  std::string event_id;
  std::string basket;  // "McDaniels 3", "Burries FT"

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
  }
};

struct RowOptions {
  bool win_chance{true};  // the Win chance switch (winbar)
  bool last_play{true};   // the Last play switch: the last basket
};

// The win chance as words, "ND 84% to win", for the side ESPN favors.
// Nothing without a number.
inline void add_win_chance(RowLines &r, const GameSnapshot &g, uint8_t secs) {
  if (g.team_win_pct < 1 || g.team_win_pct > 99)
    return;
  bool ours = g.team_win_pct >= 50;
  int pct = ours ? g.team_win_pct : 100 - g.team_win_pct;
  const std::string &abbr = ours ? g.team_abbr : g.opp_abbr;
  std::string num = abbr + " " + std::to_string(pct) + "%";
  RowLine *l = r.add(num + " to win", 0xFFFFFF, secs);
  if (l == nullptr)
    return;
  l->add_form(::esphome::gameday::panel_text(num + " win"));
  l->add_form(::esphome::gameday::panel_text(num));
}

// Down and distance, "2nd & 17", then "2nd&17" and "2&17" for when the
// clear part of the field strip is narrower (the ball or the line to gain
// near midfield).
inline void add_down(RowLines &r, const GameSnapshot &g, uint32_t rgb) {
  RowLine *l = r.add(g.short_down_distance, rgb, 8);
  if (l == nullptr)
    return;
  std::string tight = l->form(0);
  size_t amp = tight.find(" & ");
  if (amp == std::string::npos)
    return;
  tight.replace(amp, 3, "&");
  l->add_form(tight);
  // "2nd&17" to "2&17": the ordinal's letters after the leading digit.
  if (tight.size() > 3 && isdigit((unsigned char) tight[0]) && isalpha((unsigned char) tight[1]) &&
      isalpha((unsigned char) tight[2]) && tight[3] == '&')
    l->add_form(tight.substr(0, 1) + tight.substr(3));
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
      add_down(r, g, g.is_red_zone ? 0xFF3030 : white);
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
        r.add(hold.basket, white);
      r.add(g.nba.team_scorer, white);
      r.add(g.nba.opp_scorer, white);
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
