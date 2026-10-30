// Playlists: named groups of the owner's favorites. Favorite teams mode plays
// one of them, or every favorite when none is chosen, and WizMote buttons 1
// to 4 pick the first four. A playlist is kept as a mask over the favorites
// list, so it costs no team storage of its own and only the favorites are
// ever read from ESPN. Beside them, a sport row per sport in the favorites
// plays that sport's favorites; it is worked out from the favorites each
// time, so nothing about it is saved but which one plays.
// Pure, no ESPHome includes, so the host tests cover it.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "leagues.h"
#include "message.h"
#include "set_request.h"

namespace espn {

constexpr int kListMax = 16;
constexpr size_t kListNameMax = 16;  // bytes

// Per-playlist options: a schedule, Only this list's games and the list's
// own Only today's games. They ride in the lists= text as tokens among the
// playlist's teams, each starting with '@', so a playlist's options travel
// with it through a reorder or a rename, and firmware before them drops them
// as teams it doesn't know:
//   Sundays=nfl:6,ncaa:251,@d:0,@t:11:00-23:00,@only,@today:1
//   @d:<digits>          the days it is scheduled, 0 Sunday to 6 Saturday
//   @t:HH:MM-HH:MM       its hours on those days, local time; left out = all
//                        day; an end before the start runs into the next morning
//   @only                a favorite outside the list no longer takes the panel
//                        near its kickoff while the list plays
//   @today:1 / @today:0  the list's own Only today's games; left out = the switch
struct ListOpt {
  uint8_t days{0};    // bit d = day d is scheduled; 0 = no schedule
  uint16_t start{0};  // minutes after local midnight
  uint16_t end{0};    // start == end: all day
  uint8_t flags{0};   // LISTOPT_*
  bool operator==(const ListOpt &o) const {
    return days == o.days && start == o.start && end == o.end && flags == o.flags;
  }
} __attribute__((packed));
constexpr uint8_t LISTOPT_ONLY = 1;       // Only this list's games
constexpr uint8_t LISTOPT_TODAY_SET = 2;  // the list has its own Only today's games
constexpr uint8_t LISTOPT_TODAY = 4;      // and it is on

struct ListDef {
  std::string name;
  std::vector<TeamRef> teams;
  ListOpt opt;
};

// "11:00" -> minutes after midnight, -1 for anything else (0:00 to 23:59).
inline int list_parse_time(const std::string &v) {
  size_t colon = v.find(':');
  if (colon == std::string::npos || colon == 0 || colon > 2 || v.size() - colon != 3)
    return -1;
  for (size_t i = 0; i < v.size(); i++)
    if (i != colon && (v[i] < '0' || v[i] > '9'))
      return -1;
  int h = atoi(v.substr(0, colon).c_str()), m = atoi(v.substr(colon + 1).c_str());
  if (h > 23 || m > 59)
    return -1;
  return h * 60 + m;
}

inline std::string list_time_text(uint16_t minutes) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned) (minutes / 60) % 24, (unsigned) minutes % 60);
  return buf;
}

// One '@' token of a playlist's teams into its options. A token it can't
// read is dropped, like a team it doesn't know.
inline void list_opt_token(const std::string &tok, ListOpt &o) {
  if (tok.compare(0, 3, "@d:") == 0) {
    uint8_t days = 0;
    for (size_t i = 3; i < tok.size(); i++)
      if (tok[i] >= '0' && tok[i] <= '6')
        days |= (uint8_t) (1u << (tok[i] - '0'));
    o.days = days;
  } else if (tok.compare(0, 3, "@t:") == 0) {
    size_t dash = tok.find('-', 3);
    if (dash == std::string::npos)
      return;
    int start = list_parse_time(tok.substr(3, dash - 3)), end = list_parse_time(tok.substr(dash + 1));
    if (start < 0 || end < 0)
      return;
    o.start = (uint16_t) start;
    o.end = (uint16_t) end;
  } else if (tok == "@only") {
    o.flags |= LISTOPT_ONLY;
  } else if (tok == "@today:1" || tok == "@today:0") {
    o.flags = (uint8_t) ((o.flags | LISTOPT_TODAY_SET) & ~LISTOPT_TODAY);
    if (tok[7] == '1')
      o.flags |= LISTOPT_TODAY;
  }
}

// The options back as their tokens, ",@d:0,@t:11:00-23:00,@only" ("" for none),
// for the page and the settings backup to put after a playlist's teams.
inline std::string list_opt_text(const ListOpt &o) {
  std::string out;
  if (o.days != 0) {
    out += ",@d:";
    for (int d = 0; d < 7; d++)
      if ((o.days >> d) & 1u)
        out += (char) ('0' + d);
    if (o.start != o.end)
      out += ",@t:" + list_time_text(o.start) + "-" + list_time_text(o.end);
  }
  if (o.flags & LISTOPT_ONLY)
    out += ",@only";
  if (o.flags & LISTOPT_TODAY_SET)
    out += (o.flags & LISTOPT_TODAY) ? ",@today:1" : ",@today:0";
  return out;
}

// Is the list's schedule on at this local day of the week (0 Sunday) and
// minute after midnight? Hours that end before they start run from the
// start on a scheduled day into the next morning, Saturday 20:00 to 02:00
// covering Sunday 1 AM; the same start and end is all day. Local time, so
// the hours follow the clock through a daylight saving change.
inline bool list_scheduled(const ListOpt &o, int dow, int minute) {
  if (o.days == 0 || dow < 0 || dow > 6)
    return false;
  bool today = ((o.days >> dow) & 1u) != 0;
  if (o.start == o.end)
    return today;
  if (o.start < o.end)
    return today && minute >= o.start && minute < o.end;
  if (today && minute >= o.start)
    return true;
  return ((o.days >> ((dow + 6) % 7)) & 1u) != 0 && minute < o.end;
}

// The playlist a schedule puts on now, 1-based in the playlists' order,
// 0 for none. Two schedules at once: the list higher in the order wins.
inline int scheduled_list(const ListOpt *opts, int count, int dow, int minute) {
  for (int n = 0; n < count; n++)
    if (list_scheduled(opts[n], dow, minute))
      return n + 1;
  return 0;
}

// Only today's games for a list: its own setting, else the switch.
inline bool list_today(const ListOpt &o, bool global) {
  return (o.flags & LISTOPT_TODAY_SET) ? (o.flags & LISTOPT_TODAY) != 0 : global;
}

// What the schedule tick does, given the list the schedules want now (0
// none), the one the schedule last put on (applied, 0 none) and the list
// playing. A game the panel is locked on finishes first (locked: nothing
// yet). A start remembers what played before; an end puts it back, unless
// the owner picked another list meanwhile, which then stays.
struct SchedStep {
  bool start{false};    // play `list` and remember `active` as before
  bool restore{false};  // play `before` again
  int list{0};
};
inline SchedStep schedule_step(int want, int applied, int active, bool locked) {
  SchedStep st;
  if (want == applied || locked)
    return st;
  if (want != 0) {
    st.start = true;
    st.list = want;
  } else if (active == applied) {
    st.restore = true;
  }
  return st;
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
// and no more teams in one than the favorites list holds.
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
    size_t p = 0;
    while (p < refs.size() && d.teams.size() < fav_max) {
      size_t comma = refs.find(',', p);
      std::string ref = refs.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
      p = comma == std::string::npos ? refs.size() : comma + 1;
      if (!ref.empty() && ref[0] == '@') {
        list_opt_token(ref, d.opt);
        continue;
      }
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

// ---- Sport rows ---------------------------------------------------------------

// Playing's and Home Assistant's names for them, in Sport order (the keys
// are leagues.h's kSportKeys). "All ..." beside All favorites, so an owner's
// own playlist named Football never shares a name with one.
constexpr const char *kSportRowNames[kSportCount] = {"All football", "All baseball", "All basketball", "All hockey",
                                                     "All soccer"};

// The bit of a favorite's sport (bit s = Sport s), 0 for a league byte this
// firmware doesn't know.
inline uint8_t sport_bit(uint8_t league) {
  const LeagueInfo *l = league_info((League) league);
  return l != nullptr ? (uint8_t) (1u << (uint8_t) l->sport) : 0;
}

// The sports with a row in Playing, from the bits of every favorite's sport:
// all of them with two sports or more, none with one, since All favorites
// already is that sport.
inline uint8_t sport_rows(uint8_t sports) { return (sports & (sports - 1)) != 0 ? sports : 0; }

// Is row (1 + Sport) one of these rows?
inline bool sport_row_has(uint8_t rows, uint8_t row) {
  return row >= 1 && row <= kSportCount && ((rows >> (row - 1)) & 1u) != 0;
}

// What a WizMote button does now (set_request.h RemoteTarget), against the
// favorites, the playlists' names and the sport rows the favorites make. A
// target that is gone (its team left the favorites, its playlist its name,
// its sport its favorites) falls back to the button's default, favorite n.
struct RemoteAction {
  RemoteKind kind{RemoteKind::FAVORITE};
  int fav_slot{0};        // FAVORITE, TEAM: the favorite to jump to, 1-based
  int list{0};            // LIST: the playlist to play; ALL: 0
  uint8_t sport_row{0};   // SPORT: the row to play, 1 + Sport
  bool gone{false};       // the target is gone: this is the default instead
};
inline RemoteAction remote_resolve(uint8_t button, const RemoteTarget &t, const std::vector<TeamRef> &favs,
                                   const std::vector<std::string> &names) {
  RemoteAction a;
  a.fav_slot = button;
  switch ((RemoteKind) t.kind) {
    case RemoteKind::TEAM:
      for (size_t i = 0; i < favs.size(); i++)
        if (favs[i].id != 0 && favs[i].league == t.league && favs[i].id == t.id) {
          a.kind = RemoteKind::TEAM;
          a.fav_slot = (int) i + 1;
          return a;
        }
      break;
    case RemoteKind::LIST:
      for (size_t n = 0; n < names.size(); n++)
        if (names[n] == remote_name(t)) {
          a.kind = RemoteKind::LIST;
          a.list = (int) n + 1;
          return a;
        }
      break;
    case RemoteKind::SPORT: {
      uint8_t sports = 0;
      for (const auto &f : favs)
        if (f.id != 0)
          sports |= sport_bit(f.league);
      if (sport_row_has(sport_rows(sports), (uint8_t) (t.sport + 1))) {
        a.kind = RemoteKind::SPORT;
        a.sport_row = (uint8_t) (t.sport + 1);
        return a;
      }
      break;
    }
    case RemoteKind::ALL:
      a.kind = RemoteKind::ALL;
      return a;
    case RemoteKind::NONE:
      a.kind = RemoteKind::NONE;
      return a;
    default:
      return a;
  }
  a.gone = true;
  return a;
}

}  // namespace espn
