// The demo build's scripted game, its made-up teams and how it picks a
// matchup. Only demo.cpp includes this, and only in the demo build
// (firmware/gameday-demo.yaml), so release firmware carries none of it.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstddef>
#include <cstdint>

#include "badge.h"
#include "espn_parse.h"
#include "team_colors.h"
#include "teams.h"

namespace espn {

// What a demo plays. The numbers are the WizMote buttons that start them.
enum class DemoKind : uint8_t { NONE = 0, PRO = 1, COLLEGE = 2 };

struct DemoStep {
  GameState state;
  int us, them;
  const char *clock;
  int period;
  int possession;  // 1 us, 2 them
  const char *down;
  bool red_zone;
  const char *play;
  uint32_t hold_ms;
};

// A believable drive-by-drive script. Splashes come from decide_splash on the
// score deltas, so the panel celebrates exactly as it would in a real game.
// The home side wins: in a bar that is the bar's team.
constexpr DemoStep kDemoSteps[] = {
    {GameState::PRE, 0, 0, "", 0, 0, "", false, "", 3500},
    {GameState::IN, 0, 0, "15:00", 1, 1, "1st & 10", false, "Kickoff", 4000},
    {GameState::IN, 0, 0, "9:41", 1, 1, "3rd & 4", true, "Pass short left for 8 yards", 3500},
    {GameState::IN, 7, 0, "9:35", 1, 2, "", false, "Pass deep right for 21 yards, TOUCHDOWN", 5000},
    {GameState::IN, 7, 3, "2:10", 1, 1, "1st & 10", false, "38 yard field goal is GOOD", 4500},
    {GameState::IN, 7, 3, "11:22", 2, 2, "2nd & 7", false, "Rush up the middle for 3 yards", 3500},
    {GameState::IN, 7, 10, "6:48", 2, 1, "", false, "Pass deep middle for 44 yards, TOUCHDOWN", 5000},
    {GameState::IN, 14, 10, "1:03", 2, 2, "", false, "Rush right end for 12 yards, TOUCHDOWN", 5000},
    {GameState::IN, 14, 10, "0:00", 2, 0, "", false, "End of the 2nd quarter", 3500},
    {GameState::IN, 21, 10, "8:15", 4, 2, "", false, "Interception returned 31 yards, TOUCHDOWN", 5000},
    {GameState::IN, 24, 10, "2:00", 4, 2, "4th & 9", true, "27 yard field goal is GOOD", 4000},
    {GameState::POST, 24, 10, "", 4, 0, "", false, "", 6000},
};
constexpr size_t kDemoStepCount = sizeof(kDemoSteps) / sizeof(kDemoSteps[0]);

// The normal board shows this long between two runs of the script.
constexpr uint32_t kDemoGapMs = 10000;

// Invented teams for filming and for any footage that gets posted: no logos,
// and abbreviations no real team in teams.h uses (the host tests check).
// Each side's logo slot shows its abbreviation badge instead (badge.h): rgb
// is the fill and the splash color, alt the badge's ring.
struct MadeUpTeam {
  const char *abbr;
  const char *name;
  uint32_t rgb;
  uint32_t alt;
};
constexpr MadeUpTeam kMadeUpTeams[] = {
    {"HBR", "Harbor City Hawks", 0x1E5AA8, 0xF4F6F8},
    {"IRN", "Iron Valley Miners", 0xC8102E, 0x9AA3AD},
    {"SUM", "Summit State Peaks", 0x1F8A3B, 0xE9D27A},
    {"CPT", "Capitol Tech Comets", 0xF2A900, 0xFFFFFF},
};
constexpr size_t kMadeUpTeamCount = sizeof(kMadeUpTeams) / sizeof(kMadeUpTeams[0]);

// Home and away made-up teams for a demo: one pair for pro, one for college.
inline const MadeUpTeam &demo_made_up(DemoKind kind, bool home) {
  size_t base = kind == DemoKind::COLLEGE ? 2 : 0;
  return kMadeUpTeams[base + (home ? 0 : 1)];
}

inline League demo_league(DemoKind kind) { return kind == DemoKind::COLLEGE ? League::NCAA : League::NFL; }

inline const Team *find_team(League league, uint32_t espn_id) {
  for (size_t i = 0; i < kTeamCount; i++)
    if (kTeams[i].league == league && kTeams[i].espn_id == espn_id)
      return &kTeams[i];
  return nullptr;
}

// ESPN's primary color for a team in teams.h, white if it has none.
inline uint32_t team_rgb(League league, uint32_t espn_id) {
  for (size_t i = 0; i < kTeamColorCount; i++)
    if (kTeamColors[i].league == league && kTeamColors[i].espn_id == espn_id)
      return kTeamColors[i].rgb;
  return 0xFFFFFF;
}

// The stock matchups when nobody has picked one: Cowboys at Eagles, and
// Michigan at Ohio State.
constexpr uint32_t kDemoProHome = 6, kDemoProAway = 21;
constexpr uint32_t kDemoCollegeHome = 194, kDemoCollegeAway = 130;

// The two real teams a demo plays. A picked team wins. With no pick, the home
// side is the panel's own team when it plays in this league, else the stock
// home team, and the away side is the stock opponent. The two sides are
// never the same team.
struct DemoMatchup {
  const Team *home;
  const Team *away;
};
inline DemoMatchup demo_matchup(DemoKind kind, uint32_t picked_home, uint32_t picked_away, League panel_league,
                                uint32_t panel_team) {
  League lg = demo_league(kind);
  bool college = kind == DemoKind::COLLEGE;
  uint32_t stock_home = college ? kDemoCollegeHome : kDemoProHome;
  uint32_t stock_away = college ? kDemoCollegeAway : kDemoProAway;
  const Team *home = find_team(lg, picked_home);
  if (home == nullptr && panel_league == lg)
    home = find_team(lg, panel_team);
  if (home == nullptr)
    home = find_team(lg, stock_home);
  const Team *away = find_team(lg, picked_away);
  if (away == nullptr || away == home)
    away = find_team(lg, home->espn_id == stock_away ? stock_home : stock_away);
  return {home, away};
}

}  // namespace espn
