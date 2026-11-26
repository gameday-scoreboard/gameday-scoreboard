// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Playlists: named groups of the owner's favorites. Favorite teams mode plays
// one of them, or every favorite when none is chosen, and WizMote buttons 1
// to 4 pick the first four. A playlist is kept as a mask over the favorites
// list, so it costs no team storage of its own and only the favorites are
// ever read from ESPN. Beside them, a sport row per sport in the favorites
// plays that sport's favorites; it is worked out from the favorites each
// time, so nothing about it is saved but which one plays. A playlist can
// also hold whole sports (list_league_token), and then plays their live games.
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

// A playlist's look: a color and an icon, each from a small fixed set, so
// the name the panel shows when the list comes on is known across the room.
// Like the options they ride in the lists= text as '@' tokens:
//   @c:<color>   one of kListColors' keys: the name, and the icon, in it
//   @i:<icon>    one of kListIcons' keys: drawn above the name
// Kept as one byte per playlist, the color's number (1 up, 0 none) in the
// low four bits and the icon's in the high four.
struct ListColor {
  const char *key;
  uint32_t rgb;  // as the panel draws it; the device page uses the same
};
constexpr int kListColorCount = 8;
constexpr ListColor kListColors[kListColorCount] = {
    {"red", 0xFF2020},  {"orange", 0xFF8000}, {"yellow", 0xFFD800}, {"green", 0x20D020},
    {"teal", 0x00C8C8}, {"blue", 0x3070FF},   {"purple", 0xA040FF}, {"pink", 0xFF40B0},
};

// 11 by 11 pixel art, a row each from the top, bit 10 the left column.
constexpr int kListIconSize = 11;
struct ListIcon {
  const char *key;
  uint16_t rows[kListIconSize];
};
#define GD_ROW(s)                                                                                          \
  (uint16_t) (((s)[0] == '#') << 10 | ((s)[1] == '#') << 9 | ((s)[2] == '#') << 8 | ((s)[3] == '#') << 7 | \
              ((s)[4] == '#') << 6 | ((s)[5] == '#') << 5 | ((s)[6] == '#') << 4 | ((s)[7] == '#') << 3 | \
              ((s)[8] == '#') << 2 | ((s)[9] == '#') << 1 | ((s)[10] == '#'))
constexpr int kListIconCount = 6;
constexpr ListIcon kListIcons[kListIconCount] = {
    {"star",
     {GD_ROW(".....#....."), GD_ROW("....###...."), GD_ROW("....###...."), GD_ROW("###########"),
      GD_ROW(".#########."), GD_ROW("..#######.."), GD_ROW("...#####..."), GD_ROW("..#######.."),
      GD_ROW("..###.###.."), GD_ROW(".###...###."), GD_ROW(".#.......#.")}},
    {"heart",
     {GD_ROW("..........."), GD_ROW("..##...##.."), GD_ROW(".####.####."), GD_ROW("###########"),
      GD_ROW("###########"), GD_ROW("###########"), GD_ROW(".#########."), GD_ROW("..#######.."),
      GD_ROW("...#####..."), GD_ROW("....###...."), GD_ROW(".....#.....")}},
    {"bolt",
     {GD_ROW("......####."), GD_ROW(".....####.."), GD_ROW("....####..."), GD_ROW("...####...."),
      GD_ROW("..########."), GD_ROW(".########.."), GD_ROW("....####..."), GD_ROW("...####...."),
      GD_ROW("..####....."), GD_ROW("..###......"), GD_ROW(".##........")}},
    {"crown",
     {GD_ROW("..........."), GD_ROW("#....#....#"), GD_ROW("##..###..##"), GD_ROW("###.###.###"),
      GD_ROW("###########"), GD_ROW("###########"), GD_ROW("###########"), GD_ROW("..........."),
      GD_ROW("###########"), GD_ROW("###########"), GD_ROW("...........")}},
    {"flag",
     {GD_ROW("##........."), GD_ROW("#####......"), GD_ROW("########..."), GD_ROW("##########."),
      GD_ROW("########..."), GD_ROW("#####......"), GD_ROW("##........."), GD_ROW("##........."),
      GD_ROW("##........."), GD_ROW("##........."), GD_ROW("##.........")}},
    {"trophy",
     {GD_ROW(".#########."), GD_ROW("##.#####.##"), GD_ROW("#..#####..#"), GD_ROW("#..#####..#"),
      GD_ROW(".#.#####.#."), GD_ROW("..#######.."), GD_ROW("....###...."), GD_ROW(".....#....."),
      GD_ROW(".....#....."), GD_ROW("...#####..."), GD_ROW("..#######..")}},
};
#undef GD_ROW

inline uint8_t list_color(uint8_t mark) { return (uint8_t) ((mark & 0x0F) <= kListColorCount ? mark & 0x0F : 0); }
inline uint8_t list_icon(uint8_t mark) { return (uint8_t) ((mark >> 4) <= kListIconCount ? mark >> 4 : 0); }
// The key of a mark's color or icon, "" for none.
inline const char *list_color_key(uint8_t mark) {
  uint8_t c = list_color(mark);
  return c != 0 ? kListColors[c - 1].key : "";
}
inline const char *list_icon_key(uint8_t mark) {
  uint8_t i = list_icon(mark);
  return i != 0 ? kListIcons[i - 1].key : "";
}
// What the panel draws the name and the icon in: the list's color, else white.
inline uint32_t list_rgb(uint8_t mark) {
  uint8_t c = list_color(mark);
  return c != 0 ? kListColors[c - 1].rgb : 0xFFFFFF;
}

// One '@c:' or '@i:' token into a playlist's mark. False for any other
// token; a color or icon it doesn't know is no color or no icon.
inline bool list_mark_token(const std::string &tok, uint8_t &mark) {
  bool color = tok.compare(0, 3, "@c:") == 0, icon = tok.compare(0, 3, "@i:") == 0;
  if (!color && !icon)
    return false;
  std::string key = tok.substr(3);
  uint8_t n = 0;
  if (color) {
    for (int i = 0; i < kListColorCount; i++)
      if (key == kListColors[i].key)
        n = (uint8_t) (i + 1);
    mark = (uint8_t) ((mark & 0xF0) | n);
  } else {
    for (int i = 0; i < kListIconCount; i++)
      if (key == kListIcons[i].key)
        n = (uint8_t) (i + 1);
    mark = (uint8_t) ((mark & 0x0F) | (n << 4));
  }
  return true;
}

// The mark back as its tokens, ",@c:blue,@i:star" ("" for none).
inline std::string list_mark_text(uint8_t mark) {
  std::string out;
  if (list_color(mark) != 0)
    out += std::string(",@c:") + list_color_key(mark);
  if (list_icon(mark) != 0)
    out += std::string(",@i:") + list_icon_key(mark);
  return out;
}

// A playlist's sports: whole leagues beside its teams, '@lg:<key>' tokens
// like the options ("Sunday=nfl:6,nba:6,@lg:nfl"), a bit per League. A
// playlist with one plays as Live games over those leagues, and its own
// teams take the panel while their game is on, whatever their league
// (sport_list_scan, sport_list_keeps). '@lg' alone keeps the sports the
// playlist of that name has now: the settings import writes it for a
// playlist sent with no sports field, from an app that doesn't know them yet.
inline bool list_league_token(const std::string &tok, uint16_t &leagues, bool *keep = nullptr) {
  if (tok == "@lg") {
    if (keep != nullptr)
      *keep = true;
    return true;
  }
  if (tok.compare(0, 4, "@lg:") != 0)
    return false;
  const LeagueInfo *l = league_by_key(tok.c_str() + 4);
  if (l != nullptr && !leaderboard_sport(l->sport))
    leagues |= (uint16_t) (1u << (uint8_t) l->league);
  return true;
}

// Only the leagues this firmware has, and has games for.
inline uint16_t list_leagues_known(uint16_t leagues) {
  uint16_t known = 0;
  for (const auto &l : kLeagues)
    if (!leaderboard_sport(l.sport))
      known |= (uint16_t) (1u << (uint8_t) l.league);
  return leagues & known;
}

// The sports back as their tokens, in kLeagues order: ",@lg:nfl,@lg:nba" ("" for none).
inline std::string list_leagues_text(uint16_t leagues) {
  std::string out;
  for (const auto &l : kLeagues)
    if ((leagues >> (uint8_t) l.league) & 1u)
      out += std::string(",@lg:") + l.key;
  return out;
}

// The leagues a sport playlist's scan reads, in kLeagues order: its own,
// and the league of each of its teams, read for that team's game alone.
inline std::vector<League> sport_list_scan(uint16_t leagues, const std::vector<TeamRef> &teams) {
  uint16_t mask = list_leagues_known(leagues);
  for (const auto &t : teams)
    if (t.id != 0 && t.league < 16)
      mask |= (uint16_t) (1u << t.league);
  mask = list_leagues_known(mask);
  std::vector<League> out;
  for (const auto &l : kLeagues)
    if ((mask >> (uint8_t) l.league) & 1u)
      out.push_back(l.league);
  return out;
}

// Does a live game a sport playlist's scan found stay in it? Every game in
// its leagues does; from a league it reads only for its teams, only their
// games. leagues 0 (no sport playlist playing): every game.
inline bool sport_list_keeps(uint16_t leagues, const std::vector<TeamRef> &teams, uint8_t league, uint32_t away,
                             uint32_t home) {
  if (leagues == 0 || (league < 16 && ((leagues >> league) & 1u)))
    return true;
  for (const auto &t : teams)
    if (t.id != 0 && t.league == league && (t.id == away || t.id == home))
      return true;
  return false;
}

// The sports the playing playlist holds, a bit per League, 0 when Favorite
// teams plays as ever: not in Favorite teams, a button-only team holding
// the panel, every favorite or a sport row playing, or a playlist of teams
// alone. leagues[n] belongs to playlist n + 1.
inline uint16_t sport_list_playing(bool fav_mode, bool held, uint8_t active, uint8_t count, const uint16_t *leagues) {
  if (!fav_mode || held || active == 0 || active > count || active > kListMax)
    return 0;
  return list_leagues_known(leagues[active - 1]);
}

struct ListDef {
  std::string name;
  std::vector<TeamRef> teams;
  ListOpt opt;
  uint8_t mark{0};       // its color and icon (list_color, list_icon)
  uint16_t leagues{0};   // its sports, a bit per League (list_league_token)
  bool keep_leagues{false};  // '@lg': the sports the playlist of this name has now
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
// boundary. ';' and '=' separate playlists in a set request, so they go too,
// and so do bytes that aren't UTF-8, which the state and backups can't carry.
inline std::string list_name_clean(const std::string &in) {
  std::string kept;
  for (char c : in)
    if (c != ';' && c != '=')
      kept += c;
  return message_clean(utf8_only(kept), kListNameMax, false);
}

// The new name of playlist `name` when the playlists were posted again with
// it renamed: as many playlists as before, and in its place a name that is
// new where its own is gone. "" when it was kept, moved or removed. What a
// WizMote button was set to follows a rename.
inline std::string list_renamed(const std::vector<std::string> &before, const std::vector<std::string> &after,
                                const std::string &name) {
  if (name.empty() || before.size() != after.size())
    return "";
  for (size_t n = 0; n < before.size(); n++) {
    if (before[n] != name)
      continue;
    const std::string &to = after[n];
    for (size_t k = 0; k < after.size(); k++)
      if (after[k] == name || before[k] == to)
        return "";
    return to;
  }
  return "";
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
    // Tokens after a full list of teams still count.
    while (p < refs.size()) {
      size_t comma = refs.find(',', p);
      std::string ref = refs.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
      p = comma == std::string::npos ? refs.size() : comma + 1;
      if (!ref.empty() && ref[0] == '@') {
        if (!list_mark_token(ref, d.mark) && !list_league_token(ref, d.leagues, &d.keep_leagues))
          list_opt_token(ref, d.opt);
        continue;
      }
      TeamRef t;
      if (d.teams.size() >= fav_max || !parse_team_ref(ref, t.league, t.id) || !known_team(t.league, t.id))
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

// The sports playlist n (0-based) of the new playlists gets: its own, or with
// keep_leagues the ones the playlist of its name had (after a rename, the
// one it was renamed from), none for a new name.
inline uint16_t list_leagues_after(const std::vector<ListDef> &lists, size_t n, const std::vector<std::string> &old_names,
                                   const uint16_t *old_leagues) {
  const ListDef &d = lists[n];
  if (!d.keep_leagues)
    return list_leagues_known(d.leagues);
  std::vector<std::string> new_names;
  for (const auto &l : lists)
    new_names.push_back(l.name);
  for (size_t k = 0; k < old_names.size(); k++)
    if (old_names[k] == d.name || list_renamed(old_names, new_names, old_names[k]) == d.name)
      return list_leagues_known(old_leagues[k] | d.leagues);
  return list_leagues_known(d.leagues);
}

// The playlists' names and count in one number (FNV-1a), saved beside
// their sports: firmware without sports edits the playlists and leaves the
// sports as they were, so a mismatch on load means they belong to other
// playlists now, and they go.
inline uint32_t list_names_sum(const char (*names)[kListNameMax], uint8_t count) {
  uint32_t h = 2166136261u;
  auto add = [&h](uint8_t b) { h = (h ^ b) * 16777619u; };
  add(count);
  for (uint8_t n = 0; n < count && n < kListMax; n++)
    for (size_t i = 0; i < kListNameMax; i++)
      add((uint8_t) names[n][i]);
  return h;
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
constexpr const char *kSportRowNames[kSportCount] = {"All football", "All baseball", "All basketball",
                                                     "All hockey",   "All soccer",   "All golf"};

// The bit of a favorite's sport (bit s = Sport s), 0 for a league byte this
// firmware doesn't know and for a leaderboard sport, which has no games to
// play in a row.
inline uint8_t sport_bit(uint8_t league) {
  const LeagueInfo *l = league_info((League) league);
  return l != nullptr && !leaderboard_sport(l->sport) ? (uint8_t) (1u << (uint8_t) l->sport) : 0;
}

// The sports with a row in Playing, from the bits of every favorite's sport:
// all of them with two sports or more, none with one, since All favorites
// already is that sport.
inline uint8_t sport_rows(uint8_t sports) { return (sports & (sports - 1)) != 0 ? sports : 0; }

// Is row (1 + Sport) one of these rows?
inline bool sport_row_has(uint8_t rows, uint8_t row) {
  return row >= 1 && row <= kSportCount && ((rows >> (row - 1)) & 1u) != 0;
}

// ---- Button-only teams -----------------------------------------------------

// A remote button set to a team that isn't a favorite adds it to the
// favorites as a button-only team: it is kept with them so the panel reads
// ESPN for it, and counts toward their 16, but it is no favorite anywhere
// else. It stays out of All favorites, playlists, the sport rows, Favorites
// take over, Live games' automatic leagues and game-day wake, and shows only
// while its button is in use. The mark is a bit per favorite (bit i =
// favs[i]), moved with the list like a playlist's mask (remap_mask), and the
// team leaves the favorites when no button is set to it any more.

// Is favorite i a team that counts as a favorite: there, and not button-only?
inline bool fav_counts(const std::vector<TeamRef> &favs, uint16_t solo, size_t i) {
  return i < favs.size() && favs[i].id != 0 && !(i < 16 && ((solo >> i) & 1u));
}

// A sport playlist's teams, the ones that take the panel while their game is
// on: its own teams only (bit i = favs[i]), never every favorite as a
// playlist with no teams plays in Favorite teams, and no button-only team.
inline std::vector<TeamRef> sport_list_teams(uint16_t mask, const std::vector<TeamRef> &favs, uint16_t solo) {
  std::vector<TeamRef> out;
  for (size_t i = 0; i < favs.size() && i < 16; i++)
    if (((mask >> i) & 1u) && fav_counts(favs, solo, i))
      out.push_back(favs[i]);
  return out;
}

// Where a button-only team goes: the first empty place after the last team,
// else the first gap; -1 when all 16 are taken.
inline int solo_slot(const std::vector<TeamRef> &favs) {
  int last = -1;
  for (size_t i = 0; i < favs.size(); i++)
    if (favs[i].id != 0)
      last = (int) i;
  for (size_t i = (size_t) (last + 1); i < favs.size(); i++)
    if (favs[i].id == 0)
      return (int) i;
  for (size_t i = 0; i < favs.size(); i++)
    if (favs[i].id == 0)
      return (int) i;
  return -1;
}

// The button-only teams no button is set to now: a bit each, to take out.
inline uint16_t solo_unused(uint16_t solo, const std::vector<TeamRef> &favs, const RemoteTarget *btn, size_t buttons) {
  uint16_t out = 0;
  for (size_t i = 0; i < favs.size() && i < 16; i++) {
    if (!((solo >> i) & 1u) || favs[i].id == 0)
      continue;
    bool used = false;
    for (size_t b = 0; b < buttons; b++)
      used = used || (btn[b].kind == (uint8_t) RemoteKind::TEAM && btn[b].league == favs[i].league &&
                      btn[b].id == favs[i].id);
    if (!used)
      out |= (uint16_t) (1u << i);
  }
  return out;
}

// What a WizMote button does now (set_request.h RemoteTarget), against the
// favorites, the playlists' names and the sport rows the favorites make. A
// target that is gone (its team left the favorites, its playlist its name,
// its sport its favorites, its effect the build, its effect playlist its
// name or its effects) falls back to the button's
// default, favorite n. solo marks the button-only teams: no sport row counts
// them.
struct RemoteAction {
  RemoteKind kind{RemoteKind::FAVORITE};
  int fav_slot{0};        // FAVORITE, TEAM: the favorite to jump to, 1-based
  int list{0};            // LIST: the playlist to play; ALL: 0; FXLIST: the effect playlist, 1-based
  uint8_t sport_row{0};   // SPORT: the row to play, 1 + Sport
  uint8_t league{0};      // LEAGUE: the league Live games reads
  bool solo{false};       // TEAM: a button-only team, played on its own while the button is in use
  bool gone{false};       // the target is gone: this is the default instead
};
// fx_lists: the effect playlists' names in order, "" for one with no
// effects (it can't play, so a button on it counts as gone).
inline RemoteAction remote_resolve(uint8_t button, const RemoteTarget &t, const std::vector<TeamRef> &favs,
                                   const std::vector<std::string> &names, uint16_t solo = 0,
                                   const std::vector<std::string> &fx_lists = {}) {
  RemoteAction a;
  a.fav_slot = button;
  switch ((RemoteKind) t.kind) {
    case RemoteKind::TEAM:
      for (size_t i = 0; i < favs.size(); i++)
        if (favs[i].id != 0 && favs[i].league == t.league && favs[i].id == t.id) {
          a.kind = RemoteKind::TEAM;
          a.fav_slot = (int) i + 1;
          a.solo = i < 16 && ((solo >> i) & 1u) != 0;
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
      for (size_t i = 0; i < favs.size(); i++)
        if (fav_counts(favs, solo, i))
          sports |= sport_bit(favs[i].league);
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
    case RemoteKind::LIVE:
    case RemoteKind::SCOREBOARD:
    case RemoteKind::CLOCK:
    case RemoteKind::SMART:
      a.kind = (RemoteKind) t.kind;
      return a;
    case RemoteKind::EFFECTS: {
      // An effect this build doesn't offer (a saved button from another
      // build) is gone; while the names aren't known it counts as there.
      const FxNames &fx = fx_names();
      if (fx.effect == nullptr || !fx.effect(remote_name(t)).empty()) {
        a.kind = RemoteKind::EFFECTS;
        return a;
      }
      break;
    }
    case RemoteKind::LEAGUE:
      if (league_info((League) t.league) == nullptr)
        break;
      a.kind = RemoteKind::LEAGUE;
      a.league = t.league;
      return a;
    case RemoteKind::FXLIST:
      for (size_t n = 0; n < fx_lists.size(); n++)
        if (!fx_lists[n].empty() && fx_lists[n] == remote_name(t)) {
          a.kind = RemoteKind::FXLIST;
          a.list = (int) n + 1;
          return a;
        }
      break;
    default:
      return a;
  }
  a.gone = true;
  return a;
}

}  // namespace espn
