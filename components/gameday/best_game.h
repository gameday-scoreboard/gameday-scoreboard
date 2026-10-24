// Live games' "Pick the best game": which game in progress a fan would flip
// to, from what the day's scan already reads (score, period, clock, AP
// rank), and the upset alert when a ranked side trails late. Pure, no
// ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "espn_parse.h"
#include "leagues.h"

namespace espn {

// The game counts as late from here (per mille of regulation): the fourth
// quarter, the last ten minutes of a college basketball game, the 7th inning.
constexpr int kLateProgress = 750;
// What a game already on the board gets on top of its own interest, so the
// pick moves only for a clearly better game, not a field goal's worth.
constexpr int kBestStickBonus = 25;
constexpr uint32_t kUpsetColor = 0xC8102E;

namespace detail {

struct Regulation {
  uint8_t periods;
  int16_t period_s;  // 0: no clock (baseball)
};

inline Regulation regulation(League league) {
  switch (league) {
    case League::NFL:
    case League::NCAA:
      return {4, 900};
    case League::NBA:
      return {4, 720};
    case League::WNBA:
      return {4, 600};
    case League::MCBB:
      return {2, 1200};
    case League::NHL:
      return {3, 1200};
    case League::MLB:
      return {9, 0};
    case League::MLS:
    case League::EPL:
      return {2, 2700};
  }
  return {4, 900};
}

}  // namespace detail

// ESPN's display clock in seconds: "8:27", "12.3" (basketball's last
// minute) and soccer's elapsed "65'" or "90'+3'". -1 when there is none.
inline int clock_seconds(const std::string &c) {
  if (c.empty() || c[0] < '0' || c[0] > '9')
    return -1;
  // Held to four digits a part, so nonsense from the feed can't overflow.
  long a = strtol(c.c_str(), nullptr, 10);
  if (a > 9999)
    return -1;
  size_t colon = c.find(':');
  if (colon != std::string::npos) {
    long b = strtol(c.c_str() + colon + 1, nullptr, 10);
    return b < 0 || b > 9999 ? -1 : (int) (a * 60 + b);
  }
  if (c.find('\'') != std::string::npos)
    return (int) a * 60;
  return (int) a;
}

// How far through regulation the game is, 0 to 1000. A clock that runs down
// (football, basketball, hockey) is the time left in the period; soccer's
// counts up from kickoff.
inline int game_progress(League league, int period, int clock_s) {
  detail::Regulation r = detail::regulation(league);
  if (period <= 0)
    return 0;
  if (period > r.periods)
    return 1000;
  int p;
  if (r.period_s == 0) {
    p = period * 1000 / r.periods;  // an inning under way counts as played
  } else if (league_sport(league) == Sport::SOCCER) {
    p = clock_s < 0 ? (period - 1) * 500 : clock_s * 1000 / (r.periods * r.period_s);
  } else {
    int left = clock_s < 0 || clock_s > r.period_s ? r.period_s : clock_s;
    p = ((period - 1) * r.period_s + r.period_s - left) * 1000 / (r.periods * r.period_s);
  }
  return p < 0 ? 0 : p > 1000 ? 1000 : p;
}

inline bool in_overtime(League league, int period) { return period > detail::regulation(league).periods; }

// The better-ranked side trails late, or in overtime. Only college games
// carry an AP rank, so the pro leagues never alert.
inline bool upset_alert(const LiveGame &g) {
  if (g.away_rank == g.home_rank)
    return false;  // neither ranked, or (never in practice) the same rank
  League league = (League) g.league;
  if (!in_overtime(league, g.period) && game_progress(league, g.period, g.clock_s) < kLateProgress)
    return false;
  return g.away_rank < g.home_rank ? g.away_score < g.home_score : g.home_score < g.away_score;
}

// How much a fan would want to watch this game now: close beats lopsided,
// more so the later it is; overtime, an AP rank and an upset alert add on.
inline int live_interest(const LiveGame &g) {
  League league = (League) g.league;
  int margin = abs(g.away_score - g.home_score);
  int units;  // the margin in scores the trailing side needs
  switch (league_sport(league)) {
    case Sport::FOOTBALL:
      units = (margin + 7) / 8;
      break;
    case Sport::BASKETBALL:
      units = (margin + 2) / 3;
      break;
    default:
      units = margin;
      break;
  }
  int close = units >= 4 ? 0 : (4 - units) * 25;
  int score = close * (1000 + 2 * game_progress(league, g.period, g.clock_s)) / 1000;
  if (in_overtime(league, g.period))
    score += 300;
  uint8_t best = g.away_rank < g.home_rank ? g.away_rank : g.home_rank;
  uint8_t other = g.away_rank < g.home_rank ? g.home_rank : g.away_rank;
  if (best >= 1 && best <= 25)
    score += (26 - best) * 2;
  if (other >= 1 && other <= 25)
    score += 25;
  if (upset_alert(g))
    score += 200;
  return score;
}

// The index of the game to follow, the one on the board (current) winning
// near-ties. current_over: the board's poll saw it end, though the scan may
// still list it; any other game goes first. 0 for an empty list.
inline size_t best_live_game(const std::vector<LiveGame> &games, const std::string &current,
                             bool current_over = false) {
  size_t best = 0;
  int top = -1;
  for (size_t i = 0; i < games.size(); i++) {
    bool cur = !current.empty() && games[i].event_id == current;
    if (cur && current_over && games.size() > 1)
      continue;
    int s = live_interest(games[i]) + (cur ? kBestStickBonus : 0);
    if (s > top) {
      top = s;
      best = i;
    }
  }
  return best;
}

// The game on the board is between plays long enough to leave it: the end
// of a quarter, period or half inning, halftime, or not in progress at all.
// Never in the middle of a drive.
inline bool at_break(const GameSnapshot &g) {
  if (!g.valid || g.state != GameState::IN)
    return true;
  const std::string &d = g.short_detail;
  return d.rfind("End", 0) == 0 || d.rfind("Mid", 0) == 0 || d.rfind("Halftime", 0) == 0 || d == "HT";
}

// When Pick the best game may leave the game on the board: at a break, and
// for soccer, whose clock never stops, once a match reaches 45' (the first
// half's last minute and its stoppage time, "45'+2'") and from 90' on; the
// second half starts at 46'. Play is still on then, so at_break stays false
// for Jump to the action (in_action).
inline bool pick_break(const GameSnapshot &g) {
  if (at_break(g))
    return true;
  const std::string &d = g.short_detail;
  if (g.sport != Sport::SOCCER || d.empty() || d[0] < '0' || d[0] > '9')
    return false;
  int minute = 0;
  for (size_t i = 0; i < d.size() && i < 3 && d[i] >= '0' && d[i] <= '9'; i++)
    minute = minute * 10 + (d[i] - '0');
  return minute == 45 || minute >= 90;
}

// Live games following a game: whether this cycle looks for another one.
// elapsed_ms since the last look, switch_ms the owner's Switch every.
// - Stick with a favorite's game up (held): a slow look, Switch every but at
//   least 5 minutes, so a higher favorite that went live without a kickoff
//   time (TBD) still takes the board.
// - Alternate with a favorite's game up (on_fav), or a random pick (not
//   best): the plain timer.
// - Pick the best game: only at a break in the game, and once per break
//   (break_looked: a look at this break kept the game), so a long
//   intermission doesn't scan every minute.
inline bool rotate_due(uint32_t elapsed_ms, uint32_t switch_ms, bool held, bool on_fav, bool best, bool at_brk,
                       bool break_looked) {
  if (held)
    return elapsed_ms >= (switch_ms > 5u * 60 * 1000 ? switch_ms : 5u * 60 * 1000);
  if (elapsed_ms < switch_ms)
    return false;
  if (on_fav || !best)
    return true;
  return at_brk && !break_looked;
}

// The followed game as the scan would list it, from a poll of it, for the
// upset check between scans. Live games put the away side in team.
inline LiveGame live_view(const GameSnapshot &g, uint8_t league, uint8_t away_rank, uint8_t home_rank) {
  LiveGame v;
  v.league = league;
  v.event_id = g.event_id;
  v.away_score = (int16_t) g.team_score;
  v.home_score = (int16_t) g.opp_score;
  v.period = (uint8_t) (g.period < 0 ? 0 : g.period > 255 ? 255 : g.period);
  v.clock_s = clock_seconds(g.display_clock);
  v.away_rank = away_rank;
  v.home_rank = home_rank;
  return v;
}

}  // namespace espn
