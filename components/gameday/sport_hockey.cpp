// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Hockey's ticker, clock row and splashes. Pure, no ESPHome includes.
// ESPN's NHL feed has no power play field. The board must not infer one from
// penalties, because offsetting and coincidental minors would make it wrong.
#include "espn_parse.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace espn {

using detail::add_last_play;
using detail::add_odds;
using detail::add_part;
using detail::pregame_records;
using detail::records_line;

static bool shootout(const GameSnapshot &s) { return s.period >= 5 && !s.nhl.playoffs; }

// Into one of Hockey's fixed fields, cut to fit. C strings here and in the
// row below: std::string building costs kilobytes of flash on the panel.
template<size_t N> static void put(char (&dst)[N], const char *src, size_t len) {
  size_t n = len < N - 1 ? len : N - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
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
  // The hat trick: who has how many goals (fill_leaders_filter keeps the
  // leader lists; this adds the player's id).
  JsonObject list = comp["competitors"][0]["leaders"][0];
  list["name"] = true;
  JsonObject top = list["leaders"][0];
  top["displayValue"] = true;
  top["athlete"]["id"] = true;
}

// Each side's players on two goals or more in this game, from ESPN's goal
// leaders, which list every scorer once the puck drops (the season's before).
static void big_play_scorers(JsonObjectConst comp, GameSnapshot &s) {
  Hockey &h = s.nhl;
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
    const bool ours = (uint32_t) atol(str_or_empty(c["id"]).c_str()) == s.team_id;
    Hockey::Scorer *out = ours ? h.team_scorers : h.opp_scorers;
    uint8_t &count = ours ? h.team_scorer_count : h.opp_scorer_count;
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
}

// One of a side's stats ("saves", "goals") as a count, -1 when it has none
static int stat_count(JsonObjectConst competitor, const char *name) {
  for (JsonObjectConst st : competitor["statistics"].as<JsonArrayConst>()) {
    const char *k = st["name"].as<const char *>();
    if (k == nullptr || strcmp(k, name) != 0)
      continue;
    const char *v = st["displayValue"].as<const char *>();
    if (v == nullptr || *v < '0' || *v > '9')
      return -1;
    long n = strtol(v, nullptr, 10);
    return n > 999 ? 999 : (int) n;
  }
  return -1;
}

// The scorer: ESPN's short name without the initial ("M. Geekie" ->
// "Geekie", as soccer's), else the last word of the name in the goal's text
// ("Morgan Geekie Goal (5) Slap Shot, assists: ...", seen live 11/8). A goal
// ESPN has not named yet starts ", assists:" and gives none.
static void scorer(const std::string &short_name, const std::string &text, char (&out)[sizeof(Hockey::goal_name)]) {
  const char *n = short_name.c_str();
  const char *dot = strstr(n, ". ");
  if (dot != nullptr && dot - n <= 2)
    n = dot + 2;
  if (*n) {
    put(out, n, strlen(n));
    return;
  }
  const char *t = text.c_str();
  const char *paren = strstr(t, " Goal (");
  if (paren == nullptr)
    paren = strstr(t, " (");
  if (paren == nullptr || paren == t || *t == ',') {
    out[0] = '\0';
    return;
  }
  const char *start = paren;
  while (start > t && start[-1] != ' ')
    start--;
  put(out, start, paren - start);
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
    const char *id = c["id"].as<const char *>();
    int i = id != nullptr && (uint32_t) atol(id) == s.team_id ? 0 : 1;
    saves[i] = stat_count(c, "saves");
    goals[i] = stat_count(c, "goals");
  }
  if (saves[1] >= 0 && goals[0] >= 0)
    h.team_shots = (int16_t) (saves[1] + goals[0]);
  if (saves[0] >= 0 && goals[1] >= 0)
    h.opp_shots = (int16_t) (saves[0] + goals[1]);
  if (s.state == GameState::IN)
    big_play_scorers(comp, s);
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
  const std::string &clock = s.display_clock;
  if (clock.find(':') != std::string::npos && clock.size() < sizeof(h.goal_clock))
    put(h.goal_clock, clock.c_str(), clock.size());
  scorer(s.play_athlete, s.last_play, h.goal_name);
}

}  // namespace detail

// Between periods ESPN's shortDetail reads "End of 1st" (status END_PERIOD)
static bool intermission(const GameSnapshot &s) {
  return s.state == GameState::IN && s.short_detail.rfind("End of", 0) == 0;
}

// "1st", "OT", "SO", or in the playoffs "2OT", "3OT"; "" before the start
static const char *period_word(char (&buf)[16], int period, bool playoffs) {
  static const char *const kNames[] = {"", "1st", "2nd", "3rd", "OT"};
  if (period < 1)
    return "";
  if (period <= 4)
    return kNames[period];
  if (!playoffs)
    return "SO";
  snprintf(buf, sizeof(buf), "%dOT", period - 3);
  return buf;
}

static std::string period_name(int period, bool playoffs) {
  char buf[16];
  return period_word(buf, period, playoffs);
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

void hockey_row_forms(const GameSnapshot &s, const GoalHold *goal, std::string shots[3], std::string goal_line[3]) {
  for (int i = 0; i < 3; i++)
    shots[i].clear(), goal_line[i].clear();
  if (!s.valid || s.state != GameState::IN || intermission(s))
    return;
  const Hockey &h = s.nhl;
  char t[64];
  if (h.team_shots >= 0 && h.opp_shots >= 0) {
    static const char *const kLabels[] = {"Shots on goal", "Shots", "SOG"};
    for (int i = 0; i < 3; i++) {
      snprintf(t, sizeof(t), "%s %d-%d", kLabels[i], h.team_shots, h.opp_shots);
      shots[i] = t;
    }
  }
  if (goal == nullptr || goal->score == 0)
    return;
  // The goal's period, its clock and its scorer; the period gives way first
  // when it is the one on the clock row above, else the clock does.
  char pbuf[16];
  const char *per = period_word(pbuf, goal->period, h.playoffs);
  const char *name = goal->name[0] ? goal->name : "Goal";
  const char *clock = goal->clock;
  const bool same = goal->period == s.period;
  const char *forms[3][3] = {{per, clock, name}, {same ? "" : per, same ? clock : "", name}, {"", "", name}};
  for (int i = 0; i < 3; i++) {
    size_t n = 0;
    t[0] = '\0';
    for (const char *p : forms[i])
      if (*p && n < sizeof(t))
        n += snprintf(t + n, sizeof(t) - n, n > 0 ? " %s" : "%s", p);
    goal_line[i] = t;
  }
}

std::string hockey_situation(const GameSnapshot &s, const GoalHold *goal, size_t max) {
  std::string shots[3], line[3];
  hockey_row_forms(s, goal, shots, line);
  // On one line the shots are spelled out, or "Shots" when the goal or a
  // count needs the room; the goal keeps as much of itself as fits.
  for (const std::string &g : line) {
    if (g.empty())
      break;
    if (shots[0].empty() && g.size() <= max)
      return g;
    for (int i = 0; i < 2 && !shots[i].empty(); i++)
      if (shots[i].size() + 2 + g.size() <= max)
        return shots[i] + "  " + g;
  }
  for (int i = 0; i < 2; i++)
    if (!shots[i].empty() && shots[i].size() <= max)
      return shots[i];
  return line[0].empty() ? "" : line[2].substr(0, max);
}

// ---- held goals ----------------------------------------------------------------

static uint64_t event_number(const std::string &id) { return strtoull(id.c_str(), nullptr, 10); }

const GoalHold *GoalHolds::find(const std::string &event_id) const {
  uint64_t ev = event_number(event_id);
  for (const GoalHold &h : this->slot)
    if (ev != 0 && h.event == ev && h.score > 0)
      return &h;
  return nullptr;
}

void GoalHolds::update(const GameSnapshot &g) {
  if (!g.valid || g.sport != Sport::HOCKEY)
    return;
  uint64_t ev = event_number(g.event_id);
  if (ev == 0)
    return;
  GoalHold *h = nullptr;
  for (GoalHold &x : this->slot)
    if (x.event == ev)
      h = &x;
  const Hockey &n = g.nhl;
  if (n.goal_score == 0) {
    // A later play: the goal stays while the score is the one it made. A
    // goal taken back on review takes it off; so does a goal the board
    // never saw as a last play (one ESPN shows only as "Video Review", seen
    // live 11/8), since the held one is no longer the latest. The next
    // read that carries the new goal fills it in.
    if (h != nullptr && g.team_score + g.opp_score != h->score)
      *h = GoalHold{};
    return;
  }
  if (h != nullptr && h->score == n.goal_score) {
    // The same goal read again: the clock from its first read, which is
    // nearest the goal, and its scorer once ESPN has one.
    if (!h->name[0])
      memcpy(h->name, n.goal_name, sizeof(h->name));
    return;
  }
  if (h == nullptr) {
    h = &this->slot[this->next];
    this->next = (uint8_t) ((this->next + 1) % kGames);
  }
  h->event = ev;
  h->score = n.goal_score;
  h->period = n.goal_period;
  memcpy(h->clock, n.goal_clock, sizeof(h->clock));
  memcpy(h->name, n.goal_name, sizeof(h->name));
}

}  // namespace espn
