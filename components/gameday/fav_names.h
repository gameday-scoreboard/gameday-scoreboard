// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The names of the athletes the owner follows (golfers): a team's name is in
// teams.h, an athlete has no table, so the panel keeps the name whoever added
// the favorite sent with it. favs= carries it after the id,
// "pga:9478:Scottie Scheffler"; firmware without names reads the id and
// stops there. A name stays with its league and id, so a reorder that sends
// the refs alone keeps it. Pure, no ESPHome includes, so the host tests
// cover it.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "leagues.h"
#include "message.h"

namespace espn {

constexpr size_t kAthleteNameMax = 24;  // with its terminator, as GolfRow's name
constexpr size_t kFavNamesMax = 16;      // a name per favorite, FAV_MAX

// Saved as one fixed-size blob: an entry's id 0 is an empty one.
struct FavName {
  uint8_t league;
  uint32_t id;
  char name[kAthleteNameMax];
} __attribute__((packed));

struct FavNames {
  FavName e[kFavNamesMax];
} __attribute__((packed));
static_assert(sizeof(FavNames) == 464,
              "FavNames is saved: a new size needs a new preference key or a migration, not a new number");

// Favorites in these leagues are athletes, who need a kept name.
inline bool athlete_league(uint8_t league) { return league < kLeagueCount && leaderboard_league((League) league); }

// One line, no emoji or control characters, no list separators, at most
// kAthleteNameMax - 1 bytes and never cut inside a character.
inline std::string athlete_name_clean(const std::string &v) {
  std::string s;
  for (char c : v)
    if (c != ',' && c != ';')
      s += c;
  return message_clean(utf8_only(s), kAthleteNameMax - 1, false);
}

// The name after the id in a favs entry, "pga:9478:Scottie Scheffler"; ""
// when there is none.
inline std::string fav_ref_name(const std::string &ref) {
  size_t a = ref.find(':');
  size_t b = a == std::string::npos ? std::string::npos : ref.find(':', a + 1);
  return b == std::string::npos ? std::string() : athlete_name_clean(ref.substr(b + 1));
}

// The kept name of an athlete, "" for none.
inline std::string fav_name_get(const FavNames *n, uint8_t league, uint32_t id) {
  if (n == nullptr || id == 0)
    return "";
  for (const auto &e : n->e)
    if (e.league == league && e.id == id)
      return std::string(e.name, strnlen(e.name, kAthleteNameMax));
  return "";
}

// Keeps name for an athlete. Its own entry first, else an empty one, else one
// whose athlete isn't among favs any more. True when the blob changed and
// needs saving: never for the same name again, an empty name or a team.
inline bool fav_name_put(FavNames &n, uint8_t league, uint32_t id, const std::string &name,
                         const std::vector<std::pair<uint8_t, uint32_t>> &favs) {
  if (!athlete_league(league) || id == 0 || name.empty())
    return false;
  std::string text = athlete_name_clean(name);
  if (text.empty())
    return false;
  FavName *slot = nullptr;
  for (auto &e : n.e)
    if (e.league == league && e.id == id)
      slot = &e;
  for (int pass = 0; pass < 2 && slot == nullptr; pass++)
    for (auto &e : n.e) {
      bool followed = false;
      for (const auto &f : favs)
        followed = followed || (f.first == e.league && f.second == e.id);
      if (pass == 0 ? e.id == 0 : !followed) {
        slot = &e;
        break;
      }
    }
  if (slot == nullptr)
    return false;
  if (slot->league == league && slot->id == id && strncmp(slot->name, text.c_str(), kAthleteNameMax) == 0)
    return false;
  FavName e{};
  e.league = league;
  e.id = id;
  memcpy(e.name, text.data(), text.size());
  *slot = e;
  return true;
}

// The short form an app shows where a team's abbreviation goes: the surname,
// "Scheffler", past a suffix like "Jr." or "III"; "" when it is longer than
// kAthleteAbbrMax bytes or the name is one word.
constexpr size_t kAthleteAbbrMax = 10;
inline std::string athlete_abbr(const std::string &name) {
  std::vector<std::string> words;
  size_t start = 0;
  while (start < name.size()) {
    size_t sp = name.find(' ', start);
    std::string w = name.substr(start, sp == std::string::npos ? std::string::npos : sp - start);
    if (!w.empty())
      words.push_back(w);
    start = sp == std::string::npos ? name.size() : sp + 1;
  }
  static const char *const kSuffixes[] = {"Jr.", "Jr", "Sr.", "Sr", "II", "III", "IV"};
  while (words.size() > 1) {
    bool suffix = false;
    for (const char *s : kSuffixes)
      suffix = suffix || words.back() == s;
    if (!suffix)
      break;
    words.pop_back();
  }
  if (words.size() < 2 || words.back().size() > kAthleteAbbrMax)
    return "";
  return words.back();
}

}  // namespace espn
