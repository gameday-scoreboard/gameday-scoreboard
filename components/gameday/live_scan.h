// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// A Live games scan reads one scoreboard per league, one after another:
// nine leagues take 9 to 12 s, and the board used to stay empty for all of
// it. These rules get a game up sooner: the leagues that had a game on at
// the last scan are read first, and a board with no game on it shows the
// first one the scan finds while the rest are read. Pure logic, no ESPHome
// includes, so the host tests cover it.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "espn_parse.h"
#include "leagues.h"

namespace espn {

inline uint16_t scan_league_bit(uint8_t league) { return league < 16 ? (uint16_t) (1u << league) : 0; }

// The leagues with a game in progress in a scan's list, a bit per league.
inline uint16_t scan_live_leagues(const LiveList &live) {
  uint16_t mask = 0;
  for (const auto &g : live)
    mask |= scan_league_bit(g.league);
  return mask;
}

// What the panel remembers after a scan: every league read answered
// (complete), so the list is the whole truth; a scan that showed a game
// early or lost a league only adds to what it knew.
inline uint16_t scan_live_after(uint16_t before, uint16_t found, bool complete) {
  return complete ? found : (uint16_t) (before | found);
}

// The order a scan reads its leagues in: those with a game on at the last
// scan (live_last) first, each group in the owner's own order.
inline std::vector<League> scan_order(std::vector<League> leagues, uint16_t live_last) {
  std::stable_partition(leagues.begin(), leagues.end(),
                        [live_last](League l) { return (live_last & scan_league_bit((uint8_t) l)) != 0; });
  return leagues;
}

// Whether this scan may show the first game it finds: only when the board
// isn't following a game in progress (nothing picked yet, the finals, or
// the owner's own team filling in). A board with a live game on it keeps
// that game until the whole scan is read, as before.
inline bool scan_may_show_early(bool following_game, bool nothing_live) { return !following_game || nothing_live; }

// Worker, after each league: stop here and let the board show a game while
// the rest are read. Never after the last league, which is the whole scan.
inline bool scan_show_now(bool may_show_early, size_t live_found, size_t leagues_left) {
  return may_show_early && live_found > 0 && leagues_left > 0;
}

// Where a game sits in the list, -1 when it isn't there.
inline int live_index(const LiveList &games, const std::string &event_id) {
  if (event_id.empty())
    return -1;
  for (size_t i = 0; i < games.size(); i++)
    if (games[i].event_id == event_id)
      return (int) i;
  return -1;
}

// The rest of a scan that already showed a game, with Pick the best game
// off: the game it showed stays (its index), since any other would be one
// more random pick and a switch on screen for nothing. -1 when that game is
// no longer in the list, or with the switch on (best_live_game decides, and
// moves only for a clearly better game).
inline int scan_keep_shown(const LiveList &games, const std::string &shown_event, bool rest_of_scan, bool best) {
  return rest_of_scan && !best ? live_index(games, shown_event) : -1;
}

}  // namespace espn
