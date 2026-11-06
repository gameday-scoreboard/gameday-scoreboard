// ESPN fantasy football (ffl), public leagues only: the owner's team and its
// matchup this week, read from ESPN's fantasy API the way the scoreboards are
// read from the site API — no login, no key, no cookie, because a public
// league's document is public. A private league answers 401 to the same read
// and is out of scope by design: nothing credential-shaped is ever stored.
//
// The settings are two plain values like a team choice: the league id (digits,
// as printed in the league's web address) and the owner's team id in it.
// Pure header like season.h: the document parser lives in espn_parse_impl.h
// (a big parse template instantiated in gameday.cpp can overflow the Xtensa
// literal pool, same note as the other parsers) and the card snapshot in
// espn_parse.cpp. Host-tested in tests/test_parse.cpp.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace espn {

// How often a configured panel re-reads the matchup, and how long it waits
// after a failed read before trying again.
inline constexpr uint32_t kFantasyReadMs = 5 * 60 * 1000;
inline constexpr uint32_t kFantasyRetryMs = 15 * 60 * 1000;

// A league id as the owner types it: 1 to 10 digits, no leading zero (ESPN
// ids never have one; a leading zero means a typo). "" is the unset value and
// is checked by the caller, not here.
inline bool fantasy_league_id_ok(const std::string &v) {
  if (v.empty() || v.size() > 10 || v[0] == '0') return false;
  for (char c : v)
    if (c < '0' || c > '9') return false;
  return true;
}

// The owner's team id inside the league: 0 is unset, teams run 1..999 in any
// league the panel will ever meet.
inline bool fantasy_team_id_ok(int v) { return v >= 0 && v <= 999; }

// The season a date belongs to: a fantasy season runs September to January,
// so August and later is this year's season and the rest is last year's.
inline int fantasy_season_year(int year, int month) { return month >= 8 ? year : year - 1; }

// The matchup document: the mMatchupScore view carries the schedule with both
// sides' points, mTeam the names and records. Repeated view parameters — the
// API ignores a comma-joined view and answers with a bare skeleton.
inline std::string fantasy_url(const std::string &league_id, int season_year) {
  char buf[192];
  std::snprintf(buf, sizeof(buf),
                "https://lm-api-reads.fantasy.espn.com/apis/v3/games/ffl/seasons/%d/segments/0/leagues/%s"
                "?view=mMatchupScore&view=mTeam",
                season_year, league_id.c_str());
  return buf;
}

enum class FantasyState : uint8_t { PRE, IN, POST };

struct FantasySide {
  uint32_t id{0};
  std::string abbr;    // "ABC", for the card and the state document
  std::string name;    // the full fantasy team name
  std::string record;  // "7-2" or "7-2-1", "" when the document has none
  float points{-1};        // totalPoints: the score so far, final once the week ends
  float points_live{-1};   // totalPointsLive: ESPN's in-progress figure, -1 when absent
  bool winner{false};
  // The score to show: the points themselves; the live figure only stands in
  // when a document carries nothing else.
  float shown() const { return points >= 0 ? points : points_live; }
};

struct FantasyMatchup {
  bool valid{false};      // a matchup for the owner's team was found
  bool bye{false};        // the document parsed but the team has no game this period
  bool playoffs{false};
  int week{0};            // the matchup period, the NFL week for ffl
  int season{0};
  FantasyState state{FantasyState::PRE};
  FantasySide team;  // the owner's team
  FantasySide opp;
};

// "7-2" from wins/losses, "7-2-1" with ties; "" when the document has no record.
inline std::string fantasy_record(int wins, int losses, int ties) {
  if (wins < 0 || losses < 0) return "";
  char buf[24];
  if (ties > 0)
    std::snprintf(buf, sizeof(buf), "%d-%d-%d", wins, losses, ties);
  else
    std::snprintf(buf, sizeof(buf), "%d-%d", wins, losses);
  return buf;
}

// Points the way fantasy scores are written: two decimals, trimmed ("108.42",
// "108.4", "96"). -1 (no points yet) reads "".
inline std::string fantasy_points_text(float points) {
  if (points < 0) return "";
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(points));
  std::string s = buf;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s;
}

// The one line the device page and the ticker form share: who's playing whom,
// the week and the score so far or final.
inline std::string fantasy_ticker(const FantasyMatchup &m) {
  if (m.bye) return "Fantasy Week " + std::to_string(m.week) + ": " + m.team.name + " is on a bye";
  if (!m.valid) return "";
  std::string s = "Fantasy Week " + std::to_string(m.week) + " | " + m.team.abbr + " " +
                  fantasy_points_text(m.team.shown()) + " | " + m.opp.abbr + " " +
                  fantasy_points_text(m.opp.shown());
  if (m.state == FantasyState::POST) s += " | Final";
  return s;
}

// Parses the matchup document into the owner's current matchup: false when
// the document doesn't parse or the team isn't in the league; a parsed
// document with no game for the team this period returns true with
// out.bye set and out.valid clear. The streaming template (parse_fantasy)
// is defined with the other parsers in espn_parse_impl.h; this is its
// string form for the host tests.
bool parse_fantasy_str(const std::string &json, uint32_t team_id, FantasyMatchup &out);

}  // namespace espn
