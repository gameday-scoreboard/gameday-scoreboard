// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Which celebration a score or a big play gets. Pure, no ESPHome includes:
// the host tests run decide_celebration() over every sport and both sides.
// The kinds and the owner's setting are celeb_plan.h, each sport's big plays
// celeb_<sport>.h, the animations celebrate_fx.h.
#pragma once
#include <cstdint>
#include <string>

#include "celeb_baseball.h"
#include "celeb_basketball.h"
#include "celeb_football.h"
#include "celeb_hockey.h"
#include "celeb_plan.h"
#include "celeb_soccer.h"
#include "espn_parse.h"

namespace espn {

// Whether a basketball read's last play was a made three by this side.
// ESPN's scoreValue is 3 only for a made three (a miss is 0), and its team
// says whose play it was; score_log.h reads the same pair. Only a document
// with no scoreValue falls back to the words, and then a make only: ESPN
// writes "misses 26-foot three point jumper" and "missed Three Point Jumper"
// with the same words as a make.
inline bool celeb_three_play(const GameSnapshot &cur, bool ours) {
  const uint32_t side = ours ? cur.team_id : cur.opp_id;
  if (cur.play_team != 0 && cur.play_team != side)
    return false;
  if (cur.play_value > 0)
    return cur.play_value == 3;
  auto has = celeb_has_word;
  const bool three = has(cur.last_play, "three point") || has(cur.last_play, "three-point") ||
                     has(cur.last_play, "3-pt") || has(cur.play_type, "three point") ||
                     has(cur.play_type, "three-point");
  const bool made = (has(cur.last_play, "make") || has(cur.last_play, "made")) && !has(cur.last_play, "miss");
  return three && made;
}

// Who won, the way each sport's splash decides it: more points, or in soccer
// a shootout (shootout_decides).
inline bool celeb_team_won(const GameSnapshot &cur) {
  if (shootout_decides(cur))
    return cur.soc.team_shootout > cur.soc.opp_shootout;
  return cur.team_score > cur.opp_score;
}

// The win's second line: the winner's points first, and a shootout in
// brackets after the score, "1-1 (4-3)", or "1-2 (4-3)" after a second leg.
inline std::string celeb_final_line(const GameSnapshot &cur, bool team_won) {
  int a = team_won ? cur.team_score : cur.opp_score, b = team_won ? cur.opp_score : cur.team_score;
  std::string line = std::to_string(a) + "-" + std::to_string(b);
  if (shootout_decides(cur)) {
    int sa = team_won ? cur.soc.team_shootout : cur.soc.opp_shootout;
    int sb = team_won ? cur.soc.opp_shootout : cur.soc.team_shootout;
    line += " (" + std::to_string(sa) + "-" + std::to_string(sb) + ")";
  }
  return line;
}

// The celebration for this poll. splash is decide_splash's answer for it,
// which already knows about Opponent scores too and the neutral modes; the
// one score decide_splash stays quiet on, a basketball basket, is read here:
// a three-point shot (the last play says so) that is the only change in the
// read. Opponent scores (not neutral) are always MUTED when they show at all,
// and opp_splashes is the Opponent scores too switch, which the other side's
// three obeys as its other scores do.
//
// big_plays is the Big plays switch: on Full, each sport's own big plays
// (celeb_<sport>.h) come first and take over the splash they land on. Off,
// every score and win keeps its show and only the big plays go.
inline CelebPlan decide_celebration(const GameSnapshot &prev, const GameSnapshot &cur, const Splash &splash,
                                    CelebLevel level, bool neutral, bool opp_splashes = true,
                                    bool big_plays = true) {
  CelebPlan none;
  if (level == CelebLevel::OFF)
    return none;
  const bool same = celeb_same_game(prev, cur);
  const int delta = same ? cur.team_score - prev.team_score : 0;
  const int odelta = same ? cur.opp_score - prev.opp_score : 0;
  const bool live = same && prev.state == GameState::IN && cur.state == GameState::IN;

  if (level == CelebLevel::FULL && big_plays && live) {
    CelebPlan big;
    switch (cur.sport) {
      case Sport::BASEBALL:
        big = big_play_baseball(prev, cur, splash, neutral, opp_splashes);
        break;
      case Sport::BASKETBALL:
        big = big_play_basketball(prev, cur, splash, neutral, opp_splashes);
        break;
      case Sport::FOOTBALL:
        big = big_play_football(prev, cur, splash, neutral, opp_splashes);
        break;
      case Sport::HOCKEY:
        big = big_play_hockey(prev, cur, splash, neutral, opp_splashes);
        break;
      case Sport::SOCCER:
        big = big_play_soccer(prev, cur, splash, neutral, opp_splashes);
        break;
      default:
        break;
    }
    if (big.kind != CelebKind::NONE)
      return big;
  }

  if (splash.text.empty()) {
    // Basketball's three. decide_splash leaves baskets alone; Full celebrates
    // a three-point shot read on its own for our side (either side when neutral).
    if (level != CelebLevel::FULL || !live || cur.sport != Sport::BASKETBALL)
      return none;
    bool ours = delta == 3 && odelta == 0;
    bool theirs = neutral && opp_splashes && odelta == 3 && delta == 0;
    if (!ours && !theirs)
      return none;
    // The three points have to be the last play, a made three by that side:
    // an and-one or a two plus a free throw ending on a missed three is not.
    if (!celeb_three_play(cur, ours))
      return none;
    CelebPlan p;
    p.kind = CelebKind::THREE;
    p.color = celeb_bright(parse_color(ours ? cur.team_color : cur.opp_color));
    p.alt = celeb_alt_for(p.color);
    p.text = neutral ? (ours ? cur.team_abbr : cur.opp_abbr) + " THREE!" : "THREE!";
    p.duration_ms = kCelebThreeMs;
    return p;
  }

  const bool win = splash.text.find("WINS") != std::string::npos;
  // Whose splash: a win is the winner's (the shootout decides a level soccer
  // final, as soccer_splash does); a score is the side whose points moved
  // (both can move in one read; the splash picked ours).
  bool ours;
  if (win)
    ours = celeb_team_won(cur);
  else
    ours = delta > 0 || odelta <= 0;
  CelebPlan p;
  p.text = splash.text;
  p.color = celeb_bright(splash.color);
  p.alt = celeb_alt_for(p.color);
  // A splash that is not a score (UPSET ALERT) gets the plain flash, never a
  // score's show, whatever moved in the same read.
  if (!win && splash.text.find("UPSET") != std::string::npos) {
    p.kind = CelebKind::FLASH;
    p.duration_ms = kCelebFlashMs;
    return p;
  }

  if (!ours && !neutral) {
    p.kind = CelebKind::MUTED;
    p.color = celeb_dim(p.color);
    p.duration_ms = kCelebMutedMs;
    return p;
  }
  if (level == CelebLevel::SUBTLE) {
    p.kind = CelebKind::FLASH;
    p.duration_ms = kCelebFlashMs;
    return p;
  }
  if (win) {
    p.kind = CelebKind::WIN;
    p.line2 = celeb_final_line(cur, ours);
    p.duration_ms = kCelebWinMs;
    return p;
  }
  const int pts = ours ? delta : odelta;
  switch (cur.sport) {
    case Sport::FOOTBALL:
      if (pts >= 6 && pts <= 8) {
        p.kind = CelebKind::TOUCHDOWN;
        p.duration_ms = kCelebTouchdownMs;
      } else {
        p.kind = CelebKind::WAVE;
        p.duration_ms = kCelebWaveMs;
      }
      return p;
    case Sport::BASEBALL:
      if (splash.text.find("HOME RUN") != std::string::npos) {
        p.kind = CelebKind::HOME_RUN;
        p.duration_ms = kCelebHomeRunMs;
      } else {
        p.kind = CelebKind::WAVE;
        p.duration_ms = kCelebWaveMs;
      }
      return p;
    case Sport::SOCCER:
      p.kind = CelebKind::GOAL_NET;
      p.duration_ms = kCelebGoalMs;
      return p;
    case Sport::HOCKEY:
      p.kind = CelebKind::GOAL_LAMP;
      p.duration_ms = kCelebGoalMs;
      return p;
    case Sport::BASKETBALL:
    default:
      // Nothing but the win splashes here; a stray text gets the flash.
      p.kind = CelebKind::FLASH;
      p.duration_ms = kCelebFlashMs;
      return p;
  }
}

// The three and the big plays are read from the scores and the last play,
// not from a poll's arrival, and the component redraws the board for many
// reasons between polls (a failed poll's back-off, a setting change, a card
// coming or going). This remembers the read one was shown for, so the same
// (game, scores, last play) never shows twice; a stale ESPN read that steps
// back and forward again is the same read and stays quiet too. The play's
// words are in the key for the plays that score nothing (a sack): a new one
// at the same score is a new play.
struct CelebLatch {
  std::string event, play;
  int team{-1}, opp{-1};
  // True the first time this read of this game asks; false after that.
  bool take(const GameSnapshot &cur) {
    if (cur.event_id == event && cur.team_score == team && cur.opp_score == opp && cur.last_play == play)
      return false;
    event = cur.event_id;
    team = cur.team_score;
    opp = cur.opp_score;
    play = cur.last_play;
    return true;
  }
};
// Which kinds the latch guards: the ones read from a play, not a splash.
inline bool celeb_latched(CelebKind k) { return k == CelebKind::THREE || celeb_is_art(k); }

// Where the words go, so the animation leaves them room. The page uses the
// same answer for its label; the host renderer for its own text.
struct CelebTextPlace {
  int y;         // top of the first line
  int font;      // 16, 12 or 8: the Spleen size
  int line2_y;   // top of the second line (the win), -1 when none
};
inline CelebTextPlace celeb_text_place(CelebKind kind, int w, int h) {
  const bool wide = w >= 128;
  CelebTextPlace t{0, wide ? 16 : 12, -1};
  const int lh = t.font;  // Spleen's line height equals its size
  switch (kind) {
    case CelebKind::WIN:
      t.y = h / 2 - lh - 1;
      t.line2_y = h / 2 + 1;
      break;
    case CelebKind::TOUCHDOWN:
      t.y = wide ? 10 : 8;  // the goalposts stand under it
      break;
    case CelebKind::HOME_RUN:
      t.y = wide ? 4 : 2;  // the ball peaks under it and leaves high on the right
      break;
    case CelebKind::THREE:
      t.y = h - lh - (wide ? 14 : 10);  // the ball flies over it to the hoop
      break;
    default:
      if (celeb_is_art(kind)) {
        t.y = h + 4;  // the art draws its own title; the words sit off the board
        t.line2_y = -1;
        break;
      }
      t.y = (h - lh) / 2;
      break;
  }
  return t;
}

}  // namespace espn
