// Playlists: named groups of the owner's favorites. Favorite teams mode plays
// one of them, or every favorite when none is chosen, and a WizMote button
// can be set to one (remote.h). A playlist is kept as a mask over the favorites
// list, so it costs no team storage of its own and only the favorites are
// ever read from ESPN. Beside them, a sport row per sport in the favorites
// plays that sport's favorites; it is worked out from the favorites each
// time, so nothing about it is saved but which one plays.
// Pure, no ESPHome includes, so the host tests cover it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "leagues.h"
#include "message.h"
#include "set_request.h"

namespace espn {

constexpr int kListMax = 16;
constexpr size_t kListNameMax = 16;  // bytes

struct ListDef {
  std::string name;
  std::vector<TeamRef> teams;
};

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

}  // namespace espn
