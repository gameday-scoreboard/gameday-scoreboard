// Template bodies for the streaming parsers. Included from espn_parse.h.
#pragma once

#include <ArduinoJson.h>

#include <algorithm>

namespace espn {
namespace detail {

inline ArduinoJson::Allocator *&json_allocator_slot() {
  static ArduinoJson::Allocator *slot = nullptr;
  return slot;
}

// Where the component's JSON documents live. The firmware points this at
// PSRAM in GamedayComponent::setup(); host tests keep ArduinoJson's default.
inline ArduinoJson::Allocator *json_allocator() {
  ArduinoJson::Allocator *a = json_allocator_slot();
  return a != nullptr ? a : ArduinoJson::detail::DefaultAllocator::instance();
}

inline std::string str_or_empty(JsonVariantConst v) {
  const char *s = v.as<const char *>();
  return s ? std::string(s) : std::string();
}

// True when the competition says its kickoff time is not set yet
// (timeValid false). A document without the field has a real time.
inline bool time_tbd(JsonObjectConst comp) {
  JsonVariantConst v = comp["timeValid"];
  return v.is<bool>() && !v.as<bool>();
}

// The fields read from one event, whether it sits in a scoreboard's events
// array or is a single-event document's root. board_extras adds what the
// halftime and final boards need (line score, headline, the last play's
// details for the scoring log): every league polls its game on its own
// (LeagueInfo::per_event), so a whole day's scoreboard, up to 750 KB for
// college football, leaves them out.
inline void fill_event_filter(JsonObject ev, bool board_extras = true) {
  ev["id"] = true;
  ev["date"] = true;
  ev["status"]["displayClock"] = true;
  ev["status"]["period"] = true;
  ev["status"]["type"]["state"] = true;
  ev["status"]["type"]["completed"] = true;
  ev["status"]["type"]["shortDetail"] = true;
  JsonObject comp = ev["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  comp["situation"]["lastPlay"]["text"] = true;
  comp["situation"]["lastPlay"]["scoreValue"] = true;
  comp["situation"]["lastPlay"]["drive"]["description"] = true;
  comp["situation"]["lastPlay"]["probability"]["homeWinPercentage"] = true;
  comp["situation"]["lastPlay"]["probability"]["awayWinPercentage"] = true;
  comp["situation"]["downDistanceText"] = true;
  comp["situation"]["shortDownDistanceText"] = true;
  comp["situation"]["isRedZone"] = true;
  comp["situation"]["possession"] = true;
  comp["situation"]["homeTimeouts"] = true;
  comp["situation"]["awayTimeouts"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["id"] = true;
  c["homeAway"] = true;
  c["score"] = true;
  c["winner"] = true;
  c["records"].add<JsonObject>()["summary"] = true;
  c["team"]["abbreviation"] = true;
  c["team"]["color"] = true;
  c["team"]["logo"] = true;
  comp["odds"].add<JsonObject>()["details"] = true;
  comp["odds"][0]["overUnder"] = true;
  comp["broadcasts"].add<JsonObject>()["names"] = true;
  comp["venue"]["fullName"] = true;
  comp["notes"].add<JsonObject>()["headline"] = true;
  if (!board_extras)
    return;
  comp["situation"]["lastPlay"]["type"]["text"] = true;
  comp["situation"]["lastPlay"]["statYardage"] = true;
  comp["situation"]["lastPlay"]["team"]["id"] = true;
  comp["situation"]["lastPlay"]["athletesInvolved"][0]["shortName"] = true;
  c["linescores"][0]["value"] = true;
  comp["headlines"][0]["shortLinkText"] = true;
}

inline void fill_scoreboard_filter(JsonDocument &f) { fill_event_filter(f["events"].add<JsonObject>(), false); }

inline void fill_baseball_filter(JsonObject ev) {
  JsonObject comp = ev["competitions"][0];
  JsonObject sit = comp["situation"];
  sit["balls"] = true;
  sit["strikes"] = true;
  sit["outs"] = true;
  sit["onFirst"] = true;
  sit["onSecond"] = true;
  sit["onThird"] = true;
  sit["batter"]["athlete"]["shortName"] = true;
  sit["pitcher"]["athlete"]["shortName"] = true;
  sit["lastPlay"]["type"]["text"] = true;
  JsonObject c = comp["competitors"][0];
  c["hits"] = true;
  c["errors"] = true;
  c["probables"][0]["athlete"]["shortName"] = true;
  comp["series"]["summary"] = true;
}

inline void fill_upcoming_filter(JsonDocument &f) {
  JsonObject ev = f["events"].add<JsonObject>();
  ev["id"] = true;
  ev["date"] = true;
  JsonObject comp = ev["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  comp["neutralSite"] = true;
  comp["status"]["type"]["state"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["homeAway"] = true;
  c["team"]["id"] = true;
  c["team"]["abbreviation"] = true;
  c["team"]["shortDisplayName"] = true;
  comp["broadcasts"].add<JsonObject>()["media"]["shortName"] = true;
}

// finals keeps what a finished game's card needs as well (the finals
// rotation, recap.h), pending what a game later today's card needs; without
// them the scan stays as lean as it was.
inline void fill_scan_filter(JsonDocument &f, bool finals = false, bool pending = false) {
  JsonObject ev = f["events"].add<JsonObject>();
  ev["id"] = true;
  ev["date"] = true;
  ev["status"]["type"]["state"] = true;
  JsonObject comp = ev["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["id"] = true;
  c["homeAway"] = true;
  c["team"]["abbreviation"] = true;
  c["team"]["conferenceId"] = true;
  if (!finals && !pending)
    return;
  c["records"].add<JsonObject>()["summary"] = true;
  c["curatedRank"]["current"] = true;
  c["team"]["color"] = true;
  if (pending)
    comp["broadcasts"].add<JsonObject>()["names"] = true;
  if (!finals)
    return;
  ev["status"]["type"]["completed"] = true;
  ev["status"]["type"]["shortDetail"] = true;
  c["score"] = true;
  c["winner"] = true;
}

inline void fill_team_filter(JsonDocument &f) {
  f["team"]["id"] = true;
  f["team"]["color"] = true;
  f["team"]["record"]["items"].add<JsonObject>()["summary"] = true;
  f["team"]["groups"]["id"] = true;
  JsonObject ne = f["team"]["nextEvent"].add<JsonObject>();
  ne["id"] = true;
  ne["date"] = true;
  JsonObject comp = ne["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  comp["status"]["type"]["state"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["id"] = true;
  c["team"]["abbreviation"] = true;
}

inline void fill_team_game_filter(JsonDocument &f) {
  JsonObject ev = f["events"].add<JsonObject>();
  ev["id"] = true;
  ev["date"] = true;
  ev["status"]["type"]["state"] = true;
  JsonObject comp = ev["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  comp["competitors"].add<JsonObject>()["id"] = true;
}

inline std::string format_over_under(JsonVariantConst v) {
  if (v.isNull())
    return "";
  float f = v.as<float>();
  char buf[16];
  if (f == (int) f)
    snprintf(buf, sizeof(buf), "%d", (int) f);
  else
    snprintf(buf, sizeof(buf), "%.1f", f);
  return buf;
}

inline bool snapshot_from_event(JsonObjectConst ev, uint32_t our_team_id, GameSnapshot &out) {
  GameSnapshot s;
  s.event_id = str_or_empty(ev["id"]);
  s.kickoff_epoch = parse_iso8601_z(str_or_empty(ev["date"]));
  JsonObjectConst status = ev["status"];
  std::string state = str_or_empty(status["type"]["state"]);
  if (state == "in")
    s.state = GameState::IN;
  else if (state == "post")
    s.state = GameState::POST;
  else
    s.state = GameState::PRE;
  s.completed = status["type"]["completed"] | false;
  s.short_detail = str_or_empty(status["type"]["shortDetail"]);
  s.display_clock = str_or_empty(status["displayClock"]);
  s.period = status["period"] | 0;

  JsonObjectConst comp = ev["competitions"][0];
  if (comp.isNull())
    return false;
  s.time_tbd = time_tbd(comp);
  JsonObjectConst sit = comp["situation"];
  s.last_play = str_or_empty(sit["lastPlay"]["text"]);
  s.down_distance = str_or_empty(sit["downDistanceText"]);
  s.short_down_distance = str_or_empty(sit["shortDownDistanceText"]);
  s.is_red_zone = sit["isRedZone"] | false;
  std::string possession = str_or_empty(sit["possession"]);
  int home_to = sit["homeTimeouts"] | 0;
  int away_to = sit["awayTimeouts"] | 0;

  bool found_us = false;
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
    uint32_t id = (uint32_t) atol(str_or_empty(c["id"]).c_str());
    bool home = str_or_empty(c["homeAway"]) == "home";
    std::string abbr = str_or_empty(c["team"]["abbreviation"]);
    int score = atoi(str_or_empty(c["score"]).c_str());
    std::string record = str_or_empty(c["records"][0]["summary"]);
    std::string color = str_or_empty(c["team"]["color"]);
    std::string logo = dark_logo(str_or_empty(c["team"]["logo"]));
    bool winner = c["winner"] | false;
    int timeouts = home ? home_to : away_to;
    bool ours = id == our_team_id;
    if (ours)
      found_us = true;
    std::string &t_abbr = ours ? s.team_abbr : s.opp_abbr;
    t_abbr = abbr;
    (ours ? s.team_id : s.opp_id) = id;
    (ours ? s.team_score : s.opp_score) = score;
    (ours ? s.team_record : s.opp_record) = record;
    (ours ? s.team_color : s.opp_color) = color;
    (ours ? s.team_logo : s.opp_logo) = logo;
    (ours ? s.team_timeouts : s.opp_timeouts) = timeouts;
    uint8_t *lines = ours ? s.team_lines : s.opp_lines;
    uint8_t &count = ours ? s.team_line_count : s.opp_line_count;
    count = 0;
    for (JsonVariantConst ls : c["linescores"].as<JsonArrayConst>()) {
      float v = ls["value"] | 0.0f;
      int pts = v > 0 ? (int) (v + 0.5f) : 0;
      if (count < GameSnapshot::kLinesMax)
        lines[count++] = 0;
      pts += lines[count - 1];
      lines[count - 1] = (uint8_t) std::min(pts, 255);
    }
    if (ours) {
      s.team_winner = winner;
      s.team_home = home;
    }
    if (!possession.empty() && possession == str_or_empty(c["id"]))
      s.possession = ours ? 1 : 2;
  }
  if (!found_us)
    return false;

  // A touchdown is worth 6 and a field goal 3; the extra point, two-point try
  // and safety after them don't repeat the drive.
  JsonObjectConst lp = sit["lastPlay"];
  if ((lp["scoreValue"] | 0) >= 3)
    s.drive = str_or_empty(lp["drive"]["description"]);
  s.play_type = str_or_empty(lp["type"]["text"]);
  s.play_athlete = str_or_empty(lp["athletesInvolved"][0]["shortName"]);
  s.play_value = (int8_t) std::min(100, std::max(0, lp["scoreValue"] | 0));
  s.play_yards = (int16_t) std::min(200, std::max(-200, lp["statYardage"] | 0));
  s.play_team = (uint32_t) atol(str_or_empty(lp["team"]["id"]).c_str());
  JsonVariantConst win = lp["probability"][s.team_home ? "homeWinPercentage" : "awayWinPercentage"];
  if (win.is<float>()) {
    // ESPN goes to 0.999 and 0.001 well before the end; 100% would claim
    // a result the game hasn't reached.
    int pct = (int) (win.as<float>() * 100.0f + 0.5f);
    s.team_win_pct = (int8_t) std::min(99, std::max(1, pct));
  }

  JsonObjectConst odds = comp["odds"][0];
  s.odds = str_or_empty(odds["details"]);
  s.over_under = format_over_under(odds["overUnder"]);
  s.tv = str_or_empty(comp["broadcasts"][0]["names"][0]);
  s.venue = str_or_empty(comp["venue"]["fullName"]);
  s.game_name = short_game_name(str_or_empty(comp["notes"][0]["headline"]));
  s.headline = str_or_empty(comp["headlines"][0]["shortLinkText"]);
  s.valid = true;
  out = s;
  return true;
}

inline Half half_from_detail(const std::string &detail) {
  if (detail.rfind("Top", 0) == 0)
    return Half::TOP;
  if (detail.rfind("Bot", 0) == 0)
    return Half::BOTTOM;
  if (detail.rfind("Mid", 0) == 0)
    return Half::MID;
  if (detail.rfind("End", 0) == 0)
    return Half::END;
  return Half::NONE;
}

// Fills s.mlb from an event already read into s by snapshot_from_event.
inline void baseball_from_event(JsonObjectConst ev, GameSnapshot &s) {
  Baseball &b = s.mlb;
  JsonObjectConst comp = ev["competitions"][0];
  b.series = str_or_empty(comp["series"]["summary"]);
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
    bool ours = (uint32_t) atol(str_or_empty(c["id"]).c_str()) == s.team_id;
    (ours ? b.team_hits : b.opp_hits) = c["hits"] | 0;
    (ours ? b.team_errors : b.opp_errors) = c["errors"] | 0;
    (ours ? b.team_probable : b.opp_probable) = str_or_empty(c["probables"][0]["athlete"]["shortName"]);
  }
  if (s.state != GameState::IN)
    return;
  b.half = half_from_detail(s.short_detail);
  JsonObjectConst sit = comp["situation"];
  b.play_type = str_or_empty(sit["lastPlay"]["type"]["text"]);
  if (b.half != Half::TOP && b.half != Half::BOTTOM)
    return;  // between half innings ESPN keeps the last count; it means nothing
  if (!sit["balls"].isNull())
    b.balls = (int8_t) (sit["balls"] | 0);
  if (!sit["strikes"].isNull())
    b.strikes = (int8_t) (sit["strikes"] | 0);
  if (!sit["outs"].isNull())
    b.outs = (int8_t) (sit["outs"] | 0);
  b.bases = ((sit["onFirst"] | false) ? Baseball::FIRST : 0) | ((sit["onSecond"] | false) ? Baseball::SECOND : 0) |
            ((sit["onThird"] | false) ? Baseball::THIRD : 0);
  b.batter = str_or_empty(sit["batter"]["athlete"]["shortName"]);
  b.pitcher = str_or_empty(sit["pitcher"]["athlete"]["shortName"]);
  // The visitors bat in the top half: that side gets the underline.
  bool we_bat = (b.half == Half::TOP) != s.team_home;
  s.possession = we_bat ? 1 : 2;
}

// Soccer (sport_soccer.cpp): the status name, details, form, stats and
// moneyline on top of the shared event filter, read into s.soc.
void fill_soccer_filter(JsonObject ev);
void soccer_from_event(JsonObjectConst ev, GameSnapshot &s);
// Basketball (sport_basketball.cpp): the extra fields to keep, and s.nba
// filled from an event already read into s by snapshot_from_event.
void fill_basketball_filter(JsonObject ev);
void basketball_from_event(JsonObjectConst ev, GameSnapshot &s);
// Hockey's extra filter fields and their parse (sport_hockey.cpp)
void fill_hockey_filter(JsonObject ev);
void hockey_from_event(JsonObjectConst ev, GameSnapshot &s);

}  // namespace detail

// Must be set before any parse runs, since the fetch task reads it unlocked.
inline void set_json_allocator(ArduinoJson::Allocator *allocator) { detail::json_allocator_slot() = allocator; }

// Parses the team endpoint. `input` is anything ArduinoJson can read from:
// a std::string, or a reader object with read() and readBytes().
template<typename TInput> bool parse_team(TInput &input, Schedule &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_team_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  JsonObjectConst team = doc["team"];
  if (team.isNull())
    return false;
  Schedule s;
  s.team_color = detail::str_or_empty(team["color"]);
  s.team_record = detail::str_or_empty(team["record"]["items"][0]["summary"]);
  // ESPN sends the group id as a string ("1"), sometimes as a number.
  JsonVariantConst group = team["groups"]["id"];
  s.group = group.is<const char *>() ? (uint32_t) atol(group.as<const char *>()) : (group | 0);
  JsonObjectConst ne = team["nextEvent"][0];
  if (!ne.isNull()) {
    s.event_id = detail::str_or_empty(ne["id"]);
    s.kickoff_epoch = parse_iso8601_z(detail::str_or_empty(ne["date"]));
    s.time_tbd = detail::time_tbd(ne["competitions"][0]);
    s.next_final = detail::str_or_empty(ne["competitions"][0]["status"]["type"]["state"]) == "post";
    std::string us = detail::str_or_empty(team["id"]);
    for (JsonObjectConst c : ne["competitions"][0]["competitors"].as<JsonArrayConst>()) {
      std::string id = detail::str_or_empty(c["id"]);
      if (id.empty() || id == us)
        continue;
      s.opp_id = (uint32_t) atol(id.c_str());
      s.opp_abbr = detail::str_or_empty(c["team"]["abbreviation"]);
    }
  }
  s.valid = true;
  out = s;
  return true;
}

// Games that have not started yet, in the order ESPN lists them (by date).
template<typename TInput>
bool parse_upcoming(TInput &input, uint32_t our_team_id, size_t max, std::vector<Upcoming> &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_upcoming_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  out.clear();
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    if (out.size() >= max)
      break;
    JsonObjectConst comp = ev["competitions"][0];
    if (detail::str_or_empty(comp["status"]["type"]["state"]) != "pre")
      continue;
    Upcoming u;
    u.event_id = detail::str_or_empty(ev["id"]);
    u.kickoff_epoch = parse_iso8601_z(detail::str_or_empty(ev["date"]));
    u.time_tbd = detail::time_tbd(comp);
    u.neutral = comp["neutralSite"] | false;
    for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
      uint32_t id = (uint32_t) atol(detail::str_or_empty(c["team"]["id"]).c_str());
      bool home = detail::str_or_empty(c["homeAway"]) == "home";
      if (id == our_team_id) {
        u.home = home;
      } else {
        u.opp_id = id;
        u.opp_abbr = detail::str_or_empty(c["team"]["abbreviation"]);
        u.opp_name = detail::str_or_empty(c["team"]["shortDisplayName"]);
      }
    }
    JsonVariantConst tv = comp["broadcasts"][0]["media"]["shortName"];
    u.tv = detail::str_or_empty(tv);
    if (!u.event_id.empty() && u.opp_id != 0)
      out.push_back(u);
  }
  return true;
}

namespace detail {
// The better AP rank of a game's two sides, 99 when neither is ranked.
inline uint8_t best_rank(JsonObjectConst comp) {
  int rank = 99;
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
    int r = c["curatedRank"]["current"] | 99;
    if (r >= 1 && r < rank)
      rank = r;
  }
  return (uint8_t) rank;
}
}  // namespace detail

// Lists the in-progress games in a scoreboard document. With next_kickoff,
// also reports the earliest kickoff among the games not started yet, or 0
// when there is none. A game whose time is still TBD is listed at midnight
// Eastern with timeValid false, and does not count. With finals, also lists
// the games that were played to the end (not postponed or canceled). With
// pending, also lists the games next_kickoff counts (a postponed or
// canceled game is "post", not "pre"): the kPendingKeep best by
// pending_before(), in that order. Finals and pending come with no league
// set: the caller knows which league it asked for.
template<typename TInput, typename TOut>
bool parse_live_games(TInput &input, TOut &out, int64_t *next_kickoff = nullptr,
                      std::vector<Final> *finals = nullptr, std::vector<Pending> *pending = nullptr) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_scan_filter(filter, finals != nullptr, pending != nullptr);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  if (next_kickoff != nullptr)
    *next_kickoff = 0;
  // Kept apart from *pending until the end, so the cap counts this document only.
  std::vector<Pending> later;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    std::string state = detail::str_or_empty(ev["status"]["type"]["state"]);
    bool tbd = detail::time_tbd(ev["competitions"][0]);
    uint32_t away = 0;
    if (finals != nullptr || pending != nullptr)
      for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>())
        if (detail::str_or_empty(c["homeAway"]) != "home")
          away = (uint32_t) atol(detail::str_or_empty(c["id"]).c_str());
    if (state == "pre" && !tbd) {
      int64_t k = parse_iso8601_z(detail::str_or_empty(ev["date"]));
      if (k > 0 && next_kickoff != nullptr && (*next_kickoff == 0 || k < *next_kickoff))
        *next_kickoff = k;
      GameSnapshot g;
      if (k > 0 && pending != nullptr && away != 0 && detail::snapshot_from_event(ev, away, g)) {
        Pending p;
        p.rank = detail::best_rank(ev["competitions"][0]);
        p.kickoff_epoch = k;
        p.event_id = std::move(g.event_id);
        p.away_id = g.team_id;
        p.home_id = g.opp_id;
        p.away_abbr = std::move(g.team_abbr);
        p.home_abbr = std::move(g.opp_abbr);
        p.away_color = std::move(g.team_color);
        p.home_color = std::move(g.opp_color);
        p.away_record = std::move(g.team_record);
        p.home_record = std::move(g.opp_record);
        p.tv = std::move(g.tv);
        // Sorted as it goes and never more than kPendingKeep: a college
        // Saturday has sixty games to come.
        auto at = std::upper_bound(later.begin(), later.end(), p, pending_before);
        if (later.size() < kPendingKeep || at != later.end()) {
          later.insert(at, std::move(p));
          if (later.size() > kPendingKeep)
            later.pop_back();
        }
      }
    }
    if (state == "post" && finals != nullptr && (ev["status"]["type"]["completed"] | false)) {
      GameSnapshot g;
      if (away != 0 && detail::snapshot_from_event(ev, away, g)) {
        Final fin;
        fin.rank = detail::best_rank(ev["competitions"][0]);
        fin.kickoff_epoch = g.kickoff_epoch;
        fin.detail = g.short_detail;
        fin.away_id = g.team_id;
        fin.home_id = g.opp_id;
        fin.away_score = g.team_score;
        fin.home_score = g.opp_score;
        fin.away_abbr = g.team_abbr;
        fin.home_abbr = g.opp_abbr;
        fin.away_color = g.team_color;
        fin.home_color = g.opp_color;
        fin.away_record = g.team_record;
        fin.home_record = g.opp_record;
        finals->push_back(std::move(fin));
      }
      continue;
    }
    if (state != "in")
      continue;
    LiveGame g;
    g.event_id = detail::str_or_empty(ev["id"]);
    for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
      bool home = detail::str_or_empty(c["homeAway"]) == "home";
      std::string abbr = detail::str_or_empty(c["team"]["abbreviation"]);
      if (home) {
        g.home_abbr = abbr;
        g.home_id = (uint32_t) atol(detail::str_or_empty(c["id"]).c_str());
        g.group = (uint32_t) atol(detail::str_or_empty(c["team"]["conferenceId"]).c_str());
      } else {
        g.away_abbr = abbr;
        g.away_id = (uint32_t) atol(detail::str_or_empty(c["id"]).c_str());
      }
    }
    if (!g.event_id.empty() && g.away_id != 0)
      out.push_back(g);
  }
  if (pending != nullptr)
    for (auto &p : later)
      pending->push_back(std::move(p));
  return true;
}

// The first game in a day's scoreboard with our team that is not over yet
// (game 2 of a doubleheader after game 1's final). skip_event is a game the
// panel already saw finish, which the scoreboard may still list as live.
// Returns 1 and fills event_id, kickoff_epoch and time_tbd, 0 when the team
// does not play that day, -1 when the document does not parse.
template<typename TInput>
int parse_team_game(TInput &input, uint32_t our_team_id, const std::string &skip_event, Schedule &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_team_game_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return -1;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    if (detail::str_or_empty(ev["status"]["type"]["state"]) == "post")
      continue;
    if (!skip_event.empty() && detail::str_or_empty(ev["id"]) == skip_event)
      continue;
    for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
      if ((uint32_t) atol(detail::str_or_empty(c["id"]).c_str()) != our_team_id)
        continue;
      out.event_id = detail::str_or_empty(ev["id"]);
      out.kickoff_epoch = parse_iso8601_z(detail::str_or_empty(ev["date"]));
      out.time_tbd = detail::time_tbd(ev["competitions"][0]);
      return out.event_id.empty() ? 0 : 1;
    }
  }
  return 0;
}

// Parses a single-event document (event_url): the root object is the event.
// ESPN serves it with the same fields as the event's entry in the day's
// scoreboard, at a tenth of the size or less. Returns false on a JSON error,
// when our team is not in it, or when event_id is given and the document is
// some other game.
template<typename TInput>
bool parse_event(TInput &input, Sport sport, uint32_t our_team_id, GameSnapshot &out,
                 const std::string &event_id = "") {
  JsonDocument filter(detail::json_allocator());
  detail::fill_event_filter(filter.to<JsonObject>());
  if (sport == Sport::BASEBALL)
    detail::fill_baseball_filter(filter.as<JsonObject>());
  if (sport == Sport::SOCCER)
    detail::fill_soccer_filter(filter.as<JsonObject>());
  if (sport == Sport::BASKETBALL)
    detail::fill_basketball_filter(filter.as<JsonObject>());
  if (sport == Sport::HOCKEY)
    detail::fill_hockey_filter(filter.as<JsonObject>());
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  if (!event_id.empty() && detail::str_or_empty(doc["id"]) != event_id)
    return false;
  GameSnapshot s;
  if (!detail::snapshot_from_event(doc.as<JsonObjectConst>(), our_team_id, s))
    return false;
  s.sport = sport;
  if (sport == Sport::BASEBALL)
    detail::baseball_from_event(doc.as<JsonObjectConst>(), s);
  if (sport == Sport::SOCCER)
    detail::soccer_from_event(doc.as<JsonObjectConst>(), s);
  if (sport == Sport::BASKETBALL)
    detail::basketball_from_event(doc.as<JsonObjectConst>(), s);
  if (sport == Sport::HOCKEY)
    detail::hockey_from_event(doc.as<JsonObjectConst>(), s);
  out = s;
  return true;
}

// Parses a scoreboard document and extracts the event with `event_id`.
// Returns false on a JSON error or when the event is not in the document.
template<typename TInput>
bool parse_scoreboard(TInput &input, const std::string &event_id, uint32_t our_team_id, GameSnapshot &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_scoreboard_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    if (detail::str_or_empty(ev["id"]) != event_id)
      continue;
    return detail::snapshot_from_event(ev, our_team_id, out);
  }
  return false;
}

}  // namespace espn
