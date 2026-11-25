// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

// Basketball's two bottom rows on a wide board, one thought at a time
// (firmware/pages/gameday-live.yaml, gd_row_show): the row under the clock
// names a team in its color and says the story; the bottom row gives the
// detail behind it and never names a team.
//
//   USA on a 16-2 run      7 of 13 shooting
//   FAMU scoreless 4:24    0 of 6, 0 of 5 on 3s
//   FAMU three             Baker Jr. from 25 feet
//   USA Langsy 21 pts      Roman 9 reb
//
// In a break both teams' shooting, threes and rebounds, each number by its
// team's name. Pure, with no ESPHome includes, so the host tests build it.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "espn_parse.h"
#include "game_leaders.h"
#include "panel_text.h"
#include "play_pair.h"
#include "row_text.h"
#include "story.h"

namespace espn {

// A team's letters on the row: its color, a dark one brought up to full
// brightness with a little white in it (navy 00205B to 2864FF). White for
// black or no color.
inline uint32_t row_name_rgb(uint32_t rgb) {
  const int c[3] = {(int) (rgb >> 16 & 0xFF), (int) (rgb >> 8 & 0xFF), (int) (rgb & 0xFF)};
  const int peak = std::max(c[0], std::max(c[1], c[2]));
  if (peak == 0)
    return 0xFFFFFF;
  if (peak >= 0xC0)
    return rgb & 0xFFFFFF;
  uint32_t out = 0;
  for (int v : c) {
    const int x = (v * 255 + peak / 2) / peak;
    out = out << 8 | (uint32_t) std::max(40, x + (x * (255 - x) + 765) / 1530);
  }
  return out;
}

namespace detail {

inline const std::string &row_abbr(const GameSnapshot &g, int side) { return side == 1 ? g.team_abbr : g.opp_abbr; }

// One thought: a side's name and `text`, over `more`. False when it doesn't
// fit, or the row has no place left for it.
inline bool add_pair(RowLines &r, const GameSnapshot &g, int side, const std::string &text, const std::string &more,
                     uint8_t secs) {
  if (side < 1 || side > 2 || row_abbr(g, side).empty() ||
      glyph_count(row_abbr(g, side)) + 1 + glyph_count(text) > kRowChars)
    return false;
  RowLine *l = r.add(text, 0xFFFFFF, secs);
  if (l == nullptr)
    return false;
  l->side = (uint8_t) side;
  l->forms += '\n';
  if (glyph_count(more) <= kRowChars)
    l->forms += more;
  return true;
}

// Both teams' numbers, "USA 47%  FAMU 29%", over what they are.
inline void add_both(RowLines &r, const GameSnapshot &g, const std::string &a, const std::string &b, const char *what) {
  if (g.team_abbr.empty() || g.opp_abbr.empty() ||
      glyph_count(g.team_abbr) + glyph_count(g.opp_abbr) + a.size() + b.size() + 4 > kRowChars)
    return;
  RowLine *l = r.add(a, 0xFFFFFF, 5);
  if (l == nullptr)
    return;
  l->side = 3;
  l->forms += '\n' + b + '\n' + what;
}

// A side's points leader, "Langsy 21", "" under `least` points.
inline std::string row_scorer(const GameSnapshot &g, int side, int least) {
  const std::string &s = side == 1 ? g.nba.team_scorer : g.nba.opp_scorer;
  const size_t sp = s.rfind(' ');
  return sp == std::string::npos || atoi(s.c_str() + sp) < least ? "" : ::esphome::gameday::panel_text(s);
}

// "8 of 17 shooting", while the box score agrees with the score.
inline std::string row_shooting(const GameSnapshot &g, const Story *st, int side) {
  const Basketball::Box &b = side == 1 ? g.nba.team_box : g.nba.opp_box;
  if (st == nullptr || !b.has || b.fg_tried < 4 || !st->in_step(g, side))
    return "";
  return std::to_string(b.fg_made) + " of " + std::to_string(b.fg_tried) + " shooting";
}

// What the bottom row says for a story with no detail of its own: the team's
// leader.
inline std::string row_leader(const GameSnapshot &g, int side) {
  const std::string s = row_scorer(g, side, 4);
  if (!s.empty())
    return s + " pts";
  const char *extra = side == 1 ? g.nba.team_extra : g.nba.opp_extra;
  if (extra[0] != '\0')
    return extra;
  // Never the whole game's shooting: under a drought, "2 of 10 shooting"
  // would read as the drought's own.
  const std::string few = row_scorer(g, side, 1);
  return few.empty() ? "" : few + " pts";
}

inline void add_story_pair(RowLines &r, const GameSnapshot &g, const StoryLine &s) {
  add_pair(r, g, s.side, s.text, s.detail[0] != '\0' ? std::string(s.detail) : row_leader(g, s.side), 6);
}

// "USA Langsy 21 pts" over "Roman 9 reb".
inline void add_leader_pair(RowLines &r, const GameSnapshot &g, const Story *st, int side) {
  const std::string s = row_scorer(g, side, 4);
  if (s.empty())
    return;
  std::string more = side == 1 ? g.nba.team_extra : g.nba.opp_extra;
  const std::string name = s.substr(0, s.rfind(' ') + 1);
  if (more.empty())
    more = row_shooting(g, st, side);
  else if (more.compare(0, name.size(), name) == 0)
    more = "and " + more.substr(name.size());  // the same player
  if (more.empty())
    more = "Team high";
  if (!add_pair(r, g, side, s + " pts", more, 5))
    add_pair(r, g, side, s, more, 5);
}

// The team stats, the one numbered `first` of the three first.
inline void add_stats(RowLines &r, const GameSnapshot &g, int first) {
  const Basketball::Box &a = g.nba.team_box, &b = g.nba.opp_box;
  if (!a.has || !b.has)
    return;
  auto pct = [](const Basketball::Box &x) {
    return std::to_string((x.fg_made * 200 + x.fg_tried) / (2 * x.fg_tried)) + "%";
  };
  auto of = [](int made, int tried) { return std::to_string(made) + "/" + std::to_string(tried); };
  for (int i = 0; i < 3; i++) {
    switch ((first + i) % 3) {
      case 0:
        if (a.fg_tried >= 8 && b.fg_tried >= 8)
          add_both(r, g, pct(a), pct(b), "Shooting");
        else if (a.fg_tried >= 3 && b.fg_tried >= 3)
          add_both(r, g, of(a.fg_made, a.fg_tried), of(b.fg_made, b.fg_tried), "Shooting");
        break;
      case 1:
        if (a.three_tried >= 2 && b.three_tried >= 2)
          add_both(r, g, of(a.three_made, a.three_tried), of(b.three_made, b.three_tried), "Threes");
        break;
      default:
        if (a.rebounds + b.rebounds >= 6)
          add_both(r, g, std::to_string(a.rebounds), std::to_string(b.rebounds), "Rebounds");
        break;
    }
  }
}

}  // namespace detail

// Both rows' lines for a basketball game being played, into r, best first.
// `st` is nullptr when the panel has no story of it.
inline void fill_story_rows(RowLines &r, const GameSnapshot &g, const Story *st, const RowOptions &o, bool held = false) {
  using namespace detail;
  r.n = 0;
  r.play_at = 0xFF;
  if (!g.valid || g.sport != Sport::BASKETBALL || g.state != GameState::IN)
    return;
  const bool stopped = basketball_break(g, held);
  const bool decided = st != nullptr && st->decided(g);
  const int told = st != nullptr ? st->count(g) : 0;
  int at = 0;
  if (stopped)
    add_stats(r, g, 0);
  for (; at < told && st->line(at).score >= Story::kLead; at++)
    add_story_pair(r, g, st->line(at));
  const StoryLine *play = !stopped && o.last_play && st != nullptr ? st->play(g) : nullptr;
  if (play != nullptr && add_pair(r, g, play->side, play->text, play->detail, 5))
    r.play_at = r.n - 1;
  for (; at < told; at++)
    add_story_pair(r, g, st->line(at));
  if (o.win_chance && g.team_win_pct >= 1 && g.team_win_pct <= 99) {
    const int side = g.team_win_pct >= 50 ? 1 : 2;
    const int pct = side == 1 ? g.team_win_pct : 100 - g.team_win_pct;
    add_pair(r, g, side, std::to_string(pct) + "% to win", row_leader(g, side), 5);
  }
  if (g.nba.team_wins >= 0 && g.nba.opp_wins >= 0)
    add_both(r, g, std::to_string(g.nba.team_wins), std::to_string(g.nba.opp_wins), "Playoff series");
  // A blowout's last minutes: how far ahead it has been.
  const int lead_side = g.team_score >= g.opp_score ? 1 : 2;
  const int by = decided ? st->led_by(lead_side) : 0;
  if (by > abs(g.team_score - g.opp_score)) {
    const std::string n = std::to_string(by);
    std::string more = st->never_trailed(lead_side) ? std::string("Has never trailed") : row_shooting(g, st, lead_side);
    // Not over the leader: the next pair is the same player.
    if (!more.empty() && !add_pair(r, g, lead_side, "led by as many as " + n, more, 5))
      add_pair(r, g, lead_side, "led by " + n, more, 5);
  }
  add_leader_pair(r, g, st, 1);
  add_leader_pair(r, g, st, 2);
  // A quiet stretch: the team stats, a different one first as time goes by.
  if (!stopped && (told == 0 || decided))
    add_stats(r, g, std::max(0, clock_left(g.display_clock)) / 60);
  // The first minutes, with nothing to tell yet: where the game is.
  if (r.n == 0) {
    std::string where = ::esphome::gameday::panel_text(g.venue);
    if (where.empty() || glyph_count(where) > kRowChars)
      where = glyph_prefix(::esphome::gameday::panel_text(g.tv), kRowChars);
    add_both(r, g, "vs", "", where.c_str());
  }
}

}  // namespace espn
