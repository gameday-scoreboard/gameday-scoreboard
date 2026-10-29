// This game's leaders at halftime and the final: each side's top lines from
// ESPN's game summary, read once at the break and once at the final, never on
// the live poll, and the card that shows them. Pure, with no ESPHome
// includes, so the host tests compile it directly.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "espn_parse.h"
#include "line_score.h"

namespace espn {

// When the summary is read: at the break (football and basketball halftime,
// hockey's second intermission, baseball after the 5th) and at the final.
enum class LeadersStage : uint8_t { NONE, HALF, FINAL };

inline const char *leaders_stage_name(LeadersStage s) {
  switch (s) {
    case LeadersStage::HALF:
      return "halftime";
    case LeadersStage::FINAL:
      return "final";
    default:
      return "none";
  }
}

// The stage the board's game is at, NONE between them. Soccer never: the
// summary's leaders there are shots and passes, and the goal scorers are
// already on the ticker.
inline LeadersStage leaders_stage(const GameSnapshot &g) {
  if (!g.valid || g.sport == Sport::SOCCER)
    return LeadersStage::NONE;
  if (g.state == GameState::POST)
    return g.completed ? LeadersStage::FINAL : LeadersStage::NONE;
  if (g.state != GameState::IN)
    return LeadersStage::NONE;
  switch (g.sport) {
    case Sport::FOOTBALL:
    case Sport::BASKETBALL:
      return at_halftime(g) ? LeadersStage::HALF : LeadersStage::NONE;
    case Sport::HOCKEY:
      // "End of 2nd" (status END_PERIOD); a shootout never has period 2.
      return g.period == 2 && g.short_detail.rfind("End", 0) == 0 ? LeadersStage::HALF : LeadersStage::NONE;
    case Sport::BASEBALL:
      return g.period == 5 && (g.mlb.half == Half::MID || g.mlb.half == Half::END) ? LeadersStage::HALF
                                                                                    : LeadersStage::NONE;
    default:
      return LeadersStage::NONE;
  }
}

// The most lines a side gets: football's passing, rushing and receiving.
static constexpr uint8_t kLeaderLines = 3;

// One leader: the surname and the stat, kept apart so a long name can be cut
// to the card's width without touching the number.
struct LeaderLine {
  std::string name;  // "Prescott"
  std::string stat;  // "212 yds 3 TD"
};

struct LeaderSide {
  LeaderLine lines[kLeaderLines];
  uint8_t count{0};
  void add(const std::string &name, const std::string &stat) {
    if (this->count >= kLeaderLines || name.empty() || stat.empty())
      return;
    this->lines[this->count++] = {name, stat};
  }
};

// One read of the summary, oriented like the GameSnapshot it was read for.
struct GameLeaders {
  bool valid{false};
  std::string event_id;
  LeadersStage stage{LeadersStage::NONE};
  LeaderSide team, opp;
  bool any() const { return this->team.count > 0 || this->opp.count > 0; }
};

// How the firmware retries a summary read that failed: once a minute, up to
// three times, then it lets that stage go.
static constexpr uint8_t kLeadersTries = 3;
static constexpr uint32_t kLeadersRetryMs = 60 * 1000;

namespace detail {

// "D. Prescott" to "Prescott"; "M. Penix Jr." keeps the suffix.
inline std::string leader_surname(const std::string &short_name) {
  std::string name = short_name;
  while (name.size() > 3 && name[1] == '.' && name[2] == ' ')
    name.erase(0, 3);
  return name;
}

// A football game line from ESPN's displayValue, "15/27, 212 YDS, 3 TD, 1
// INT" or "25 CAR, 155 YDS, 1 TD": the yards and the touchdowns, "212 yds 3
// TD", "62 yds" with none. "" when the line has no yards.
inline std::string football_game_stat(const std::string &display_value) {
  std::string yards, tds;
  size_t start = 0;
  while (start <= display_value.size()) {
    size_t comma = display_value.find(", ", start);
    std::string tok = display_value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    size_t sp = tok.find(' ');
    if (sp != std::string::npos) {
      std::string num = tok.substr(0, sp), unit = tok.substr(sp + 1);
      if (unit == "YDS")
        yards = num;
      else if (unit == "TD" && num != "0")
        tds = num;
    }
    if (comma == std::string::npos)
      break;
    start = comma + 2;
  }
  if (yards.empty())
    return "";
  return tds.empty() ? yards + " yds" : yards + " yds " + tds + " TD";
}

// A whole number above 0 from ESPN's displayValue, else -1.
inline int leader_count(const std::string &value) {
  if (value.empty() || value.size() > 4)
    return -1;
  for (char c : value)
    if (c < '0' || c > '9')
      return -1;
  int n = atoi(value.c_str());
  return n > 0 ? n : -1;
}

// "2 goals", "1 assist", "40 pts": a count with its word.
inline std::string count_stat(const std::string &value, const char *one, const char *many) {
  int n = leader_count(value);
  if (n < 0)
    return "";
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

// The column of a box score label ("IP", "ER") in the group's labels, -1 when
// the group has no such column.
inline int stat_column(const std::vector<std::string> &labels, const char *label) {
  for (size_t i = 0; i < labels.size(); i++)
    if (labels[i] == label)
      return (int) i;
  return -1;
}

inline std::string stat_at(const std::vector<std::string> &stats, int col) {
  return col >= 0 && (size_t) col < stats.size() ? stats[col] : std::string();
}

// A pitcher's line from the box score's pitching group: "4.0 IP 2 ER 6 K".
// "" without innings.
inline std::string pitching_stat(const std::vector<std::string> &labels, const std::vector<std::string> &stats) {
  std::string ip = stat_at(stats, stat_column(labels, "IP"));
  if (ip.empty() || ip == "0.0")
    return "";
  std::string out = ip + " IP";
  std::string er = stat_at(stats, stat_column(labels, "ER"));
  if (!er.empty())
    out += " " + er + " ER";
  std::string k = stat_at(stats, stat_column(labels, "K"));
  if (!k.empty())
    out += " " + k + " K";
  return out;
}

// A hitter's line from the batting group: hits for at-bats, then the home
// runs when there were any, else the runs batted in: "2-6 2 RBI", "1-4 1 HR".
// "" without a hit.
inline std::string batting_stat(const std::vector<std::string> &labels, const std::vector<std::string> &stats) {
  std::string hab = stat_at(stats, stat_column(labels, "H-AB"));
  if (hab.empty() || hab[0] == '0')
    return "";
  std::string out = hab;
  int hr = leader_count(stat_at(stats, stat_column(labels, "HR")));
  int rbi = leader_count(stat_at(stats, stat_column(labels, "RBI")));
  if (hr > 0)
    out += " " + std::to_string(hr) + " HR";
  else if (rbi > 0)
    out += " " + std::to_string(rbi) + " RBI";
  return out;
}

// Which hitter leads: hits first, then runs batted in, then home runs. Each
// is as leader_count() reads it (-1 for none).
inline bool batter_before(int h_a, int rbi_a, int hr_a, int h_b, int rbi_b, int hr_b) {
  if (h_a != h_b)
    return h_a > h_b;
  if (rbi_a != rbi_b)
    return rbi_a > rbi_b;
  return hr_a > hr_b;
}

// The line as the card draws it, at most max_chars: the name is cut, never
// the stat, and a name keeps at least three characters.
inline std::string fit_leader(const LeaderLine &l, size_t max_chars) {
  std::string line = l.name + " " + l.stat;
  if (line.size() <= max_chars)
    return line;
  size_t room = max_chars > l.stat.size() + 1 ? max_chars - l.stat.size() - 1 : 0;
  if (room < 3)
    room = 3;
  return l.name.substr(0, std::min(room, l.name.size())) + " " + l.stat;
}

}  // namespace detail

// The card laid out for a display width_px wide. Pure, so the host tests
// check the text; firmware/pages/gameday-live.yaml draws it.
struct LeadersCardText {
  // 0 when there is nothing to draw, else the Spleen font's height for the
  // lines: 12 (6 px a character) or 8 (5 px).
  uint8_t font{0};
  // 128 wide and up: each side is a block with its logo at half size over
  // its abbreviation down the left, and its lines to the right. Narrower:
  // the abbreviation on one row and the top line under it, per side.
  bool logos{false};
  uint8_t text_x{0};       // where the lines start, px
  uint8_t pitch{0};        // px from one line to the next
  uint8_t block_h{0};      // px per side
  std::string team_body;   // the lines, '\n' between them
  std::string opp_body;
};

// Pixels per character and lines per side decide the font: the big one when
// both sides have at most two lines, so basketball, hockey and baseball read
// from across the room; football's three lines a side take the small one.
inline LeadersCardText leaders_card_text(const GameLeaders &l, int width_px, int height_px = 55) {
  LeadersCardText out;
  if (!l.valid || !l.any())
    return out;
  const uint8_t lines = std::max(l.team.count, l.opp.count);
  auto body = [](const LeaderSide &s, size_t max_chars, uint8_t take) {
    std::string b;
    for (uint8_t i = 0; i < s.count && i < take; i++) {
      if (!b.empty())
        b += '\n';
      b += detail::fit_leader(s.lines[i], max_chars);
    }
    return b;
  };
  if (width_px < 128) {
    // One line a side under its abbreviation, in the small font.
    out.font = 8;
    out.pitch = 9;
    out.block_h = 18;
    out.text_x = 0;
    size_t chars = (size_t) width_px / 5;
    out.team_body = body(l.team, chars, 1);
    out.opp_body = body(l.opp, chars, 1);
    return out;
  }
  out.logos = true;
  out.text_x = 22;  // a 16 px logo, or a four letter abbreviation, and a gap
  const int avail = height_px / 2;
  if (lines <= 2 && 2 * 13 <= avail) {
    out.font = 12;
    out.pitch = 13;
  } else {
    out.font = 8;
    out.pitch = 9;
  }
  out.block_h = (uint8_t) (lines * out.pitch);
  size_t chars = (size_t) (width_px - out.text_x) / (out.font == 12 ? 6 : 5);
  out.team_body = body(l.team, chars, kLeaderLines);
  out.opp_body = body(l.opp, chars, kLeaderLines);
  return out;
}

// ---- parsing ---------------------------------------------------------------------

// The summary endpoint: the game's box score, leaders, plays and more,
// 400 KB to 1.2 MB on the wire.
std::string summary_url(League league, const std::string &event_id);

namespace detail {

// The fields read from the summary: the event id to check the document is
// the game asked for, and per sport the leaders (football, basketball,
// hockey) or the box score's players (baseball, whose summary has no
// leaders). Everything else, the plays and drives that make up most of the
// document, is skipped as it streams.
inline void fill_summary_filter(JsonObject f, Sport sport) {
  f["header"]["id"] = true;
  if (sport == Sport::BASEBALL) {
    JsonObject p = f["boxscore"]["players"].add<JsonObject>();
    p["team"]["id"] = true;
    JsonObject s = p["statistics"].add<JsonObject>();
    s["type"] = true;
    s["labels"] = true;
    JsonObject a = s["athletes"].add<JsonObject>();
    a["athlete"]["shortName"] = true;
    a["stats"] = true;
    return;
  }
  JsonObject l = f["leaders"].add<JsonObject>();
  l["team"]["id"] = true;
  JsonObject cat = l["leaders"].add<JsonObject>();
  cat["name"] = true;
  JsonObject top = cat["leaders"].add<JsonObject>();
  top["displayValue"] = true;
  top["athlete"]["shortName"] = true;
}

// One side's top entry in a leaders category, null when it has none.
inline JsonObjectConst summary_top(JsonObjectConst side, const char *name) {
  for (JsonObjectConst cat : side["leaders"].as<JsonArrayConst>())
    if (str_or_empty(cat["name"]) == name)
      return cat["leaders"][0];
  return JsonObjectConst();
}

inline std::vector<std::string> string_list(JsonArrayConst a) {
  std::vector<std::string> out;
  for (JsonVariantConst v : a)
    out.push_back(str_or_empty(v));
  return out;
}

// Football, basketball and hockey, from the summary's leaders.
inline void side_from_leaders(JsonObjectConst side, Sport sport, LeaderSide &out) {
  auto name = [](JsonObjectConst top) { return leader_surname(str_or_empty(top["athlete"]["shortName"])); };
  auto value = [](JsonObjectConst top) { return str_or_empty(top["displayValue"]); };
  if (sport == Sport::FOOTBALL) {
    for (const char *cat : {"passingYards", "rushingYards", "receivingYards"}) {
      JsonObjectConst top = summary_top(side, cat);
      out.add(name(top), football_game_stat(value(top)));
    }
  } else if (sport == Sport::BASKETBALL) {
    JsonObjectConst pts = summary_top(side, "points"), reb = summary_top(side, "rebounds");
    out.add(name(pts), count_stat(value(pts), "pt", "pts"));
    out.add(name(reb), count_stat(value(reb), "reb", "reb"));
  } else if (sport == Sport::HOCKEY) {
    JsonObjectConst g = summary_top(side, "goals"), a = summary_top(side, "assists");
    out.add(name(g), count_stat(value(g), "goal", "goals"));
    out.add(name(a), count_stat(value(a), "assist", "assists"));
  }
}

// Baseball, from the box score: the starting pitcher's line (first in the
// pitching group) and the top hitter's.
inline void side_from_boxscore(JsonObjectConst side, LeaderSide &out) {
  LeaderLine pitcher, batter;
  int best_h = -1, best_rbi = -1, best_hr = -1;
  for (JsonObjectConst group : side["statistics"].as<JsonArrayConst>()) {
    std::string type = str_or_empty(group["type"]);
    std::vector<std::string> labels = string_list(group["labels"]);
    for (JsonObjectConst a : group["athletes"].as<JsonArrayConst>()) {
      std::string name = leader_surname(str_or_empty(a["athlete"]["shortName"]));
      std::vector<std::string> stats = string_list(a["stats"]);
      if (type == "pitching") {
        if (pitcher.name.empty()) {
          std::string line = pitching_stat(labels, stats);
          if (!line.empty())
            pitcher = {name, line};
        }
      } else if (type == "batting") {
        int h = leader_count(stat_at(stats, stat_column(labels, "H")));
        int rbi = leader_count(stat_at(stats, stat_column(labels, "RBI")));
        int hr = leader_count(stat_at(stats, stat_column(labels, "HR")));
        if (h > 0 && batter_before(h, rbi, hr, best_h, best_rbi, best_hr)) {
          std::string line = batting_stat(labels, stats);
          if (!line.empty()) {
            batter = {name, line};
            best_h = h;
            best_rbi = rbi;
            best_hr = hr;
          }
        }
      }
    }
  }
  out.add(pitcher.name, pitcher.stat);
  out.add(batter.name, batter.stat);
}

}  // namespace detail

// Parses a summary document (summary_url) for the game's leaders. Returns
// false on a JSON error, a document cut short, or a document for some other
// game than event_id; a game with no leaders yet parses as valid with no
// lines. our_team_id picks which side is `team`.
template<typename TInput>
bool parse_summary(TInput &input, Sport sport, uint32_t our_team_id, const std::string &event_id, LeadersStage stage,
                   GameLeaders &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_summary_filter(filter.to<JsonObject>(), sport);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                              DeserializationOption::NestingLimit(40));
  if (err)
    return false;
  if (detail::str_or_empty(doc["header"]["id"]) != event_id)
    return false;
  GameLeaders l;
  l.event_id = event_id;
  l.stage = stage;
  JsonArrayConst sides = sport == Sport::BASEBALL ? doc["boxscore"]["players"].as<JsonArrayConst>()
                                                  : doc["leaders"].as<JsonArrayConst>();
  for (JsonObjectConst side : sides) {
    bool ours = (uint32_t) atol(detail::str_or_empty(side["team"]["id"]).c_str()) == our_team_id;
    LeaderSide &s = ours ? l.team : l.opp;
    if (sport == Sport::BASEBALL)
      detail::side_from_boxscore(side, s);
    else
      detail::side_from_leaders(side, sport, s);
  }
  l.valid = true;
  out = l;
  return true;
}

bool parse_summary_str(const std::string &json, Sport sport, uint32_t our_team_id, const std::string &event_id,
                       LeadersStage stage, GameLeaders &out);

}  // namespace espn
