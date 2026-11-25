// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Volleyball's board numbers, ticker, clock row and splashes. Pure, no ESPHome
// includes. ESPN's score for a side is the sets it has won, and its line
// score is the points in each set; the period is the set under way. It sends
// no plays and no serve.
#include "espn_parse.h"

#include <cstdio>

namespace espn {

using detail::add_last_play;
using detail::add_odds;
using detail::add_part;
using detail::pregame_records;
using detail::records_line;

// The set on the board, 1 to 5 in a match under way, 0 before ESPN names one.
static int set_number(const GameSnapshot &s) { return s.period > 0 ? s.period : 0; }

bool volleyball_set_over(const GameSnapshot &s) {
  if (s.sport != Sport::VOLLEYBALL || s.state != GameState::IN || set_number(s) == 0)
    return false;
  return s.team_score + s.opp_score >= set_number(s);
}

static int set_points(const uint8_t *lines, uint8_t count, int set) {
  return set >= 1 && set <= count ? lines[set - 1] : 0;
}

int board_team_score(const GameSnapshot &s) {
  if (s.sport != Sport::VOLLEYBALL || s.state != GameState::IN)
    return s.team_score;
  return set_points(s.team_lines, s.team_line_count, set_number(s));
}

int board_opp_score(const GameSnapshot &s) {
  if (s.sport != Sport::VOLLEYBALL || s.state != GameState::IN)
    return s.opp_score;
  return set_points(s.opp_lines, s.opp_line_count, set_number(s));
}

bool volleyball_match_point(const GameSnapshot &s) {
  if (s.sport != Sport::VOLLEYBALL || s.state != GameState::IN || volleyball_set_over(s))
    return false;
  // A set is won at 25 points and the fifth at 15, by two.
  const int need = set_number(s) >= 5 ? 14 : 24;
  const int us = board_team_score(s), them = board_opp_score(s);
  return (s.team_score == 2 && us >= need && us > them) || (s.opp_score == 2 && them >= need && them > us);
}

std::string volleyball_sets(const GameSnapshot &s, const char *sep) {
  std::string out;
  if (s.sport != Sport::VOLLEYBALL || (s.state != GameState::IN && s.state != GameState::POST))
    return out;
  int sets = s.team_line_count < s.opp_line_count ? s.team_line_count : s.opp_line_count;
  // The set under way is the board's big numbers.
  if (s.state == GameState::IN && !volleyball_set_over(s) && sets >= set_number(s))
    sets = set_number(s) - 1;
  char t[12];
  for (int i = 0; i < sets; i++) {
    snprintf(t, sizeof(t), "%u-%u", (unsigned) s.team_lines[i], (unsigned) s.opp_lines[i]);
    if (!out.empty())
      out += sep;
    out += t;
  }
  return out;
}

// "Set 3  1-1": the set and the sets won, ours first like the board; "End 2
// 1-1" between sets. At most 10 characters, the room a 128-wide board has.
std::string volleyball_clock_text(const GameSnapshot &s) {
  if (s.state != GameState::IN || set_number(s) == 0)
    return s.short_detail;
  char t[32];
  snprintf(t, sizeof(t), "%s %d  %d-%d", volleyball_set_over(s) ? "End" : "Set", set_number(s), s.team_score,
           s.opp_score);
  return t;
}

std::string volleyball_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local) {
  std::string out;
  switch (s.state) {
    case GameState::NOT_FOUND:
      return "No upcoming game";
    case GameState::PRE:
      add_part(out, s.game_name);  // a tournament round
      add_part(out, kickoff_local.empty() ? s.short_detail : kickoff_local);
      if (o.records)
        add_part(out, pregame_records(s));
      add_odds(out, s, o);
      add_part(out, detail::venue_line(s, o));
      return out;
    case GameState::POST:
      // Postponed, Canceled or Suspended: ESPN's post state without a result.
      if (!s.completed && !s.short_detail.empty())
        return s.short_detail;
      out = "Final";
      add_part(out, records_line(s));
      return out;
    case GameState::IN:
      break;
  }
  if (o.clock)
    add_part(out, s.short_detail);
  // A wide board has the finished sets on the row under the clock.
  if (!o.row_sets)
    add_part(out, volleyball_sets(s, ", "));
  if (o.last_play)
    add_last_play(out, s.last_play);
  add_part(out, records_line(s));
  if (out.empty())
    out = s.short_detail.empty() ? "In Progress" : s.short_detail;
  return out;
}

// "SET!" for a set of ours, "WIS SET" for one of theirs or for either side in
// the live modes, and the win at the end. Points don't splash.
Splash volleyball_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral) {
  Splash none;
  if (!prev.valid || !cur.valid || prev.event_id != cur.event_id)
    return none;
  if (prev.state == GameState::IN && cur.state == GameState::POST) {
    if (!cur.completed)
      return none;
    if (cur.team_score > cur.opp_score)
      return Splash{cur.team_abbr + " WINS!", parse_color(cur.team_color)};
    if (neutral && cur.opp_score > cur.team_score)
      return Splash{cur.opp_abbr + " WINS!", parse_color(cur.opp_color)};
    return none;
  }
  if (prev.state != GameState::IN || cur.state != GameState::IN)
    return none;
  if (cur.team_score > prev.team_score)
    return Splash{neutral ? cur.team_abbr + " SET" : "SET!", parse_color(cur.team_color)};
  if (cur.opp_score > prev.opp_score && opponent_splashes)
    return Splash{cur.opp_abbr + " SET", parse_color(cur.opp_color)};
  return none;
}

}  // namespace espn
