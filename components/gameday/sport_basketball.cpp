// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Basketball's parse extras, ticker, clock row and splash (NBA, WNBA, men's
// college). Pure, no ESPHome includes.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "espn_parse.h"
#include "game_leaders.h"

namespace espn {

using detail::add_last_play;
using detail::add_odds;
using detail::add_part;
using detail::pregame_records;
using detail::records_line;

namespace detail {

void fill_basketball_filter(JsonObject ev) {
  ev["status"]["type"]["name"] = true;
  JsonObject comp = ev["competitions"][0];
  comp["format"]["regulation"]["periods"] = true;
  JsonObject series = comp["series"].to<JsonObject>();
  series["type"] = true;
  series["summary"] = true;
  JsonObject c = series["competitors"].add<JsonObject>();
  c["id"] = true;
  c["wins"] = true;
  // The team stats, for the ticker in a break.
  JsonObject stat = comp["competitors"][0]["statistics"].add<JsonObject>();
  stat["name"] = true;
  stat["displayValue"] = true;
}

static Basketball::Box box_of(JsonObjectConst side) {
  Basketball::Box b;
  struct {
    const char *name;
    uint8_t *to;
  } const want[] = {{"fieldGoalsMade", &b.fg_made},
                    {"fieldGoalsAttempted", &b.fg_tried},
                    {"threePointFieldGoalsMade", &b.three_made},
                    {"threePointFieldGoalsAttempted", &b.three_tried},
                    {"rebounds", &b.rebounds},
                    {"freeThrowsMade", &b.ft_made},
                    {"freeThrowsAttempted", &b.ft_tried}};
  constexpr unsigned kTeam = 0x1F, kFree = 0x60;  // want's bits: the first five, the last two
  unsigned found = 0;
  for (JsonObjectConst s : side["statistics"].as<JsonArrayConst>()) {
    const char *name = s["name"].as<const char *>();
    const char *value = s["displayValue"].as<const char *>();
    if (name == nullptr || value == nullptr || !isdigit((unsigned char) value[0]))
      continue;
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++)
      if (strcmp(name, want[i].name) == 0) {
        *want[i].to = (uint8_t) std::min(255L, atol(value));
        found |= 1u << i;
      }
  }
  b.has = (found & kTeam) == kTeam && b.fg_tried > 0;
  b.has_ft = b.has && (found & kFree) == kFree;
  return b;
}

void basketball_from_event(JsonObjectConst ev, GameSnapshot &s) {
  Basketball &b = s.nba;
  JsonObjectConst comp = ev["competitions"][0];
  int periods = comp["format"]["regulation"]["periods"] | 4;
  b.regulation = (uint8_t) (periods > 0 && periods < 10 ? periods : 4);
  std::string name = str_or_empty(ev["status"]["type"]["name"]);
  if (name == "STATUS_IN_PROGRESS")
    b.phase = Phase::PLAY;
  else if (name == "STATUS_END_PERIOD")
    b.phase = Phase::END_PERIOD;
  else if (name == "STATUS_HALFTIME")
    b.phase = Phase::HALFTIME;
  // A college game's misses come with the points the shot was worth.
  if (s.last_play.find("misses ") != std::string::npos)
    s.play_value = 0;
  // The game's points leaders, for the situation row (row_text.h). Before
  // the start ESPN's list is the season's (points per game), so only now.
  if (s.state == GameState::IN) {
    for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
      const bool ours = (uint32_t) atol(str_or_empty(c["id"]).c_str()) == s.team_id;
      (ours ? b.team_box : b.opp_box) = box_of(c);
      JsonObjectConst top = top_leader(c, "points");
      std::string who = leader_short_surname(str_or_empty(top["athlete"]["shortName"]));
      std::string pts = str_or_empty(top["displayValue"]);
      // Before anyone scores there is no line for the side at all.
      JsonObjectConst reb = top_leader(c, "rebounds"), ast = top_leader(c, "assists");
      const int rebs = std::min(99, std::max(0, atoi(str_or_empty(reb["displayValue"]).c_str())));
      const int asts = std::min(99, std::max(0, atoi(str_or_empty(ast["displayValue"]).c_str())));
      char *extra = ours ? b.team_extra : b.opp_extra;
      const bool dish = asts >= 3 && asts * 4 >= rebs * 3;
      if (dish || rebs >= 3) {
        std::string name = leader_surname(str_or_empty((dish ? ast : reb)["athlete"]["shortName"]));
        while (name.size() > 16 && glyph_count(name) > 0)  // bytes, never half a letter
          name = glyph_prefix(name, glyph_count(name) - 1);
        snprintf(extra, sizeof(b.team_extra), "%.16s %d %s", name.c_str(), dish ? asts : rebs, dish ? "ast" : "reb");
      }
      if (who.empty() || pts.empty() || pts == "0")
        continue;
      (ours ? b.team_scorer : b.opp_scorer) = who + " " + pts;
    }
  }
  // Keep the series of a playoff game only
  JsonObjectConst series = comp["series"];
  if (str_or_empty(series["type"]) != "playoff")
    return;
  b.series = str_or_empty(series["summary"]);
  for (JsonObjectConst c : series["competitors"].as<JsonArrayConst>()) {
    uint32_t id = (uint32_t) atol(str_or_empty(c["id"]).c_str());
    if (c["wins"].isNull())
      continue;
    int8_t wins = (int8_t) (c["wins"] | 0);
    if (id == s.team_id)
      b.team_wins = wins;
    else if (id == s.opp_id)
      b.opp_wins = wins;
  }
}

}  // namespace detail

// "1st".."4th" in regulation (halves in college), then "OT", "2OT", ...
static std::string period_label(const GameSnapshot &s) {
  static const char *kOrdinals[] = {"1st", "2nd", "3rd", "4th"};
  int reg = s.nba.regulation;
  if (s.period <= 0)
    return "";
  if (s.period <= reg)
    return s.period <= 4 ? kOrdinals[s.period - 1] : std::to_string(s.period) + "th";
  int ot = s.period - reg;
  return ot == 1 ? "OT" : std::to_string(ot) + "OT";
}

std::string basketball_series(const GameSnapshot &s) {
  const Basketball &b = s.nba;
  if (b.team_wins < 0 || b.opp_wins < 0)
    return "";
  int hi = b.team_wins > b.opp_wins ? b.team_wins : b.opp_wins;
  int lo = b.team_wins > b.opp_wins ? b.opp_wins : b.team_wins;
  std::string score = std::to_string(hi) + "-" + std::to_string(lo);
  if (hi == lo)
    return "Series " + score;
  const std::string &leader = b.team_wins > b.opp_wins ? s.team_abbr : s.opp_abbr;
  std::string out = leader + " lead " + score;
  // Say "up" when "lead" overflows the 12-character row ("UTAH lead 2-1")
  return out.size() <= 12 ? out : leader + " up " + score;
}

bool basketball_junk_play(const GameSnapshot &s) {
  return s.play_type.find("Substitution") != std::string::npos ||
         s.play_type.find("Dead Ball") != std::string::npos ||
         s.last_play.find(" subbing in for ") != std::string::npos ||
         s.last_play.find(" subbing out for ") != std::string::npos ||
         s.last_play.find(" enters the game for ") != std::string::npos;
}

std::string basketball_box_line(const GameSnapshot &s) {
  const Basketball::Box &a = s.nba.team_box, &b = s.nba.opp_box;
  if (!a.has || !b.has)
    return "";
  auto pct = [](const Basketball::Box &x) { return (x.fg_made * 200 + x.fg_tried) / (2 * x.fg_tried); };
  const char *us = s.team_abbr.c_str(), *them = s.opp_abbr.c_str();
  char buf[128];
  snprintf(buf, sizeof(buf), "FG %s %d%%, %s %d%% | 3PT %s %u/%u, %s %u/%u | Rebounds %s %u, %s %u", us, pct(a), them,
           pct(b), us, a.three_made, a.three_tried, them, b.three_made, b.three_tried, us, a.rebounds, them,
           b.rebounds);
  return buf;
}

// The ticker in a break: the series, else the team stats. On a wide board
// the records are under the logos already, so without stats it says the
// network and the arena rather than the records again.
static std::string break_line(const GameSnapshot &s, const TickerOptions &o) {
  if (!s.nba.series.empty())
    return s.nba.series;
  std::string out = basketball_box_line(s);
  if (out.empty() && o.row_timeout) {
    add_part(out, s.tv);
    add_part(out, s.venue);
  }
  return out.empty() ? records_line(s) : out;
}

std::string basketball_tidy_play(const GameSnapshot &s, const std::string &play) {
  std::string out;
  out.reserve(play.size() + 4);
  // ESPN puts a line break in some ("bad pass\nturnover").
  for (char c : play) {
    if (isspace((unsigned char) c))
      c = ' ';
    if (c != ' ' || (!out.empty() && out.back() != ' '))
      out += c;
  }
  while (out.size() > 1 && out.compare(out.size() - 2, 2, "..") == 0)
    out.pop_back();  // "Foul on Antonio Baker Jr.."
  if (strncasecmp(out.c_str(), "team ", 5) == 0 && (s.play_team == s.team_id || s.play_team == s.opp_id))
    out.replace(0, 4, s.play_team == s.team_id ? s.team_abbr : s.opp_abbr);
  return out;
}

std::string basketball_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local) {
  const Basketball &b = s.nba;
  std::string out;
  switch (s.state) {
    case GameState::NOT_FOUND:
      return "No upcoming game";
    case GameState::PRE:
      add_part(out, s.game_name);  // a playoff round, like "East 1st Round - Game 2"
      add_part(out, kickoff_local.empty() ? s.short_detail : kickoff_local);
      add_part(out, b.series);
      if (o.records)
        add_part(out, pregame_records(s));
      add_odds(out, s, o);
      detail::add_leaders(out, s, o);
      add_part(out, detail::venue_line(s, o));
      return out;
    case GameState::POST:
      // Postponed, Canceled or Suspended: ESPN's post state without a result.
      if (!s.completed && !s.short_detail.empty())
        return s.short_detail;
      // "Final/OT", "Final/2OT" after overtime
      out = s.short_detail.rfind("Final", 0) == 0 ? s.short_detail : "Final";
      add_part(out, b.series.empty() ? records_line(s) : b.series);
      return out;
    case GameState::IN:
      break;
  }
  if (b.phase == Phase::HALFTIME || b.phase == Phase::END_PERIOD) {
    // The clock row says which break it is.
    out = break_line(s, o);
    return out.empty() ? s.short_detail : out;
  }
  if (o.clock)
    add_part(out, s.short_detail);
  // A wide board shows no timeout line (row_text.h): the stopped clock
  // says it. The ticker shows what a break does; the clock would repeat the
  // clock row. A player checking in is no news on any board: the last play
  // worth reading stays up.
  if (o.last_play) {
    if (o.row_timeout && timeout_play(s.last_play))
      add_part(out, break_line(s, o));
    else if (!basketball_junk_play(s))
      add_last_play(out, basketball_tidy_play(s, s.last_play));
    else if (o.row_timeout && o.held_break)
      add_part(out, break_line(s, o));  // the timeout goes on through substitutions
    else if (o.held_play != nullptr && o.held_play[0] != '\0')
      add_last_play(out, o.held_play);  // tidied when it was held
    else
      add_part(out, break_line(s, o));
  } else if (o.row_timeout && timeout_play(s.last_play) && out.empty()) {
    add_part(out, break_line(s, o));
  }
  if (out.empty())
    out = s.short_detail.empty() ? "In Progress" : s.short_detail;
  return out;
}

// "1:04 4th", "45.3 4th", "2:10 OT", "Halftime", "End 3rd".
// ESPN drops the minutes under a minute.
// The period is named from its number so college halves read "12:34 2nd",
// which fits the clock row's 10 characters.
std::string basketball_clock_text(const GameSnapshot &s) {
  const Basketball &b = s.nba;
  if (s.state == GameState::IN && s.period > 0) {
    if (b.phase == Phase::HALFTIME)
      return "Halftime";
    if (b.phase == Phase::END_PERIOD)
      return "End " + period_label(s);
    if (b.phase == Phase::PLAY && !s.display_clock.empty())
      return s.display_clock + " " + period_label(s);
  }
  std::string t = s.short_detail;  // "Delayed" and the like
  size_t dash = t.find(" - ");
  if (dash != std::string::npos)
    t.replace(dash, 3, " ");
  return t;
}

// Win only: a basketball game has around a hundred scoring plays.
Splash basketball_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool /*opponent_splashes*/,
                         bool neutral) {
  Splash none;
  if (!prev.valid || !cur.valid || prev.event_id != cur.event_id)
    return none;
  if (prev.state != GameState::IN || cur.state != GameState::POST || !cur.completed)
    return none;
  if (cur.team_score > cur.opp_score)
    return Splash{cur.team_abbr + " WINS!", parse_color(cur.team_color)};
  if (neutral && cur.opp_score > cur.team_score)
    return Splash{cur.opp_abbr + " WINS!", parse_color(cur.opp_color)};
  return none;
}

}  // namespace espn
