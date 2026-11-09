// Basketball's big plays: a dunk and an alley-oop, read from the last play
// the way the three is (celebrate.h). Pure, no ESPHome includes; the host
// tests run it on recorded ESPN plays.
#pragma once
#include <string>

#include "celeb_plan.h"
#include "espn_parse.h"

namespace espn {

// Whether the read's last play was a made two of this kind by this side.
// ESPN's scoreValue is 2 for a made two and 0 for a miss, and its team says
// whose play it was. Only a document with no scoreValue falls back to the
// words, and then a make only ("makes driving dunk", never "misses").
inline bool celeb_two_play(const GameSnapshot &cur, bool ours, const char *kind) {
  const uint32_t side = ours ? cur.team_id : cur.opp_id;
  if (cur.play_team != 0 && cur.play_team != side)
    return false;
  const bool named = celeb_has_word(cur.play_type, kind) || (cur.play_type.empty() && celeb_has_word(cur.last_play, kind));
  if (!named)
    return false;
  if (cur.play_value > 0)
    return cur.play_value == 2;
  return (celeb_has_word(cur.last_play, "make") || celeb_has_word(cur.last_play, "made")) &&
         !celeb_has_word(cur.last_play, "miss");
}

// The big play in this read, or kind NONE. decide_celebration calls it only
// on Full with Big plays on, for two reads of the same game in progress.
// Like the three: the two points have to be the only change in the read,
// ours, or either side's in the neutral modes (theirs with Opponent scores
// too on), and the last play has to be that basket. An and-one's free throw
// makes it three points and its own last play, so it stays quiet.
inline CelebPlan big_play_basketball(const GameSnapshot &prev, const GameSnapshot &cur, const Splash &splash,
                                     bool neutral, bool opp_splashes) {
  CelebPlan none;
  if (!splash.text.empty())
    return none;  // a win's splash is never a dunk's
  const int delta = cur.team_score - prev.team_score, odelta = cur.opp_score - prev.opp_score;
  const bool ours = delta == 2 && odelta == 0;
  const bool theirs = neutral && opp_splashes && odelta == 2 && delta == 0;
  if (!ours && !theirs)
    return none;
  CelebPlan p;
  // An alley-oop dunk is an alley-oop first: "Alley Oop Dunk Shot".
  if (celeb_two_play(cur, ours, "alley oop")) {
    p.kind = CelebKind::ALLEY_OOP;
    p.text = "ALLEY-OOP!";
    p.duration_ms = kCelebAlleyOopMs;
  } else if (celeb_two_play(cur, ours, "dunk")) {
    p.kind = CelebKind::DUNK;
    p.text = "DUNK!";
    p.duration_ms = kCelebDunkMs;
  } else {
    return none;
  }
  if (neutral)
    p.text = (ours ? cur.team_abbr : cur.opp_abbr) + " " + p.text;
  p.color = celeb_bright(parse_color(ours ? cur.team_color : cur.opp_color));
  p.alt = celeb_alt_for(p.color);
  return p;
}

}  // namespace espn
