// The finals rotation: what a live mode shows on a panel with no team picked
// when nothing is live. The games later today under a "Later today" header,
// then a header per game day and league ("Yesterday's NHL scores") and that
// day's finals, and a "Next: BOS at NYR Thu 6:00 PM" card when nothing is
// left today, round and round. Pure, with no ESPHome includes, so the host
// tests compile it directly.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "espn_parse.h"

namespace espn {

// A day the way ESPN's scoreboard `dates=` parameter counts it for a league:
// US Eastern, shifted like scan_url() (5 h for football, 9 h for the rest),
// so a West Coast game that ends at 1 AM Eastern lands on the day it started.
inline int32_t espn_day(int64_t epoch, uint8_t league) {
  int64_t s = epoch - scan_day_shift((League) league);
  return (int32_t) (s >= 0 ? s / 86400 : (s - 86399) / 86400);
}

// Back to an epoch inside that day (noon Eastern), for scan_url(). Noon is
// inside the day for either shift.
inline int64_t espn_day_epoch(int32_t day) { return (int64_t) day * 86400 + 17 * 3600; }

// One game day of one league, newest day first in a list: its finals, and
// for today the games not started yet.
struct RecapDay {
  uint8_t league{0};
  int32_t day{0};
  std::vector<Final> finals;
  std::vector<Pending> later;  // today only, best first (pending_before)
};

// Walking back for older finals stops once a league has this many.
static const size_t kRecapMinFinals = 4;
// How far back the walk, and the forward search for the next kickoff, look.
static const int kRecapMaxDays = 7;
// Game cards in one loop, later today and finals together, shared between
// the leagues shown. Two logos each, so 22 stays inside the 48-logo cache
// (four are kept pinned) and a loop does not download the same logos again
// every time round. The later-today cards come first, up to kRecapLaterCap,
// and the finals get the rest: 14 with a full eight.
static const size_t kRecapCap = 22;
static const size_t kRecapLaterCap = kPendingKeep;

// "Today's", "Yesterday's", or the weekday: "Saturday's".
inline std::string recap_day_word(int32_t day, int32_t today) {
  static const char *kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  if (day == today)
    return "Today's";
  if (day == today - 1)
    return "Yesterday's";
  int w = (int) (((int64_t) day + 4) % 7);  // 1970-01-01 was a Thursday
  if (w < 0)
    w += 7;
  return std::string(kDays[w]) + "'s";
}

// The league in a header: its kLeagues prefix ("NFL", "NHL"), and "college"
// for college football as before.
inline const char *recap_league_word(uint8_t league) {
  if (league == (uint8_t) League::NCAA)
    return "college";
  const LeagueInfo *l = league_info((League) league);
  return l != nullptr ? l->prefix : "";
}

// "Saturday's college scores", counted in the league's own day.
inline std::string recap_header(const RecapDay &d, int64_t now) {
  return recap_day_word(d.day, espn_day(now, d.league)) + " " + recap_league_word(d.league) + " scores";
}

// The ticker under a game later today: "Later today's NHL games | Today
// 7:00 PM | ESPN+". when is the kickoff_label().
inline std::string recap_later_ticker(const Pending &p, const std::string &when) {
  std::string out = std::string("Later today's ") + recap_league_word(p.league) + " games | " + when;
  if (!p.tv.empty())
    out += " | " + p.tv;
  return out;
}

// The Next card: "Next: BOS at NYR Thu 6:00 PM", or "Next: Thu 6:00 PM" when
// the matchup is not known.
inline std::string recap_next_text(const std::string &matchup, const std::string &when) {
  return "Next: " + (matchup.empty() ? std::string() : matchup + " ") + when;
}

inline std::string recap_matchup(const Pending &p) { return p.away_abbr + " at " + p.home_abbr; }

// Ranked games first (best rank on either side), then by kickoff, the way
// ESPN lists them. Games outside college football carry no rank and keep
// kickoff order.
inline void recap_sort(std::vector<Final> &finals) {
  std::stable_sort(finals.begin(), finals.end(), [](const Final &a, const Final &b) {
    if (a.rank != b.rank)
      return a.rank < b.rank;
    return a.kickoff_epoch < b.kickoff_epoch;
  });
}

// Puts a day's finals and games to come into the list, replacing what it had
// for that league and day (a scan later the same day has more finals and
// fewer games to come), newest day first. A day keeps only what one loop
// could show; an empty day is not stored.
inline void recap_merge(std::vector<RecapDay> &days, RecapDay fresh) {
  for (auto it = days.begin(); it != days.end(); ++it) {
    if (it->league == fresh.league && it->day == fresh.day) {
      days.erase(it);
      break;
    }
  }
  if (fresh.finals.empty() && fresh.later.empty())
    return;
  recap_sort(fresh.finals);
  if (fresh.finals.size() > kRecapCap)
    fresh.finals.resize(kRecapCap);
  std::stable_sort(fresh.later.begin(), fresh.later.end(), pending_before);
  if (fresh.later.size() > kRecapLaterCap)
    fresh.later.resize(kRecapLaterCap);
  auto at = days.begin();
  while (at != days.end() && (at->day > fresh.day || (at->day == fresh.day && at->league < fresh.league)))
    ++at;
  days.insert(at, std::move(fresh));
}

// Drops days a week old or more, and per league every day older than the
// ones that already make up kRecapMinFinals: a new game day pushes out the
// last one instead of piling up behind it. Games to come are only kept for
// the league's today.
inline void recap_trim(std::vector<RecapDay> &days, int64_t now) {
  size_t have[kLeagueCount] = {};
  std::vector<RecapDay> kept;
  for (auto &d : days) {
    if (d.league >= kLeagueCount)
      continue;
    int32_t today = espn_day(now, d.league);
    if (today - d.day >= kRecapMaxDays || have[d.league] >= kRecapMinFinals)
      continue;
    if (d.day != today)
      std::vector<Pending>().swap(d.later);
    if (d.finals.empty() && d.later.empty())
      continue;
    have[d.league] += d.finals.size();
    kept.push_back(std::move(d));
  }
  days = std::move(kept);
}

// Shares cap between the leagues one card at a time, round and round, until
// the cap is used up or no league has more: as even as it can be, and a
// league with fewer leaves the rest to the others. have[l] is what league l
// has to show; got[l] gets its share.
inline void recap_share(const size_t (&have)[kLeagueCount], size_t cap, size_t (&got)[kLeagueCount]) {
  for (size_t l = 0; l < kLeagueCount; l++)
    got[l] = 0;
  bool more = true;
  while (cap > 0 && more) {
    more = false;
    for (size_t l = 0; l < kLeagueCount && cap > 0; l++) {
      if (got[l] < have[l]) {
        got[l]++;
        cap--;
        more = true;
      }
    }
  }
}

// A scan's finals and games to come are today's: they replace what the list
// had for today, so the rotation fills up as games end and drops the ones
// that started. A league with neither keeps its days, so a scan that failed
// for one league never empties it. Each league keeps only the games to come
// its share of a loop could show, since every one is internal RAM.
inline void recap_absorb(std::vector<RecapDay> &days, const std::vector<Final> &finals,
                         const std::vector<Pending> &pending, int64_t now) {
  size_t have[kLeagueCount] = {}, keep[kLeagueCount];
  auto today = [now](const Pending &p) {
    return p.kickoff_epoch > now && espn_day(p.kickoff_epoch, p.league) == espn_day(now, p.league);
  };
  for (const auto &p : pending)
    if (p.league < kLeagueCount && today(p))
      have[p.league]++;
  recap_share(have, kRecapLaterCap, keep);
  for (uint8_t league = 0; league < kLeagueCount; league++) {
    RecapDay d;
    d.league = league;
    d.day = espn_day(now, league);
    for (const auto &f : finals)
      if (f.league == league)
        d.finals.push_back(f);
    // Best first already (parse_live_games), so the share is the best games.
    for (const auto &p : pending)
      if (p.league == league && d.later.size() < keep[league] && today(p))
        d.later.push_back(p);
    if (!d.finals.empty() || !d.later.empty())
      recap_merge(days, std::move(d));
  }
  recap_trim(days, now);
}

// A scan's next kickoff still ahead, 0 for none: the walk forward waits
// while there is one. ESPN can list a game as not started for a few minutes
// past its kickoff, and the scan's first kickoff is then behind us with
// other games still to come today, so fall back on the first kept game
// ahead.
inline int64_t recap_scan_next(int64_t next_kickoff, const std::vector<Pending> &pending, int64_t now) {
  if (next_kickoff > now)
    return next_kickoff;
  int64_t best = 0;
  for (const auto &p : pending)
    if (p.kickoff_epoch > now && (best == 0 || p.kickoff_epoch < best))
      best = p.kickoff_epoch;
  return best;
}

inline size_t recap_count(const std::vector<RecapDay> &days, uint8_t league) {
  size_t n = 0;
  for (const auto &d : days)
    if (d.league == league)
      n += d.finals.size();
  return n;
}

// LATER_HEADER is the "Later today" card, LATER a game later today.
enum class RecapKind : uint8_t { HEADER, FINAL, NEXT, LATER_HEADER, LATER };

struct RecapCard {
  RecapKind kind{RecapKind::NEXT};
  uint8_t day{0};     // index into the day list
  uint16_t final{0};  // index into that day's finals, or its later list for LATER
};

// Which leagues a mode reads, for recap_cards(): 1 << League per league.
static const uint16_t kRecapNfl = 1 << (int) League::NFL, kRecapCollege = 1 << (int) League::NCAA;
inline uint16_t recap_bit(uint8_t league) { return league < 16 ? (uint16_t) (1u << league) : 0; }

// How long each card stays up.
inline uint32_t recap_card_ms(RecapKind k) {
  switch (k) {
    case RecapKind::HEADER:
    case RecapKind::LATER_HEADER:
      return 4000;
    case RecapKind::FINAL:
    case RecapKind::LATER:
      return 7000;
    default:
      return 6000;
  }
}

// One loop of cards for the leagues in `leagues` (recap_bit() per league):
// "Later today" and the games still to come at `now`, in kickoff order; then
// per day a header and its finals; then the next kickoff when there is one
// and nothing is left today. The cap is shared evenly between the leagues
// that have cards, later today first; a day the cap leaves empty gets no
// header.
inline std::vector<RecapCard> recap_cards(const std::vector<RecapDay> &days, uint16_t leagues, bool have_next,
                                          int64_t now = 0, size_t cap = kRecapCap) {
  size_t later_have[kLeagueCount] = {}, final_have[kLeagueCount] = {};
  for (const auto &d : days) {
    if (d.league >= kLeagueCount || !(leagues & recap_bit(d.league)))
      continue;
    final_have[d.league] += d.finals.size();
    for (const auto &p : d.later)
      if (p.kickoff_epoch > now)
        later_have[d.league]++;
  }
  size_t later_left[kLeagueCount], final_left[kLeagueCount];
  recap_share(later_have, std::min(cap, kRecapLaterCap), later_left);
  size_t used = 0;
  for (size_t l = 0; l < kLeagueCount; l++)
    used += later_left[l];
  recap_share(final_have, cap - used, final_left);

  std::vector<RecapCard> later;
  for (size_t i = 0; i < days.size(); i++) {
    const RecapDay &d = days[i];
    if (d.league >= kLeagueCount || !(leagues & recap_bit(d.league)))
      continue;
    // Kept best first, so a league's share is its best games.
    for (size_t f = 0; f < d.later.size() && later_left[d.league] > 0; f++) {
      if (d.later[f].kickoff_epoch <= now)
        continue;  // under way, or over: the next scan has it
      later.push_back({RecapKind::LATER, (uint8_t) i, (uint16_t) f});
      later_left[d.league]--;
    }
  }
  std::stable_sort(later.begin(), later.end(), [&days](const RecapCard &a, const RecapCard &b) {
    return days[a.day].later[a.final].kickoff_epoch < days[b.day].later[b.final].kickoff_epoch;
  });
  std::vector<RecapCard> out;
  if (!later.empty()) {
    out.push_back({RecapKind::LATER_HEADER, 0, 0});
    out.insert(out.end(), later.begin(), later.end());
  }
  for (size_t i = 0; i < days.size(); i++) {
    const RecapDay &d = days[i];
    if (d.league >= kLeagueCount || !(leagues & recap_bit(d.league)))
      continue;
    size_t take = std::min(d.finals.size(), final_left[d.league]);
    if (take == 0)
      continue;
    out.push_back({RecapKind::HEADER, (uint8_t) i, 0});
    for (size_t f = 0; f < take; f++)
      out.push_back({RecapKind::FINAL, (uint8_t) i, (uint16_t) f});
    final_left[d.league] -= take;
  }
  if (have_next && later.empty())
    out.push_back({RecapKind::NEXT, 0, 0});
  return out;
}

// Panels flashed before "no team" existed saved Dallas (ESPN id 6) as the
// team on first boot. Whether that Dallas was ever really picked: not when
// setup was never finished, and not on a panel that is in a Live mode,
// which is how an owner who chose only a mode ends up with it. Dallas in My
// team or Favorites mode is kept, since there is no telling it apart from a
// pick. Any other team was picked.
inline bool legacy_team_unpicked(uint8_t league, uint32_t team_id, bool setup_done, bool live_mode) {
  if (league != (uint8_t) League::NFL || team_id != 6)
    return false;
  return !setup_done || live_mode;
}

}  // namespace espn
