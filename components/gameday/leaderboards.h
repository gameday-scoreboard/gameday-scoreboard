// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The leaderboards setting: the leaderboard leagues (leagues.h
// leaderboard_sport) the leaderboard cards show between games without a
// followed athlete, a bit per League value. As text, one sport per part:
//   golf:pga,lpga         a save from the page: golf's tours, other sports as they are
//   golf:                 golf off
//   golf:pga;racing:      a backup or the state, every sport written out
// A sport or league this firmware doesn't have is left out and listed, so a
// backup from newer firmware still restores. Pure, no ESPHome includes, so
// the host tests cover it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "leagues.h"

namespace espn {

static_assert(kLeagueCount <= 32, "the leaderboards mask holds a bit per League value");

constexpr uint32_t league_bit32(League l) { return 1u << (uint8_t) l; }

// The leaderboard leagues of sport s, a bit each.
inline uint32_t leaderboard_sport_mask(Sport s) {
  uint32_t mask = 0;
  for (const auto &l : kLeagues)
    if (l.sport == s && leaderboard_sport(s))
      mask |= league_bit32(l.league);
  return mask;
}

// Every leaderboard league this firmware has.
inline uint32_t leaderboard_mask_all() {
  uint32_t mask = 0;
  for (int s = 0; s < kSportCount; s++)
    mask |= leaderboard_sport_mask((Sport) s);
  return mask;
}

// The name the device page and the apps show on a leaderboard league's chip.
inline const char *leaderboard_name(League l) {
  switch (l) {
    case League::PGA:
      return "PGA Tour";
    case League::LPGA:
      return "LPGA";
    case League::EUR:
      return "DP World Tour";
    default: {
      const LeagueInfo *info = league_info(l);
      return info != nullptr ? info->prefix : "";
    }
  }
}

struct LeaderboardsText {
  uint32_t on{0};     // the leagues picked
  uint32_t named{0};  // every league of the sports the text names: the ones it sets
  std::vector<std::string> skipped;  // "racing", "golf:nfl": a sport or league this firmware doesn't have
};

// False when the text names no leaderboard sport this firmware has, so
// nothing changes. A sport named twice takes its last part.
inline bool parse_leaderboards(const std::string &text, LeaderboardsText &out) {
  out = LeaderboardsText{};
  size_t start = 0;
  while (start < text.size()) {
    size_t semi = text.find(';', start);
    std::string part = text.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
    start = semi == std::string::npos ? text.size() : semi + 1;
    if (part.empty())
      continue;
    size_t colon = part.find(':');
    std::string sport = part.substr(0, colon);
    int s = -1;
    for (int k = 0; k < kSportCount; k++)
      if (sport == kSportKeys[k] && leaderboard_sport((Sport) k))
        s = k;
    if (colon == std::string::npos || s < 0) {
      out.skipped.push_back(sport);
      continue;
    }
    const uint32_t mask = leaderboard_sport_mask((Sport) s);
    out.named |= mask;
    out.on &= ~mask;
    size_t k = colon + 1;
    while (k < part.size()) {
      size_t comma = part.find(',', k);
      std::string key = part.substr(k, comma == std::string::npos ? std::string::npos : comma - k);
      k = comma == std::string::npos ? part.size() : comma + 1;
      if (key.empty())
        continue;
      const LeagueInfo *info = league_by_key(key.c_str());
      if (info != nullptr && (league_bit32(info->league) & mask))
        out.on |= league_bit32(info->league);
      else
        out.skipped.push_back(sport + ":" + key);
    }
  }
  return out.named != 0;
}

// The saved mask after a parsed text: the sports it names take its leagues,
// the others keep theirs.
inline uint32_t leaderboards_apply(uint32_t mask, const LeaderboardsText &t) {
  return (mask & ~t.named) | (t.on & t.named);
}

// "golf:pga,lpga": every leaderboard sport in Sport order, its leagues in
// kLeagues order, a sport with none written as "golf:".
inline std::string leaderboards_text(uint32_t mask) {
  std::string out;
  for (int s = 0; s < kSportCount; s++) {
    if (!leaderboard_sport((Sport) s))
      continue;
    if (!out.empty())
      out += ';';
    out += kSportKeys[s];
    out += ':';
    bool first = true;
    for (const auto &l : kLeagues)
      if (l.sport == (Sport) s && (mask & league_bit32(l.league))) {
        if (!first)
          out += ',';
        out += l.key;
        first = false;
      }
  }
  return out;
}

}  // namespace espn
