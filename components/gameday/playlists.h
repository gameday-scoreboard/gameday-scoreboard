// Playlists: named groups of the owner's favorites. Favorite teams mode plays
// one of them, or every favorite when none is chosen, and WizMote buttons 1
// to 4 pick the first four. A playlist is kept as a mask over the favorites
// list, so it costs no team storage of its own and only the favorites are
// ever read from ESPN.
// Pure, no ESPHome includes, so the host tests cover it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "message.h"
#include "night.h"
#include "set_request.h"

namespace espn {

constexpr int kListMax = 16;
constexpr size_t kListNameMax = 16;  // bytes

// A playlist's own options. Out of the box a playlist has no schedule, lets
// any favorite take the panel near its kickoff and follows the panel's Only
// today's games switch.
struct ListOpts {
  uint8_t days{0};   // its schedule: bit 0 Sunday to bit 6 Saturday, 0 = none
  uint16_t from{0};  // minutes after midnight on those days
  uint16_t to{0};    // the same as from is all day; earlier runs past midnight
  bool only{false};  // only its own favorites take the panel near kickoff
  int8_t today{-1};  // Only today's games: -1 follows the panel's switch, 0 off, 1 on
  bool operator==(const ListOpts &o) const {
    return days == o.days && from == o.from && to == o.to && only == o.only && today == o.today;
  }
};

struct ListDef {
  std::string name;
  std::vector<TeamRef> teams;
  ListOpts opts;
  bool has_opts{false};  // the request carried options; without, it keeps the ones it had
};

// "65,11:00,23:00,1,-1": days, from, to, only, today, the part of a lists=
// playlist after '@'. False, with o untouched, when any part doesn't read.
inline bool parse_list_opts(const std::string &v, ListOpts &o) {
  std::vector<std::string> part;
  size_t start = 0;
  while (start <= v.size() && part.size() < 6) {
    size_t comma = v.find(',', start);
    part.push_back(v.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
    start = comma == std::string::npos ? v.size() + 1 : comma + 1;
  }
  int days = 0, only = 0, today = 0;
  if (part.size() != 5 || !parse_whole_int(part[0], days) || days < 0 || days > 127 ||
      !parse_whole_int(part[3], only) || only < 0 || only > 1 || !parse_whole_int(part[4], today) || today < -1 ||
      today > 1)
    return false;
  int from = night_parse_time(part[1]), to = night_parse_time(part[2]);
  if (from < 0 || to < 0)
    return false;
  o.days = (uint8_t) days;
  o.from = (uint16_t) from;
  o.to = (uint16_t) to;
  o.only = only == 1;
  o.today = (int8_t) today;
  return true;
}

inline std::string list_opts_text(const ListOpts &o) {
  return std::to_string(o.days) + "," + night_time_text(o.from) + "," + night_time_text(o.to) + "," +
         (o.only ? "1" : "0") + "," + std::to_string(o.today);
}

// Is the schedule on at this minute of this weekday (0 Sunday)? A window
// belongs to the day it starts on, so Saturday 8 PM to 1 AM still runs at
// 12:30 AM Sunday.
inline bool list_sched_on(const ListOpts &o, int wday, int minute) {
  if (o.days == 0 || wday < 0 || wday > 6)
    return false;
  int len = ((int) o.to - (int) o.from + (int) kMinutesPerDay) % (int) kMinutesPerDay;
  if (len == 0)
    len = kMinutesPerDay;
  if (((o.days >> wday) & 1u) && minute >= o.from && minute - o.from < len)
    return true;
  int yesterday = (wday + 6) % 7;
  return ((o.days >> yesterday) & 1u) && minute + (int) kMinutesPerDay - o.from < len;
}

// The playlist the schedules want now, 1 to the count, 0 for none. When two
// overlap the one higher in the order wins.
inline uint8_t list_sched_want(const ListOpts *opts, uint8_t count, int wday, int minute) {
  for (uint8_t n = 0; n < count; n++)
    if (list_sched_on(opts[n], wday, minute))
      return n + 1;
  return 0;
}

// What the schedules did last, kept in flash so a restart mid-window carries on.
struct ListSched {
  uint8_t on{0};      // the playlist a schedule put on, 0 for none
  uint8_t before{0};  // what was playing before it, to go back to
};

// One step: the playlist to switch to now, or -1 to leave it. active is
// playing now, want is list_sched_want's, held is a favorite's game holding
// the panel, which finishes first. A playlist picked by hand while a
// schedule runs stays when the window ends.
inline int list_sched_step(ListSched &s, uint8_t active, uint8_t want, bool held) {
  if (want == s.on || held)
    return -1;
  if (want != 0) {
    // From one schedule straight into another, back is still what played before both.
    if (s.on == 0 || active != s.on)
      s.before = active;
    s.on = want;
    return want == active ? -1 : want;
  }
  bool ours = active == s.on;
  uint8_t back = s.before;
  s = ListSched{};
  return ours && back != active ? back : -1;
}

// One line of printable text, at most kListNameMax bytes cut at a character
// boundary. ';' and '=' separate playlists in a set request, so they go too.
inline std::string list_name_clean(const std::string &in) {
  std::string kept;
  for (char c : in)
    if (c != ';' && c != '=')
      kept += c;
  return message_clean(kept, kListNameMax, false);
}

// "Football=nfl:6,ncaa:251;Hoops=nba:6" -> playlists in order. "" clears
// them all. A playlist with no name after cleaning is left out; unknown
// teams and repeats in one playlist drop out. At most kListMax playlists,
// and no more teams in one than the favorites list holds. A playlist's
// options follow its teams after '@' ("Football=nfl:6@1,11:00,23:00,1,-1",
// parse_list_opts); without them, or with options that don't read, it keeps
// the ones it had.
inline std::vector<ListDef> parse_lists(const std::string &v, size_t fav_max) {
  std::vector<ListDef> out;
  size_t start = 0;
  while (start < v.size() && out.size() < (size_t) kListMax) {
    size_t semi = v.find(';', start);
    std::string part = v.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
    start = semi == std::string::npos ? v.size() : semi + 1;
    size_t eq = part.find('=');
    ListDef d;
    d.name = list_name_clean(part.substr(0, eq));
    if (d.name.empty())
      continue;
    std::string refs = eq == std::string::npos ? "" : part.substr(eq + 1);
    size_t at = refs.find('@');
    if (at != std::string::npos) {
      d.has_opts = parse_list_opts(refs.substr(at + 1), d.opts);
      refs.resize(at);
    }
    size_t p = 0;
    while (p < refs.size() && d.teams.size() < fav_max) {
      size_t comma = refs.find(',', p);
      std::string ref = refs.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
      p = comma == std::string::npos ? refs.size() : comma + 1;
      TeamRef t;
      if (!parse_team_ref(ref, t.league, t.id) || !known_team(t.league, t.id))
        continue;
      bool dup = false;
      for (const auto &o : d.teams)
        dup = dup || o == t;
      if (!dup)
        d.teams.push_back(t);
    }
    out.push_back(d);
  }
  return out;
}

// A bit per favorite (bit i = favs[i]) for the teams of a playlist that are
// favorites. A team that isn't one has nowhere to go and is left out.
inline uint16_t list_mask(const std::vector<TeamRef> &teams, const std::vector<TeamRef> &favs) {
  uint16_t mask = 0;
  for (size_t i = 0; i < favs.size() && i < 16; i++)
    for (const auto &t : teams)
      if (favs[i].id != 0 && favs[i] == t)
        mask |= (uint16_t) (1u << i);
  return mask;
}

// The same teams after the favorites list changed from before to after
// (reordered, a team added or taken out): each team keeps its playlists
// wherever it moved, and a team that left the favorites leaves them.
inline uint16_t remap_mask(uint16_t mask, const std::vector<TeamRef> &before, const std::vector<TeamRef> &after) {
  std::vector<TeamRef> teams;
  for (size_t i = 0; i < before.size() && i < 16; i++)
    if ((mask >> i) & 1u)
      teams.push_back(before[i]);
  return list_mask(teams, after);
}

// Does favorite i play in the playlist with this mask? Every favorite does
// when no playlist is chosen, and so does every one in a playlist with no
// teams left, so choosing it never leaves the panel blank.
inline bool list_has(uint16_t mask, bool chosen, size_t i) {
  return !chosen || mask == 0 || (i < 16 && ((mask >> i) & 1u) != 0);
}

}  // namespace espn
