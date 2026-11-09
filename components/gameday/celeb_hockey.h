// Hockey's big play: the hat trick. Pure, no ESPHome includes; the host
// tests run it on recorded ESPN reads.
//
// No power play or shorthanded goal: ESPN's play-by-play marks each goal's
// strength, but the live read the panel makes (scoreboard/<id>) leaves it
// off the last play, a power play goal included (seen live 11/8).
#pragma once
#include <cstdint>

#include "celeb_plan.h"
#include "espn_parse.h"

namespace espn {

// Whether one of this side's players went from two goals to three between
// the two reads. ESPN's goal leaders for the game carry each player's count
// (sport_hockey.cpp keeps the ones on two or more), so a panel that starts
// mid-game, or misses the read with the goal in it, can miss a hat trick but
// never shows one for a player it didn't see on two.
inline bool celeb_hat_trick(const GameSnapshot &prev, const GameSnapshot &cur, bool ours) {
  const Hockey::Scorer *now = ours ? cur.nhl.team_scorers : cur.nhl.opp_scorers;
  const Hockey::Scorer *was = ours ? prev.nhl.team_scorers : prev.nhl.opp_scorers;
  const uint8_t n_now = ours ? cur.nhl.team_scorer_count : cur.nhl.opp_scorer_count;
  const uint8_t n_was = ours ? prev.nhl.team_scorer_count : prev.nhl.opp_scorer_count;
  for (uint8_t i = 0; i < n_now; i++) {
    if (now[i].goals != 3)
      continue;
    for (uint8_t j = 0; j < n_was; j++)
      if (was[j].id == now[i].id && was[j].goals == 2)
        return true;
  }
  return false;
}

// The big play in this read, or kind NONE. decide_celebration calls it only
// on Full with Big plays on, for two reads of the same game in progress.
// Ours, or either side's in the neutral modes (theirs with Opponent scores
// too on). It takes over the goal's lamp when the third goal and the count
// arrive in the same read, and plays on its own when ESPN's count catches
// up a read later.
inline CelebPlan big_play_hockey(const GameSnapshot &prev, const GameSnapshot &cur, const Splash &splash,
                                 bool neutral, bool opp_splashes) {
  (void) splash;  // a goal's splash is the lamp this takes over
  CelebPlan none;
  if (cur.period >= 5 && !cur.nhl.playoffs)
    return none;  // the shootout: its goals aren't goals
  if (cur.team_score < prev.team_score || cur.opp_score < prev.opp_score)
    return none;  // a goal taken back
  bool ours;
  if (celeb_hat_trick(prev, cur, true))
    ours = true;
  else if (neutral && opp_splashes && celeb_hat_trick(prev, cur, false))
    ours = false;
  else
    return none;
  CelebPlan p;
  p.kind = CelebKind::HAT_TRICK;
  p.text = neutral ? (ours ? cur.team_abbr : cur.opp_abbr) + " HAT TRICK!" : "HAT TRICK!";
  p.duration_ms = kCelebHatTrickMs;
  p.color = celeb_bright(parse_color(ours ? cur.team_color : cur.opp_color));
  p.alt = celeb_alt_for(p.color);
  return p;
}

}  // namespace espn
