// Which frames each big play draws, at the board's width. Every play has a
// set drawn at 128x64 (two panels) and one at 64x64 (one panel); the
// single-panel build can change width at runtime, so it carries both.
#pragma once

#include "celeb_art.h"
#include "celeb_art_alley_oop_128.h"
#include "celeb_art_alley_oop_64.h"
#include "celeb_art_brace_128.h"
#include "celeb_art_brace_64.h"
#include "celeb_art_dunk_128.h"
#include "celeb_art_dunk_64.h"
#include "celeb_art_hat_trick_128.h"
#include "celeb_art_hat_trick_64.h"
#include "celeb_art_late_goal_128.h"
#include "celeb_art_late_goal_64.h"
#include "celeb_art_penalty_128.h"
#include "celeb_art_penalty_64.h"
#include "celeb_caption.h"
#include "celeb_plan.h"

namespace espn {

// The art for kind on a board w pixels wide, or nullptr for a kind drawn in
// code.
inline const CelebArt *celeb_art_for(CelebKind k, int w) {
  const bool wide = w >= 128;
  switch (k) {
    case CelebKind::DUNK:
      return wide ? &celeb_art_dunk_128() : &celeb_art_dunk_64();
    case CelebKind::ALLEY_OOP:
      return wide ? &celeb_art_alley_oop_128() : &celeb_art_alley_oop_64();
    case CelebKind::BRACE:
      return wide ? &celeb_art_brace_128() : &celeb_art_brace_64();
    case CelebKind::HAT_TRICK:
      return wide ? &celeb_art_hat_trick_128() : &celeb_art_hat_trick_64();
    case CelebKind::PENALTY:
      return wide ? &celeb_art_penalty_128() : &celeb_art_penalty_64();
    case CelebKind::LATE_GOAL:
      return wide ? &celeb_art_late_goal_128() : &celeb_art_late_goal_64();
    default:
      return nullptr;
  }
}

// Where the words that change from game to game (plan.line2: a scorer's
// name, a minute) go over kind's frames on a board w pixels wide, or nullptr
// for a play with none. The frames leave that space clear from that frame
// on; tools/celeb_art/soccer_frames.py's CAPTIONS is the same table.
inline const CelebCaption *celeb_caption_for(CelebKind k, int w) {
  const bool wide = w >= 128;
  // {top row, first frame, scale, blink}
  static constexpr CelebCaption kBrace128{22, 27, 2, false}, kBrace64{21, 27, 2, false};
  static constexpr CelebCaption kHatTrick128{22, 27, 2, false}, kHatTrick64{37, 27, 1, false};
  static constexpr CelebCaption kLate128{24, 27, 3, true}, kLate64{38, 27, 2, true};
  switch (k) {
    case CelebKind::BRACE:
      return wide ? &kBrace128 : &kBrace64;
    case CelebKind::HAT_TRICK:
      return wide ? &kHatTrick128 : &kHatTrick64;
    case CelebKind::LATE_GOAL:
      return wide ? &kLate128 : &kLate64;
    default:
      return nullptr;
  }
}

}  // namespace espn
