// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// What an effect looks like on a panel with a team, and the holiday effects.
//
// Team colors: wled_fx has WLED's three color slots, and WLED's "* "
// palettes are built from them. The panel loads the team's color into slot
// 1 and a second color into slot 2 (the opponent's during a live game, else
// white, or gold for a near-white team), and starts on "* Colors Only", so
// an effect draws in the team's own colors and nothing in between. Effects
// that name slot 2 as their background (Fireworks, Rain, Halloween Eyes,
// Snow Fall and others) keep it black, or the whole screen would be the
// second color; their second color goes in slot 3, where "* Colors Only"
// still finds it. The palette is only the default: an owner who picks one
// keeps it, as before.
//
// Holiday effects: on a day in kHolidays, the effect After the game puts up
// between games is the holiday's (Holiday effects, on by default, and only
// inside After the game: Effects, which is off out of the box). The owner's
// effect, palette, speed and intensity come back when the day or the
// stretch between games ends; picking one by hand during it ends the
// holiday until the next stretch.
//
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>

namespace espn {

// The palette a panel with a team starts its effects on.
inline constexpr const char *kFxTeamPalette = "* Colors Only";

// A dark color vanishes on the panel and black would not draw at all: lift
// the brightest channel to 0xC0, as the clock does with the team's color
// (offseason.h clock_color).
inline uint32_t fx_lift(uint32_t rgb) {
  uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  uint32_t m = r > g ? (r > b ? r : b) : (g > b ? g : b);
  if (m == 0)
    return 0xFFFFFF;
  if (m < 0xC0) {
    r = r * 0xC0 / m;
    g = g * 0xC0 / m;
    b = b * 0xC0 / m;
  }
  return (r << 16) | (g << 8) | b;
}

// White next to the team's color, or gold when that color is itself near
// white (celeb_plan.h celeb_alt_for does the same for a celebration).
inline uint32_t fx_second(uint32_t team) {
  uint32_t r = (team >> 16) & 0xFF, g = (team >> 8) & 0xFF, b = team & 0xFF;
  return (r > 0xC0 && g > 0xC0 && b > 0xC0) ? 0xFFD000 : 0xFFFFFF;
}

struct FxSlots {
  uint32_t c[3]{0, 0, 0};
  bool operator==(const FxSlots &o) const { return c[0] == o.c[0] && c[1] == o.c[1] && c[2] == o.c[2]; }
  bool operator!=(const FxSlots &o) const { return !(*this == o); }
};

// What wled_fx starts its slots on (wf_segment.h): orange, then black, black.
inline FxSlots fx_wled_slots() {
  FxSlots s;
  s.c[0] = 0xFFA000;
  return s;
}

// The slots for a team: its color, and the opponent's while live is true.
// bg: the running effect names slot 2 as its background.
inline FxSlots fx_team_slots(uint32_t team, bool live, uint32_t opp, bool bg) {
  const uint32_t one = fx_lift(team);
  uint32_t two = live ? fx_lift(opp) : fx_second(one);
  // Two sides in the same color (or a lift that made them so) would be one
  // color: the second goes to white or gold instead.
  if (two == one)
    two = fx_second(one);
  FxSlots s;
  s.c[0] = one;
  s.c[bg ? 2 : 1] = two;
  return s;
}

// What the gameday component reaches wled_fx through, which only the
// effects package has (pages/effects.yaml sets these at boot). Unset, the
// panel leaves the effects' colors and palette alone.
struct FxHooks {
  // Loads the three color slots.
  void (*slots)(const uint32_t c[3]){nullptr};
  // The running effect names slot 2 as its background.
  bool (*bg_slot)(){nullptr};
  // Lets go of the palette the panel picked, so the effect's own comes back.
  void (*palette_unpin)(){nullptr};
};
inline FxHooks &fx_hooks() {
  static FxHooks h;
  return h;
}

// ---- Holidays ------------------------------------------------------------------

struct Holiday {
  uint8_t month;  // 1..12
  uint8_t first;  // day of the month, the first day it runs
  uint8_t last;   // and the last
  const char *name;
  const char *effect;   // in the Effect select's spelling (pages/effects.yaml)
  const char *palette;  // in the Effect Palette select's spelling
  uint8_t speed;
  uint8_t intensity;
  uint32_t c[3];  // the color slots
};

// PS Fireworks and PS Ghost Rider draw from the palette only, so their slot
// 2 can be a color; Snow Fall draws its snow in white over slot 2 as the
// background. Halloween Eyes was the obvious pick, but its eyes are two
// pixels each on a 64-wide panel and off 16 to 32 s at a time, so the
// screen sat dark. PS Fireworks at 200 and 120 bursts often enough to read
// as fireworks on a 64x64 panel (its defaults, 128 and 50, mostly show the
// dark sky).
inline constexpr Holiday kHolidays[] = {
    {7, 4, 4, "July 4", "PS Fireworks", "* Colors Only", 200, 120, {0xFF0000, 0xFFFFFF, 0x0030FF}},
    {10, 31, 31, "Halloween", "PS Ghost Rider", "* Colors Only", 70, 0, {0xFF5000, 0x8000FF, 0x000000}},
    {12, 24, 26, "Christmas", "Snow Fall", "Default", 128, 128, {0xFFFFFF, 0x000000, 0x000000}},
};
inline constexpr int kHolidayCount = (int) (sizeof(kHolidays) / sizeof(kHolidays[0]));

// The holiday on this local date, -1 for none (or no date: month 0).
inline int holiday_on(int month, int day) {
  for (int i = 0; i < kHolidayCount; i++)
    if (kHolidays[i].month == month && day >= kHolidays[i].first && day <= kHolidays[i].last)
      return i;
  return -1;
}

// What the holiday effect does, ticked with the effect between games.
// up: the holiday on the screen now, -1 for none. dropped: somebody picked
// an effect setting by hand during this stretch between games, so no
// holiday until the next.
struct HolState {
  int8_t up{-1};
  bool dropped{false};
};

enum class HolStep : uint8_t {
  NONE,
  START,   // save the owner's effect settings, then put holiday `to` up
  SWITCH,  // another holiday runs into this one: put `to` up, keep the saved settings
  END,     // put the owner's saved settings back
};

// enabled: the Holiday effects switch. ours_up: After the game has its
// Effects page up between games. today: holiday_on() for today's date.
inline HolStep holiday_step(HolState &s, bool enabled, bool ours_up, int today, int &to) {
  to = -1;
  if (!ours_up) {
    s.dropped = false;
    if (s.up < 0)
      return HolStep::NONE;
    s.up = -1;
    return HolStep::END;
  }
  if (s.dropped)
    return HolStep::NONE;
  const int want = enabled && today >= 0 && today < kHolidayCount ? today : -1;
  if (want == s.up)
    return HolStep::NONE;
  const int8_t was = s.up;
  s.up = (int8_t) want;
  if (want < 0)
    return HolStep::END;
  to = want;
  return was < 0 ? HolStep::START : HolStep::SWITCH;
}

// Somebody picked an effect, a palette, a speed or an intensity while a
// holiday was up: theirs stays, and nothing is put back. True when a
// holiday was up.
inline bool holiday_hand_pick(HolState &s) {
  if (s.up < 0)
    return false;
  s.up = -1;
  s.dropped = true;
  return true;
}

}  // namespace espn
