// The multi-game board: several games on the panel at once as tiles. Which
// games get a tile and in what order, how tiles keep their places between
// polls, how many tiles the owner's setting and the display give, and the
// short status line on each tile. Pure, no ESPHome includes, so the host
// tests cover the picks and the stability rules.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "best_game.h"
#include "espn_parse.h"
#include "leagues.h"
#include "tiles.h"

namespace espn {

// One game that may get a tile, from a scan (Live games) or a favorite's
// card (Favorite teams). Away on the left like a broadcast.
struct TileGame {
  std::string event_id;
  uint8_t league{0};
  GameState state{GameState::NOT_FOUND};
  int64_t kickoff_epoch{0};
  bool time_tbd{false};
  std::string away_abbr, home_abbr;
  uint32_t away_id{0}, home_id{0};
  int away_score{0}, home_score{0};
  uint32_t away_color{0}, home_color{0};  // ESPN's primary colors, 0 unknown
  std::string away_record, home_record;   // "3-2", for a game to come; "" unknown
  uint8_t period{0};
  int32_t clock_s{-1};  // left in the period, or soccer's elapsed; -1 none
  std::string detail;   // ESPN's shortDetail: "4:12 - 3rd", "Halftime", "Top 5th", "Final/OT"
  uint8_t possession{0};  // 0 none, 1 away, 2 home: the ball, or the side batting
  int fav_rank{-1};       // the list position of a favorite playing, -1 none
  int64_t final_epoch{0};  // when the panel saw the game end, 0 when it was found final
};

struct TileRules {
  int64_t final_hold_s{30 * 60};  // a final the panel saw end keeps its tile this long
};

// May this game hold a tile now? Live games always; a final the panel saw
// end for the hold, a final found that way until something live wants the
// tile; a game to come likewise.
inline bool tile_worthy(const TileGame &g, int64_t now, const TileRules &r) {
  switch (g.state) {
    case GameState::IN:
      return true;
    case GameState::POST:
      return g.final_epoch == 0 || now - g.final_epoch < r.final_hold_s;
    case GameState::PRE:
      return true;
    default:
      return false;
  }
}

// Fill order: live games first, among them the owner's favorites by list
// position and then the most interesting (best_game.h); then finals, the
// ones the panel saw end before those found final, newest first; then games
// to come, favorites first, then the soonest kickoff.
inline int tile_class(const TileGame &g) {
  switch (g.state) {
    case GameState::IN:
      return 0;
    case GameState::POST:
      return 1;
    case GameState::PRE:
      return 2;
    default:
      return 3;
  }
}

inline int tile_interest(const TileGame &g) {
  LiveGame l;
  l.league = g.league;
  l.away_score = (int16_t) g.away_score;
  l.home_score = (int16_t) g.home_score;
  l.period = g.period;
  l.clock_s = g.clock_s;
  return live_interest(l);
}

inline bool tile_before(const TileGame &a, const TileGame &b) {
  int ca = tile_class(a), cb = tile_class(b);
  if (ca != cb)
    return ca < cb;
  bool fa = a.fav_rank >= 0, fb = b.fav_rank >= 0;
  if (fa != fb)
    return fa;
  if (fa && a.fav_rank != b.fav_rank)
    return a.fav_rank < b.fav_rank;
  switch (ca) {
    case 0:
      return tile_interest(a) > tile_interest(b);
    case 1:
      if ((a.final_epoch != 0) != (b.final_epoch != 0))
        return a.final_epoch != 0;
      return a.final_epoch > b.final_epoch;
    case 2:
      return a.kickoff_epoch < b.kickoff_epoch;
    default:
      return false;
  }
}

// The tiles for this poll: an index into cands per slot, -1 for an empty
// slot. slots is what the board shows now (event ids, "" for empty). A game
// keeps its slot while it may hold one (tile_worthy), so nothing jumps
// around between polls; a live game waiting for a slot takes one from a
// final or a game to come, the lowest-ranked holder first, never from
// another live game.
template<typename TGames>
inline std::vector<int> plan_tiles(const TGames &cands, const std::vector<std::string> &slots, int count, int64_t now,
                                   const TileRules &r) {
  if (count < 1)
    count = 1;
  std::vector<int> out((size_t) count, -1);
  std::vector<bool> used(cands.size(), false);
  for (size_t s = 0; s < (size_t) count && s < slots.size(); s++) {
    if (slots[s].empty())
      continue;
    for (size_t i = 0; i < cands.size(); i++) {
      if (used[i] || cands[i].event_id != slots[s] || !tile_worthy(cands[i], now, r))
        continue;
      out[s] = (int) i;
      used[i] = true;
      break;
    }
  }
  std::vector<int> order;
  order.reserve(cands.size());
  for (size_t i = 0; i < cands.size(); i++)
    if (!used[i] && tile_worthy(cands[i], now, r))
      order.push_back((int) i);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return tile_before(cands[a], cands[b]); });
  for (int idx : order) {
    int free_slot = -1;
    for (size_t s = 0; s < out.size() && free_slot < 0; s++)
      if (out[s] < 0)
        free_slot = (int) s;
    if (free_slot >= 0) {
      out[(size_t) free_slot] = idx;
      continue;
    }
    if (cands[(size_t) idx].state != GameState::IN)
      break;  // the slots are full and nothing live is waiting for one
    // Bump the weakest holder that is not live.
    int victim = -1;
    for (size_t s = 0; s < out.size(); s++) {
      const TileGame &h = cands[(size_t) out[s]];
      if (h.state == GameState::IN)
        continue;
      if (victim < 0 || tile_before(cands[(size_t) out[(size_t) victim]], h))
        victim = (int) s;
    }
    if (victim < 0)
      break;
    out[(size_t) victim] = idx;
  }
  return out;
}

// A record cut to wins and losses, "4-5-0" to "4-5": a tile's score slot
// has room for five characters, and hockey's overtime losses make six.
inline std::string tile_record(const std::string &record) {
  size_t first = record.find('-');
  if (first == std::string::npos)
    return record;
  size_t second = record.find('-', first + 1);
  return second == std::string::npos ? record : record.substr(0, second);
}

// "7:30 PM"
inline std::string tile_time_12h(const struct tm &t) {
  int h = t.tm_hour % 12;
  if (h == 0)
    h = 12;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d %s", h, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
  return buf;
}

// The period's short name on a tile: "Q3", "H2" (college basketball), "P2"
// (hockey), "OT", "2OT".
inline std::string tile_period(League league, int period) {
  detail::Regulation reg = detail::regulation(league);
  Sport sport = league_sport(league);
  if (period > reg.periods) {
    int ot = period - reg.periods;
    return ot == 1 ? "OT" : std::to_string(ot) + "OT";
  }
  const char *unit = sport == Sport::HOCKEY ? "P" : reg.periods == 2 ? "H" : "Q";
  return std::string(unit) + std::to_string(period);
}

// The tile's status line: "Q3 4:12", "HALF", "END Q3", "TOP 5", "65'",
// "F", "F/OT", "7:30 PM", "Sun 7:30 PM". kick_local and now_local are the
// kickoff and now in the panel's time zone, for a game to come; nullptr
// leaves that line empty.
inline std::string tile_status(const TileGame &g, const struct tm *kick_local, const struct tm *now_local) {
  Sport sport = league_sport((League) g.league);
  std::string lower = g.detail;
  for (char &c : lower)
    c = (char) tolower((unsigned char) c);
  switch (g.state) {
    case GameState::POST: {
      std::string f = "F";
      if (g.detail.find("/OT") != std::string::npos || g.detail.find("OT") != std::string::npos)
        f += "/OT";
      else if (g.detail.find("/SO") != std::string::npos)
        f += "/SO";
      return f;
    }
    case GameState::PRE: {
      if (kick_local == nullptr || now_local == nullptr)
        return "";
      static const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      if (kick_local->tm_wday < 0 || kick_local->tm_wday > 6)
        return "";
      std::string when = g.time_tbd ? std::string("TBD") : tile_time_12h(*kick_local);
      if (local_day(*kick_local) != local_day(*now_local))
        when = std::string(kDays[kick_local->tm_wday]) + " " + when;
      return when;
    }
    case GameState::IN:
      break;
    default:
      return "";
  }
  if (lower.find("delay") != std::string::npos)
    return "DELAY";
  if (sport == Sport::SOCCER) {
    if (lower.find("half") != std::string::npos)
      return "HT";
    if (g.clock_s >= 0) {
      int m = g.clock_s / 60;
      return std::to_string(m) + "'";
    }
    return g.period >= 3 ? "ET" : "LIVE";
  }
  if (sport == Sport::BASEBALL) {
    const char *half = lower.compare(0, 3, "top") == 0   ? "TOP"
                       : lower.compare(0, 3, "bot") == 0 ? "BOT"
                       : lower.compare(0, 3, "mid") == 0 ? "MID"
                       : lower.compare(0, 3, "end") == 0 ? "END"
                                                         : "";
    if (*half == '\0')
      return g.period > 0 ? "INN " + std::to_string(g.period) : "LIVE";
    return std::string(half) + " " + std::to_string(g.period);
  }
  if (lower.find("half") != std::string::npos)
    return "HALF";
  std::string per = g.period > 0 ? tile_period((League) g.league, g.period) : "";
  if (lower.compare(0, 3, "end") == 0)
    return per.empty() ? "END" : "END " + per;
  if (g.clock_s < 0)
    return per.empty() ? "LIVE" : per;
  char clock[16];
  snprintf(clock, sizeof(clock), "%d:%02d", g.clock_s / 60, g.clock_s % 60);
  return per.empty() ? std::string(clock) : per + " " + clock;
}

}  // namespace espn
