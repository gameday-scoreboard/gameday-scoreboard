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
#include <cstring>
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
inline bool add_pair(RowLines &r, const GameSnapshot &g, int side, const char *text, const char *more, uint8_t secs) {
  if (side < 1 || side > 2 || row_abbr(g, side).empty() ||
      glyph_count(row_abbr(g, side)) + 1 + row_glyphs(text) > kRowChars)
    return false;
  RowLine *l = r.put(text, row_glyphs(more) <= kRowChars ? more : "", nullptr, secs);
  if (l == nullptr)
    return false;
  l->side = (uint8_t) side;
  return true;
}

// Both teams' numbers, "USA 47%  FAMU 29%", over what they are.
inline void add_both(RowLines &r, const GameSnapshot &g, const char *a, const char *b, const char *what) {
  if (g.team_abbr.empty() || g.opp_abbr.empty() ||
      glyph_count(g.team_abbr) + glyph_count(g.opp_abbr) + row_glyphs(a) + row_glyphs(b) + 4 > kRowChars)
    return;
  RowLine *l = r.put(a, b, what, 5);
  if (l != nullptr)
    l->side = 3;
}

// ESPN's text as the panel draws it, into out. Plain ASCII needs no work; only
// a name with an accent goes through panel_text (and its one allocation).
template<size_t N> inline void row_text_of(char (&out)[N], const std::string &s) {
  bool plain = true;
  for (char c : s)
    plain = plain && c >= 0x20 && c < 0x7F;
  snprintf(out, N, "%s", plain ? s.c_str() : ::esphome::gameday::panel_text(s).c_str());
}

// A side's points leader, "Langsy 21", "" under `least` points.
template<size_t N> inline void row_scorer(char (&out)[N], const GameSnapshot &g, int side, int least) {
  const std::string &s = side == 1 ? g.nba.team_scorer : g.nba.opp_scorer;
  const size_t sp = s.rfind(' ');
  out[0] = '\0';
  if (sp != std::string::npos && atoi(s.c_str() + sp) >= least)
    row_text_of(out, s);
}

// "8 of 17 shooting", while the box score agrees with the score.
template<size_t N> inline void row_shooting(char (&out)[N], const GameSnapshot &g, const Story *st, int side) {
  const Basketball::Box &b = side == 1 ? g.nba.team_box : g.nba.opp_box;
  out[0] = '\0';
  if (st != nullptr && b.has && b.fg_tried >= 4 && st->in_step(g, side))
    snprintf(out, N, "%u of %u shooting", b.fg_made, b.fg_tried);
}

// What the bottom row says for a story with no detail of its own: the team's
// leader.
template<size_t N> inline void row_leader(char (&out)[N], const GameSnapshot &g, int side) {
  char who[40];
  row_scorer(who, g, side, 4);
  const char *extra = side == 1 ? g.nba.team_extra : g.nba.opp_extra;
  if (who[0] != '\0') {
    snprintf(out, N, "%s pts", who);
    return;
  }
  if (extra[0] != '\0') {
    snprintf(out, N, "%s", extra);
    return;
  }
  // Never the whole game's shooting: under a drought, "2 of 10 shooting"
  // would read as the drought's own.
  row_scorer(who, g, side, 1);
  snprintf(out, N, who[0] == '\0' ? "%s" : "%s pts", who);
}

inline void add_story_pair(RowLines &r, const GameSnapshot &g, const StoryLine &s) {
  char more[56];
  if (s.detail[0] != '\0')
    snprintf(more, sizeof(more), "%s", s.detail);
  else
    row_leader(more, g, s.side);
  add_pair(r, g, s.side, s.text, more, 6);
}

// "USA Langsy 21 pts" over "Roman 9 reb".
inline void add_leader_pair(RowLines &r, const GameSnapshot &g, const Story *st, int side) {
  char who[40], text[48], more[40];
  row_scorer(who, g, side, 4);
  if (who[0] == '\0')
    return;
  const char *extra = side == 1 ? g.nba.team_extra : g.nba.opp_extra;
  const char *sp = strrchr(who, ' ');
  const size_t name = sp != nullptr ? (size_t) (sp - who) + 1 : 0;  // "Langsy "
  if (extra[0] == '\0')
    row_shooting(more, g, st, side);
  else if (name > 0 && strncmp(extra, who, name) == 0)
    snprintf(more, sizeof(more), "and %s", extra + name);  // the same player
  else
    snprintf(more, sizeof(more), "%s", extra);
  if (more[0] == '\0')
    snprintf(more, sizeof(more), "Team high");
  snprintf(text, sizeof(text), "%s pts", who);
  if (!add_pair(r, g, side, text, more, 5))
    add_pair(r, g, side, who, more, 5);
}

// The team stats, the one numbered `first` of the three first.
inline void add_stats(RowLines &r, const GameSnapshot &g, int first) {
  const Basketball::Box &a = g.nba.team_box, &b = g.nba.opp_box;
  if (!a.has || !b.has)
    return;
  char x[12], y[12];
  auto pct = [](char *out, const Basketball::Box &v) {
    snprintf(out, 12, "%u%%", (v.fg_made * 200u + v.fg_tried) / (2u * v.fg_tried));
  };
  auto of = [](char *out, int made, int tried) { snprintf(out, 12, "%d/%d", made, tried); };
  for (int i = 0; i < 3; i++) {
    switch ((first + i) % 3) {
      case 0:
        if (a.fg_tried >= 8 && b.fg_tried >= 8) {
          pct(x, a);
          pct(y, b);
          add_both(r, g, x, y, "Shooting");
        } else if (a.fg_tried >= 3 && b.fg_tried >= 3) {
          of(x, a.fg_made, a.fg_tried);
          of(y, b.fg_made, b.fg_tried);
          add_both(r, g, x, y, "Shooting");
        }
        break;
      case 1:
        if (a.three_tried >= 2 && b.three_tried >= 2) {
          of(x, a.three_made, a.three_tried);
          of(y, b.three_made, b.three_tried);
          add_both(r, g, x, y, "Threes");
        }
        break;
      default:
        if (a.rebounds + b.rebounds >= 6) {
          snprintf(x, sizeof(x), "%u", a.rebounds);
          snprintf(y, sizeof(y), "%u", b.rebounds);
          add_both(r, g, x, y, "Rebounds");
        }
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
    char text[16], more[56];
    snprintf(text, sizeof(text), "%d%% to win", pct);
    row_leader(more, g, side);
    add_pair(r, g, side, text, more, 5);
  }
  if (g.nba.team_wins >= 0 && g.nba.opp_wins >= 0) {
    char x[8], y[8];
    snprintf(x, sizeof(x), "%d", g.nba.team_wins);
    snprintf(y, sizeof(y), "%d", g.nba.opp_wins);
    add_both(r, g, x, y, "Playoff series");
  }
  // A blowout's last minutes: how far ahead it has been.
  const int lead_side = g.team_score >= g.opp_score ? 1 : 2;
  const int by = decided ? st->led_by(lead_side) : 0;
  if (by > abs(g.team_score - g.opp_score)) {
    char text[32], more[40];
    if (st->never_trailed(lead_side))
      snprintf(more, sizeof(more), "Has never trailed");
    else
      row_shooting(more, g, st, lead_side);
    // Not over the leader: the next pair is the same player.
    snprintf(text, sizeof(text), "led by as many as %d", by);
    if (more[0] != '\0' && !add_pair(r, g, lead_side, text, more, 5)) {
      snprintf(text, sizeof(text), "led by %d", by);
      add_pair(r, g, lead_side, text, more, 5);
    }
  }
  add_leader_pair(r, g, st, 1);
  add_leader_pair(r, g, st, 2);
  // A quiet stretch: the team stats, a different one first as time goes by.
  if (!stopped && (told == 0 || decided))
    add_stats(r, g, std::max(0, clock_left(g.display_clock)) / 60);
  // The first minutes, with nothing to tell yet: where the game is.
  if (r.n == 0) {
    char where[64];
    row_text_of(where, g.venue);
    if (where[0] == '\0' || row_glyphs(where) > kRowChars)
      row_text_of(where, glyph_count(g.tv) > kRowChars ? glyph_prefix(g.tv, kRowChars) : g.tv);
    add_both(r, g, "vs", "", where);
  }
}

}  // namespace espn
