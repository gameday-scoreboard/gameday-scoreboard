// Fuzz target: one input goes through every ESPN parser the panel runs (the
// scoreboards, team, schedule, season, event, summary, Top 25, news and
// golf reads), the text built from what they read, and the finals rotation
// (recap.h). ESPN's documents share one shape (events, competitions,
// competitors), so a mutated scoreboard also reaches the event and team
// parsers. See fuzz_main.cpp and the fuzz targets in the Makefile.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "espn_parse.h"
#include "game_leaders.h"
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

// The same reader behind the ByteSource the season read takes.
struct ChunkSource : ByteSource {
  ChunkReader r;
  ChunkSource(const uint8_t *data, size_t size, size_t chunk) : r(data, size, chunk) {}
  int read() override { return this->r.read(); }
  size_t readBytes(char *dst, size_t n) override { return this->r.readBytes(dst, n); }
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
  // A game summary: its event id is header.id, its teams in the header,
  // the leaders and the box score.
  filter["header"]["id"] = true;
  filter["header"]["competitions"][0]["competitors"][0]["id"] = true;
  filter["header"]["competitions"][0]["competitors"][0]["team"]["id"] = true;
  filter["leaders"][0]["team"]["id"] = true;
  filter["boxscore"]["players"][0]["team"]["id"] = true;
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
  std::string summary_id = espn::detail::str_or_empty(doc["header"]["id"]);
  for (JsonObjectConst c : doc["header"]["competitions"][0]["competitors"].as<JsonArrayConst>()) {
    add_pick(picks, summary_id, c["id"]);
    add_pick(picks, summary_id, c["team"]["id"]);
  }
  for (JsonObjectConst side : doc["leaders"].as<JsonArrayConst>())
    add_pick(picks, summary_id, side["team"]["id"]);
  for (JsonObjectConst side : doc["boxscore"]["players"].as<JsonArrayConst>())
    add_pick(picks, summary_id, side["team"]["id"]);
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
size_t recap(FinalList finals, PendingList pending, int64_t next_kickoff, int64_t now,
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

// parse_golf as it read the whole document through one filter before it
// streamed, kept here to check the streaming read against. It has the
// rules the streaming read added: a followed golfer with no row takes no
// room, and a golfer keeps lines for kGolfLines rounds. top_full is set
// when more than kGolfTopRows golfers are in the top, where the streaming
// read stops at kGolfTopRows rows and this one doesn't.
bool golf_whole_document(const uint8_t *data, size_t size, const uint32_t *fav_ids, size_t fav_count, GolfBoard &out,
                         bool &top_full) {
  top_full = false;
  JsonDocument filter;
  JsonObject ev = filter["events"].add<JsonObject>();
  ev["name"] = true;
  ev["date"] = true;
  ev["endDate"] = true;
  ev["status"]["type"]["state"] = true;
  JsonObject comp = ev["competitions"].add<JsonObject>();
  comp["status"]["period"] = true;
  comp["status"]["type"]["state"] = true;
  comp["status"]["type"]["completed"] = true;
  comp["status"]["type"]["detail"] = true;
  comp["status"]["type"]["shortDetail"] = true;
  JsonObject c = comp["competitors"].add<JsonObject>();
  c["id"] = true;
  c["order"] = true;
  c["score"] = true;
  c["athlete"]["shortName"] = true;
  c["athlete"]["displayName"] = true;
  c["team"]["displayName"] = true;
  JsonObject ls = c["linescores"].add<JsonObject>();
  ls["period"] = true;
  ls["displayValue"] = true;
  ls["linescores"].add<JsonObject>()["period"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, (const char *) data, size, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(20)))
    return false;
  if (!doc["events"].is<JsonArrayConst>())
    return false;
  // In progress, then over, then listed with a field, then any.
  JsonObjectConst pick;
  int best = -1;
  for (JsonObjectConst e : doc["events"].as<JsonArrayConst>()) {
    if (e.isNull())
      continue;
    const std::string st = espn::detail::str_or_empty(e["status"]["type"]["state"]);
    const bool listed = e["competitions"][0]["competitors"].as<JsonArrayConst>().size() != 0;
    const int rank = !listed ? 0 : st == "in" ? 3 : st == "post" ? 2 : 1;
    if (rank > best && best < 3) {
      pick = e;
      best = rank;
    }
  }
  JsonObjectConst comp0 = pick["competitions"][0];
  GolfBoard b;
  b.league = League::PGA;
  const std::string estate = espn::detail::str_or_empty(pick["status"]["type"]["state"]);
  b.state = estate == "in" ? GolfState::IN : estate == "post" ? GolfState::POST : GolfState::PRE;
  b.round_live = b.state == GolfState::IN && espn::detail::str_or_empty(comp0["status"]["type"]["state"]) == "in";
  snprintf(b.event, sizeof(b.event), "%s", espn::detail::str_or_empty(pick["name"]).c_str());
  std::string detail = espn::detail::str_or_empty(comp0["status"]["type"]["shortDetail"]);
  if (detail.empty())
    detail = espn::detail::str_or_empty(comp0["status"]["type"]["detail"]);
  snprintf(b.detail, sizeof(b.detail), "%s", detail.c_str());
  b.round = (uint8_t) (comp0["status"]["period"] | 0);
  b.round_done = comp0["status"]["type"]["completed"] | false;
  b.playoff = strncmp(b.detail, "Playoff", 7) == 0;
  b.start_epoch = parse_iso8601_z(espn::detail::str_or_empty(pick["date"]));
  b.end_epoch = parse_iso8601_z(espn::detail::str_or_empty(pick["endDate"]));
  size_t fav_kept = 0, top = 0;
  std::vector<espn::detail::GolfPending> kept;
  // Every golfer's total and whether they have a round 3 line, for the places.
  struct Total {
    bool scored;
    int total;
    bool weekend;
  };
  std::vector<Total> totals;
  for (JsonObjectConst gc : comp0["competitors"].as<JsonArrayConst>()) {
    b.field++;
    espn::detail::GolfPending p;
    GolfRow &r = p.row;
    Total t{false, 0, false};
    const std::string score = espn::detail::str_or_empty(gc["score"]);
    t.scored = golf_score(score.c_str(), t.total);
    if (t.scored)
      snprintf(r.score, sizeof(r.score), "%s", score.c_str());
    for (JsonObjectConst gl : gc["linescores"].as<JsonArrayConst>())
      t.weekend = t.weekend || (gl["period"] | 0) == 3;
    totals.push_back(t);
    r.place = (uint16_t) totals.size();  // for now, which golfer of the field this is
    r.pos = (uint16_t) (gc["order"] | 0);
    r.id = (uint32_t) atol(espn::detail::str_or_empty(gc["id"]).c_str());
    bool is_fav = false;
    for (size_t i = 0; i < fav_count; i++)
      is_fav = is_fav || fav_ids[i] == r.id;
    if (r.pos <= kGolfTopRows && ++top > kGolfTopRows)
      top_full = true;
    if (r.pos > kGolfTopRows && (!is_fav || fav_kept >= kGolfFavRows))
      continue;
    r.favorite = is_fav;
    snprintf(r.name, sizeof(r.name), "%s", ascii_fold(espn::detail::str_or_empty(gc["athlete"]["shortName"])).c_str());
    std::string full = ascii_fold(espn::detail::str_or_empty(gc["athlete"]["displayName"]));
    size_t sp = full.rfind(' ');
    std::string last = sp == std::string::npos ? full : full.substr(sp + 1);
    const std::string brief = ascii_fold(espn::detail::str_or_empty(gc["athlete"]["shortName"]));
    if (brief.find(' ') != std::string::npos)
      last = brief.substr(brief.find(' ') + 1);
    snprintf(r.last, sizeof(r.last), "%s", last.c_str());
    if (r.name[0] == '\0' && r.last[0] == '\0') {
      const std::string pair = ascii_fold(espn::detail::str_or_empty(gc["team"]["displayName"]));
      const size_t slash = pair.find('/');
      if (slash != std::string::npos) {
        snprintf(r.name, sizeof(r.name), "%s", pair.c_str());
        snprintf(r.last, sizeof(r.last), "%s", pair.substr(0, slash + 2).c_str());
      }
    }
    if (r.name[0] == '\0' && r.last[0] == '\0')
      continue;
    if (r.pos > kGolfTopRows)
      fav_kept++;
    for (JsonObjectConst gl : gc["linescores"].as<JsonArrayConst>()) {
      const int period = gl["period"] | 0;
      size_t i = 0;
      while (i < p.lines && p.line[i].period != period)
        i++;
      if (i == p.lines) {
        if (p.lines >= espn::detail::kGolfLines)
          continue;
        p.line[p.lines++].period = period;
      }
      const std::string dv = espn::detail::str_or_empty(gl["displayValue"]);
      int round_score = 0;
      if (golf_score(dv.c_str(), round_score))
        snprintf(p.line[i].today, sizeof(p.line[i].today), "%s", dv.c_str());
      size_t holes = gl["linescores"].as<JsonArrayConst>().size();
      p.line[i].thru = (uint8_t) (holes > 18 ? 18 : holes);
    }
    kept.push_back(p);
  }
  auto same = [&totals](size_t i, size_t j) {
    return totals[i].scored && totals[j].scored && totals[i].total == totals[j].total &&
           totals[i].weekend == totals[j].weekend;
  };
  for (const auto &p : kept) {
    GolfRow r = p.row;
    size_t first = r.place - 1, last = first;
    r.place = 0;
    if (totals[first].scored) {
      while (first > 0 && same(first - 1, first))
        first--;
      while (last + 1 < totals.size() && same(last, last + 1))
        last++;
      r.place = (uint16_t) (first + 1);
      r.tied = last > first;
      if (b.state == GolfState::POST && first == 0 && last > 0) {
        r.place = b.rows.empty() ? 1 : 2;
        r.tied = !b.rows.empty() && last > 1;
      }
    }
    auto line = [](const espn::detail::GolfPending &g, int period) {
      for (uint8_t i = 0; i < g.lines; i++)
        if (g.line[i].period == period)
          return &g.line[i];
      return (const espn::detail::GolfLine *) nullptr;
    };
    const int rounds = std::min(4, (int) b.round - (b.playoff ? 1 : 0));
    for (int k = 1; k <= rounds && r.gone == GolfGone::NO; k++) {
      const auto *lead = line(kept[0], k), *mine = line(p, k);
      if (lead == nullptr)
        continue;
      if (mine == nullptr && k >= 3)
        r.gone = GolfGone::CUT;
      else if (lead->today[0] != '\0' && (mine == nullptr || mine->today[0] == '\0') &&
               (k < (int) b.round || b.state == GolfState::POST))
        r.gone = GolfGone::LEFT;
    }
    if (const auto *now = line(p, b.round)) {
      memcpy(r.today, now->today, sizeof(r.today));
      r.thru = now->thru;
    }
    b.rows.push_back(r);
  }
  b.valid = true;
  out = std::move(b);
  return true;
}

// Every field of two boards, or what differs first.
const char *golf_differs(const GolfBoard &a, const GolfBoard &b) {
  if (a.valid != b.valid || a.state != b.state || a.round_live != b.round_live || a.round != b.round ||
      a.round_done != b.round_done || a.playoff != b.playoff)
    return "state or round";
  if (strcmp(a.event, b.event) != 0 || strcmp(a.detail, b.detail) != 0)
    return "event or detail";
  if (a.start_epoch != b.start_epoch || a.end_epoch != b.end_epoch || a.field != b.field)
    return "dates or field";
  if (a.rows.size() != b.rows.size())
    return "row count";
  for (size_t i = 0; i < a.rows.size(); i++) {
    const GolfRow &x = a.rows[i], &y = b.rows[i];
    if (x.pos != y.pos || x.place != y.place || x.tied != y.tied || x.gone != y.gone || x.id != y.id || x.thru != y.thru || x.favorite != y.favorite || strcmp(x.name, y.name) != 0 ||
        strcmp(x.last, y.last) != 0 || strcmp(x.score, y.score) != 0 || strcmp(x.today, y.today) != 0)
      return "a row";
  }
  return nullptr;
}

// The schedule and season recap cards from one season read (season.h).
size_t season(const SeasonRead &r, int64_t now) {
  size_t n = season_post_line(r).size() + recap_header("DAL", r.year).size();
  for (Sport sport : kSports)
    for (const char *record : {"", "10-2", "8-3-1"}) {
      n += season_record(r, sport, record).size() + recap_body(r, sport, record).size();
      n += season_recap_ready(r, record) ? 1 : 0;
    }
  for (const SeasonGame &g : season_upcoming(r, now))
    for (bool wide : {false, true})
      n += schedule_line(panel_tm(g.kickoff_epoch), g, wide).size();
  return n;
}

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
    LiveList live;
    FinalList finals;
    PendingList pending;
    int64_t next = 0;
    ChunkReader r(data, size, chunk);
    if (parse_live_games(r, live, &next, &finals, &pending))
      sink = sink + recap(finals, pending, next, now, league);
  }
  {
    // The plain scan, with nothing kept for the rotation.
    LiveList live;
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

  {
    // The Top 25 card's poll, and the rows it draws.
    Poll poll;
    ChunkReader r(data, size, chunk);
    if (parse_rankings(r, poll))
      for (uint32_t pg = 0; pg < top25_pages(poll, 2); pg++)
        sink = sink + top25_column(League::NCAA, poll, pg, 2, 1).size() + top25_header(League::MCBB, poll).size();
  }

  {
    // The news card's headlines (news.h).
    std::string list;
    ChunkReader r(data, size, chunk);
    sink = sink + (size_t) (parse_news(r, list) + 1) + list.size();
  }

  {
    // A golf tour's leaderboard (golf.h), with the input's ids followed.
    // The stream, the string and the whole-document read must give the
    // same board.
    uint32_t favs[8];
    size_t fav_count = 0;
    for (const Pick &p : picks)
      if (fav_count < 8)
        favs[fav_count++] = p.team_id;
    GolfBoard a, b, w;
    ChunkReader r(data, size, chunk);
    const bool ok_a = parse_golf(r, League::PGA, favs, fav_count, a);
    const bool ok_b = parse_golf(json, League::PGA, favs, fav_count, b);
    bool top_full = false;
    const bool ok_w = golf_whole_document(data, size, favs, fav_count, w, top_full);
    const char *diff = ok_a != ok_b ? "the result" : golf_differs(a, b);
    if (diff != nullptr) {
      fprintf(stderr, "parse_golf read a stream and a string differently: %s\n", diff);
      abort();
    }
    diff = ok_a != ok_w ? "the result" : top_full ? nullptr : golf_differs(a, w);
    if (diff != nullptr) {
      fprintf(stderr, "parse_golf (%d) and the whole-document read (%d) differ: %s\n", ok_a, ok_w, diff);
      abort();
    }
    if (ok_a) {
      if (a.rows.size() > kGolfRowsMax) {
        fprintf(stderr, "parse_golf kept %zu rows\n", a.rows.size());
        abort();
      }
      char head[kGolfHead], text[kGolfText];
      for (uint32_t pg = 0; pg < golf_pages(a); pg++)
        for (bool wide : {false, true}) {
          golf_page(a, pg, wide, text);
          sink = sink + strlen(text);
        }
      golf_header(a, head);
      GolfBoard kept;
      golf_take(kept, std::move(b), now);
      golf_take(kept, std::move(w), now + 60);
      sink = sink + strlen(head) + golf_read_ms(kept) + (golf_fresh(kept, now + 60) ? 1 : 0);
    }
  }

  GameSnapshot prev;
  for (const Pick &p : picks) {
    {
      // The team's season schedule (season.h), read the way the panel reads it.
      SeasonRead s;
      ChunkSource src(data, size, chunk);
      if (parse_season_from(src, p.team_id, s))
        sink = sink + season(s, now);
    }

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
      {
        // The game summary at halftime and the final (game_leaders.h).
        GameLeaders l;
        ChunkReader r5(data, size, chunk);
        if (parse_summary(r5, sport, p.team_id, p.event_id, LeadersStage::FINAL, l))
          sink = sink + leaders_card_text(l, 128).team_body.size() + leaders_card_text(l, 64).opp_body.size();
      }
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
