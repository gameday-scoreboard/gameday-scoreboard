// Football's big plays: an interception (a pick-six bigger), a fumble the
// defense recovers, and a sack, read from the last play's type in the game
// read. Pure, no ESPHome includes; the host tests run it on recorded ESPN
// plays.
#pragma once
#include <string>

#include "celeb_plan.h"
#include "espn_parse.h"

namespace espn {

enum class FootballBigPlay : uint8_t { NONE, INTERCEPTION, PICK_SIX, FUMBLE, SACK };

// Which big play ESPN's play type names. The NFL and college write the same
// types: "Pass Interception Return", "Interception Return Touchdown",
// "Fumble Recovery (Opponent)", "Sack Opp Fumble Recovery", "Sack". College
// also writes a plain "Interception" for one caught in the end zone and
// downed there. Not big plays: "Fumble Recovery (Own)" and college's bare
// "Fumble" (the offense kept the ball), "Fumble Return Touchdown" (the
// touchdown's own show), "Safety" (the score's sweep).
inline FootballBigPlay football_big_play(const std::string &type) {
  if (celeb_has_word(type, "interception"))
    return celeb_has_word(type, "touchdown") ? FootballBigPlay::PICK_SIX : FootballBigPlay::INTERCEPTION;
  if (celeb_has_word(type, "fumble recovery (opponent)") || celeb_has_word(type, "sack opp fumble recovery"))
    return FootballBigPlay::FUMBLE;
  return celeb_has_word(type, "sack") ? FootballBigPlay::SACK : FootballBigPlay::NONE;
}

// The big play in this read, or kind NONE. decide_celebration calls it only
// on Full with Big plays on, for two reads of the same game in progress.
// Ours, or either side's in the neutral modes (theirs with Opponent scores
// too on), as the three is:
// - An interception or a fumble recovery is the defense's: the side that has
//   the ball after it (the read's possession). Nothing may score in the same
//   read.
// - A pick-six is the side whose score went up 6. It takes over the
//   touchdown's splash and plays the goalposts in its own frames.
// - A sack is the side without the ball: the play's team is the offense
//   (the quarterback's), or, when the read has no play team, the side that
//   doesn't have the ball.
// The emit_ latch keys on the play's text and the scores, so a redraw of the
// same read never plays it twice.
inline CelebPlan big_play_football(const GameSnapshot &prev, const GameSnapshot &cur, const Splash &splash,
                                   bool neutral, bool opp_splashes) {
  CelebPlan none;
  const FootballBigPlay play = football_big_play(cur.play_type);
  if (play == FootballBigPlay::NONE || cur.last_play.empty() || cur.last_play == prev.last_play)
    return none;
  const int delta = cur.team_score - prev.team_score, odelta = cur.opp_score - prev.opp_score;
  bool ours = false, theirs = false;
  if (play == FootballBigPlay::PICK_SIX) {
    ours = delta == 6 && odelta == 0;
    theirs = odelta == 6 && delta == 0;
  } else {
    if (!splash.text.empty() || delta != 0 || odelta != 0)
      return none;
    if (play == FootballBigPlay::SACK && cur.play_team != 0) {
      ours = cur.play_team == cur.opp_id;
      theirs = cur.play_team == cur.team_id;
    } else {
      // A sack leaves the ball with the offense; the turnovers hand it over.
      const int defense = play == FootballBigPlay::SACK ? (cur.possession == 2 ? 1 : cur.possession == 1 ? 2 : 0)
                                                        : cur.possession;
      ours = defense == 1;
      theirs = defense == 2;
    }
  }
  if (!ours && !(theirs && neutral && opp_splashes))
    return none;
  CelebPlan p;
  switch (play) {
    case FootballBigPlay::INTERCEPTION:
      p.kind = CelebKind::INTERCEPTION;
      p.text = "INTERCEPTION!";
      p.duration_ms = kCelebInterceptionMs;
      break;
    case FootballBigPlay::PICK_SIX:
      p.kind = CelebKind::PICK_SIX;
      p.text = "PICK SIX!";
      p.duration_ms = kCelebPickSixMs;
      break;
    case FootballBigPlay::FUMBLE:
      p.kind = CelebKind::FUMBLE;
      p.text = "FUMBLE!";
      p.duration_ms = kCelebFumbleMs;
      break;
    default:
      p.kind = CelebKind::SACK;
      p.text = "SACK!";
      p.duration_ms = kCelebSackMs;
      break;
  }
  if (neutral)
    p.text = (ours ? cur.team_abbr : cur.opp_abbr) + " " + p.text;
  p.color = celeb_bright(parse_color(ours ? cur.team_color : cur.opp_color));
  p.alt = celeb_alt_for(p.color);
  return p;
}

}  // namespace espn
