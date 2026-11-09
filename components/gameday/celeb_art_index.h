// Which frames each big play draws, at the board's width. Every play has a
// set drawn at 128x64 (two panels) and one at 64x64 (one panel); the
// single-panel build can change width at runtime, so it carries both.
#pragma once

#include "celeb_art.h"
#include "celeb_art_alley_oop_128.h"
#include "celeb_art_alley_oop_64.h"
#include "celeb_art_dunk_128.h"
#include "celeb_art_dunk_64.h"
#include "celeb_art_fumble_128.h"
#include "celeb_art_fumble_64.h"
#include "celeb_art_hattrick_128.h"
#include "celeb_art_hattrick_64.h"
#include "celeb_art_grand_slam_128.h"
#include "celeb_art_grand_slam_64.h"
#include "celeb_art_interception_128.h"
#include "celeb_art_interception_64.h"
#include "celeb_art_pick_six_128.h"
#include "celeb_art_pick_six_64.h"
#include "celeb_art_sack_128.h"
#include "celeb_art_sack_64.h"
#include "celeb_art_walk_off_128.h"
#include "celeb_art_walk_off_64.h"
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
    case CelebKind::INTERCEPTION:
      return wide ? &celeb_art_interception_128() : &celeb_art_interception_64();
    case CelebKind::PICK_SIX:
      return wide ? &celeb_art_pick_six_128() : &celeb_art_pick_six_64();
    case CelebKind::FUMBLE:
      return wide ? &celeb_art_fumble_128() : &celeb_art_fumble_64();
    case CelebKind::SACK:
      return wide ? &celeb_art_sack_128() : &celeb_art_sack_64();
    case CelebKind::HAT_TRICK:
      return wide ? &celeb_art_hattrick_128() : &celeb_art_hattrick_64();
    case CelebKind::GRAND_SLAM:
      return wide ? &celeb_art_grand_slam_128() : &celeb_art_grand_slam_64();
    case CelebKind::WALK_OFF:
      return wide ? &celeb_art_walk_off_128() : &celeb_art_walk_off_64();
    default:
      return nullptr;
  }
}

}  // namespace espn
