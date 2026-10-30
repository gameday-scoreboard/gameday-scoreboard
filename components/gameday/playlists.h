// Playlists: named groups of the owner's favorites. Favorite teams mode plays
// one of them, or every favorite when none is chosen, and WizMote buttons 1
// to 4 pick the first four. A playlist is kept as a mask over the favorites
// list, so it costs no team storage of its own and only the favorites are
// ever read from ESPN.
//
// Sport rows: when the favorites span more than one sport, Playing also
// offers one row per sport that has favorites (Football, Basketball, ...).
// They are worked out from the favorites each time, never stored, so they
// come and go with the favorites and never go stale. The playing list is
// one number: 0 every favorite, 1 to kListMax the owner's playlists in
// order, and sport_list(s), a negative number, a sport row, so an owner's
// playlist keeps its number whatever rows exist.
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
constexpr int kSportCount = 5;       // Sport::FOOTBALL to Sport::SOCCER
// set_lists_'s keep with no list= in the request (a sport row is -1 to -5).
constexpr int kListKeepNone = -99;

// The sport rows' names, in Sport order (the order Playing lists them).
inline const char *sport_name(Sport s) {
  switch (s) {
    case Sport::BASEBALL:
      return "Baseball";
    case Sport::BASKETBALL:
      return "Basketball";
    case Sport::HOCKEY:
      return "Hockey";
    case Sport::SOCCER:
      return "Soccer";
    default:
      return "Football";
  }
}

// The sport's word in the state document and set requests: "football".
inline const char *sport_key(Sport s) {
  switch (s) {
    case Sport::BASEBALL:
      return "baseball";
    case Sport::BASKETBALL:
      return "basketball";
    case Sport::HOCKEY:
      return "hockey";
    case Sport::SOCCER:
      return "soccer";
    default:
      return "football";
  }
}

// "football" -> Sport::FOOTBALL; false for any other word.
inline bool sport_by_key(const std::string &key, Sport &out) {
  for (int i = 0; i < kSportCount; i++)
    if (key == sport_key((Sport) i)) {
      out = (Sport) i;
      return true;
    }
  return false;
}

// A sport row's number in the playing list: -1 Football to -5 Soccer.
inline int sport_list(Sport s) { return -((int) s + 1); }
inline bool list_is_sport(int list) { return list < 0 && list >= -kSportCount; }
inline Sport list_sport(int list) { return (Sport) (-list - 1); }

// A bit per favorite (bit i = favs[i]) for the favorites in this sport.
inline uint16_t sport_mask(Sport s, const std::vector<TeamRef> &favs) {
  uint16_t mask = 0;
  for (size_t i = 0; i < favs.size() && i < 16; i++)
    if (favs[i].id != 0 && league_sport((League) favs[i].league) == s)
      mask |= (uint16_t) (1u << i);
  return mask;
}

// A bit per Sport (bit = (int) Sport) for the sport rows Playing offers:
// every sport with a favorite, once the favorites span more than one sport.
// Favorites in one sport only have no rows: every favorite is that sport.
inline uint8_t sport_rows(const std::vector<TeamRef> &favs) {
  uint8_t rows = 0;
  for (const auto &f : favs)
    if (f.id != 0)
      rows |= (uint8_t) (1u << (int) league_sport((League) f.league));
  return (rows & (rows - 1)) != 0 ? rows : 0;  // more than one bit set
}

inline bool sport_row_exists(int list, const std::vector<TeamRef> &favs) {
  return list_is_sport(list) && (sport_rows(favs) & (1u << (int) list_sport(list))) != 0;
}

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

}  // namespace espn
