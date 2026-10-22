// Favorite Teams mode: which of the four favorites the panel shows right now.
// Pure logic on epoch seconds, no ESPHome includes, so the host tests cover
// the timing rules (lock-on, release, collisions, the idle playlist).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "best_game.h"
#include "espn_parse.h"

namespace espn {

// What the decision needs to know about one favorite's next game.
struct FavGame {
  bool valid{false};  // a card has been fetched for this slot
  GameState state{GameState::NOT_FOUND};
  int64_t kickoff_epoch{0};
  bool time_tbd{false};    // no kickoff time yet: kickoff_epoch is a midnight Eastern placeholder
  int64_t final_epoch{0};  // when the panel first saw it go final; 0 = fetched already final
  // How far ahead (or back, for a final) a card may be and still cycle while
  // idle; 0 = no limit. Football has none, so a bye week never hides a team;
  // other leagues use it to drop a team that is out of season.
  int64_t horizon_s{0};
  bool countdown{false};  // its countdown card takes a turn after it (countdown.h)
  // A recent poll found the game at its action (in_action), for Jump to the action.
  bool action{false};
  // The team is in the playlist being played (playlists.h). One that isn't
  // still locks on near its kickoff, but stays out of the idle playlist.
  bool in_list{true};
};

struct FavRules {
  int64_t lockon_s{15 * 60};  // a game takes the panel this long before kickoff
  int64_t release_s{30};      // and keeps it this long after the final
  bool alternate{false};      // two locked games: flip between them (else the higher slot wins)
  int64_t rotate_s{5 * 60};   // flip interval when alternating
  int64_t dwell_s{10};        // seconds per card in the idle playlist
  int64_t countdown_s{0};     // and after a card with a countdown, the countdown's
  bool today_only{false};     // idle playlist: only games today (local time) or live
  int64_t day_start{0};       // today's local midnight, epoch seconds
  int64_t day_end{0};         // the next one
};

struct FavChoice {
  int index{-1};  // slot to show, -1 when no favorite has a card
  bool locked{false};
  bool again{false};  // the playlist moved on and came back to the card already up (a playlist of one)
};

inline bool fav_locked(const FavGame &g, int64_t now, const FavRules &r) {
  if (!g.valid)
    return false;
  switch (g.state) {
    case GameState::IN:
      return true;
    case GameState::PRE:
      // Includes kickoffs already past: ESPN can report PRE a poll or two late.
      // A TBD game has no kickoff to count down to; it locks once it starts.
      return !g.time_tbd && g.kickoff_epoch - now <= r.lockon_s;
    case GameState::POST:
      return g.final_epoch != 0 && now - g.final_epoch < r.release_s;
    default:
      return false;
  }
}

// May the idle playlist show this card? `today` applies the today-only filter.
inline bool fav_idle_ok(const FavGame &g, int64_t now, const FavRules &r, bool today) {
  if (!g.valid)
    return false;
  if (g.state == GameState::IN)
    return true;
  if (g.horizon_s > 0) {
    if (g.state == GameState::PRE && g.kickoff_epoch - now > g.horizon_s)
      return false;
    if (g.state == GameState::POST && now - g.kickoff_epoch > g.horizon_s)
      return false;
  }
  if (!today)
    return true;
  int64_t day = g.time_tbd ? tbd_day_epoch(g.kickoff_epoch) : g.kickoff_epoch;
  return day >= r.day_start && day < r.day_end;
}

// Next entry after `from` that satisfies `ok`, wrapping; -1 if none.
template<typename F> static int fav_next_(const std::vector<FavGame> &games, int from, F ok) {
  int n = (int) games.size();
  for (int step = 1; step <= n; step++) {
    int i = ((from < 0 ? -1 : from) + step) % n;
    if (i < 0)
      i += n;
    if (ok(i))
      return i;
  }
  return -1;
}

// games: slot order = priority. current/shown_since: what is up now and since
// when. pinned: a slot picked with the remote, -1 for none.
inline FavChoice pick_favorite(const std::vector<FavGame> &games, int64_t now, const FavRules &r, int current,
                               int64_t shown_since, int pinned) {
  FavChoice c;
  if (games.empty())
    return c;
  auto locked = [&](int i) { return i >= 0 && i < (int) games.size() && fav_locked(games[i], now, r); };
  auto valid = [&](int i) { return i >= 0 && i < (int) games.size() && games[i].valid; };
  bool any_locked = false;
  for (int i = 0; i < (int) games.size(); i++)
    any_locked = any_locked || locked(i);

  if (any_locked) {
    c.locked = true;
    if (locked(pinned)) {
      c.index = pinned;
    } else if (!r.alternate) {
      for (int i = 0; i < (int) games.size(); i++)
        if (locked(i)) {
          c.index = i;
          break;
        }
    } else if (locked(current) && now - shown_since < r.rotate_s) {
      c.index = current;
    } else {
      c.index = fav_next_(games, current, locked);
    }
    return c;
  }

  // Idle playlist. A pinned card seeds it; dwell then moves on like any other.
  // Cards that pass the filters cycle. With none passing, the filters relax
  // one at a time (today only, then the season horizon) so the panel never
  // goes blank while it has cards.
  int level = 0;
  auto ok_at = [&](int i, int lv) {
    if (!valid(i) || !games[i].in_list)
      return false;
    if (lv >= 2)
      return true;
    return fav_idle_ok(games[i], now, r, lv == 0 && r.today_only);
  };
  for (; level < 2; level++) {
    bool any = false;
    for (int i = 0; i < (int) games.size() && !any; i++)
      any = ok_at(i, level);
    if (any)
      break;
  }
  auto ok = [&](int i) { return ok_at(i, level); };
  int64_t dwell = r.dwell_s + (valid(current) && games[current].countdown ? r.countdown_s : 0);
  // A remote press shows its card for the dwell even when a filter would skip it.
  if ((ok(current) || (current == pinned && valid(current))) && now - shown_since < dwell)
    c.index = current;
  else {
    c.index = fav_next_(games, current, ok);
    c.again = c.index >= 0 && c.index == current;
  }
  return c;
}

// How long a game counts as at its action after the last poll that saw it
// there, so the touchdown that ends a red zone trip, or the run that clears
// the bases, still shows before the board goes back.
constexpr int64_t kActionHoldS = 60;
// How far behind a side can be for the last two minutes to count, in
// points: one score in football, two possessions in basketball, a goal in
// hockey and soccer.
inline int action_margin(Sport sport) {
  switch (sport) {
    case Sport::FOOTBALL:
      return 8;
    case Sport::BASKETBALL:
      return 6;
    default:
      return 1;
  }
}

// The moment a fan would want to see: football in the red zone, the bases
// loaded, or a close game in its last two minutes (or overtime). Baseball
// has no clock, so only the bases count there.
inline bool in_action(const GameSnapshot &g, League league) {
  if (!g.valid || g.state != GameState::IN || at_break(g))
    return false;
  Sport sport = league_sport(league);
  if (sport == Sport::BASEBALL) {
    const uint8_t loaded = Baseball::FIRST | Baseball::SECOND | Baseball::THIRD;
    return (g.mlb.half == Half::TOP || g.mlb.half == Half::BOTTOM) && (g.mlb.bases & loaded) == loaded;
  }
  if (sport == Sport::FOOTBALL && g.is_red_zone)
    return true;
  if (abs(g.team_score - g.opp_score) > action_margin(sport))
    return false;
  int clock = clock_seconds(g.display_clock);
  if (clock < 0)
    return false;
  if (sport == Sport::SOCCER)
    return g.period >= 2 && clock >= 88 * 60;  // soccer's clock counts up; extra time counts too
  uint8_t last = sport == Sport::BASKETBALL ? g.nba.regulation : detail::regulation(league).periods;
  return g.period >= last && clock <= 120;
}

// Jump to the action: the favorite to put up in place of `base` (the slot
// pick_favorite chose) for its action, -1 to leave the board on base.
// `current` is on the board now: a game already up for its action keeps the
// board while the action lasts, and when no game is at its action the board
// goes back to base.
inline int action_favorite(const std::vector<FavGame> &games, int base, int current) {
  auto act = [&](int i) {
    return i >= 0 && i < (int) games.size() && games[i].valid && games[i].state == GameState::IN && games[i].action;
  };
  if (act(base))
    return -1;
  if (act(current))
    return current;
  for (int i = 0; i < (int) games.size(); i++)
    if (act(i))
      return i;
  return -1;
}

// The banner when another favorite scores: "KC TD, 21-17", the favorite's
// score first. Football leaves out the extra point and other one or two
// point plays, which follow the touchdown a poll later. Basketball has none:
// it scores every trip down the floor. "" when there is nothing to say.
inline std::string fav_score_banner(const GameSnapshot &prev, const GameSnapshot &cur) {
  if (!prev.valid || !cur.valid || prev.event_id != cur.event_id || prev.state != GameState::IN ||
      cur.state != GameState::IN)
    return "";
  int delta = cur.team_score - prev.team_score;
  if (delta <= 0)
    return "";
  std::string word;
  switch (cur.sport) {
    case Sport::FOOTBALL:
      if (delta >= 6)
        word = "TD";
      else if (delta >= 3)
        word = "FG";
      break;
    case Sport::BASEBALL:
      word = baseball_home_run(cur) ? "HR" : delta == 1 ? "RUN" : std::to_string(delta) + " RUNS";
      break;
    case Sport::HOCKEY:
    case Sport::SOCCER:
      word = "GOAL";
      break;
    case Sport::BASKETBALL:
      break;
  }
  if (word.empty())
    return "";
  return cur.team_abbr + " " + word + ", " + std::to_string(cur.team_score) + "-" + std::to_string(cur.opp_score);
}

// The favorite Home Assistant reports while the idle playlist rotates, so its
// sensors don't change with every card: the next kickoff, else the first slot
// with a card, among the playlist being played. -1 when none has one.
inline int followed_favorite(const std::vector<FavGame> &games) {
  int best = -1;
  for (int i = 0; i < (int) games.size(); i++) {
    const FavGame &g = games[i];
    if (!g.valid || !g.in_list)
      continue;
    if (best < 0) {
      best = i;
      continue;
    }
    const FavGame &b = games[best];
    if (g.state == GameState::PRE && (b.state != GameState::PRE || g.kickoff_epoch < b.kickoff_epoch))
      best = i;
  }
  return best;
}

// ---- Favorites take over (Live games) ----------------------------------------
// Live games keeps its rotation, but a game a favorite plays in takes the
// board while it is live. Two favorites live at once follow Favorites' own
// rule: stick with the one higher in the list, or alternate on the rotate timer.

// One favorite team. ESPN team ids are per league.
struct FavRef {
  uint8_t league{0};
  uint32_t id{0};
};

// The list position (the priority) of the best favorite playing in g, -1
// when no favorite is.
inline int live_fav_rank(const LiveGame &g, const std::vector<FavRef> &favs) {
  for (int i = 0; i < (int) favs.size(); i++)
    if (favs[i].league == g.league && favs[i].id != 0 && (favs[i].id == g.away_id || favs[i].id == g.home_id))
      return i;
  return -1;
}

// The live game to lock onto, as an index into live, or -1 to leave the pick
// to the rotation. current is the game on the board; skip_current leaves it
// out (it ended, though the scan may still list it as in progress). With
// alternate, a scan with the board on one favorite's game moves to the next
// favorite's, in list order, wrapping; a lone favorite's game stays.
inline int take_over_pick(const std::vector<LiveGame> &live, const std::vector<FavRef> &favs,
                          const std::string &current, bool skip_current, bool alternate) {
  // Candidates by priority: (rank, index), insertion-sorted, a few at most.
  std::vector<std::pair<int, int>> cand;
  for (int i = 0; i < (int) live.size(); i++) {
    if (skip_current && !current.empty() && live[i].event_id == current)
      continue;
    int r = live_fav_rank(live[i], favs);
    if (r < 0)
      continue;
    auto at = cand.begin();
    while (at != cand.end() && at->first <= r)
      ++at;
    cand.insert(at, {r, i});
  }
  if (cand.empty())
    return -1;
  if (!alternate)
    return cand[0].second;
  for (size_t k = 0; k < cand.size(); k++)
    if (!current.empty() && live[cand[k].second].event_id == current)
      return cand[(k + 1) % cand.size()].second;
  return cand[0].second;
}

// When to scan again for a favorite's kickoff: at the earliest kickoff the
// last scan saw for one (0 for none). A kickoff already past means ESPN still
// says "pre": look again in two minutes, and after twenty, every ten, so a
// delayed start doesn't keep the panel reading scoreboards.
inline int64_t take_over_rescan_at(int64_t now, int64_t fav_kickoff) {
  if (fav_kickoff <= 0)
    return 0;
  if (fav_kickoff > now)
    return fav_kickoff;
  return now + (now - fav_kickoff < 20 * 60 ? 2 * 60 : 10 * 60);
}

// The same after a scan where a league's read failed: that league's favorite
// kickoff can be missing from `next`, so the rescan already set stays when it
// comes sooner. One already due moves two minutes out, so a league that keeps
// failing is retried without a scan on every poll.
inline int64_t take_over_rescan_keep(int64_t now, int64_t next, int64_t prev) {
  if (prev == 0)
    return next;
  int64_t kept = prev > now ? prev : now + 2 * 60;
  return next == 0 || kept < next ? kept : next;
}

}  // namespace espn
