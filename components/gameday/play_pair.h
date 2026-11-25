// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

// A basketball play as two rows (story_rows.h): whose play and what it was
// ("FAMU three"), then who and from where ("Baker Jr. from 25 feet"). Read
// from ESPN's play text. Only plays worth showing: baskets, steals, blocks
// and turnovers. Misses, rebounds and fouls have none. A play this can't
// read for certain has none either.
//
// Pure, with no ESPHome includes, so the host tests build it.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "espn_parse.h"
#include "game_leaders.h"
#include "panel_text.h"

namespace espn {

// The panel's 5 px font: 25 characters on a 128-wide row.
static constexpr size_t kRowChars = 25;

// The first of the forms that fits the row, into out; "" when none does.
template<size_t N> inline void fit_row(char (&out)[N], const std::string &a, const std::string &b = "",
                                       const std::string &c = "") {
  out[0] = '\0';
  for (const std::string *s : {&a, &b, &c})
    if (!s->empty() && detail::glyph_count(*s) <= kRowChars && s->size() < N) {
      memcpy(out, s->c_str(), s->size() + 1);
      return;
    }
}

struct PlayPair {
  uint8_t side{0};     // whose: 1 ours, 2 theirs; 0 for no play worth showing
  bool basket{false};  // points went up with it
  char what[24]{};     // "three", "driving layup", "steal"
  char who[40]{};      // "Baker Jr.", "" when ESPN names nobody
  uint32_t who_id{0};  // the full name, hashed: two players can share a last name
  char detail[56]{};   // row 4, "Baker Jr. from 25 feet", at most kRowChars; "" for none
  // The basket in one piece, "Baker Jr. three" (a run's end, a lead change).
  std::string who_what() const {
    return this->who[0] == '\0' ? std::string(this->what) : std::string(this->who) + " " + this->what;
  }
};

namespace detail {

// "Antonio Baker Jr." to "Baker Jr.": ESPN's play text has full names.
// Nothing for "team".
inline std::string play_surname(const std::string &full) {
  if (full.empty() || full == "team" || full == "Team")
    return "";
  const size_t sp = full.find(' ');
  return sp == std::string::npos ? full : full.substr(sp + 1);
}

inline bool ends_with(const std::string &s, const char *tail) {
  const size_t n = strlen(tail);
  return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

inline bool play_has(const std::string &s, const char *word) { return s.find(word) != std::string::npos; }

// "three", "driving layup", "floater" from what follows "makes ".
inline const char *shot_kind(const std::string &shot, int points) {
  if (points == 1 || play_has(shot, "free throw"))
    return "free throw";
  if (points >= 3 || play_has(shot, "three point"))
    return "three";
  if (play_has(shot, "alley oop"))
    return play_has(shot, "dunk") ? "alley-oop dunk" : "alley-oop";
  if (play_has(shot, "dunk"))
    return "dunk";
  if (play_has(shot, "tip "))
    return "tip-in";
  if (play_has(shot, "layup"))
    return play_has(shot, "driving") ? "driving layup" : "layup";
  if (play_has(shot, "hook"))
    return "hook shot";
  if (play_has(shot, "floating"))
    return "floater";
  if (play_has(shot, "jump"))
    return "jumper";
  return "basket";
}

}  // namespace detail

inline bool play_pair(const GameSnapshot &g, PlayPair &p) {
  using detail::ends_with;
  using detail::play_has;
  using detail::play_surname;
  p = PlayPair();
  if (g.sport != Sport::BASKETBALL || g.last_play.empty())
    return false;
  const uint8_t side = g.play_team == 0 ? 0 : g.play_team == g.team_id ? 1 : g.play_team == g.opp_id ? 2 : 0;
  if (side == 0)
    return false;
  const std::string text = ::esphome::gameday::panel_text(basketball_tidy_play(g, g.last_play));
  std::string who, what, a, b, c, full_name;
  bool block_by_other = false;
  size_t at;
  if (g.play_value > 0 && (text.rfind("makes ", 0) == 0 || (at = text.find(" makes ")) != std::string::npos)) {
    at = text.rfind("makes ", 0) == 0 ? 0 : text.find(" makes ");
    // "team makes 24-foot three point jumper" until ESPN has the player:
    // basketball_tidy_play made it the abbreviation.
    const std::string full = text.substr(0, at);
    who = full == g.team_abbr || full == g.opp_abbr ? "" : play_surname(full);
    if (!who.empty())
      full_name = full;
    const std::string shot = text.substr(at + (at == 0 ? 6 : 7));
    what = detail::shot_kind(shot, g.play_value);
    p.basket = true;
    // "25-foot three point jumper"; a shot at the rim isn't worth its feet.
    const size_t foot = shot.find("-foot");
    const int feet = foot != std::string::npos ? atoi(shot.c_str()) : 0;
    const bool rim = play_has(what, "layup") || play_has(what, "dunk") || play_has(what, "tip-in") || play_has(what, "alley-oop");
    // "(Diego Roman assists)"
    std::string assist;
    const size_t open = shot.rfind('('), verb = shot.rfind(" assists)");
    if (open != std::string::npos && verb != std::string::npos && verb > open)
      assist = play_surname(shot.substr(open + 1, verb - open - 1));
    if (what != "free throw" && feet >= 8 && feet < 100 && !rim) {
      const std::string n = std::to_string(feet);
      a = who.empty() ? "From " + n + " feet" : who + " from " + n + " feet";
      if (!who.empty())
        b = who + ", " + n + " feet";
    } else if (!who.empty() && !assist.empty()) {
      a = who + ", " + assist + " assist";
    }
    c = who;
  } else if (ends_with(text, " Steal.")) {
    what = "steal";
    c = who = play_surname(text.substr(0, text.size() - 7));
  } else if (const size_t mid = text.find(" blocks "); mid != std::string::npos && mid > 0) {
    // The NBA's and WNBA's: "Peyton Watson blocks Josh Giddey's driving
    // dunk", sent as the shooter's own play. The block is the other side's.
    what = "block";
    c = who = play_surname(text.substr(0, mid));
    block_by_other = true;
  } else if (ends_with(text, " Block.")) {
    what = "block";
    c = who = play_surname(text.substr(0, text.size() - 7));
  } else if (play_has(g.play_type, "Turnover") || ends_with(text, " turnover")) {
    // "Jaden Kempson bad pass turnover", "USA shot clock turnover", the
    // NBA's "Cooper Flagg bad pass (Christian Braun steals)".
    static const char *const kHow[] = {"bad pass",      "traveling",        "lost ball", "offensive foul",
                                       "double dribble", "shot clock",       "3 second",  "5 second",
                                       "8 second",      "10 second",        "backcourt", "palming",
                                       "carrying",      "out of bounds"};
    std::string body = text.substr(0, text.find(" ("));
    if (ends_with(body, " turnover"))
      body.resize(body.size() - 9);
    size_t cut = std::string::npos;
    const char *how = "";
    for (const char *h : kHow) {
      const size_t i = body.find(std::string(" ") + h);
      if (i != std::string::npos && i < cut) {
        cut = i;
        how = h;
      }
    }
    // "out of bounds bad pass" is a bad pass.
    if (strcmp(how, "out of bounds") == 0 && play_has(body, "bad pass"))
      how = "bad pass";
    // Without a known way there is only a name, and only when ESPN's text is
    // a name and "turnover".
    if (cut == std::string::npos && !ends_with(text, " turnover"))
      return false;
    const std::string full = body.substr(0, cut);
    what = "turnover";
    if (full == g.team_abbr || full == g.opp_abbr) {  // the team's own (basketball_tidy_play)
      c = how;
      if (!c.empty())
        c[0] = (char) toupper((unsigned char) c[0]);
    } else {
      who = play_surname(full);
      a = *how != '\0' && !who.empty() ? who + " " + how : "";
      c = who;
    }
  } else {
    return false;
  }
  p.side = block_by_other ? (uint8_t) (3 - side) : side;
  snprintf(p.what, sizeof(p.what), "%s", what.c_str());
  if (who.size() < sizeof(p.who))
    memcpy(p.who, who.c_str(), who.size() + 1);
  uint32_t h = 2166136261u;
  for (char ch : full_name)
    h = (h ^ (uint8_t) ch) * 16777619u;
  p.who_id = full_name.empty() ? 0 : h;
  fit_row(p.detail, a, b, c);
  return true;
}

// The side whose shot the last play tells of, 0 for none: the one that shot
// it, or the other way round for a defensive rebound or a block. `shot` is
// whether the play is the shot itself.
inline int play_shooter(const GameSnapshot &g, bool &shot) {
  shot = false;
  const int side = g.play_team == 0 ? 0 : g.play_team == g.team_id ? 1 : g.play_team == g.opp_id ? 2 : 0;
  if (side == 0)
    return 0;
  const std::string &t = g.last_play;
  if (detail::play_has(t, "makes ") || detail::play_has(t, "misses ") || detail::play_has(t, "missed ") ||
      detail::play_has(t, " blocks ")) {
    shot = true;
    return side;
  }
  if (detail::play_has(t, " Block.") || detail::play_has(t, "efensive Rebound") || detail::play_has(t, "efensive rebound"))
    return 3 - side;
  if (detail::play_has(t, "ffensive Rebound") || detail::play_has(t, "ffensive rebound"))
    return side;
  return 0;
}

}  // namespace espn
