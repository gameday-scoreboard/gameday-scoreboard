// Fuzz target: one input goes through every ESPN parser the panel runs, the
// text built from what they read, and the finals rotation (recap.h). ESPN's
// documents share one shape (events, competitions, competitors), so a
// mutated scoreboard also reaches the event and team parsers. See
// fuzz_main.cpp and the fuzz targets in the Makefile.
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "espn_parse.h"
#include "line_score.h"
#include "panel_text.h"
#include "recap.h"
#include "score_log.h"

using namespace espn;

namespace {

// The firmware's ContainerReader: ArduinoJson reads the document a byte or a
// block at a time, the way it arrives from the socket. Small blocks put the
// block edges inside tokens.
class ChunkReader {
 public:
  ChunkReader(const uint8_t *data, size_t size, size_t chunk) : data_(data), size_(size), chunk_(chunk) {}
  int read() { return this->pos_ < this->size_ ? this->data_[this->pos_++] : -1; }
  size_t readBytes(char *dst, size_t n) {
    size_t take = std::min(std::min(n, this->chunk_), this->size_ - this->pos_);
    memcpy(dst, this->data_ + this->pos_, take);
    this->pos_ += take;
    return take;
  }

 private:
  const uint8_t *data_;
  size_t size_, chunk_, pos_{0};
};

// Which teams and events the input names, so the parsers that look for "our"
// team find one. Read with its own permissive filter.
struct Pick {
  std::string event_id;
  uint32_t team_id;
};

void add_pick(std::vector<Pick> &picks, const std::string &event_id, JsonVariantConst id) {
  if (picks.size() >= 6)
    return;
  uint32_t team = (uint32_t) atol(espn::detail::str_or_empty(id).c_str());
  if (team != 0)
    picks.push_back({event_id, team});
}

std::vector<Pick> find_picks(const uint8_t *data, size_t size) {
  JsonDocument filter;
  filter["id"] = true;
  filter["team"]["id"] = true;
  filter["competitions"][0]["competitors"][0]["id"] = true;
  filter["competitions"][0]["competitors"][0]["team"]["id"] = true;
  JsonObject ev = filter["events"].add<JsonObject>();
  ev["id"] = true;
  ev["competitions"][0]["competitors"][0]["id"] = true;
  ev["competitions"][0]["competitors"][0]["team"]["id"] = true;
  JsonDocument doc;
  std::vector<Pick> picks;
  if (deserializeJson(doc, (const char *) data, size, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(40)))
    return picks;
  add_pick(picks, "", doc["team"]["id"]);
  std::string root_id = espn::detail::str_or_empty(doc["id"]);
  JsonArrayConst root_comps = doc["competitions"][0]["competitors"];
  for (JsonObjectConst c : root_comps) {
    add_pick(picks, root_id, c["id"]);
    add_pick(picks, root_id, c["team"]["id"]);
  }
  for (JsonObjectConst e : doc["events"].as<JsonArrayConst>()) {
    std::string id = espn::detail::str_or_empty(e["id"]);
    for (JsonObjectConst c : e["competitions"][0]["competitors"].as<JsonArrayConst>()) {
      add_pick(picks, id, c["id"]);
      add_pick(picks, id, c["team"]["id"]);
    }
  }
  return picks;
}

// ESPTime::from_epoch_local(epoch).to_c_tm() on the panel, in UTC. An epoch
// gmtime can't convert gives an invalid ESPTime, all zero, whose month and
// weekday come out as -1.
struct tm panel_tm(int64_t epoch) {
  struct tm out {};
  time_t t = (time_t) epoch;
  if (gmtime_r(&t, &out) == nullptr) {
    out = tm{};
    out.tm_mon = out.tm_wday = out.tm_yday = -1;
    out.tm_year = -1900;
  }
  return out;
}

// What the board and Home Assistant get from a snapshot, for every set of
// ticker switches that changes the text.
size_t render(const GameSnapshot &prev, const GameSnapshot &g, int64_t now) {
  size_t n = 0;
  int64_t at = g.time_tbd ? tbd_day_epoch(g.kickoff_epoch) : g.kickoff_epoch;
  std::string kick_local = kickoff_label(panel_tm(at), panel_tm(now), g.time_tbd);
  // The follow-up reads a parsed game leads to.
  for (uint8_t league = 0; league < kLeagueCount; league++) {
    n += scoreboard_url((League) league, 1, g.kickoff_epoch, g.time_tbd).size();
    n += team_day_url((League) league, 1, g.kickoff_epoch).size();
    n += event_url((League) league, g.event_id).size();
  }
  for (int bits = 0; bits < 4; bits++) {
    TickerOptions o;
    o.clock = o.down_distance = o.baseball_count = o.soccer_stats = (bits & 1) != 0;
    o.last_play = o.odds = (bits & 2) != 0;
    n += esphome::gameday::panel_text(status_text(g, o, kick_local)).size();
  }
  n += esphome::gameday::panel_text(clock_text(g)).size();
  n += ha_status_text(g, kick_local).size();
  for (int flags = 0; flags < 4; flags++)
    n += decide_splash(prev, g, (flags & 1) != 0, (flags & 2) != 0).text.size();
  n += parse_color(g.team_color) + parse_color(g.opp_color);
  if (g.sport == Sport::BASEBALL)
    n += baseball_count(g).size();
  if (g.sport == Sport::SOCCER) {
    for (size_t max = 0; max <= 16; max += 4)
      n += soccer_situation(g, max).size();
    n += soccer_board_record(g.team_record).size() + soccer_board_record(g.opp_record).size();
  }
  if (g.sport == Sport::BASKETBALL)
    n += basketball_series(g).size();
  // The halftime and final boards: the line score at both widths, and the
  // scoring log from the two reads.
  for (int width : {64, 128, 256}) {
    LineScoreText t = line_score_text(g, width);
    n += t.head.size() + t.body.size() + t.title.size();
  }
  ScoreLog log;
  log.update(prev);
  log.update(g);
  n += with_score_log(status_text(g, TickerOptions{}, kick_local), g.headline, log.lines(g.event_id)).size();
  return n;
}

// The finals rotation as the live modes run it: absorb the scan, build a
// loop of cards and draw each one the way recap_show_() does.
size_t recap(std::vector<Final> finals, std::vector<Pending> pending, int64_t next_kickoff, int64_t now,
             uint8_t league) {
  for (auto &f : finals)
    f.league = league;
  for (auto &p : pending)
    p.league = league;
  std::vector<RecapDay> days;
  recap_absorb(days, finals, pending, now);
  // A second scan the next day with the same games, as the walk back reads.
  recap_absorb(days, finals, pending, now + 86400);
  size_t n = recap_count(days, league);
  int64_t next = recap_scan_next(next_kickoff, pending, now);
  std::vector<RecapCard> cards = recap_cards(days, 0xFFFF, next != 0, now);
  for (const RecapCard &c : cards) {
    n += recap_card_ms(c.kind);
    switch (c.kind) {
      case RecapKind::HEADER:
        n += recap_header(days.at(c.day), now).size();
        break;
      case RecapKind::FINAL:
        n += render(GameSnapshot{}, final_snapshot(days.at(c.day).finals.at(c.final)), now);
        n += recap_header(days.at(c.day), now).size();
        break;
      case RecapKind::LATER: {
        const Pending &p = days.at(c.day).later.at(c.final);
        n += render(GameSnapshot{}, pending_snapshot(p), now);
        std::string when = kickoff_label(panel_tm(p.kickoff_epoch), panel_tm(now));
        n += recap_later_ticker(p, when).size() + recap_matchup(p).size();
        break;
      }
      default:
        n += recap_next_text("", kickoff_label(panel_tm(next), panel_tm(now))).size();
        break;
    }
  }
  return n;
}

const Sport kSports[] = {Sport::FOOTBALL, Sport::BASEBALL, Sport::BASKETBALL, Sport::HOCKEY, Sport::SOCCER};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // The clock of the day the parsers are run on: 2026-10-17, noon Eastern.
  const int64_t now = 1792252800;
  // Keeps the work from being optimized away.
  volatile size_t sink = 0;
  size_t chunk = size > 0 ? 1 + data[0] % 64 : 1;
  std::string json((const char *) data, size);
  std::vector<Pick> picks = find_picks(data, size);

  for (uint8_t league = 0; league < kLeagueCount; league += 4) {
    std::vector<LiveGame> live;
    std::vector<Final> finals;
    std::vector<Pending> pending;
    int64_t next = 0;
    ChunkReader r(data, size, chunk);
    if (parse_live_games(r, live, &next, &finals, &pending))
      sink = sink + recap(finals, pending, next, now, league);
  }
  {
    // The plain scan, with nothing kept for the rotation.
    std::vector<LiveGame> live;
    ChunkReader r(data, size, chunk);
    parse_live_games(r, live);
    for (const auto &g : live)
      sink = sink + g.away_abbr.size() + g.home_abbr.size();
  }

  Schedule team;
  {
    ChunkReader r(data, size, chunk);
    if (parse_team(r, team)) {
      std::vector<Schedule> comps{team, team};
      comps[1].kickoff_epoch = now + 3600;
      sink = sink + (size_t) (pick_schedule(comps, now, team.event_id) + 1);
    }
  }

  GameSnapshot prev;
  for (const Pick &p : picks) {
    std::vector<Upcoming> up;
    ChunkReader r1(data, size, chunk);
    if (parse_upcoming(r1, p.team_id, 5, up))
      for (const auto &u : up)
        sink = sink + u.opp_name.size() + u.tv.size();

    Schedule day;
    ChunkReader r2(data, size, chunk);
    if (parse_team_game(r2, p.team_id, "", day) == 1)
      sink = sink + (today_game_first(team, day) ? 1 : 0);
    sink = sink + parse_team_game_str(json, p.team_id, day, p.event_id);

    GameSnapshot g;
    ChunkReader r3(data, size, chunk);
    if (parse_scoreboard(r3, p.event_id, p.team_id, g)) {
      sink = sink + render(prev, g, now);
      prev = g;
    }

    for (Sport sport : kSports) {
      GameSnapshot e;
      ChunkReader r4(data, size, chunk);
      if (!parse_event(r4, sport, p.team_id, e, p.event_id))
        continue;
      sink = sink + render(prev, e, now);
      // The same game a poll later: what a splash compares.
      sink = sink + render(e, e, now);
      prev = e;
    }
  }
  (void) sink;
  return 0;
}
