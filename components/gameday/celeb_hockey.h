// Hockey's big plays: a hat trick, a shorthanded goal and a power play goal.
// Pure, no ESPHome includes; the host tests run it on recorded ESPN reads.
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
// too on). A hat trick goes first: a shorthanded third goal is a hat trick.
// The strength needs the goal itself as the read's last play, one goal in
// the read, by that side; ESPN moves the last play on to the faceoff within
// a minute, so a read that lands after it shows the goal lamp as before.
inline CelebPlan big_play_hockey(const GameSnapshot &prev, const GameSnapshot &cur, const Splash &splash,
                                 bool neutral, bool opp_splashes) {
  (void) splash;  // a goal's splash is the lamp these take over
  CelebPlan none;
  if (cur.period >= 5 && !cur.nhl.playoffs)
    return none;  // the shootout: its goals aren't goals
  const int delta = cur.team_score - prev.team_score, odelta = cur.opp_score - prev.opp_score;
  CelebPlan p;
  bool ours = true;
  if (delta >= 0 && odelta >= 0) {
    if (celeb_hat_trick(prev, cur, true)) {
      p.kind = CelebKind::HAT_TRICK;
    } else if (neutral && opp_splashes && celeb_hat_trick(prev, cur, false)) {
      p.kind = CelebKind::HAT_TRICK;
      ours = false;
    }
  }
  if (p.kind == CelebKind::NONE) {
    const bool mine = delta == 1 && odelta == 0;
    const bool theirs = neutral && opp_splashes && odelta == 1 && delta == 0;
    if (!mine && !theirs)
      return none;
    ours = mine;
    const uint32_t side = ours ? cur.team_id : cur.opp_id;
    if (cur.play_type != "Goal" || cur.play_team != side)
      return none;
    if (cur.nhl.goal_strength == "shorthanded")
      p.kind = CelebKind::SHORTHANDED;
    else if (cur.nhl.goal_strength == "power-play")
      p.kind = CelebKind::POWER_PLAY;
    else
      return none;
  }
  switch (p.kind) {
    case CelebKind::HAT_TRICK:
      p.text = "HAT TRICK!";
      p.duration_ms = kCelebHatTrickMs;
      break;
    case CelebKind::SHORTHANDED:
      p.text = "SHORTHANDED GOAL!";
      p.duration_ms = kCelebShorthandedMs;
      break;
    default:
      p.text = "POWER PLAY GOAL!";
      p.duration_ms = kCelebPowerPlayMs;
      break;
  }
  if (neutral)
    p.text = (ours ? cur.team_abbr : cur.opp_abbr) + " " + p.text;
  p.color = celeb_bright(parse_color(ours ? cur.team_color : cur.opp_color));
  p.alt = celeb_alt_for(p.color);
  return p;
}

}  // namespace espn
