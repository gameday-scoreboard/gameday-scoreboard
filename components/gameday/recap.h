// The finals rotation: what a live mode shows on a panel with no team picked
// when nothing is live. A header per game day ("Saturday's college scores"),
// that day's finals, and a "Next: Sat 11:00 AM" card, round and round. Pure,
// with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "espn_parse.h"

namespace espn {

// A day the way ESPN's scoreboard `dates=` parameter counts it: US Eastern,
// shifted by a flat five hours like scan_url().
inline int32_t espn_day(int64_t epoch) {
  int64_t s = epoch - 5 * 3600;
  return (int32_t) (s >= 0 ? s / 86400 : (s - 86399) / 86400);
}

// Back to an epoch inside that day (noon Eastern), for scan_url().
inline int64_t espn_day_epoch(int32_t day) { return (int64_t) day * 86400 + 17 * 3600; }

// One game day of finals for one league, newest day first in a list.
struct RecapDay {
  uint8_t league{0};
  int32_t day{0};
  std::vector<Final> finals;
};

// Walking back for older finals stops once a league has this many.
static const size_t kRecapMinFinals = 4;
// How far back the walk, and the forward search for the next kickoff, look.
static const int kRecapMaxDays = 7;
// Finals in one loop, shared between the leagues shown. Two logos each, so 22
// stays inside the 48-logo cache (four are kept pinned) and a loop does not
// download the same logos again every time round.
static const size_t kRecapCap = 22;

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

inline const char *recap_league_word(uint8_t league) { return league == (uint8_t) League::NFL ? "NFL" : "college"; }

// "Saturday's college scores"
inline std::string recap_header(const RecapDay &d, int32_t today) {
  return recap_day_word(d.day, today) + " " + recap_league_word(d.league) + " scores";
}

// Ranked games first (best rank on either side), then by kickoff, the way
// ESPN lists them. NFL games carry no rank and keep kickoff order.
inline void recap_sort(std::vector<Final> &finals) {
  std::stable_sort(finals.begin(), finals.end(), [](const Final &a, const Final &b) {
    if (a.rank != b.rank)
      return a.rank < b.rank;
    return a.kickoff_epoch < b.kickoff_epoch;
  });
}

// Puts a day's finals into the list, replacing what it had for that league
// and day (a scan later the same day has more of them), newest day first.
// A day keeps only the finals one loop could show; an empty day is not
// stored.
inline void recap_merge(std::vector<RecapDay> &days, RecapDay fresh) {
  for (auto it = days.begin(); it != days.end(); ++it) {
    if (it->league == fresh.league && it->day == fresh.day) {
      days.erase(it);
      break;
    }
  }
  if (fresh.finals.empty())
    return;
  recap_sort(fresh.finals);
  if (fresh.finals.size() > kRecapCap)
    fresh.finals.resize(kRecapCap);
  auto at = days.begin();
  while (at != days.end() && (at->day > fresh.day || (at->day == fresh.day && at->league < fresh.league)))
    ++at;
  days.insert(at, std::move(fresh));
}

// Drops days a week old or more, and per league every day older than the
// ones that already make up kRecapMinFinals: a new game day pushes out the
// last one instead of piling up behind it.
inline void recap_trim(std::vector<RecapDay> &days, int32_t today) {
  size_t have[2] = {0, 0};
  std::vector<RecapDay> kept;
  for (auto &d : days) {
    size_t l = d.league == (uint8_t) League::NFL ? 0 : 1;
    if (today - d.day >= kRecapMaxDays || have[l] >= kRecapMinFinals)
      continue;
    have[l] += d.finals.size();
    kept.push_back(std::move(d));
  }
  days = std::move(kept);
}

inline size_t recap_count(const std::vector<RecapDay> &days, uint8_t league) {
  size_t n = 0;
  for (const auto &d : days)
    if (d.league == league)
      n += d.finals.size();
  return n;
}

enum class RecapKind : uint8_t { HEADER, FINAL, NEXT };

struct RecapCard {
  RecapKind kind{RecapKind::NEXT};
  uint8_t day{0};     // index into the day list
  uint16_t final{0};  // index into that day's finals
};

// Which leagues a mode reads, for recap_cards().
static const uint8_t kRecapNfl = 1, kRecapCollege = 2;
inline uint8_t recap_bit(uint8_t league) { return league == (uint8_t) League::NFL ? kRecapNfl : kRecapCollege; }

// How long each card stays up.
inline uint32_t recap_card_ms(RecapKind k) {
  switch (k) {
    case RecapKind::HEADER:
      return 4000;
    case RecapKind::FINAL:
      return 7000;
    default:
      return 6000;
  }
}

// One loop of cards for the leagues in `leagues` (kRecapNfl | kRecapCollege):
// per day a header and its finals, then the next kickoff when there is one.
// The cap is split evenly between the leagues that have finals; a day the
// cap leaves empty gets no header.
inline std::vector<RecapCard> recap_cards(const std::vector<RecapDay> &days, uint8_t leagues, bool have_next,
                                          size_t cap = kRecapCap) {
  bool has[2] = {false, false};
  for (const auto &d : days)
    if (leagues & recap_bit(d.league))
      has[d.league == (uint8_t) League::NFL ? 0 : 1] = true;
  size_t shown = (has[0] ? 1 : 0) + (has[1] ? 1 : 0);
  size_t per = shown > 0 ? cap / shown : 0;
  size_t used[2] = {0, 0};
  std::vector<RecapCard> out;
  for (size_t i = 0; i < days.size(); i++) {
    if (!(leagues & recap_bit(days[i].league)))
      continue;
    size_t l = days[i].league == (uint8_t) League::NFL ? 0 : 1;
    size_t take = std::min(days[i].finals.size(), per - std::min(per, used[l]));
    if (take == 0)
      continue;
    out.push_back({RecapKind::HEADER, (uint8_t) i, 0});
    for (size_t f = 0; f < take; f++)
      out.push_back({RecapKind::FINAL, (uint8_t) i, (uint16_t) f});
    used[l] += take;
  }
  if (have_next)
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
