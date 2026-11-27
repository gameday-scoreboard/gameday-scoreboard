// Team colors in the effects and the holiday effects
// (components/gameday/fx_theme.h): the color slots, the date table and the
// holiday's start, switch, end and hand pick.
#include <cstdio>
#include <cstring>

#include "fx_theme.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define TCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

void test_lift() {
  TCHECK(fx_lift(0x000000) == 0xFFFFFF, "black draws white");
  TCHECK(fx_lift(0xE31837) == 0xE31837, "a bright color stays");
  // Navy 0x0B1C3A: blue 0x3A lifted to 0xC0, the rest in step.
  TCHECK(fx_lift(0x0B1C3A) == ((0x0B * 0xC0 / 0x3A) << 16 | (0x1C * 0xC0 / 0x3A) << 8 | 0xC0), "navy lifted");
  TCHECK(fx_second(0xE31837) == 0xFFFFFF, "white next to red");
  TCHECK(fx_second(0xF0F0F0) == 0xFFD000, "gold next to near white");
}

void test_slots() {
  // Between games: team, then white.
  FxSlots s = fx_team_slots(0xE31837, false, 0x002A5C, false);
  TCHECK(s.c[0] == 0xE31837 && s.c[1] == 0xFFFFFF && s.c[2] == 0, "between games %06X %06X %06X",
         (unsigned) s.c[0], (unsigned) s.c[1], (unsigned) s.c[2]);
  // Live: the opponent's color, lifted.
  s = fx_team_slots(0xE31837, true, 0x002A5C, false);
  TCHECK(s.c[0] == 0xE31837 && s.c[1] == fx_lift(0x002A5C) && s.c[2] == 0, "live has the opponent");
  // An effect with a background slot keeps it black.
  s = fx_team_slots(0xE31837, true, 0x002A5C, true);
  TCHECK(s.c[0] == 0xE31837 && s.c[1] == 0 && s.c[2] == fx_lift(0x002A5C), "background stays black");
  // Same colors on both sides: the second is white.
  s = fx_team_slots(0x002A5C, true, 0x002A5C, false);
  TCHECK(s.c[1] == 0xFFFFFF, "same color twice");
  // A black team draws white, and its second is then gold.
  s = fx_team_slots(0x000000, false, 0, false);
  TCHECK(s.c[0] == 0xFFFFFF && s.c[1] == 0xFFD000, "black team");
  TCHECK(fx_wled_slots().c[0] == 0xFFA000 && fx_wled_slots().c[1] == 0, "WLED's own");
}

void test_dates() {
  TCHECK(holiday_on(7, 4) >= 0 && strcmp(kHolidays[holiday_on(7, 4)].name, "July 4") == 0, "July 4");
  TCHECK(holiday_on(7, 3) < 0 && holiday_on(7, 5) < 0, "around July 4");
  TCHECK(holiday_on(10, 31) >= 0 && strcmp(kHolidays[holiday_on(10, 31)].effect, "PS Ghost Rider") == 0,
         "Halloween");
  TCHECK(holiday_on(10, 30) < 0 && holiday_on(11, 1) < 0, "around Halloween");
  for (int d = 24; d <= 26; d++)
    TCHECK(holiday_on(12, d) >= 0 && strcmp(kHolidays[holiday_on(12, d)].name, "Christmas") == 0, "Dec %d", d);
  TCHECK(holiday_on(12, 23) < 0 && holiday_on(12, 27) < 0, "around Christmas");
  TCHECK(holiday_on(0, 0) < 0, "no date");
  TCHECK(holiday_on(4, 31) < 0, "an ordinary day");
}

void test_steps() {
  const int xmas = holiday_on(12, 25), h = holiday_on(10, 31);
  HolState s;
  int to = 0;
  // No holiday, or the switch off, or the page not up: nothing.
  TCHECK(holiday_step(s, true, true, -1, to) == HolStep::NONE && to == -1, "no holiday");
  TCHECK(holiday_step(s, false, true, xmas, to) == HolStep::NONE, "switch off");
  TCHECK(holiday_step(s, true, false, xmas, to) == HolStep::NONE, "page down");
  // The page goes up on Christmas: start, once.
  TCHECK(holiday_step(s, true, true, xmas, to) == HolStep::START && to == xmas, "start");
  TCHECK(holiday_step(s, true, true, xmas, to) == HolStep::NONE, "once");
  // A made-up run from one holiday straight into another keeps the saved settings.
  TCHECK(holiday_step(s, true, true, h, to) == HolStep::SWITCH && to == h, "switch");
  // The day ends with the page up: the owner's settings come back.
  TCHECK(holiday_step(s, true, true, -1, to) == HolStep::END && s.up == -1, "day ends");
  TCHECK(holiday_step(s, true, true, -1, to) == HolStep::NONE, "ended once");
  // The switch turned off during it ends it too.
  TCHECK(holiday_step(s, true, true, xmas, to) == HolStep::START, "start again");
  TCHECK(holiday_step(s, false, true, xmas, to) == HolStep::END, "switch off ends it");
  // The page coming down ends it.
  TCHECK(holiday_step(s, true, true, xmas, to) == HolStep::START, "start for the page");
  TCHECK(holiday_step(s, true, false, xmas, to) == HolStep::END, "page down ends it");
  TCHECK(holiday_step(s, true, false, xmas, to) == HolStep::NONE, "page down once");
  // A hand pick: no restore, no holiday until the next stretch.
  TCHECK(!holiday_hand_pick(s), "no holiday up, not a holiday hand pick");
  TCHECK(holiday_step(s, true, true, xmas, to) == HolStep::START, "up for the pick");
  TCHECK(holiday_hand_pick(s) && s.up == -1 && s.dropped, "hand pick");
  TCHECK(holiday_step(s, true, true, xmas, to) == HolStep::NONE, "stays the owner's");
  TCHECK(holiday_step(s, true, false, xmas, to) == HolStep::NONE && !s.dropped, "stretch ends, nothing to put back");
  TCHECK(holiday_step(s, true, true, xmas, to) == HolStep::START, "next stretch has it again");
}

}  // namespace

int run_fx_theme_tests(int &total_checks) {
  test_lift();
  test_slots();
  test_dates();
  test_steps();
  total_checks += checks;
  return failures;
}
