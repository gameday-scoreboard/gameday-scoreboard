// Hockey's ticker, clock row and splashes. Pure, no ESPHome includes.
// ESPN's NHL feed has no power play field. The board must not infer one from
// penalties, because offsetting and coincidental minors would make it wrong.
#include "espn_parse.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace espn {

using detail::add_last_play;
using detail::add_odds;
using detail::add_part;
using detail::pregame_records;
using detail::records_line;

static bool shootout(const GameSnapshot &s) { return s.period >= 5 && !s.nhl.playoffs; }

// "B. Saka" -> "Saka", as soccer's
static std::string player_name(const std::string &short_name) {
  size_t dot = short_name.find(". ");
  if (dot != std::string::npos && dot <= 2)
    return short_name.substr(dot + 2);
  return short_name;
}

// Into one of Hockey's fixed fields, cut to fit
template<size_t N> static void put(char (&dst)[N], const std::string &src) {
  snprintf(dst, N, "%s", src.c_str());
}

namespace detail {

void fill_hockey_filter(JsonObject ev) {
  ev["season"]["type"] = true;
  JsonObject comp = ev["competitions"][0];
  comp["series"]["type"] = true;
  comp["series"]["summary"] = true;
  JsonObject stat = comp["competitors"][0]["statistics"].add<JsonObject>();
  stat["name"] = true;
  stat["displayValue"] = true;
}

// One of a side's stats ("saves", "goals") as a count, -1 when it has none
static int stat_count(JsonObjectConst competitor, const char *name) {
  for (JsonObjectConst st : competitor["statistics"].as<JsonArrayConst>()) {
    if (str_or_empty(st["name"]) != name)
      continue;
    const char *v = st["displayValue"].as<const char *>();
    if (v == nullptr || *v < '0' || *v > '9')
      return -1;
    long n = strtol(v, nullptr, 10);
    return n > 999 ? 999 : (int) n;
  }
  return -1;
}

// The scorer from a goal's text, "Connor McDavid (5) Wrist Shot, assists: ..."
// -> "McDavid". A goal ESPN has not named yet starts ", assists:".
static std::string scorer_from_text(const std::string &text) {
  size_t paren = text.find(" (");
  if (paren == std::string::npos || paren == 0 || text.rfind(",", 0) == 0)
    return "";
  std::string name = text.substr(0, paren);
  size_t space = name.rfind(' ');
  return space == std::string::npos ? name : name.substr(space + 1);
}

void hockey_from_event(JsonObjectConst ev, GameSnapshot &s) {
  Hockey &h = s.nhl;
  h.playoffs = (ev["season"]["type"] | 0) == 3;
  JsonObjectConst comp = ev["competitions"][0];
  // A playoff series only, as basketball: in the regular season ESPN's
  // series is the season series, and the records stay.
  JsonObjectConst series = comp["series"];
  if (h.playoffs || str_or_empty(series["type"]) == "playoff")
    h.series = str_or_empty(series["summary"]);
  // Each side's shots are the saves the other goalie made plus its own
  // goals. ESPN's goals stat leaves out the shootout, unlike its score.
  int saves[2] = {-1, -1}, goals[2] = {-1, -1};
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
    int i = (uint32_t) atol(str_or_empty(c["id"]).c_str()) == s.team_id ? 0 : 1;
    saves[i] = stat_count(c, "saves");
    goals[i] = stat_count(c, "goals");
  }
  if (saves[1] >= 0 && goals[0] >= 0)
    h.team_shots = (int16_t) (saves[1] + goals[0]);
  if (saves[0] >= 0 && goals[1] >= 0)
    h.opp_shots = (int16_t) (saves[0] + goals[1]);
  // The latest goal, while the puck is in play. A shootout's goals don't
  // count on the scoreboard until the end, so the last real one stays.
  if (s.state != GameState::IN || shootout(s) || s.play_value <= 0)
    return;
  int total = s.team_score + s.opp_score;
  if (total <= 0)
    return;
  h.goal_score = (uint16_t) total;
  h.goal_period = (uint8_t) (s.period > 0 && s.period < 255 ? s.period : 0);
  // The clock stops after a goal until the faceoff, so the read's clock is
  // the goal's.
  if (s.display_clock.find(':') != std::string::npos && s.display_clock.size() <= 5)
    put(h.goal_clock, s.display_clock);
  std::string name = player_name(s.play_athlete);
  put(h.goal_name, name.empty() ? scorer_from_text(s.last_play) : name);
}

}  // namespace detail

// Between periods ESPN's shortDetail reads "End of 1st" (status END_PERIOD)
static bool intermission(const GameSnapshot &s) {
  return s.state == GameState::IN && s.short_detail.rfind("End of", 0) == 0;
}

static std::string period_name(int period, bool playoffs) {
  switch (period) {
    case 1:
      return "1st";
    case 2:
      return "2nd";
    case 3:
      return "3rd";
    case 4:
      return "OT";
    default:
      if (period < 1)
        return "";
      return period >= 5 && !playoffs ? "SO" : std::to_string(period - 3) + "OT";  // playoffs: 2OT, 3OT
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
  return s.display_clock + " " + period_name(s.period, s.nhl.playoffs);
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

// ---- the row under the clock ---------------------------------------------------

std::string hockey_situation(const GameSnapshot &s, size_t max) {
  if (!s.valid || s.state != GameState::IN || intermission(s))
    return "";
  const Hockey &h = s.nhl;
  std::string sog;
  if (h.team_shots >= 0 && h.opp_shots >= 0)
    sog = "SOG " + std::to_string(h.team_shots) + "-" + std::to_string(h.opp_shots);
  if (sog.size() > max)
    sog.clear();
  if (h.goal_score == 0)
    return sog;
  // The goal's period, its clock and its scorer; the period gives way first
  // when it is the one on the clock row above, else the clock does.
  const std::string per = period_name(h.goal_period, h.playoffs);
  const std::string name = h.goal_name[0] ? h.goal_name : "Goal";
  const std::string clock = h.goal_clock;
  const bool same = h.goal_period == s.period;
  auto join = [](std::initializer_list<const std::string *> parts) {
    std::string t;
    for (const std::string *p : parts) {
      if (p->empty())
        continue;
      if (!t.empty())
        t += ' ';
      t += *p;
    }
    return t;
  };
  const std::string tries[] = {join({&per, &clock, &name}), same ? join({&clock, &name}) : join({&per, &name}),
                               name};
  const size_t room = sog.empty() ? max : max >= sog.size() + 2 ? max - sog.size() - 2 : 0;
  for (const std::string &t : tries)
    if (t.size() <= room)
      return sog.empty() ? t : sog + "  " + t;
  if (!sog.empty())
    return sog;
  return name.substr(0, max);
}

void hockey_hold_goal(const GameSnapshot &prev, GameSnapshot &cur) {
  const Hockey &p = prev.nhl;
  Hockey &c = cur.nhl;
  if (!prev.valid || !cur.valid || prev.event_id != cur.event_id || p.goal_score == 0)
    return;
  if (c.goal_score == p.goal_score) {
    // The same goal read again: the clock from its first read, which is
    // nearest the goal, and its scorer once ESPN has one.
    c.goal_period = p.goal_period;
    memcpy(c.goal_clock, p.goal_clock, sizeof(c.goal_clock));
    if (!c.goal_name[0])
      memcpy(c.goal_name, p.goal_name, sizeof(c.goal_name));
    return;
  }
  // A later play: keep the goal while the score still has it (a goal taken
  // back on review drops it).
  if (c.goal_score == 0 && cur.team_score + cur.opp_score >= p.goal_score) {
    c.goal_score = p.goal_score;
    c.goal_period = p.goal_period;
    memcpy(c.goal_clock, p.goal_clock, sizeof(c.goal_clock));
    memcpy(c.goal_name, p.goal_name, sizeof(c.goal_name));
  }
}

}  // namespace espn
