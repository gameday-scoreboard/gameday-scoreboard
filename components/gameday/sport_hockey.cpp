// Hockey's ticker, clock row and splashes. Pure, no ESPHome includes.
// ESPN's NHL feed has no power play field. The board must not infer one from
// penalties, because offsetting and coincidental minors would make it wrong.
#include "espn_parse.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace espn {

using detail::add_last_play;
using detail::add_odds;
using detail::add_part;
using detail::pregame_records;
using detail::records_line;

namespace detail {

void fill_hockey_filter(JsonObject ev) {
  ev["season"]["type"] = true;
  ev["competitions"][0]["series"]["type"] = true;
  ev["competitions"][0]["series"]["summary"] = true;
  // The big plays: who has how many goals (fill_leaders_filter keeps the
  // leader lists; this adds the player's id), and a goal's strength.
  ev["competitions"][0]["competitors"][0]["leaders"][0]["name"] = true;
  JsonObject top = ev["competitions"][0]["competitors"][0]["leaders"][0]["leaders"][0];
  top["displayValue"] = true;
  top["athlete"]["id"] = true;
  ev["competitions"][0]["situation"]["lastPlay"]["strength"]["abbreviation"] = true;
}

void hockey_from_event(JsonObjectConst ev, GameSnapshot &s) {
  s.nhl.playoffs = (ev["season"]["type"] | 0) == 3;
  // A playoff series only, as basketball: in the regular season ESPN's
  // series is the season series, and the records stay.
  JsonObjectConst series = ev["competitions"][0]["series"];
  if (s.nhl.playoffs || str_or_empty(series["type"]) == "playoff")
    s.nhl.series = str_or_empty(series["summary"]);
  if (s.state != GameState::IN)
    return;
  // Before the start ESPN's leaders are the season's; from the puck drop
  // they are this game's.
  for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
    const bool ours = (uint32_t) atol(str_or_empty(c["id"]).c_str()) == s.team_id;
    Hockey::Scorer *out = ours ? s.nhl.team_scorers : s.nhl.opp_scorers;
    uint8_t &count = ours ? s.nhl.team_scorer_count : s.nhl.opp_scorer_count;
    count = 0;
    for (JsonObjectConst l : c["leaders"].as<JsonArrayConst>()) {
      if (str_or_empty(l["name"]) != "goals")
        continue;
      for (JsonObjectConst top : l["leaders"].as<JsonArrayConst>()) {
        const int goals = atoi(str_or_empty(top["displayValue"]).c_str());
        const uint32_t id = (uint32_t) atol(str_or_empty(top["athlete"]["id"]).c_str());
        if (goals < 2 || id == 0 || count >= Hockey::kScorersMax)
          continue;
        out[count].id = id;
        out[count].goals = (uint8_t) std::min(goals, 99);
        count++;
      }
    }
  }
  if (s.play_type == "Goal")
    s.nhl.goal_strength = str_or_empty(ev["competitions"][0]["situation"]["lastPlay"]["strength"]["abbreviation"]);
}

}  // namespace detail

static bool shootout(const GameSnapshot &s) { return s.period >= 5 && !s.nhl.playoffs; }

// Between periods ESPN's shortDetail reads "End of 1st" (status END_PERIOD)
static bool intermission(const GameSnapshot &s) {
  return s.state == GameState::IN && s.short_detail.rfind("End of", 0) == 0;
}

static std::string period_name(const GameSnapshot &s) {
  switch (s.period) {
    case 1:
      return "1st";
    case 2:
      return "2nd";
    case 3:
      return "3rd";
    case 4:
      return "OT";
    default:
      if (s.period < 1)
        return "";
      return shootout(s) ? "SO" : std::to_string(s.period - 3) + "OT";  // playoffs: 2OT, 3OT
  }
}

// "12:34 2nd", "3:10 OT", "SO", or ESPN's own words between periods ("End of 1st")
std::string hockey_clock_text(const GameSnapshot &s) {
  if (s.state == GameState::IN && shootout(s))
    return "SO";
  bool has_clock = s.display_clock.find(':') != std::string::npos && s.period > 0;
  bool special = s.short_detail.find(':') == std::string::npos;
  if (!has_clock || special) {
    std::string t = s.short_detail;
    size_t dash = t.find(" - ");
    if (dash != std::string::npos)
      t.replace(dash, 3, " ");
    return t;
  }
  return s.display_clock + " " + period_name(s);
}

std::string hockey_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local) {
  const Hockey &h = s.nhl;
  // The series line replaces the W-L-OTL records in the playoffs
  const std::string standing = h.series.empty() ? records_line(s) : h.series;
  std::string out;
  switch (s.state) {
    case GameState::NOT_FOUND:
      return "No upcoming game";
    case GameState::PRE:
      add_part(out, s.game_name);  // a playoff round, like "East 1st Round - Game 2"
      add_part(out, kickoff_local.empty() ? s.short_detail : kickoff_local);
      add_part(out, h.series);
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
      // "Final/OT", "Final/SO", "Final/2OT"
      out = s.short_detail.rfind("Final", 0) == 0 ? s.short_detail : "Final";
      add_part(out, standing);
      return out;
    case GameState::IN:
      break;
  }
  if (intermission(s)) {
    // The clock row already says which period ended
    out = standing;
    return out.empty() ? s.short_detail : out;
  }
  if (o.clock)
    add_part(out, s.short_detail);
  if (o.last_play) {
    std::string play = s.last_play;
    // A fresh goal can arrive before its scorer: ", assists: N. Pionk (1)"
    if (play.rfind(", ", 0) == 0)
      play.erase(0, 2);
    add_last_play(out, play);
  }
  if (out.empty())
    out = s.short_detail.empty() ? "In Progress" : s.short_detail;
  return out;
}

// "GOAL!" for us, "PIT GOAL" for them or for either side in the live modes.
// A shootout winner's extra goal splashes like any other goal.
Splash hockey_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral) {
  Splash none;
  if (!prev.valid || !cur.valid || prev.event_id != cur.event_id)
    return none;
  if (prev.state == GameState::IN && cur.state == GameState::POST) {
    if (!cur.completed)
      return none;
    if (cur.team_score > cur.opp_score)
      return Splash{cur.team_abbr + " WINS!", parse_color(cur.team_color)};
    if (neutral && cur.opp_score > cur.team_score)
      return Splash{cur.opp_abbr + " WINS!", parse_color(cur.opp_color)};
    return none;
  }
  if (prev.state != GameState::IN || cur.state != GameState::IN)
    return none;
  if (cur.team_score > prev.team_score)
    return Splash{neutral ? cur.team_abbr + " GOAL" : "GOAL!", parse_color(cur.team_color)};
  if (cur.opp_score > prev.opp_score && opponent_splashes)
    return Splash{cur.opp_abbr + " GOAL", parse_color(cur.opp_color)};
  return none;
}

std::string hockey_board_record(const std::string &wlo) {
  if (wlo.size() <= 7)
    return wlo;
  // Three digits a count at most, as soccer_board_record: a longer one shows
  // the record as it came.
  int w = 0, l = 0, otl = 0;
  if (sscanf(wlo.c_str(), "%3d-%3d-%3d", &w, &l, &otl) != 3)
    return wlo;
  return std::to_string(2 * w + otl) + " pts";
}

}  // namespace espn
