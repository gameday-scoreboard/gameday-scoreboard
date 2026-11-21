// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// What Showing (the page select) has up, for what the panel reads and tells
// Home Assistant. On the Scoreboard the panel follows its games. On a page
// somebody picked (Clock, Effects, a Smart board picked under Showing) it
// reads nothing from ESPN and sends Home Assistant nothing about games until
// the Scoreboard is back. A page After the game put up between games keeps
// the reads that bring the scoreboard back for the next game, and still
// sends Home Assistant nothing.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace espn {

enum class Showing : uint8_t { BOARD, AUTO, PICKED };

// page is the select's option ("Scoreboard", "Clock", "Effects", "Smart
// board"); null or empty before the page manager has published one, which
// counts as the Scoreboard. fx_ours: the panel put the Effects page up for
// After the game. sb_pinned: somebody picked the Smart board under Showing.
inline Showing showing_now(const char *page, bool fx_ours, bool sb_pinned) {
  if (page == nullptr || *page == '\0' || strcmp(page, "Scoreboard") == 0)
    return Showing::BOARD;
  if (strcmp(page, "Effects") == 0 && fx_ours)
    return Showing::AUTO;
  if (strcmp(page, "Smart board") == 0 && !sb_pinned)
    return Showing::AUTO;
  return Showing::PICKED;
}

inline const char *showing_word(Showing s) {
  return s == Showing::BOARD ? "the Scoreboard" : s == Showing::AUTO ? "a page After the game put up" : "a picked page";
}

// The second line under "Looking for" while the first scoreboard loads: at
// start-up, after a team change, and back on the Scoreboard from a picked
// page. live_word is the Live games league ("NFL ", "college ", "" for
// several); team is the one favorite the board can show, or null.
inline std::string looking_for(bool live_mode, const std::string &live_word, const char *team) {
  if (live_mode)
    return "live " + live_word + "games";
  if (team != nullptr && *team != '\0')
    return team;
  return "your teams";
}

}  // namespace espn
