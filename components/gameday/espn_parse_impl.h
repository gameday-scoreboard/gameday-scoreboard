// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Template bodies for the streaming parsers. Included from espn_parse.h.
#pragma once

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "badge.h"
#include "message.h"
#include "news.h"
#include "season.h"

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

// A score, "21" or 21, kept to 0..999. strtol, not atoi: a number past
// int's range saturates instead of being undefined, and a huge score can't
// overflow the sums the splashes make from it.
inline int score_value(JsonVariantConst v) {
  long n = 0;
  if (v.is<const char *>())
    n = strtol(v.as<const char *>(), nullptr, 10);
  else if (v.is<long>())
    n = v.as<long>();
  return (int) (n < 0 ? 0 : n > 999 ? 999 : n);
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
// details for the scoring log), each side's second color and the pre-game
// ticker's week and forecast:
// every league polls its game on its own (fetch_game_), so a whole day's
// scoreboard, up to 750 KB for college football, leaves them out.
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
  comp["situation"]["lastPlay"]["id"] = true;
  comp["situation"]["lastPlay"]["text"] = true;
  comp["situation"]["lastPlay"]["scoreValue"] = true;
  comp["situation"]["lastPlay"]["drive"]["description"] = true;
  comp["situation"]["lastPlay"]["probability"]["homeWinPercentage"] = true;
  comp["situation"]["lastPlay"]["probability"]["awayWinPercentage"] = true;
  comp["situation"]["downDistanceText"] = true;
  comp["situation"]["shortDownDistanceText"] = true;
  comp["situation"]["isRedZone"] = true;
  comp["situation"]["possession"] = true;
  comp["situation"]["yardLine"] = true;
  comp["situation"]["down"] = true;
  comp["situation"]["distance"] = true;
  comp["situation"]["homeTimeouts"] = true;
  comp["situation"]["awayTimeouts"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["id"] = true;
  c["homeAway"] = true;
  c["score"] = true;
  c["winner"] = true;
  c["records"].add<JsonObject>()["summary"] = true;
  c["curatedRank"]["current"] = true;
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
  // The second color, for the letters of a side with no logo (badge.h).
  c["team"]["alternateColor"] = true;
  comp["headlines"][0]["shortLinkText"] = true;
  ev["season"]["type"] = true;
  ev["week"]["number"] = true;
  ev["weather"]["temperature"] = true;
  ev["weather"]["displayValue"] = true;
  ev["weather"]["conditionId"] = true;
  comp["venue"]["indoor"] = true;
}

// The venue's city, for a forecast of the panel's own (weather.h). Only in
// the sports played outside: an arena's would only cost memory.
inline void fill_venue_place_filter(JsonObject ev) {
  for (const char *k : {"city", "state", "country"})
    ev["competitions"][0]["venue"]["address"][k] = true;
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
  // Where each live game stands, for the best-game pick.
  ev["status"]["period"] = true;
  ev["status"]["clock"] = true;
  c["score"] = true;
  c["curatedRank"]["current"] = true;
  if (!finals && !pending)
    return;
  c["records"].add<JsonObject>()["summary"] = true;
  c["team"]["color"] = true;
  if (pending)
    comp["broadcasts"].add<JsonObject>()["names"] = true;
  if (!finals)
    return;
  ev["status"]["type"]["completed"] = true;
  ev["status"]["type"]["shortDetail"] = true;
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
  // Range first: casting a float past int's range is undefined.
  if (f > -1e6f && f < 1e6f && f == (int) f)
    snprintf(buf, sizeof(buf), "%d", (int) f);
  else
    snprintf(buf, sizeof(buf), "%.1f", f);
  return buf;
}

inline bool has_letter(const char *s) {
  for (; s != nullptr && *s; s++)
    if (isalpha((unsigned char) *s))
      return true;
  return false;
}

// "76F Cloudy" from an event's forecast, "" when it has no temperature.
// ESPN sometimes puts a number in displayValue and the words in conditionId
// (RUTG at NU, 2026-10-24: "33" and "Clear"), so the words come from
// whichever of the two has letters. Words past 32 characters are cut at a
// space.
inline std::string weather_text(JsonObjectConst w) {
  JsonVariantConst t = w["temperature"];
  if (!t.is<float>())
    return "";
  float f = t.as<float>();
  if (!(f > -80.0f && f < 150.0f))  // NaN fails too
    return "";
  std::string out = std::to_string((int) (f < 0 ? f - 0.5f : f + 0.5f)) + "F";
  const char *words = w["displayValue"].as<const char *>();
  if (!has_letter(words))
    words = w["conditionId"].as<const char *>();
  if (!has_letter(words))
    return out;
  std::string text = words;
  if (text.size() > 32) {
    size_t cut = text.rfind(' ', 32);
    text.resize(cut != std::string::npos && cut > 0 ? cut : 32);
  }
  return out + " " + text;
}

// The venue as the weather lookup sends it (weather.h): "Evanston, IL", "San
// Diego, California", "London, England", from ESPN's address (the city, then
// the state, else the country). "" with no city.
constexpr size_t kVenuePlaceMax = 48;
inline std::string venue_place(const std::string &city, const std::string &state, const std::string &country) {
  if (city.empty())
    return "";
  const std::string &region = state.empty() ? country : state;
  // MLS writes "Miami, Florida" as the city, with its state in it
  std::string out = region.empty() || city.find(',') != std::string::npos ? city : city + ", " + region;
  return message_clean(utf8_only(out), kVenuePlaceMax, false);
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
  // A string of digits, "-89356481" in college football; 0 when it has none.
  s.play_id = (int64_t) strtoll(str_or_empty(sit["lastPlay"]["id"]).c_str(), nullptr, 10);
  s.down_distance = str_or_empty(sit["downDistanceText"]);
  s.short_down_distance = str_or_empty(sit["shortDownDistanceText"]);
  s.is_red_zone = sit["isRedZone"] | false;
  // Range-checked before the casts: ESPN's numbers are ints, but a document
  // with something else in them must not wrap.
  int yard = sit["yardLine"] | -1;
  s.yard_line = (uint8_t) (yard >= 0 && yard <= 100 ? yard : 255);
  int down = sit["down"] | 0;
  s.down = (uint8_t) (down >= 1 && down <= 4 ? down : 0);
  int dist = sit["distance"] | 0;
  s.distance = (uint8_t) (dist >= 0 && dist <= 100 ? dist : 0);
  std::string possession = str_or_empty(sit["possession"]);
  int home_to = sit["homeTimeouts"] | 0;
  int away_to = sit["awayTimeouts"] | 0;

  bool found_us = false;
  for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
    uint32_t id = (uint32_t) atol(str_or_empty(c["id"]).c_str());
    bool home = str_or_empty(c["homeAway"]) == "home";
    std::string abbr = str_or_empty(c["team"]["abbreviation"]);
    int score = score_value(c["score"]);
    std::string record = str_or_empty(c["records"][0]["summary"]);
    // ESPN's AP rank: 1 to 25, 99 (or nothing) when unranked
    int rank_v = c["curatedRank"]["current"] | 99;
    uint8_t rank = (uint8_t) (rank_v >= 1 && rank_v <= 25 ? rank_v : 99);
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
    (ours ? s.team_rank : s.opp_rank) = rank;
    (ours ? s.team_color : s.opp_color) = color;
    (ours ? s.team_alt : s.opp_alt) = badge_rgb(str_or_empty(c["team"]["alternateColor"]));
    (ours ? s.team_logo : s.opp_logo) = logo;
    (ours ? s.team_timeouts : s.opp_timeouts) = timeouts;
    uint8_t *lines = ours ? s.team_lines : s.opp_lines;
    uint8_t &count = ours ? s.team_line_count : s.opp_line_count;
    count = 0;
    for (JsonVariantConst ls : c["linescores"].as<JsonArrayConst>()) {
      // Capped before the cast: a value past int's range is undefined.
      float v = ls["value"] | 0.0f;
      int pts = v > 0 ? (int) (std::min(v, 255.0f) + 0.5f) : 0;
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
  float win_f = win.is<float>() ? win.as<float>() : -1.0f;
  if (win_f >= 0.0f && win_f <= 1.0f) {  // NaN fails too; past int's range the cast is undefined
    // ESPN goes to 0.999 and 0.001 well before the end; 100% would claim
    // a result the game hasn't reached.
    int pct = (int) (win_f * 100.0f + 0.5f);
    s.team_win_pct = (int8_t) std::min(99, std::max(1, pct));
  }

  JsonObjectConst odds = comp["odds"][0];
  s.odds = str_or_empty(odds["details"]);
  s.over_under = format_over_under(odds["overUnder"]);
  s.tv = str_or_empty(comp["broadcasts"][0]["names"][0]);
  s.venue = str_or_empty(comp["venue"]["fullName"]);
  s.game_name = short_game_name(str_or_empty(comp["notes"][0]["headline"]));
  s.headline = str_or_empty(comp["headlines"][0]["shortLinkText"]);
  if (s.state == GameState::PRE) {
    // The week only in the regular season (type 2): a playoff game has a name.
    int week = ev["week"]["number"] | 0;
    if ((ev["season"]["type"] | 0) == 2 && week >= 1 && week <= 30)
      s.week = (uint8_t) week;
    // A dome's forecast is the city's, not the game's.
    JsonVariantConst indoor = comp["venue"]["indoor"];
    s.venue_indoor = indoor.is<bool>() ? (indoor.as<bool>() ? 1 : 0) : -1;
    if (s.venue_indoor != 1) {
      s.weather = weather_text(ev["weather"]);
      if (!s.weather.empty()) {
        // weather_text read a temperature that fits
        float f = ev["weather"]["temperature"].as<float>();
        s.weather_f = (int16_t) (f < 0 ? f - 0.5f : f + 0.5f);
        // The icon number rides in whichever field has no letters
        for (const char *k : {"conditionId", "displayValue"}) {
          const char *v = ev["weather"][k].as<const char *>();
          int n = v != nullptr && *v && !has_letter(v) ? atoi(v) : 0;
          if (n > 0 && n < 100) {
            s.weather_accu = (uint8_t) n;
            break;
          }
        }
      }
      JsonObjectConst a = comp["venue"]["address"];
      s.venue_place = venue_place(str_or_empty(a["city"]), str_or_empty(a["state"]), str_or_empty(a["country"]));
    }
  }
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

// Each side's leaders (football, baseball, basketball, hockey), for the
// pre-game ticker. Before the start ESPN lists season leaders; once the game
// is on they are the game's.
inline void fill_leaders_filter(JsonObject ev) {
  JsonObject c = ev["competitions"][0]["competitors"][0];
  JsonObject l = c["leaders"].add<JsonObject>();
  l["name"] = true;
  JsonObject top = l["leaders"].add<JsonObject>();
  top["displayValue"] = true;
  top["athlete"]["shortName"] = true;
}

// One side's top entry in ESPN's leader list `name`, null when it has none.
inline JsonObjectConst top_leader(JsonObjectConst competitor, const char *name) {
  for (JsonObjectConst l : competitor["leaders"].as<JsonArrayConst>())
    if (str_or_empty(l["name"]) == name)
      return l["leaders"][0];
  return JsonObjectConst();
}

// Fills s.team_leader and s.opp_leader before the start, from an event
// already read into s by snapshot_from_event (s.sport set). Football: the
// passing leader, or the rushing or receiving leader when ESPN has no passing
// line yet. Baseball: the home run leader, with the RBIs when the same player
// leads both. Basketball: points per game. Hockey: points.
inline void leaders_from_event(JsonObjectConst ev, GameSnapshot &s) {
  if (s.state != GameState::PRE)
    return;
  static const char *const kFootball[] = {"passingLeader", "rushingLeader", "receivingLeader"};
  auto name = [](JsonObjectConst top) { return str_or_empty(top["athlete"]["shortName"]); };
  auto value = [](JsonObjectConst top) { return str_or_empty(top["displayValue"]); };
  for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
    bool ours = (uint32_t) atol(str_or_empty(c["id"]).c_str()) == s.team_id;
    std::string &line = ours ? s.team_leader : s.opp_leader;
    if (s.sport == Sport::FOOTBALL) {
      for (const char *want : kFootball) {
        JsonObjectConst top = top_leader(c, want);
        line = leader_line(name(top), value(top));
        if (!line.empty())
          break;
      }
    } else if (s.sport == Sport::BASEBALL) {
      JsonObjectConst hr = top_leader(c, "homeRuns"), rbi = top_leader(c, "RBIs");
      line = stat_leader(name(hr), value(hr), "HR");
      if (!line.empty() && name(hr) == name(rbi) && !stat_leader(name(rbi), value(rbi), "RBI").empty())
        line += " " + value(rbi) + " RBI";
    } else if (s.sport == Sport::BASKETBALL) {
      JsonObjectConst top = top_leader(c, "pointsPerGame");
      line = stat_leader(name(top), value(top), "PPG");
    } else if (s.sport == Sport::HOCKEY) {
      JsonObjectConst top = top_leader(c, "points");
      line = stat_leader(name(top), value(top), "pts");
    }
  }
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

// Parses an AP poll from ESPN's core API (rankings.h): the ranks to
// kTop25Max, every team of a tie at the last one, at most kTop25RowsMax
// rows. The rest of the document (the season's types and weeks, the teams
// that got votes) is skipped as it streams.
template<typename TInput> bool parse_rankings(TInput &input, Poll &out) {
  JsonDocument filter(detail::json_allocator());
  filter["date"] = true;
  filter["occurrence"]["displayValue"] = true;
  filter["occurrence"]["last"] = true;
  JsonObject rank = filter["ranks"][0].to<JsonObject>();
  rank["current"] = true;
  rank["previous"] = true;
  rank["record"]["summary"] = true;
  rank["team"]["$ref"] = true;
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(20));
  if (err)
    return false;
  JsonArrayConst ranks = doc["ranks"];
  if (ranks.isNull() || ranks.size() == 0)
    return false;
  Poll p;
  p.date = parse_iso8601_z(detail::str_or_empty(doc["date"]));
  top25_when(doc["occurrence"]["displayValue"] | "", doc["occurrence"]["last"] | false, p.when);
  for (JsonObjectConst r : ranks) {
    if (p.rows.size() >= kTop25RowsMax)
      break;
    RankRow row;
    int cur = r["current"] | 0;
    int prev = r["previous"] | 0;
    row.id = top25_ref_id(r["team"]["$ref"] | "");
    if (cur < 1 || cur > kTop25Max || row.id == 0)
      continue;
    row.rank = (uint8_t) cur;
    row.prev = prev > 0 && prev < 100 ? (uint8_t) prev : 0;
    const char *rec = r["record"]["summary"] | "";
    // "10-2", "8-3-1": digits and dashes only, so a long or odd value can't
    // spill out of its column.
    size_t n = 0;
    for (; rec[n] != '\0' && n < sizeof(row.rec) - 1; n++)
      if (!((rec[n] >= '0' && rec[n] <= '9') || rec[n] == '-'))
        break;
    if (rec[n] == '\0')
      memcpy(row.rec, rec, n);
    p.rows.push_back(row);
  }
  if (p.rows.empty())
    return false;
  out = std::move(p);
  return true;
}

namespace detail {

// One byte of parse_golf's input, -1 at the end: a string in the host tests,
// or a stream with read() such as the panel's ContainerReader.
template<typename T> inline int next_byte(T &in, size_t & /*at*/) { return in.read(); }
inline int next_byte(std::string &s, size_t &at) { return at < s.size() ? (unsigned char) s[at++] : -1; }
inline int next_byte(const std::string &s, size_t &at) { return at < s.size() ? (unsigned char) s[at++] : -1; }

// Walks a JSON document as it streams in, so a parse can step through its
// containers and deserialize one array element at a time. It is also the
// reader deserializeJson takes for each element. ArduinoJson stops right
// after an object's closing brace or a string's closing quote, so the walk
// carries on from there. It accepts and refuses what ArduinoJson does when
// it reads the whole document through a filter.
template<typename TInput> class JsonWalk {
 public:
  explicit JsonWalk(TInput &in) : in_(in), key_(json_allocator()), sink_(json_allocator()) {}

  // ArduinoJson's reader interface.
  int read() {
    const int c = this->look_();
    this->held_ = false;
    return c;
  }
  size_t readBytes(char *dst, size_t n) {
    size_t k = 0;
    for (int c; k < n && (c = this->read()) >= 0; k++)
      dst[k] = (char) c;
    return k;
  }

  bool failed() const { return this->failed_; }

  // The next character after white space, left unread. -1 at the end.
  int peek() {
    int c;
    while ((c = this->look_()) == ' ' || c == '\t' || c == '\r' || c == '\n')
      this->held_ = false;
    return c;
  }

  // Steps into the object or array that comes next. limit is the nesting
  // left, counted as ArduinoJson's NestingLimit counts it, and becomes the
  // limit of the members.
  bool enter(char open, uint8_t &limit) {
    if (this->failed_ || limit == 0 || this->peek() != open)
      return this->fail_();
    this->held_ = false;
    limit--;
    return true;
  }

  // Reads the next member's key into key[n]. A key that doesn't fit, or
  // holds a NUL, reads as "" so it matches none. False after the closing
  // brace and on an error (failed()).
  bool next_key(bool &first, char *key, size_t n) {
    if (!this->step_(first, '}'))
      return false;
    const int c = this->peek();
    key[0] = '\0';
    if (c == '"' || c == '\'') {
      // ArduinoJson reads a quoted key, so its escapes decode as they do in
      // a whole-document read.
      if (deserializeJson(this->key_, *this))
        return this->fail_();
      const JsonString k = this->key_.template as<JsonString>();
      if (k.size() < n && strlen(k.c_str()) == k.size())
        memcpy(key, k.c_str(), k.size() + 1);
    } else {
      // ArduinoJson also takes a key with no quotes, made of letters,
      // digits and the characters between '_' and 'z'.
      size_t len = 0;
      for (int k; (k = this->look_()) > 0 && is_key_char_(k); len++) {
        if (len + 1 < n)
          key[len] = (char) k;
        this->held_ = false;
      }
      if (len == 0)
        return this->fail_();
      key[len < n ? len : 0] = '\0';
    }
    if (this->peek() != ':')
      return this->fail_();
    this->held_ = false;
    return true;
  }

  // Moves to the array's next element. False after the closing bracket and
  // on an error.
  bool next_item(bool &first) { return this->step_(first, ']'); }

  // Skips the value that comes next, with limit as for enter().
  // A null filter keeps nothing.
  bool skip(uint8_t limit) { return this->value(this->sink_, JsonVariantConst(), limit); }

  // Deserializes the value that comes next into doc through filter. The
  // walk reads a number, true, false or null itself and leaves doc empty,
  // because ArduinoJson reads one character past a number and the walk
  // would lose it.
  bool value(JsonDocument &doc, JsonVariantConst filter, uint8_t limit) {
    int c = this->peek();
    if (this->failed_)
      return false;
    if (c == '{' || c == '[' || c == '"' || c == '\'') {
      if (deserializeJson(doc, *this, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(limit)))
        return this->fail_();
      return true;
    }
    doc.clear();
    if (c == 't' || c == 'f' || c == 'n') {
      for (const char *word = c == 't' ? "true" : c == 'f' ? "false" : "null"; *word != '\0'; word++) {
        if (this->look_() != *word)
          return this->fail_();
        this->held_ = false;
      }
      return true;
    }
    // Take the characters a number can have, as ArduinoJson does. A value
    // the filter skips may have none.
    char number[64];
    size_t len = 0;
    for (; (c = this->look_()) > 0 && is_number_char_(c); len++) {
      if (len < sizeof(number))
        number[len] = (char) c;
      this->held_ = false;
    }
    if (!(filter == true))
      return true;
    // A value the filter keeps must be a number ArduinoJson reads, in at
    // most 63 characters.
    if (len == 0 || len >= sizeof(number) || deserializeJson(doc, number, len))
      return this->fail_();
    return true;
  }

 private:
  int look_() {
    if (!this->held_) {
      const int c = next_byte(this->in_, this->at_);
      // ArduinoJson ends a document at a NUL too.
      this->byte_ = c > 0 ? c : -1;
      this->held_ = true;
    }
    return this->byte_;
  }
  bool fail_() {
    this->failed_ = true;
    return false;
  }
  static bool is_number_char_(int c) {
    return (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E';
  }
  static bool is_key_char_(int c) { return (c >= '0' && c <= '9') || (c >= '_' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
  // Moves past the comma before the next member or element, or reads the
  // closing character and returns false.
  bool step_(bool &first, char close) {
    if (this->failed_)
      return false;
    const int c = this->peek();
    if (c == close) {
      this->held_ = false;
      return false;
    }
    if (!first) {
      if (c != ',')
        return this->fail_();
      this->held_ = false;
    }
    first = false;
    return true;
  }

  TInput &in_;
  size_t at_{0};
  int byte_{-1};
  bool held_{false};
  bool failed_{false};
  JsonDocument key_;   // the key next_key() read
  JsonDocument sink_;  // what a skipped value is read into: always empty
};

// The fields of one golfer that parse_golf reads.
inline void fill_golf_competitor_filter(JsonObject c) {
  c["id"] = true;
  c["order"] = true;
  c["score"] = true;
  c["athlete"]["shortName"] = true;
  c["athlete"]["displayName"] = true;
  // A team event (the Zurich Classic's pairs) lists teams with the pair's
  // score: "Smalley/Springer".
  c["team"]["displayName"] = true;
  JsonObject ls = c["linescores"].add<JsonObject>();
  ls["period"] = true;
  ls["displayValue"] = true;
  // Only the number of holes counts, so each hole is kept empty.
  ls["linescores"].add<JsonObject>();
}

// An event's or its competition's status.
inline void fill_golf_status_filter(JsonObject s) {
  s["period"] = true;
  s["type"]["state"] = true;
  s["type"]["completed"] = true;
  s["type"]["detail"] = true;
  s["type"]["shortDetail"] = true;
}

// The whole document's filter: everything parse_golf reads. The fuzz seeds
// cut the fixtures down with it.
inline void fill_golf_filter(JsonDocument &f) {
  JsonObject ev = f["events"].add<JsonObject>();
  ev["name"] = true;
  ev["date"] = true;
  ev["endDate"] = true;
  fill_golf_status_filter(ev["status"].to<JsonObject>());
  JsonObject comp = ev["competitions"].add<JsonObject>();
  fill_golf_status_filter(comp["status"].to<JsonObject>());
  fill_golf_competitor_filter(comp["competitors"].add<JsonObject>());
}

// One golfer's line for a round, kept until the competition's status says
// which round is current. The status comes after the competitors.
struct GolfLine {
  int period{0};
  uint8_t thru{0};
  char today[6]{};
};
constexpr uint8_t kGolfLines = 8;  // rounds kept per golfer
constexpr uint16_t kGolfRoundMin = 10;  // a playoff has fewer golfers, a cut or a round more

// The rounds a golfer has a line for, and has a score in: bit 1 is round 1.
struct GolfRounds {
  uint16_t has{0}, scored{0};
};

struct GolfPending {
  GolfRow row;
  GolfRounds played;
  uint8_t lines{0};
  GolfLine line[kGolfLines];

  const GolfLine *round(int period) const {
    for (uint8_t i = 0; i < this->lines; i++)
      if (this->line[i].period == period)
        return &this->line[i];
    return nullptr;
  }
};

// One event of a tour's document, read as it streams in.
struct GolfEvent {
  GolfBoard b;
  bool listed{false};    // its first competition lists competitors
  bool round_in{false};  // the competition's own state is "in"
  uint8_t top{0};        // rows kept from the top of the leaderboard
  uint8_t favs{0};       // followed golfers kept past the top
  // Golfers on one score, one after another, share a place: the first one's.
  uint16_t run_first{0}, run_n{0};
  uint16_t lead_n{0};  // golfers on the leader's score
  int run_total{0};
  bool run_weekend{false};  // they have a line for round 3 (a cut golfer doesn't)
  uint16_t played[kGolfLines + 1]{};  // golfers with a line for each round
  uint16_t unplayed3{0};  // golfers with rounds 1 and 2 played and an empty line for round 3
  PsVector<GolfPending> rows;

  void clear_field() {
    this->listed = false;
    this->b.field = 0;
    this->top = this->favs = 0;
    this->run_first = this->run_n = this->lead_n = this->unplayed3 = 0;
    memset(this->played, 0, sizeof(this->played));
    this->rows.clear();
  }
};

// parse_golf's state between the walk's steps: the filters, the document
// each value is read into, the event being read and the one picked so far.
class GolfRead {
 public:
  GolfRead(const uint32_t *fav_ids, size_t fav_count)
      : doc(json_allocator()), all_(json_allocator()), filters_(json_allocator()), fav_ids_(fav_ids), fav_count_(fav_count) {
    this->all_.set(true);
    fill_golf_status_filter(this->filters_["status"].to<JsonObject>());
    fill_golf_competitor_filter(this->filters_["golfer"].to<JsonObject>());
    // Give back the rest of the document's first block, most of its 4 KB.
    this->filters_.shrinkToFit();
  }

  JsonDocument doc;

  // A second "events" key replaces the first, as it does in ArduinoJson.
  void restart(bool list) {
    this->have_pick_ = false;
    this->seen_ = list;
    this->pick_ = GolfEvent();
  }

  void begin_event() {
    this->cur_ = GolfEvent();
    this->cur_.rows.reserve(kGolfRowsMax);
  }

  // The filter for one of the event's own keys. Null for a key the board
  // doesn't use.
  JsonVariantConst event_filter(const char *key) const {
    if (strcmp(key, "name") == 0 || strcmp(key, "date") == 0 || strcmp(key, "endDate") == 0)
      return this->all_.as<JsonVariantConst>();
    if (strcmp(key, "status") == 0)
      return this->status_filter();
    return JsonVariantConst();
  }
  JsonVariantConst status_filter() const { return this->filters_["status"]; }
  JsonVariantConst golfer_filter() const { return this->filters_["golfer"]; }

  void event_value(const char *key, JsonVariantConst v) {
    GolfBoard &b = this->cur_.b;
    if (strcmp(key, "name") == 0) {
      snprintf(b.event, sizeof(b.event), "%s", str_or_empty(v).c_str());
    } else if (strcmp(key, "date") == 0) {
      b.start_epoch = parse_iso8601_z(str_or_empty(v));
    } else if (strcmp(key, "endDate") == 0) {
      b.end_epoch = parse_iso8601_z(str_or_empty(v));
    } else {
      const std::string state = str_or_empty(v["type"]["state"]);
      b.state = state == "in" ? GolfState::IN : state == "post" ? GolfState::POST : GolfState::PRE;
    }
  }

  // A new "competitions" key: whatever an earlier one listed is gone.
  void clear_competition() {
    this->cur_.clear_field();
    this->competition_status(JsonVariantConst());
  }
  void clear_field() { this->cur_.clear_field(); }

  // The status of the event's first competition: the round, and whether
  // it is being played right now. The event stays "in" from the first tee
  // time to the last putt. The competition's state is "post" overnight and
  // while play is stopped ("Round 3 - Suspended").
  void competition_status(JsonVariantConst s) {
    GolfBoard &b = this->cur_.b;
    this->cur_.round_in = str_or_empty(s["type"]["state"]) == "in";
    std::string detail = str_or_empty(s["type"]["shortDetail"]);
    if (detail.empty())
      detail = str_or_empty(s["type"]["detail"]);
    snprintf(b.detail, sizeof(b.detail), "%s", detail.c_str());
    b.round = (uint8_t) (s["period"] | 0);
    b.round_done = s["type"]["completed"] | false;
    b.playoff = strncmp(b.detail, "Playoff", 7) == 0;
  }

  // One element of the first competition's competitors, null when it is
  // not an object. Every element counts in the field. The top kGolfTopRows
  // become rows, and followed golfers past them up to kGolfFavRows more.
  void golfer(JsonObjectConst gc) {
    GolfEvent &ev = this->cur_;
    ev.listed = true;
    ev.b.field++;
    GolfPending p;
    GolfRow &r = p.row;
    r.pos = (uint16_t) (gc["order"] | 0);
    r.id = (uint32_t) atol(str_or_empty(gc["id"]).c_str());
    int total = 0;
    const std::string score = str_or_empty(gc["score"]);
    const bool scored = golf_score(score.c_str(), total);
    this->count_(gc, p.played);
    if (scored) {
      snprintf(r.score, sizeof(r.score), "%s", score.c_str());
      this->place_(total, (p.played.has & (1u << 3)) != 0);
      r.place = ev.run_first;
      r.tied = ev.run_n > 1;
    } else {
      ev.run_n = 0;
    }
    const bool is_fav = this->fav_(r.id);
    // The top is full at kGolfTopRows rows even when a document repeats an
    // order or has none, so a read keeps kGolfRowsMax rows at most.
    const bool top = r.pos <= kGolfTopRows && ev.top < kGolfTopRows;
    if (!top && (!is_fav || ev.favs >= kGolfFavRows))
      return;
    r.favorite = is_fav;
    // The surname, for a narrow panel, is the short name without its initial
    // ("H. Varner III"), or the full name's last word.
    const std::string brief = ascii_fold(str_or_empty(gc["athlete"]["shortName"]));
    const std::string full = ascii_fold(str_or_empty(gc["athlete"]["displayName"]));
    const size_t sp = brief.find(' '), cut = full.rfind(' ');
    snprintf(r.name, sizeof(r.name), "%s", brief.c_str());
    snprintf(r.last, sizeof(r.last), "%s",
             sp != std::string::npos ? brief.c_str() + sp + 1 : full.c_str() + (cut == std::string::npos ? 0 : cut + 1));
    // A pair in a team event goes by the pair's name, "Smalley/Springer",
    // and on a single panel by the first name and the second's initial,
    // "Smalley/S". A side of a team match (the Presidents Cup) is no pair.
    if (r.name[0] == '\0' && r.last[0] == '\0') {
      const std::string pair = ascii_fold(str_or_empty(gc["team"]["displayName"]));
      const size_t slash = pair.find('/');
      if (slash != std::string::npos) {
        snprintf(r.name, sizeof(r.name), "%s", pair.c_str());
        snprintf(r.last, sizeof(r.last), "%s", pair.substr(0, slash + 2).c_str());
      }
    }
    // Neither a golfer nor a pair (a team match, a team game's document read
    // by mistake): it counts in the field but is never a row.
    if (r.name[0] == '\0' && r.last[0] == '\0')
      return;
    // Each round's score to par and how many holes of it the document
    // shows. The PGA Tour's document lists the holes, the LPGA's and the DP
    // World Tour's don't, so their rows have no thru. A later line for the
    // same round wins, as does a later score that reads as one.
    for (JsonObjectConst gl : gc["linescores"].as<JsonArrayConst>()) {
      const int period = gl["period"] | 0;
      GolfLine *line = const_cast<GolfLine *>(p.round(period));
      if (line == nullptr) {
        if (p.lines >= kGolfLines)
          continue;
        line = &p.line[p.lines++];
        line->period = period;
      }
      const std::string dv = str_or_empty(gl["displayValue"]);
      int round_score = 0;
      if (golf_score(dv.c_str(), round_score))
        snprintf(line->today, sizeof(line->today), "%s", dv.c_str());
      const size_t holes = gl["linescores"].as<JsonArrayConst>().size();
      line->thru = (uint8_t) (holes > 18 ? 18 : holes);
    }
    if (top)
      ev.top++;
    else
      ev.favs++;
    ev.rows.push_back(p);
  }

  // The event to show: one in progress wins, then the first over, then the
  // first with a field. An event with no field yet is kept only until one
  // has: it says when the tour plays next.
  void end_event() {
    const uint8_t rank = this->rank_(this->cur_);
    if (this->have_pick_ && rank <= this->rank_(this->pick_))
      return;
    this->pick_ = std::move(this->cur_);
    this->have_pick_ = true;
  }

  // The picked event's board, with each row's line for the current round.
  // A tour between events reads well with no rows. False when the document
  // has no list of events at all.
  bool finish(League league, GolfBoard &out) {
    if (!this->seen_)
      return false;
    GolfEvent &ev = this->pick_;
    GolfBoard &b = ev.b;
    b.league = league;
    b.valid = true;
    b.round_live = b.state == GolfState::IN && ev.round_in;
    b.rows.clear();
    b.rows.reserve(ev.rows.size());
    for (const GolfPending &p : ev.rows) {
      GolfRow r = p.row;
      const GolfLine *line = p.round(b.round);
      if (line != nullptr) {
        memcpy(r.today, line->today, sizeof(r.today));
        r.thru = line->thru;
      }
      r.gone = this->gone_(ev, p);
      // A playoff leaves its golfers on one score: the winner is listed
      // first, and the rest share second.
      if (b.state == GolfState::POST && r.place == 1 && ev.lead_n > 1) {
        const bool won = b.rows.empty();
        r.place = won ? 1 : 2;
        r.tied = !won && ev.lead_n > 2;
      }
      b.rows.push_back(r);
    }
    out = std::move(b);
    return true;
  }

 private:
  bool fav_(uint32_t id) const {
    for (size_t i = 0; i < this->fav_count_; i++)
      if (this->fav_ids_[i] == id)
        return true;
    return false;
  }

  static uint8_t rank_(const GolfEvent &ev) {
    return !ev.listed ? 0 : ev.b.state == GolfState::IN ? 3 : ev.b.state == GolfState::POST ? 2 : 1;
  }

  // The rounds this golfer has lines for, counted over the field.
  void count_(JsonObjectConst gc, GolfRounds &g) {
    GolfEvent &ev = this->cur_;
    for (JsonObjectConst gl : gc["linescores"].as<JsonArrayConst>()) {
      const int k = gl["period"] | 0;
      if (k < 1 || k > kGolfLines)
        continue;
      int n = 0;
      if (golf_score(gl["displayValue"] | "", n))
        g.scored |= (uint16_t) (1u << k);
      if (!(g.has & (1u << k)))
        ev.played[k]++;
      g.has |= (uint16_t) (1u << k);
    }
    if ((g.scored & 0b1110) == 0b0110 && (g.has & (1u << 3)))
      ev.unplayed3++;
  }

  // The golfer just counted has this total: the run they join or start.
  void place_(int total, bool weekend) {
    GolfEvent &ev = this->cur_;
    if (ev.run_n > 0 && total == ev.run_total && weekend == ev.run_weekend) {
      ev.run_n++;
      for (GolfPending &p : ev.rows)
        p.row.tied = p.row.tied || p.row.place == ev.run_first;
    } else {
      ev.run_first = ev.b.field;
      ev.run_n = 1;
      ev.run_total = total;
      ev.run_weekend = weekend;
    }
    if (ev.run_first == 1)
      ev.lead_n = ev.run_n;
  }

  // Whether a golfer is still in the tournament, against the leader's
  // rounds: ESPN marks neither a missed cut nor a withdrawal. A golfer who
  // missed the cut has no line for round 3 (the PGA Tour, the LPGA) or, like
  // dozens of others, an empty one (the DP World Tour). One who withdrew has
  // a round with no score that the leader played. A round few golfers have
  // a line for is a playoff, whatever its number.
  static GolfGone gone_(const GolfEvent &ev, const GolfPending &p) {
    const GolfBoard &b = ev.b;
    const GolfRounds &lead = ev.rows[0].played, &mine = p.played;
    for (int k = 1; k <= kGolfLines && k <= (int) b.round; k++) {
      const uint16_t bit = (uint16_t) (1u << k);
      if (ev.played[k] < kGolfRoundMin || !(lead.has & bit) || (mine.scored & bit))
        continue;
      if (!(mine.has & bit) && k >= 3)
        return GolfGone::CUT;
      if ((lead.scored & bit) && (k < (int) b.round || b.state == GolfState::POST))
        return k == 3 && ev.unplayed3 >= kGolfRoundMin ? GolfGone::CUT : GolfGone::LEFT;
    }
    return GolfGone::NO;
  }

  JsonDocument all_;      // the filter that keeps everything
  JsonDocument filters_;  // the status filter and the golfer filter
  const uint32_t *fav_ids_;
  size_t fav_count_;
  GolfEvent cur_, pick_;
  bool have_pick_{false};
  bool seen_{false};  // the document has a list of events
};

// The first competition of an event: its status, and its competitors one
// at a time. A later competition is read the same way and dropped, so a bad
// value in it fails the read as it does for ArduinoJson. limit is the nesting
// left at the competitions array.
template<typename TInput> void golf_competitions(JsonWalk<TInput> &w, GolfRead &g, uint8_t limit) {
  g.clear_competition();
  if (w.peek() != '[') {
    w.skip(limit);
    return;
  }
  w.enter('[', limit);
  char key[16];
  for (bool first = true, zero = true; w.next_item(first); zero = false) {
    uint8_t comp = limit;
    if (w.peek() != '{') {
      w.skip(limit);
      continue;
    }
    w.enter('{', comp);
    for (bool kf = true; w.next_key(kf, key, sizeof(key));) {
      if (strcmp(key, "status") == 0) {
        if (w.value(g.doc, g.status_filter(), comp) && zero)
          g.competition_status(g.doc.as<JsonVariantConst>());
      } else if (strcmp(key, "competitors") != 0) {
        w.skip(comp);
      } else {
        if (zero)
          g.clear_field();
        uint8_t list = comp;
        if (w.peek() != '[') {
          w.skip(comp);
          continue;
        }
        w.enter('[', list);
        for (bool cf = true; w.next_item(cf);)
          if (w.value(g.doc, g.golfer_filter(), list) && zero)
            g.golfer(g.doc.as<JsonObjectConst>());
      }
    }
  }
}

}  // namespace detail

// Parses a tour's scoreboard document (golf.h): the event being played, else
// the one over, else the next one, its state and round, and the leaderboard
// rows the panel keeps.
// Those are the top kGolfTopRows, plus the followed golfers (fav_ids, this
// tour's athlete ids) wherever they stand, up to kGolfFavRows more.
// Competitors arrive in leaderboard order. The document is 90-600 KB, most
// of it statistics and hole-by-hole linescores for the whole field, so it
// is walked as it streams in and each golfer is deserialized on their own.
// A read holds one golfer's document at a time, a few KB.
template<typename TInput>
bool parse_golf(TInput &input, League league, const uint32_t *fav_ids, size_t fav_count, GolfBoard &out) {
  detail::JsonWalk<TInput> w(input);
  detail::GolfRead g(fav_ids, fav_count);
  char key[16];
  // ArduinoJson's NestingLimit as the whole-document read had it.
  uint8_t root = 20;
  if (!w.enter('{', root))
    return false;
  for (bool rf = true; w.next_key(rf, key, sizeof(key));) {
    if (strcmp(key, "events") != 0) {
      w.skip(root);
      continue;
    }
    g.restart(w.peek() == '[');
    if (w.peek() != '[') {
      w.skip(root);
      continue;
    }
    uint8_t events = root;
    w.enter('[', events);
    for (bool ef = true; w.next_item(ef);) {
      uint8_t ev = events;
      if (w.peek() != '{') {
        w.skip(events);
        continue;
      }
      w.enter('{', ev);
      g.begin_event();
      for (bool kf = true; w.next_key(kf, key, sizeof(key));) {
        if (strcmp(key, "competitions") == 0) {
          detail::golf_competitions(w, g, ev);
          continue;
        }
        JsonVariantConst filter = g.event_filter(key);
        if (filter.isNull())
          w.skip(ev);
        else if (w.value(g.doc, filter, ev))
          g.event_value(key, g.doc.as<JsonVariantConst>());
      }
      g.end_event();
    }
  }
  if (w.failed())
    return false;
  return g.finish(league, out);
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

// The season read (season.h): parse_upcoming's fields, and what a final
// needs for the recap.
namespace detail {
inline void fill_season_filter(JsonDocument &f) {
  f["requestedSeason"]["year"] = true;
  f["season"]["year"] = true;
  JsonObject ev = f["events"].add<JsonObject>();
  ev["date"] = true;
  ev["seasonType"]["type"] = true;
  ev["seasonType"]["name"] = true;
  JsonObject comp = ev["competitions"].add<JsonObject>();
  comp["timeValid"] = true;
  comp["status"]["type"]["state"] = true;
  comp["status"]["type"]["completed"] = true;
  comp["status"]["period"] = true;
  comp["notes"].add<JsonObject>()["headline"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["homeAway"] = true;
  c["winner"] = true;
  c["team"]["id"] = true;
  c["team"]["abbreviation"] = true;
  comp["broadcasts"].add<JsonObject>()["media"]["shortName"] = true;
}
}  // namespace detail

// A team's season schedule into out: the games not started (soonest first,
// up to kSeasonGames), the regular season's finals counted (and the losses
// past a third period, for hockey's record), and the last
// playoff game's round and result. A playoff game is season type 3, or a
// soccer type named for the playoffs; preseason (type 1) doesn't count.
// False when the document doesn't parse.
template<typename TInput> bool parse_season(TInput &input, uint32_t our_team_id, SeasonRead &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_season_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err || !doc["events"].is<JsonArrayConst>())
    return false;
  out = SeasonRead{};
  out.year = (uint16_t) (doc["requestedSeason"]["year"] | (doc["season"]["year"] | 0));
  int64_t last_post = 0;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    JsonObjectConst comp = ev["competitions"][0];
    const std::string state = detail::str_or_empty(comp["status"]["type"]["state"]);
    const int64_t at = parse_iso8601_z(detail::str_or_empty(ev["date"]));
    JsonObjectConst us, them;
    for (JsonObjectConst c : comp["competitors"].as<JsonArrayConst>()) {
      if ((uint32_t) atol(detail::str_or_empty(c["team"]["id"]).c_str()) == our_team_id)
        us = c;
      else
        them = c;
    }
    if (us.isNull() || them.isNull())
      continue;
    if (state == "pre") {
      SeasonGame g;
      g.kickoff_epoch = at;
      g.time_tbd = detail::time_tbd(comp);
      g.home = detail::str_or_empty(us["homeAway"]) == "home";
      g.opp = detail::str_or_empty(them["team"]["abbreviation"]);
      g.tv = detail::str_or_empty(comp["broadcasts"][0]["media"]["shortName"]);
      // Short strings stay inline in the PSRAM entry; a network this long
      // never fits on the card's line anyway.
      if (g.tv.size() > 15)
        g.tv.clear();
      if (g.opp.size() > 15)
        g.opp.resize(15);
      if (at > 0 && !g.opp.empty())
        out.next.push_back(std::move(g));
      continue;
    }
    if (state != "post" || !(comp["status"]["type"]["completed"] | false))
      continue;  // postponed or canceled
    const int type = ev["seasonType"]["type"] | 0;
    const std::string type_name = detail::str_or_empty(ev["seasonType"]["name"]);
    const bool won = us["winner"] | false, lost = them["winner"] | false;
    if (type == 3 || type_name.find("Playoff") != std::string::npos) {
      // Soccer lists its results newest first: the latest by date.
      if (at >= last_post) {
        last_post = at;
        out.post = true;
        out.post_won = won;
        out.set_round(season_round(detail::str_or_empty(comp["notes"][0]["headline"]), type_name));
      }
    } else if (type != 1) {
      uint8_t &n = won ? out.wins : lost ? out.losses : out.ties;
      if (n < 255) {
        n++;
        if (lost && (comp["status"]["period"] | 0) > 3)
          out.late_losses++;
      }
    }
  }
  // Soonest first, in place: an insertion sort is plenty for a season.
  for (size_t i = 1; i < out.next.size(); i++)
    for (size_t k = i; k > 0 && out.next[k].kickoff_epoch < out.next[k - 1].kickoff_epoch; k--)
      std::swap(out.next[k], out.next[k - 1]);
  if (out.next.size() > kSeasonGames)
    out.next.resize(kSeasonGames);
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
// set: the caller knows which league it asked for. With watch (team ids) and
// watch_kickoff, also reports the earliest kickoff not started yet of a game
// one of those teams plays, 0 when there is none (Favorites take over).
template<typename TInput, typename TOut>
bool parse_live_games(TInput &input, TOut &out, int64_t *next_kickoff = nullptr,
                      FinalList *finals = nullptr, PendingList *pending = nullptr,
                      const std::vector<uint32_t> *watch = nullptr, int64_t *watch_kickoff = nullptr) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_scan_filter(filter, finals != nullptr, pending != nullptr);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  if (next_kickoff != nullptr)
    *next_kickoff = 0;
  if (watch_kickoff != nullptr)
    *watch_kickoff = 0;
  // Kept apart from *pending until the end, so the cap counts this document only.
  PendingList later;
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
      if (k > 0 && watch != nullptr && watch_kickoff != nullptr && (*watch_kickoff == 0 || k < *watch_kickoff))
        for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
          uint32_t id = (uint32_t) atol(detail::str_or_empty(c["id"]).c_str());
          if (id != 0 && std::find(watch->begin(), watch->end(), id) != watch->end()) {
            *watch_kickoff = k;
            break;
          }
        }
      GameSnapshot g;
      if (k > 0 && pending != nullptr && away != 0 && detail::snapshot_from_event(ev, away, g)) {
        Pending p;
        p.rank = detail::best_rank(ev["competitions"][0]);
        p.kickoff_epoch = k;
        p.event_id = std::move(g.event_id);
        p.away_id = g.team_id;
        p.home_id = g.opp_id;
        p.away_rank = g.team_rank;
        p.home_rank = g.opp_rank;
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
        fin.away_rank = g.team_rank;
        fin.home_rank = g.opp_rank;
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
    int period = ev["status"]["period"] | 0;
    g.period = (uint8_t) (period < 0 ? 0 : period > 255 ? 255 : period);
    float clock = ev["status"]["clock"] | -1.0f;
    g.clock_s = clock >= 0 && clock <= 100000 ? (int32_t) clock : -1;  // NaN fails both
    for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
      bool home = detail::str_or_empty(c["homeAway"]) == "home";
      std::string abbr = detail::str_or_empty(c["team"]["abbreviation"]);
      int16_t pts = (int16_t) detail::score_value(c["score"]);
      int r = c["curatedRank"]["current"] | 99;
      uint8_t rank = (uint8_t) (r >= 1 && r <= 25 ? r : 99);
      if (home) {
        g.home_abbr = abbr;
        g.home_id = (uint32_t) atol(detail::str_or_empty(c["id"]).c_str());
        g.group = (uint32_t) atol(detail::str_or_empty(c["team"]["conferenceId"]).c_str());
        g.home_score = pts;
        g.home_rank = rank;
      } else {
        g.away_abbr = abbr;
        g.away_id = (uint32_t) atol(detail::str_or_empty(c["id"]).c_str());
        g.away_score = pts;
        g.away_rank = rank;
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
  if (sport != Sport::SOCCER)
    detail::fill_leaders_filter(filter.as<JsonObject>());
  if (sport == Sport::BASEBALL)
    detail::fill_baseball_filter(filter.as<JsonObject>());
  if (sport == Sport::SOCCER)
    detail::fill_soccer_filter(filter.as<JsonObject>());
  if (sport == Sport::BASKETBALL)
    detail::fill_basketball_filter(filter.as<JsonObject>());
  if (sport == Sport::HOCKEY)
    detail::fill_hockey_filter(filter.as<JsonObject>());
  if (sport == Sport::FOOTBALL || sport == Sport::BASEBALL || sport == Sport::SOCCER)
    detail::fill_venue_place_filter(filter.as<JsonObject>());
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
  detail::leaders_from_event(doc.as<JsonObjectConst>(), s);
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

// Parses a news feed (news_url) into its headlines, up to kNewsMax, each one
// line cleaned like a message entry and joined by '\n' into list (S is the
// string type it fills, PSRAM on the panel). Returns how many, or -1 on a
// JSON error or a document with no articles list.
template<typename TInput, typename S> int parse_news(TInput &input, S &list) {
  JsonDocument filter(detail::json_allocator());
  filter["articles"][0]["headline"] = true;
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err || !doc["articles"].is<JsonArrayConst>())
    return -1;
  list.clear();
  int n = 0;
  for (JsonObjectConst a : doc["articles"].as<JsonArrayConst>()) {
    if (n >= (int) kNewsMax)
      break;
    std::string h = message_clean(detail::str_or_empty(a["headline"]), kNewsLineMax, false);
    if (h.empty())
      continue;
    if (!list.empty())
      list += '\n';
    list.append(h.data(), h.size());
    n++;
  }
  return n;
}

}  // namespace espn
