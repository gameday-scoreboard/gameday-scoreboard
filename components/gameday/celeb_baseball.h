// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Baseball's big plays: the grand slam and the walk-off. Pure, no ESPHome
// includes: tests/test_celeb_baseball.cpp runs them over recorded ESPN reads.
// The frames they play are celeb_art_grand_slam_*.h and celeb_art_walk_off_*.h.
#pragma once
#include <string>

#include "celeb_plan.h"
#include "espn_parse.h"

namespace espn {

// How many runners a play's words say scored: "Soto homered to right center
// (419 feet), McNeil scored, Mullins scored and Torrens scored." is 3.
inline int baseball_runners_scored(const std::string &text) {
  int n = 0;
  for (size_t at = text.find(" scored"); at != std::string::npos; at = text.find(" scored", at + 7))
    n++;
  return n;
}

// A grand slam in this read: a home run worth four. ESPN puts 4 in the play's
// scoreValue, on the play's result ("Play Result", the words) and in some
// games on the batter's closing row too, which has no words. A play worth
// four whose words aren't a home run (a bases-loaded hit and an error) is
// four runs, not a slam. With no value, the words count the runners. The
// side's runs went up by four or more since the last read, so the next
// batter's read never shows it again.
inline bool baseball_grand_slam(const GameSnapshot &prev, const GameSnapshot &cur, bool ours) {
  const int runs = ours ? cur.team_score - prev.team_score : cur.opp_score - prev.opp_score;
  const int other = ours ? cur.opp_score - prev.opp_score : cur.team_score - prev.team_score;
  if (runs < 4 || other != 0)
    return false;
  if (cur.play_value == 4)
    return cur.last_play.empty() || baseball_home_run(cur);
  return cur.play_value == 0 && baseball_home_run(cur) && baseball_runners_scored(cur.last_play) == 3;
}

inline int baseball_home_score(const GameSnapshot &s) { return s.team_home ? s.team_score : s.opp_score; }
inline int baseball_away_score(const GameSnapshot &s) { return s.team_home ? s.opp_score : s.team_score; }

// A walk-off: the home side takes the lead in the bottom of the 9th or
// later, which ends the game there and then. The read before was in the 9th
// or later with the home side level or behind (any half: a missed read can
// skip the break), and this one has it ahead, either still in progress in
// the bottom half (ESPN often posts the run before the final) or final.
inline bool baseball_walk_off(const GameSnapshot &prev, const GameSnapshot &cur) {
  if (!celeb_same_game(prev, cur) || cur.sport != Sport::BASEBALL)
    return false;
  if (prev.state != GameState::IN || prev.period < 9)
    return false;
  if (baseball_home_score(prev) > baseball_away_score(prev))
    return false;
  if (baseball_home_score(cur) <= baseball_away_score(cur) || baseball_home_score(cur) <= baseball_home_score(prev))
    return false;
  if (cur.state == GameState::IN)
    return cur.period >= 9 && cur.mlb.half == Half::BOTTOM;
  return cur.state == GameState::POST && cur.completed;
}

inline CelebPlan baseball_big_plan_(CelebKind kind, const GameSnapshot &cur, bool ours, const char *words,
                                    uint32_t ms) {
  CelebPlan p;
  p.kind = kind;
  p.color = celeb_bright(parse_color(ours ? cur.team_color : cur.opp_color));
  p.alt = celeb_alt_for(p.color);
  p.text = words;  // for the log and the page's label; the frames draw their own words
  p.duration_ms = ms;
  return p;
}

// The big play in a live read, kind NONE when there is none. Called only on
// Full with Big plays on, for the same game in progress in both reads. Our
// side's plays only; either side's on the neutral boards when Opponent scores
// too is on. A walk-off grand slam is the walk-off: the game is over.
inline CelebPlan big_play_baseball(const GameSnapshot &prev, const GameSnapshot &cur, const Splash &splash,
                                   bool neutral, bool opp_splashes) {
  (void) splash;
  const bool theirs_ok = neutral && opp_splashes;
  if (baseball_walk_off(prev, cur)) {
    const bool ours = cur.team_home;
    if (ours || theirs_ok)
      return baseball_big_plan_(CelebKind::WALK_OFF, cur, ours, "WALK-OFF!", kCelebWalkOffMs);
    return CelebPlan{};
  }
  if (baseball_grand_slam(prev, cur, true))
    return baseball_big_plan_(CelebKind::GRAND_SLAM, cur, true, "GRAND SLAM!", kCelebGrandSlamMs);
  if (theirs_ok && baseball_grand_slam(prev, cur, false))
    return baseball_big_plan_(CelebKind::GRAND_SLAM, cur, false, "GRAND SLAM!", kCelebGrandSlamMs);
  return CelebPlan{};
}

// A walk-off ESPN posts with the final in one read: the walk-off plays first
// and the win screen after it. win is the plan decide_celebration made for
// that read; the answer is the walk-off to play in its place (the component
// shows win when it ends), or kind NONE. Full with Big plays on only, as above.
inline CelebPlan walk_off_before_win(const GameSnapshot &prev, const GameSnapshot &cur, const CelebPlan &win) {
  if (win.kind != CelebKind::WIN || !baseball_walk_off(prev, cur))
    return CelebPlan{};
  CelebPlan p = win;
  p.kind = CelebKind::WALK_OFF;
  p.text = "WALK-OFF!";
  p.line2.clear();
  p.duration_ms = kCelebWalkOffMs;
  return p;
}

}  // namespace espn
