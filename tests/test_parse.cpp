// Host tests for the ESPN parser and game logic. Build with `make` in tests/.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "auto_update.h"
#include "night.h"
#include "badge.h"
#include "best_game.h"
#include "captive.h"
#include "countdown.h"
#include "demo.h"
#include "espn_parse.h"
#include "espn_parse_impl.h"
#include "favorites.h"
#include "guard.h"
#include "ha_events.h"
#include "health.h"
#include "hook.h"
#include "line_score.h"
#include "logo_lru.h"
#include "message.h"
#include "offseason.h"
#include "panel_text.h"
#include "recap.h"
#include "regulatory.h"
#include "score_log.h"
#include "poll_timing.h"
#include "prefs_sync.h"
#include "startup.h"
#include "timezones.h"
#include "wifi_history.h"

using namespace espn;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    checks++;                                                                  \
    auto va = (a);                                                             \
    auto vb = (b);                                                             \
    if (!(va == vb)) {                                                         \
      failures++;                                                              \
      std::ostringstream os;                                                   \
      os << va << " != " << vb;                                                \
      printf("FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #a, os.str().c_str()); \
    }                                                                          \
  } while (0)

static std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Fixture facts (recorded 2026-09-05, see fixtures/README.md)
static const char *kLiveEvent = "401858433";  // BOIS @ ORE, 15:00 4th, ORE ball
static const char *kPostEvent = "401858432";  // BALL @ OSU, Final 3-56
static const char *kNflEvent = "401872656";   // SEA vs NE, 9/9 8:20 PM EDT
static const uint32_t kBoise = 68, kOregon = 2483, kBallState = 2050, kOhioState = 194, kSeahawks = 26, kPatriots = 17;

static void test_urls() {
  CHECK_EQ(team_url(League::NFL, 6), std::string("https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/6"));
  // Monday night kickoff 00:20Z on 9/15 is still 9/14 in the US
  CHECK_EQ(scoreboard_url(League::NFL, 0, parse_iso8601_z("2026-09-15T00:20Z")), std::string("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard?dates=20260914"));
  // 2026-09-05T19:30Z is Sep 5 in the US; 2026-09-06T02:00Z is still Sep 5 Eastern
  CHECK_EQ(scoreboard_url(League::NCAA, 1, parse_iso8601_z("2026-09-05T19:30Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard?groups=1&dates=20260905"));
  CHECK_EQ(scoreboard_url(League::NCAA, 9, parse_iso8601_z("2026-09-06T02:00Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard?groups=9&dates=20260905"));
  CHECK_EQ(event_url(League::NFL, "401872999"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard/401872999"));
  CHECK_EQ(event_url(League::NCAA, "401871053"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard/401871053"));
  CHECK_EQ(dark_logo("https://a.espncdn.com/i/teamlogos/ncaa/500/68.png"), std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/68.png&w=64&h=64"));
  CHECK_EQ(dark_logo("https://a.espncdn.com/i/teamlogos/nfl/500/scoreboard/sea.png"), std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nfl/500-dark/scoreboard/sea.png&w=64&h=64"));
  CHECK_EQ(team_logo_url(League::NFL, 6, "DAL"), std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nfl/500-dark/scoreboard/dal.png&w=64&h=64"));
  CHECK_EQ(team_logo_url(League::NCAA, 228, "CLEM"), std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/228.png&w=64&h=64"));
  CHECK_EQ(dark_logo(""), std::string(""));
  // The bench's unreachable root reaches every URL, and the default comes back
  set_site_root("https://bench.invalid/sports/");
  CHECK_EQ(team_url(League::NFL, 6), std::string("https://bench.invalid/sports/football/nfl/teams/6"));
  set_site_root("https://site.api.espn.com/apis/site/v2/sports/");
  CHECK_EQ(team_url(League::NFL, 6), std::string("https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/6"));
}

static void test_iso() {
  CHECK_EQ(parse_iso8601_z("1970-01-01T00:00Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-09-05T19:30Z"), (int64_t) 1788636600);
  CHECK_EQ(parse_iso8601_z("2026-09-10T00:20:00Z"), (int64_t) 1788999600);
  CHECK_EQ(parse_iso8601_z("garbage"), (int64_t) 0);
  // HTTP Date headers, as ESPN sends them.
  CHECK_EQ(parse_http_date("Sat, 17 Oct 2026 23:39:29 GMT"), (int64_t) 1792280369);
  CHECK_EQ(parse_http_date("Sat, 05 Sep 2026 19:30:00 GMT"), (int64_t) 1788636600);
  CHECK_EQ(parse_http_date("Sun, 01 Mar 2048 00:00:00 GMT"), (int64_t) 2466633600);
  CHECK_EQ(parse_http_date(""), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat, 17 Oct 2026 23:39:29 UTC"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat, 17 Okt 2026 23:39:29 GMT"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat, 7 Oct 2026 23:39:29 GMT"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat, 17 Oct 2026 24:39:29 GMT"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat, 17 Oct 2O26 23:39:29 GMT"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Thu, 01 Jan 1970 00:00:00 GMT"), (int64_t) 0);  // outside 2020 to 2099
  CHECK_EQ(parse_http_date("Saturday, 17-Oct-26 23:39:29 GMT"), (int64_t) 0);
  CHECK_EQ(parse_http_date("Sat Oct 17 23:39:29 2026"), (int64_t) 0);
  // Fields too long or out of range read as no date instead of overflowing
  // (found by fuzz_espn).
  CHECK_EQ(parse_iso8601_z("2026-10-17T999999999:00Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-999999999-17T19:30Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-10-2147483647T19:30Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-10-17T19:999999999Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2147483647-10-17T19:30Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-13-01T19:30Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-10-17T24:00Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("2026-10-17T-1:00Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("1969-12-31T23:59Z"), (int64_t) 0);
  CHECK_EQ(parse_iso8601_z("9999-12-31T23:59:59Z"), (int64_t) 253402300799);
}

static void test_team_parse() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_ncaa_bc.json"), s));
  CHECK(s.valid);
  CHECK_EQ(s.event_id, std::string("401856777"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-09-05T19:30Z"));
  CHECK_EQ(s.group, (uint32_t) 1);
  CHECK_EQ(s.team_color, std::string("8c2232"));
  CHECK_EQ(s.team_record, std::string("0-0"));
  CHECK_EQ(s.opp_id, (uint32_t) 2132);  // Cincinnati, the other side of the next game
  CHECK_EQ(s.opp_abbr, std::string("CIN"));

  Schedule d;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_dal.json"), d));
  CHECK_EQ(d.event_id, std::string("401872930"));
  CHECK_EQ(d.team_color, std::string("002a5c"));
  CHECK_EQ(d.team_record, std::string("2-1"));
  CHECK_EQ(d.opp_id, (uint32_t) 19);
  CHECK_EQ(d.opp_abbr, std::string("NYG"));

  // No next game: no opponent.
  Schedule none;
  CHECK(parse_team_str(R"({"team":{"id":"6","color":"002a5c","nextEvent":[]}})", none));
  CHECK(none.event_id.empty());
  CHECK_EQ(none.opp_id, (uint32_t) 0);

  Schedule bad;
  CHECK(!parse_team_str("{not json", bad));
  CHECK(!bad.valid);
}

static void test_scoreboard_live() {
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  GameSnapshot s;
  CHECK(parse_scoreboard_str(json, kLiveEvent, kBoise, s));
  CHECK(s.valid);
  CHECK(s.state == GameState::IN);
  CHECK_EQ(s.team_abbr, std::string("BOIS"));
  CHECK_EQ(s.opp_abbr, std::string("ORE"));
  CHECK_EQ(s.team_id, kBoise);
  CHECK_EQ(s.opp_id, kOregon);
  CHECK_EQ(s.team_score, 24);
  CHECK_EQ(s.opp_score, 24);
  CHECK_EQ(s.possession, 2);  // Oregon has the ball
  CHECK_EQ(s.team_timeouts, 1);
  CHECK_EQ(s.opp_timeouts, 1);
  CHECK_EQ(s.down_distance, std::string("3rd & 10 at BOIS 25"));
  CHECK_EQ(s.short_down_distance, std::string("3rd & 10"));
  CHECK(!s.is_red_zone);
  CHECK_EQ(s.short_detail, std::string("15:00 - 4th"));
  CHECK_EQ(s.period, 4);
  CHECK_EQ(s.team_color, std::string("0033a0"));
  CHECK_EQ(s.opp_color, std::string("00934b"));
  CHECK_EQ(s.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/68.png&w=64&h=64"));
  CHECK_EQ(s.opp_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/2483.png&w=64&h=64"));
  CHECK_EQ(team_logo_url(League::NCAA, s.team_id, "BOIS"), s.team_logo);
  CHECK_EQ(team_logo_url(League::NCAA, s.opp_id, s.opp_abbr.c_str()), s.opp_logo);
  CHECK(s.last_play.rfind("End of 3rd quarter", 0) == 0);
  CHECK(!s.completed);

  // The drive comes with a touchdown or field goal only: TEX's field goal
  // carries it, ME @ APP's rushing touchdown too, the failed two-point try
  // after FUR's touchdown (scoreValue 0) doesn't.
  GameSnapshot fg;
  CHECK(parse_scoreboard_str(json, "401856667", 251, fg));
  CHECK_EQ(fg.drive, std::string("12 plays, 50 yards, 5:48"));
  CHECK_EQ((int) fg.team_win_pct, 99);  // 0.999 home, never 100
  GameSnapshot td;
  CHECK(parse_scoreboard_str(json, "401866627", 2026, td));
  CHECK_EQ(td.drive, std::string("3 plays, 68 yards, 1:16"));
  GameSnapshot two;
  CHECK(parse_scoreboard_str(json, "401856666", 231, two));
  CHECK(two.drive.empty());
  CHECK_EQ((int) two.team_win_pct, -1);  // no probability on that play

  // Same event from Oregon's side flips everything
  GameSnapshot o;
  CHECK(parse_scoreboard_str(json, kLiveEvent, kOregon, o));
  CHECK_EQ(o.team_abbr, std::string("ORE"));
  CHECK_EQ(o.possession, 1);

  // Event present but we are not in it
  GameSnapshot x;
  CHECK(!parse_scoreboard_str(json, kLiveEvent, kSeahawks, x));
  // Event missing
  CHECK(!parse_scoreboard_str(json, "1", kBoise, x));
}

// The single-game document (event_url), recorded 2026-10-13 during FIU @ JXST.
static void test_event_live() {
  std::string json = slurp("fixtures/event_ncaa_live.json");
  GameSnapshot s;
  CHECK(parse_event_str(json, Sport::FOOTBALL, 2229, s, "401871053"));  // FIU's side
  CHECK(s.valid);
  CHECK(s.state == GameState::IN);
  CHECK_EQ(s.team_abbr, std::string("FIU"));
  CHECK_EQ(s.opp_abbr, std::string("JXST"));
  CHECK_EQ(s.team_score, 24);
  CHECK_EQ(s.opp_score, 27);
  CHECK_EQ(s.possession, 1);  // FIU has the ball
  CHECK_EQ(s.team_timeouts, 0);
  CHECK_EQ(s.opp_timeouts, 1);
  CHECK_EQ(s.down_distance, std::string("3rd & 4 at FIU 31"));
  CHECK_EQ(s.short_detail, std::string("0:33 - 4th"));
  CHECK_EQ(s.team_record, std::string("3-2"));
  CHECK_EQ(s.tv, std::string("ESPNU"));
  CHECK_EQ(s.venue, std::string("AmFirst Stadium"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-10-14T00:00Z"));
  CHECK(s.last_play.find("J.Kohl pass complete") != std::string::npos);
  // JXST is home at 0.9567; a plain catch doesn't repeat its drive.
  CHECK_EQ((int) s.team_win_pct, 4);
  CHECK_EQ(win_chance(s), std::string("JXST 96% to win"));
  CHECK(s.drive.empty());

  GameSnapshot x;
  CHECK(!parse_event_str(json, Sport::FOOTBALL, kSeahawks, x, "401871053"));  // not our game
  CHECK(!parse_event_str(json, Sport::FOOTBALL, 2229, x, "401871054"));       // some other event
  CHECK(!parse_event_str("{\"code\":404,\"message\":\"No event found\"}", Sport::FOOTBALL, 2229, x, "401871053"));
  CHECK(!parse_event_str("{not json", Sport::FOOTBALL, 2229, x, "401871053"));

  GameSnapshot chi;
  CHECK(parse_event_str(slurp("fixtures/event_nfl_pre.json"), Sport::FOOTBALL, 3, chi, "401872999"));
  CHECK(chi.state == GameState::PRE);
  CHECK_EQ(chi.opp_abbr, std::string("ATL"));
  CHECK_EQ(chi.odds, std::string("CHI -3"));
  CHECK_EQ(chi.over_under, std::string("49.5"));
  CHECK_EQ(chi.tv, std::string("Fox"));

  GameSnapshot sea;
  CHECK(parse_event_str(slurp("fixtures/event_nfl_super_bowl.json"), Sport::FOOTBALL, 26, sea, "401772988"));
  CHECK(sea.completed);
  CHECK(sea.team_winner);
  CHECK_EQ(sea.game_name, std::string("Super Bowl LX"));

  // Records and season leaders for the pre-game ticker, from both sides of one game
  GameSnapshot dal;
  CHECK(parse_event_str(slurp("fixtures/event_nfl_pre_dal.json"), Sport::FOOTBALL, 6, dal, "401873007"));
  CHECK(dal.state == GameState::PRE);
  CHECK_EQ(dal.team_record, std::string("2-3"));
  CHECK_EQ(dal.opp_record, std::string("3-2"));
  CHECK_EQ(dal.team_leader, std::string("Prescott 1,381 yds 9 TD"));
  CHECK_EQ(dal.opp_leader, std::string("Love 1,408 yds 10 TD"));
  // A 128-wide board shows the records under the logos; one 64-wide panel
  // has no room, so its ticker carries them
  TickerOptions wide;
  CHECK_EQ(status_text(dal, wide, "Sun 7:20 PM"),
           std::string("Week 6 | Sun 7:20 PM | GB -1.5 | O/U 53.5 | NBC | Prescott 1,381 yds 9 TD | "
                       "Love 1,408 yds 10 TD | 56F Clear at Lambeau Field"));
  TickerOptions narrow;
  narrow.records = true;
  CHECK_EQ(status_text(dal, narrow, "Sun 7:20 PM"),
           std::string("Week 6 | Sun 7:20 PM | DAL 2-3, GB 3-2 | GB -1.5 | O/U 53.5 | NBC | "
                       "Prescott 1,381 yds 9 TD | Love 1,408 yds 10 TD | 56F Clear at Lambeau Field"));
  TickerOptions no_leaders = narrow;
  no_leaders.leaders = false;
  CHECK_EQ(status_text(dal, no_leaders, "Sun 7:20 PM"),
           std::string("Week 6 | Sun 7:20 PM | DAL 2-3, GB 3-2 | GB -1.5 | O/U 53.5 | NBC | "
                       "56F Clear at Lambeau Field"));
  GameSnapshot gb;
  CHECK(parse_event_str(slurp("fixtures/event_nfl_pre_dal.json"), Sport::FOOTBALL, 9, gb, "401873007"));
  CHECK_EQ(gb.team_leader, std::string("Love 1,408 yds 10 TD"));
  CHECK_EQ(gb.opp_leader, std::string("Prescott 1,381 yds 9 TD"));
  // A passing line with no touchdowns ("42/64, 409 YDS, 3 INT")
  CHECK_EQ(chi.team_leader, std::string("Bagent 409 yds"));
  CHECK_EQ(chi.opp_leader, std::string("Penix Jr. 719 yds 3 TD"));
}

static bool same_snapshot(const GameSnapshot &a, const GameSnapshot &b) {
  return a.valid == b.valid && a.state == b.state && a.event_id == b.event_id && a.kickoff_epoch == b.kickoff_epoch &&
         a.completed == b.completed && a.short_detail == b.short_detail && a.display_clock == b.display_clock &&
         a.period == b.period && a.team_abbr == b.team_abbr && a.opp_abbr == b.opp_abbr && a.team_id == b.team_id &&
         a.opp_id == b.opp_id && a.team_score == b.team_score && a.opp_score == b.opp_score &&
         a.team_record == b.team_record && a.opp_record == b.opp_record && a.team_color == b.team_color &&
         a.opp_color == b.opp_color && a.team_logo == b.team_logo && a.opp_logo == b.opp_logo &&
         a.team_winner == b.team_winner && a.possession == b.possession && a.team_timeouts == b.team_timeouts &&
         a.opp_timeouts == b.opp_timeouts && a.last_play == b.last_play && a.down_distance == b.down_distance &&
         a.short_down_distance == b.short_down_distance && a.is_red_zone == b.is_red_zone && a.odds == b.odds &&
         a.over_under == b.over_under && a.tv == b.tv && a.venue == b.venue && a.game_name == b.game_name;
}

// ESPN's single-game document is the game's entry from the day's scoreboard on
// its own (checked against 48 live, upcoming and final games on 2026-10-13).
// So every game in every scoreboard fixture, cut out on its own, must read the
// same through parse_event as through parse_scoreboard, from both sides.
static void test_event_matches_scoreboard() {
  const char *files[] = {"fixtures/scoreboard_ncaa_live.json",          "fixtures/scoreboard_nfl.json",
                         "fixtures/scoreboard_ncaa_conf_titles.json",   "fixtures/scoreboard_ncaa_cfp_quarterfinals.json",
                         "fixtures/scoreboard_ncaa_cfp_final.json",     "fixtures/scoreboard_nfl_wild_card.json",
                         "fixtures/scoreboard_nfl_super_bowl.json"};
  int compared = 0;
  for (const char *file : files) {
    std::string json = slurp(file);
    JsonDocument doc;
    CHECK(!deserializeJson(doc, json, DeserializationOption::NestingLimit(40)));
    for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
      std::string id = ev["id"] | "";
      std::string one;
      serializeJson(ev, one);
      for (JsonObjectConst c : ev["competitions"][0]["competitors"].as<JsonArrayConst>()) {
        uint32_t team = (uint32_t) atol(c["id"] | "0");
        GameSnapshot from_board, alone;
        bool ok_board = parse_scoreboard_str(json, id, team, from_board);
        bool ok_alone = parse_event_str(one, Sport::FOOTBALL, team, alone, id);
        CHECK_EQ(ok_alone, ok_board);
        if (!same_snapshot(alone, from_board)) {
          failures++;
          printf("FAIL %s: event %s team %u reads differently on its own\n", file, id.c_str(), (unsigned) team);
        }
        compared++;
      }
    }
  }
  CHECK(compared > 200);
}

static void test_scoreboard_post() {
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  GameSnapshot s;
  CHECK(parse_scoreboard_str(json, kPostEvent, kBallState, s));
  CHECK(s.state == GameState::POST);
  CHECK(s.completed);
  CHECK_EQ(s.team_score, 3);
  CHECK_EQ(s.opp_score, 56);
  CHECK(!s.team_winner);
  CHECK_EQ(s.team_record, std::string("0-1"));
  CHECK_EQ(s.opp_record, std::string("1-0"));
  CHECK_EQ(s.short_detail, std::string("Final"));
  GameSnapshot w;
  CHECK(parse_scoreboard_str(json, kPostEvent, kOhioState, w));
  CHECK(w.team_winner);
}

static void test_scoreboard_pre_nfl() {
  std::string json = slurp("fixtures/scoreboard_nfl.json");
  GameSnapshot s;
  CHECK(parse_scoreboard_str(json, kNflEvent, kPatriots, s));
  CHECK(s.state == GameState::PRE);
  CHECK_EQ(s.team_abbr, std::string("NE"));
  CHECK_EQ(s.opp_abbr, std::string("SEA"));
  CHECK_EQ(s.odds, std::string("SEA -3.5"));
  CHECK_EQ(s.over_under, std::string("44.5"));
  CHECK_EQ(s.tv, std::string("NBC"));
  CHECK_EQ(s.venue, std::string("Lumen Field"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-09-10T00:20Z"));
  CHECK_EQ(s.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nfl/500-dark/scoreboard/ne.png&w=64&h=64"));
  // The logo cache keys on the URL: the one built before the scoreboard
  // arrives must be the one the scoreboard gives, or it downloads twice.
  CHECK_EQ(team_logo_url(League::NFL, s.team_id, "NE"), s.team_logo);
  CHECK_EQ(team_logo_url(League::NFL, s.opp_id, s.opp_abbr.c_str()), s.opp_logo);
  CHECK_EQ(s.possession, 0);
}

static GameSnapshot live(int us, int them, GameState st = GameState::IN) {
  GameSnapshot s;
  s.valid = true;
  s.state = st;
  s.completed = st == GameState::POST;
  s.event_id = "1";
  s.team_abbr = "DAL";
  s.opp_abbr = "PHI";
  s.team_score = us;
  s.opp_score = them;
  s.team_color = "002a5c";
  s.opp_color = "004c54";
  return s;
}

// Live games' best-game pick and the upset alert (best_game.h).
static LiveGame lg(League l, int away, int home, int period, int clock, uint8_t away_rank = 99,
                   uint8_t home_rank = 99, const char *id = "1") {
  LiveGame g;
  g.league = (uint8_t) l;
  g.event_id = id;
  g.away_score = (int16_t) away;
  g.home_score = (int16_t) home;
  g.period = (uint8_t) period;
  g.clock_s = clock;
  g.away_rank = away_rank;
  g.home_rank = home_rank;
  return g;
}

static void test_best_game() {
  // The scan reads where each game stands.
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  LiveList games;
  CHECK(parse_live_games(json, games));
  const LiveGame *ore = nullptr;
  for (auto &g : games) {
    g.league = (uint8_t) League::NCAA;
    if (g.event_id == kLiveEvent)
      ore = &g;
  }
  CHECK(ore != nullptr);
  if (ore != nullptr) {
    CHECK_EQ((int) ore->away_score, 24);
    CHECK_EQ((int) ore->home_score, 24);
    CHECK_EQ((int) ore->period, 4);
    CHECK_EQ((int) ore->clock_s, 900);
    CHECK_EQ((int) ore->away_rank, 99);
    CHECK_EQ((int) ore->home_rank, 2);
  }
  // #2 Oregon tied with Boise State in the 4th beats the blowouts and the
  // closer games still in the 3rd.
  CHECK_EQ(games[best_live_game(games, "")].event_id, std::string(kLiveEvent));
  // The same holds with finals and pending asked for (the no-team scan).
  LiveList again;
  FinalList finals;
  PendingList pending;
  int64_t next = 0;
  CHECK(parse_live_games(json, again, &next, &finals, &pending));
  CHECK_EQ(again.size(), games.size());
  for (size_t i = 0; i < again.size() && i < games.size(); i++)
    CHECK_EQ((int) again[i].home_score, (int) games[i].home_score);

  CHECK_EQ(clock_seconds("8:27"), 507);
  CHECK_EQ(clock_seconds("12.3"), 12);
  CHECK_EQ(clock_seconds("65'"), 3900);
  CHECK_EQ(clock_seconds("90'+3'"), 5400);
  CHECK_EQ(clock_seconds(""), -1);
  CHECK_EQ(clock_seconds("99999999999:00"), -1);
  CHECK_EQ(clock_seconds("1:99999999999"), -1);
  // An over/under far past int's range prints as a decimal (the fuzzer's find).
  JsonDocument ou;
  deserializeJson(ou, "[44, 47.5, 2.14748e10]");
  CHECK_EQ(espn::detail::format_over_under(ou[0]), std::string("44"));
  CHECK_EQ(espn::detail::format_over_under(ou[1]), std::string("47.5"));
  CHECK(espn::detail::format_over_under(ou[2]).find('.') != std::string::npos);

  CHECK_EQ(game_progress(League::NCAA, 4, 900), 750);
  CHECK_EQ(game_progress(League::NFL, 1, 900), 0);
  CHECK_EQ(game_progress(League::NFL, 4, 0), 1000);
  CHECK_EQ(game_progress(League::NBA, 3, 507), 573);
  CHECK_EQ(game_progress(League::MCBB, 2, 600), 750);
  CHECK_EQ(game_progress(League::MLB, 7, 0), 777);
  CHECK_EQ(game_progress(League::EPL, 1, 1500), 277);
  CHECK_EQ(game_progress(League::NHL, 4, 300), 1000);
  CHECK_EQ(game_progress(League::NFL, 0, 0), 0);
  CHECK(in_overtime(League::NFL, 5));
  CHECK(!in_overtime(League::MCBB, 2));
  CHECK(in_overtime(League::MLB, 10));

  // Upset alert: the better-ranked side trails late.
  CHECK(upset_alert(lg(League::NCAA, 24, 21, 4, 600, 99, 2)));
  CHECK(!upset_alert(lg(League::NCAA, 21, 24, 4, 600, 99, 2)));  // the ranked side leads
  CHECK(!upset_alert(lg(League::NCAA, 24, 24, 4, 600, 99, 2)));  // tied
  CHECK(!upset_alert(lg(League::NCAA, 24, 21, 3, 600, 99, 2)));  // not late yet
  CHECK(upset_alert(lg(League::NCAA, 27, 24, 5, -1, 99, 2)));    // overtime
  CHECK(upset_alert(lg(League::NCAA, 17, 20, 4, 300, 3, 18)));   // #3 trails #18
  CHECK(!upset_alert(lg(League::NCAA, 20, 17, 4, 300, 3, 18)));
  CHECK(!upset_alert(lg(League::MCBB, 70, 60, 2, 660, 99, 5)));  // 11 minutes left
  CHECK(upset_alert(lg(League::MCBB, 70, 60, 2, 540, 99, 5)));   // 9 minutes left
  CHECK(!upset_alert(lg(League::NBA, 100, 90, 4, 60)));          // no ranks in the pros

  // Close beats lopsided, late beats early, overtime beats regulation.
  int tie_late = live_interest(lg(League::NFL, 21, 21, 4, 120));
  int tie_early = live_interest(lg(League::NFL, 7, 7, 1, 600));
  int blowout = live_interest(lg(League::NFL, 35, 3, 4, 120));
  int one_score = live_interest(lg(League::NFL, 17, 21, 4, 120));
  int ot = live_interest(lg(League::NFL, 24, 24, 5, 400));
  CHECK(tie_late > tie_early);
  CHECK(tie_late > one_score);
  CHECK(one_score > blowout);
  CHECK(ot > tie_late);
  CHECK_EQ(blowout, 0);
  // An AP rank and an upset add on.
  CHECK(live_interest(lg(League::NCAA, 14, 14, 2, 300, 99, 4)) > live_interest(lg(League::NCAA, 14, 14, 2, 300)));
  CHECK(live_interest(lg(League::NCAA, 28, 10, 4, 300, 99, 4)) > tie_late);  // #4 down 18 late
  CHECK(live_interest(lg(League::NHL, 2, 2, 3, 300)) > live_interest(lg(League::NHL, 4, 1, 3, 300)));
  CHECK(live_interest(lg(League::MLB, 3, 3, 9, 0)) > live_interest(lg(League::MLB, 3, 3, 3, 0)));

  // The game on the board keeps it against a near tie, not a clearly better one.
  LiveList two = {lg(League::NFL, 17, 21, 4, 120, 99, 99, "a"), lg(League::NFL, 21, 21, 4, 120, 99, 99, "b")};
  CHECK_EQ(two[best_live_game(two, "")].event_id, std::string("b"));
  CHECK_EQ(two[best_live_game(two, "a")].event_id, std::string("b"));  // 75 x 3 is well short of a tie's 300
  two[0] = lg(League::NFL, 21, 21, 4, 150, 99, 99, "a");
  CHECK_EQ(two[best_live_game(two, "a")].event_id, std::string("a"));
  // A game the board saw end goes last, even when the scan still lists it.
  two[0] = lg(League::NFL, 21, 21, 4, 0, 99, 99, "a");
  CHECK_EQ(two[best_live_game(two, "a", true)].event_id, std::string("b"));
  LiveList one = {two[0]};
  CHECK_EQ(best_live_game(one, "a", true), (size_t) 0);
  LiveList empty;
  CHECK_EQ(best_live_game(empty, ""), (size_t) 0);

  // Breaks in play: the only time the pick moves off the game on the board.
  GameSnapshot s = live(14, 10);
  for (const char *d : {"End of 3rd", "End 5th", "Mid 7th", "Halftime", "HT", "End of 1st Quarter"}) {
    s.short_detail = d;
    CHECK(at_break(s));
  }
  for (const char *d : {"8:27 - 3rd", "Top 7th", "Bot 3rd", "25'", "0:00 - 2nd", ""}) {
    s.short_detail = d;
    CHECK(!at_break(s));
  }
  CHECK(at_break(live(14, 10, GameState::POST)));
  CHECK(at_break(GameSnapshot{}));
  // Pick the best game's breaks: every break, and in soccer from 45' to the
  // half and from 90' on; the rest is play.
  GameSnapshot fut = live(1, 0);
  fut.sport = Sport::SOCCER;
  for (const char *d : {"45'", "45'+2'", "HT", "90'", "90'+4'", "105'", "120'+1'"}) {
    fut.short_detail = d;
    CHECK(pick_break(fut));
  }
  for (const char *d : {"1'", "25'", "44'", "46'", "89'", ""}) {
    fut.short_detail = d;
    CHECK(!pick_break(fut));
  }
  // Play is still on at 90', so Jump to the action's at_break says no.
  fut.short_detail = "90'+2'";
  CHECK(!at_break(fut));
  // The same minute in another sport is not a soccer clock.
  s.short_detail = "45'";
  CHECK(!pick_break(s));
  s.short_detail = "End of 3rd";
  CHECK(pick_break(s));

  // When Live games looks for another game. Switch every 1 minute.
  const uint32_t sw = 60 * 1000, m5 = 5 * 60 * 1000;
  CHECK(!rotate_due(30 * 1000, sw, false, false, false, false, false));  // random: the timer
  CHECK(rotate_due(sw, sw, false, false, false, false, false));
  CHECK(!rotate_due(sw, sw, false, false, true, false, false));  // best: mid-play, no
  CHECK(rotate_due(sw, sw, false, false, true, true, false));    // at a break
  // A look at this break kept the game: no second scan until play resumes,
  // however long the intermission.
  CHECK(!rotate_due(18 * sw, sw, false, false, true, true, true));
  CHECK(rotate_due(sw, sw, false, true, true, false, true));  // Alternate: the plain timer
  // Stick with a favorite up: a slow look, at least every 5 minutes.
  CHECK(!rotate_due(4 * sw, sw, true, true, true, true, false));
  CHECK(rotate_due(m5, sw, true, true, true, false, false));
  CHECK(!rotate_due(m5, 10 * sw, true, true, false, false, false));  // or Switch every when longer
  CHECK(rotate_due(10 * sw, 10 * sw, true, true, false, false, false));

  // A poll of the followed game, the away side as team, checked for an upset.
  GameSnapshot p = live(24, 21);
  p.period = 4;
  p.display_clock = "4:12";
  LiveGame v = live_view(p, (uint8_t) League::NCAA, 99, 2);
  CHECK_EQ((int) v.clock_s, 252);
  CHECK_EQ((int) v.away_score, 24);
  CHECK(upset_alert(v));
  CHECK(!upset_alert(live_view(p, (uint8_t) League::NCAA, 2, 99)));
}

// Home Assistant's score and final events (ha_events.h). The scores and finals of a tracker's events, for the tests that predate
// the other kinds.
static std::vector<HaEvent> sf(std::vector<HaEvent> v) {
  std::vector<HaEvent> out;
  for (auto &e : v)
    if (e.kind == HaKind::SCORE || e.kind == HaKind::FINAL)
      out.push_back(e);
  return out;
}

static void test_ha_events() {
  HaTracker t;
  // The first read of a game fires nothing, a repeat of it nothing either.
  CHECK(sf(t.update(live(7, 0), false)).empty());
  CHECK(sf(t.update(live(7, 0), false)).empty());
  // A field goal for us, then a touchdown for them.
  auto e = sf(t.update(live(10, 0), false));
  CHECK_EQ(e.size(), (size_t) 1);
  CHECK_EQ(e[0].team, std::string("DAL"));
  CHECK_EQ(e[0].opponent, std::string("PHI"));
  CHECK_EQ(e[0].play, std::string("field_goal"));
  CHECK_EQ(e[0].points, 3);
  CHECK_EQ(e[0].team_score, 10);
  CHECK_EQ(e[0].opponent_score, 0);
  CHECK_EQ(e[0].sport, std::string("football"));
  CHECK(e[0].my_team);
  e = sf(t.update(live(10, 7), false));
  CHECK_EQ(e.size(), (size_t) 1);
  CHECK_EQ(e[0].team, std::string("PHI"));
  CHECK_EQ(e[0].play, std::string("touchdown"));
  CHECK_EQ(e[0].team_score, 7);
  CHECK_EQ(e[0].opponent_score, 10);
  CHECK(!e[0].my_team);
  // Both sides between two reads, ours first; a score taken back fires nothing.
  e = sf(t.update(live(12, 8), false));
  CHECK_EQ(e.size(), (size_t) 2);
  CHECK_EQ(e[0].play, std::string("two_point"));
  CHECK_EQ(e[1].play, std::string("extra_point"));
  // (It holds kHaStepBackHoldS before it counts: an old copy of the
  // document would go back the same way, ha_events_stale below.)
  HaContext later;
  CHECK(t.update(live(10, 8), later).empty());
  later.mono_s = kHaStepBackHoldS;
  CHECK(t.update(live(10, 8), later).empty());
  // A score on the last play comes with the final.
  e = sf(t.update(live(13, 8, GameState::POST), false));
  CHECK_EQ(e.size(), (size_t) 2);
  CHECK_EQ(e[0].play, std::string("field_goal"));
  CHECK_EQ(e[1].play, std::string("final"));
  CHECK_EQ(e[1].team, std::string("DAL"));
  CHECK_EQ(e[1].result, std::string("win"));
  CHECK_EQ(e[1].winner, std::string("DAL"));
  CHECK(e[1].my_team);
  CHECK(sf(t.update(live(13, 8, GameState::POST), false)).empty());  // once

  // A suspended game goes to post without completed: no final.
  HaTracker s;
  sf(s.update(live(3, 0), false));
  GameSnapshot sus = live(3, 0, GameState::POST);
  sus.completed = false;
  CHECK(sf(s.update(sus, false)).empty());
  // A game first seen as a final, or before kickoff, fires nothing.
  HaTracker f;
  CHECK(sf(f.update(live(20, 3, GameState::POST), false)).empty());
  HaTracker pre;
  CHECK(sf(pre.update(live(0, 0, GameState::PRE), false)).empty());
  CHECK(sf(pre.update(live(0, 0), false)).empty());

  // The Live modes: nobody's team. The final names the left side.
  HaTracker n;
  sf(n.update(live(0, 0), true));
  e = sf(n.update(live(6, 0), true));
  CHECK_EQ(e.size(), (size_t) 1);
  CHECK(!e[0].my_team);
  e = sf(n.update(live(6, 9, GameState::POST), true));
  CHECK_EQ(e.size(), (size_t) 2);
  CHECK_EQ(e[1].result, std::string("loss"));
  CHECK_EQ(e[1].winner, std::string("PHI"));
  CHECK(!e[1].my_team);

  // Two games alternating on the board each keep their own last read.
  HaTracker a;
  GameSnapshot g2 = live(0, 0);
  g2.event_id = "2";
  g2.team_abbr = "KC";
  g2.opp_abbr = "BUF";
  sf(a.update(live(0, 0), false));
  sf(a.update(g2, false));
  CHECK_EQ(sf(a.update(live(7, 0), false)).size(), (size_t) 1);
  g2.opp_score = 3;
  e = sf(a.update(g2, false));
  CHECK_EQ(e.size(), (size_t) 1);
  CHECK_EQ(e[0].team, std::string("BUF"));
  // The same game read from the other side doesn't fire.
  GameSnapshot flip = live(0, 7);
  flip.team_abbr = "PHI";
  flip.opp_abbr = "DAL";
  CHECK(sf(a.update(flip, false)).empty());

  // The game followed least recently is the one forgotten: game 2, read
  // again in the middle of twelve other games, is still known; game 1 isn't.
  for (int i = 3; i <= 8; i++) {
    GameSnapshot o = live(0, 0);
    o.event_id = std::to_string(i);
    sf(a.update(o, false));
  }
  sf(a.update(g2, false));
  for (int i = 9; i <= 14; i++) {
    GameSnapshot o = live(0, 0);
    o.event_id = std::to_string(i);
    sf(a.update(o, false));
  }
  g2.opp_score = 6;
  CHECK_EQ(sf(a.update(g2, false)).size(), (size_t) 1);
  CHECK(sf(a.update(live(14, 0), false)).empty());  // a first read again

  // Other sports' plays.
  HaTracker b;
  GameSnapshot mlb = live(1, 0);
  mlb.sport = Sport::BASEBALL;
  sf(b.update(mlb, false));
  mlb.team_score = 3;
  mlb.mlb.play_type = "Home Run";
  e = sf(b.update(mlb, false));
  CHECK_EQ(e.size(), (size_t) 1);
  CHECK_EQ(e[0].play, std::string("home_run"));
  CHECK_EQ(e[0].points, 2);
  CHECK_EQ(e[0].sport, std::string("baseball"));
  mlb.mlb.play_type = "Single";
  mlb.last_play = "Smith singled to left, Jones scored.";
  mlb.opp_score = 1;
  CHECK_EQ(sf(b.update(mlb, false))[0].play, std::string("run"));
  HaTracker nhl;
  GameSnapshot h = live(0, 0);
  h.sport = Sport::HOCKEY;
  sf(nhl.update(h, false));
  h.opp_score = 1;
  CHECK_EQ(sf(nhl.update(h, false))[0].play, std::string("goal"));
  HaTracker nba;
  GameSnapshot bb = live(50, 48);
  bb.sport = Sport::BASKETBALL;
  sf(nba.update(bb, false));
  bb.team_score = 53;
  CHECK_EQ(sf(nba.update(bb, false))[0].play, std::string("score"));
  // A level soccer game decided on penalties.
  HaTracker soc;
  GameSnapshot fc = live(1, 1);
  fc.sport = Sport::SOCCER;
  sf(soc.update(fc, false));
  fc.state = GameState::POST;
  fc.completed = true;
  fc.soc.team_shootout = 3;
  fc.soc.opp_shootout = 4;
  e = sf(soc.update(fc, false));
  CHECK_EQ(e.size(), (size_t) 1);
  CHECK_EQ(e[0].result, std::string("loss"));
  CHECK_EQ(e[0].winner, std::string("PHI"));
  fc.soc.team_shootout = fc.soc.opp_shootout = -1;
  HaTracker draw;
  fc.state = GameState::IN;
  sf(draw.update(fc, false));
  fc.state = GameState::POST;
  e = sf(draw.update(fc, false));
  CHECK_EQ(e[0].result, std::string("draw"));
  CHECK_EQ(e[0].winner, std::string(""));
}

// The kinds of a tracker's events, in order: "start,score,lead_change".
static std::string kinds(const std::vector<HaEvent> &v) {
  std::string out;
  for (const auto &e : v)
    out += (out.empty() ? "" : ",") + std::string(ha_kind_name(e.kind));
  return out;
}

static HaContext ha_ctx(uint32_t mono = 0) {
  HaContext c;
  c.league = "nfl";
  c.mono_s = mono;
  return c;
}

// The start, a change of lead and the end of each period (ha_events.h).
static void test_ha_game_events() {
  HaTracker t;
  HaContext c = ha_ctx();
  // Seen before the start, then under way: the start, with the colors.
  CHECK(t.update(live(0, 0, GameState::PRE), c).empty());
  CHECK(t.update(live(0, 0, GameState::PRE), c).empty());
  GameSnapshot g = live(0, 0);
  g.period = 1;
  g.short_detail = "15:00 - 1st";
  auto e = t.update(g, c);
  CHECK_EQ(kinds(e), std::string("start"));
  CHECK_EQ(e[0].team, std::string("DAL"));
  CHECK_EQ(e[0].league, std::string("nfl"));
  CHECK_EQ(e[0].team_rgb, (uint32_t) 0x002a5c);
  CHECK_EQ(e[0].opponent_rgb, (uint32_t) 0x004c54);
  CHECK(e[0].my_team);
  CHECK(t.update(g, c).empty());  // once
  // The first score takes the lead; the other side's goes ahead; a tie.
  g.team_score = 7;
  e = t.update(g, c);
  CHECK_EQ(kinds(e), std::string("score,lead_change"));
  CHECK_EQ(e[1].change, std::string("took_lead"));
  CHECK_EQ(e[1].leader, std::string("DAL"));
  CHECK_EQ(e[1].previous_leader, std::string(""));
  CHECK_EQ(e[1].team, std::string("DAL"));
  CHECK(e[1].my_team);
  g.opp_score = 10;
  e = t.update(g, c);
  CHECK_EQ(kinds(e), std::string("score,lead_change"));
  CHECK_EQ(e[1].change, std::string("lead_change"));
  CHECK_EQ(e[1].leader, std::string("PHI"));
  CHECK_EQ(e[1].previous_leader, std::string("DAL"));
  CHECK_EQ(e[1].team, std::string("PHI"));
  CHECK_EQ(e[1].team_score, 10);
  CHECK_EQ(e[1].opponent_score, 7);
  CHECK(!e[1].my_team);
  g.team_score = 10;
  e = t.update(g, c);
  CHECK_EQ(kinds(e), std::string("score,lead_change"));
  CHECK_EQ(e[1].change, std::string("tied"));
  CHECK_EQ(e[1].leader, std::string(""));
  CHECK_EQ(e[1].team, std::string("DAL"));
  // A score that keeps the lead where it was has no lead event.
  g.team_score = 17;
  CHECK_EQ(kinds(t.update(g, c)), std::string("score,lead_change"));
  g.team_score = 20;
  CHECK_EQ(kinds(t.update(g, c)), std::string("score"));

  // The break after a period, once; the next period has nothing more.
  g.short_detail = "End of 1st";
  e = t.update(g, c);
  CHECK_EQ(kinds(e), std::string("period"));
  CHECK_EQ(e[0].period, 1);
  CHECK_EQ(e[0].period_name, std::string("1st quarter"));
  CHECK(t.update(g, c).empty());
  g.period = 2;
  g.short_detail = "14:55 - 2nd";
  CHECK(t.update(g, c).empty());
  g.short_detail = "Halftime";
  e = t.update(g, c);
  CHECK_EQ(kinds(e), std::string("period"));
  CHECK_EQ(e[0].period_name, std::string("2nd quarter"));
  g.period = 3;
  g.short_detail = "15:00 - 3rd";
  CHECK(t.update(g, c).empty());
  // A break the panel missed: the next period under way ends the last one.
  g.period = 4;
  g.short_detail = "11:20 - 4th";
  e = t.update(g, c);
  CHECK_EQ(kinds(e), std::string("period"));
  CHECK_EQ(e[0].period, 3);
  g.period = 5;
  g.short_detail = "10:00 - OT";
  CHECK_EQ(t.update(g, c)[0].period_name, std::string("4th quarter"));
  // Two breaks missed in one gap: both periods, in order.
  HaTracker gap;
  GameSnapshot q1 = live(3, 0);
  q1.period = 1;
  q1.short_detail = "5:00 - 1st";
  gap.update(q1, c);
  q1.period = 3;
  q1.short_detail = "12:00 - 3rd";
  e = gap.update(q1, c);
  CHECK_EQ(kinds(e), std::string("period,period"));
  CHECK_EQ(e[0].period, 1);
  CHECK_EQ(e[1].period, 2);
  CHECK_EQ(e[1].period_name, std::string("2nd quarter"));
  CHECK(gap.update(q1, c).empty());
  // The final ends the last period itself.
  g.state = GameState::POST;
  g.completed = true;
  CHECK_EQ(kinds(t.update(g, c)), std::string("final"));

  // The words for each sport's periods.
  GameSnapshot w = live(0, 0);
  CHECK_EQ(ha_period_name(w, 5), std::string("overtime"));
  CHECK_EQ(ha_period_name(w, 6), std::string("2nd overtime"));
  w.sport = Sport::HOCKEY;
  CHECK_EQ(ha_period_name(w, 3), std::string("3rd period"));
  CHECK_EQ(ha_period_name(w, 4), std::string("overtime"));
  w.sport = Sport::SOCCER;
  CHECK_EQ(ha_period_name(w, 1), std::string("1st half"));
  w.sport = Sport::BASKETBALL;
  w.nba.regulation = 2;
  CHECK_EQ(ha_period_name(w, 2), std::string("2nd half"));
  w.nba.regulation = 4;
  CHECK_EQ(ha_period_name(w, 2), std::string("2nd quarter"));
  w.sport = Sport::BASEBALL;
  CHECK_EQ(ha_period_name(w, 11), std::string("11th inning"));
  CHECK_EQ(ha_period_name(w, 12), std::string("12th inning"));
  CHECK_EQ(ha_period_name(w, 22), std::string("22nd inning"));

  // Each sport's breaks.
  HaTracker h;
  GameSnapshot hk = live(1, 0);
  hk.sport = Sport::HOCKEY;
  hk.period = 1;
  h.update(hk, c);
  hk.short_detail = "End of 1st";
  CHECK_EQ(h.update(hk, c)[0].period_name, std::string("1st period"));
  HaTracker bk;
  GameSnapshot nb = live(40, 38);
  nb.sport = Sport::BASKETBALL;
  nb.nba.regulation = 2;
  nb.period = 1;
  bk.update(nb, c);
  nb.nba.phase = Phase::HALFTIME;
  CHECK_EQ(bk.update(nb, c)[0].period_name, std::string("1st half"));
  HaTracker mb;
  GameSnapshot ml = live(2, 1);
  ml.sport = Sport::BASEBALL;
  ml.period = 3;
  ml.mlb.half = Half::BOTTOM;
  mb.update(ml, c);
  ml.mlb.half = Half::MID;
  CHECK(mb.update(ml, c).empty());  // the middle of an inning isn't its end
  ml.mlb.half = Half::END;
  CHECK_EQ(mb.update(ml, c)[0].period_name, std::string("3rd inning"));
  ml.period = 4;
  ml.mlb.half = Half::TOP;
  CHECK(mb.update(ml, c).empty());
  HaTracker sc;
  GameSnapshot fc = live(0, 1);
  fc.sport = Sport::SOCCER;
  fc.period = 1;
  sc.update(fc, c);
  fc.soc.status = "STATUS_HALFTIME";
  CHECK_EQ(sc.update(fc, c)[0].period_name, std::string("1st half"));

  // A restart at halftime: that break is already over as far as the
  // events go, and the 3rd quarter's start brings nothing.
  HaTracker r;
  GameSnapshot half = live(10, 7);
  half.period = 2;
  half.short_detail = "Halftime";
  CHECK(r.update(half, c).empty());
  CHECK(r.update(half, c).empty());
  half.period = 3;
  half.short_detail = "15:00 - 3rd";
  CHECK(r.update(half, c).empty());

  // Live games: nobody's team, so my_team is false all through.
  HaTracker n;
  HaContext nc = ha_ctx();
  nc.neutral = true;
  n.update(live(0, 0, GameState::PRE), nc);
  e = n.update(live(3, 0), nc);
  CHECK_EQ(kinds(e), std::string("start,score,lead_change"));
  for (const auto &x : e)
    CHECK(!x.my_team);
}

// The reminder before the start goes by the clock.
static void test_ha_pregame() {
  const int64_t kick = 1790000000;
  GameSnapshot p = live(0, 0, GameState::PRE);
  p.kickoff_epoch = kick;
  HaContext c = ha_ctx();
  c.pregame_minutes = 15;
  c.now_epoch = kick - 3600;
  HaTracker t;
  CHECK(t.update(p, c).empty());
  c.now_epoch = kick - 16 * 60;
  CHECK(t.update(p, c).empty());
  c.now_epoch = kick - 15 * 60;
  auto e = t.update(p, c);
  CHECK_EQ(kinds(e), std::string("pregame"));
  CHECK_EQ(e[0].minutes, 15);
  CHECK(e[0].my_team);
  c.now_epoch = kick - 14 * 60;
  CHECK(t.update(p, c).empty());  // once
  // A panel that starts inside the window has missed it: nothing, even
  // though the reminder never went out (it may have, before the restart).
  HaTracker r;
  c.now_epoch = kick - 10 * 60;
  CHECK(r.update(p, c).empty());
  c.now_epoch = kick - 9 * 60;
  CHECK(r.update(p, c).empty());
  // No time yet, no clock, a reminder of 0 or a kickoff to be decided: none.
  HaTracker z;
  c.now_epoch = kick - 3600;
  z.update(p, c);
  c.pregame_minutes = 0;
  c.now_epoch = kick - 60;
  CHECK(z.update(p, c).empty());
  HaTracker tbd;
  GameSnapshot q = p;
  q.time_tbd = true;
  c.pregame_minutes = 15;
  c.now_epoch = kick - 3600;
  tbd.update(q, c);
  c.now_epoch = kick - 60;
  CHECK(tbd.update(q, c).empty());
  HaTracker clock;
  c.now_epoch = 0;
  clock.update(p, c);
  CHECK(clock.update(p, c).empty());
  // A restart inside the window with the clock arriving on the second tick:
  // still nothing, the decision waited for the clock.
  HaTracker late;
  c.now_epoch = 0;
  CHECK(late.update(p, c).empty());
  c.now_epoch = kick - 10 * 60;
  CHECK(late.update(p, c).empty());
  c.now_epoch = kick - 9 * 60;
  CHECK(late.update(p, c).empty());
  // The clock arriving an hour out leaves the reminder to come.
  HaTracker early;
  c.now_epoch = 0;
  CHECK(early.update(p, c).empty());
  c.now_epoch = kick - 3600;
  CHECK(early.update(p, c).empty());
  c.now_epoch = kick - 15 * 60;
  CHECK_EQ(kinds(early.update(p, c)), std::string("pregame"));
  // A rounded-up minute: 14 min 10 s to go reads 15.
  HaTracker up;
  c.now_epoch = kick - 3600;
  up.update(p, c);
  c.now_epoch = kick - 14 * 60 - 10;
  CHECK_EQ(up.update(p, c)[0].minutes, 15);
}

// Old copies of the document from ESPN's cache never repeat an event.
static void test_ha_events_stale() {
  HaTracker t;
  GameSnapshot g = live(14, 10);
  g.period = 3;
  t.update(g, ha_ctx(100));
  GameSnapshot old = live(7, 10);
  old.period = 2;
  CHECK(t.update(old, ha_ctx(110)).empty());
  CHECK(t.update(g, ha_ctx(120)).empty());  // back to where it was: nothing again
  g.team_score = 17;
  auto e = t.update(g, ha_ctx(130));
  CHECK_EQ(kinds(e), std::string("score"));
  CHECK_EQ(e[0].points, 3);
  // A final, then an old live copy, then the final again: one final.
  g.state = GameState::POST;
  g.completed = true;
  CHECK_EQ(kinds(t.update(g, ha_ctx(140))), std::string("final"));
  GameSnapshot live_copy = g;
  live_copy.state = GameState::IN;
  live_copy.completed = false;
  CHECK(t.update(live_copy, ha_ctx(150)).empty());
  CHECK(t.update(g, ha_ctx(160)).empty());
  // A correction that holds becomes the last read, without an event, and
  // the score after it counts from there.
  HaTracker c;
  GameSnapshot r = live(7, 0);
  c.update(r, ha_ctx(0));
  r.team_score = 0;
  CHECK(c.update(r, ha_ctx(10)).empty());
  CHECK(c.update(r, ha_ctx(10 + kHaStepBackHoldS - 1)).empty());
  CHECK(c.update(r, ha_ctx(10 + kHaStepBackHoldS)).empty());
  r.team_score = 3;
  e = c.update(r, ha_ctx(200));
  CHECK_EQ(kinds(e), std::string("score,lead_change"));
  CHECK_EQ(e[0].points, 3);
  CHECK_EQ(e[0].play, std::string("field_goal"));
  // A second step back right after one held waits its own 90 s.
  r.team_score = 0;
  CHECK(c.update(r, ha_ctx(210)).empty());
  r.team_score = 3;
  CHECK(c.update(r, ha_ctx(220)).empty());  // not taken: the score stayed 3
  r.team_score = 6;
  CHECK_EQ(c.update(r, ha_ctx(230))[0].points, 3);
  // A step back that comes and goes starts its wait again next time.
  HaTracker w;
  GameSnapshot s = live(7, 0);
  w.update(s, ha_ctx(0));
  s.team_score = 0;
  w.update(s, ha_ctx(10));
  s.team_score = 7;
  w.update(s, ha_ctx(20));
  s.team_score = 0;
  w.update(s, ha_ctx(10 + kHaStepBackHoldS));
  s.team_score = 7;
  CHECK(w.update(s, ha_ctx(11 + kHaStepBackHoldS)).empty());
}

// The fields Home Assistant and the webhook get, and the sensors' words.
static void test_ha_fields() {
  HaTracker t;
  HaContext c = ha_ctx();
  t.update(live(0, 0), c);
  auto e = t.update(live(7, 0), c);
  HaFields f = ha_event_fields(e[0]);
  std::string keys;
  for (const auto &kv : f)
    keys += std::string(kv.first) + "=" + kv.second + ";";
  CHECK_EQ(keys, std::string("kind=score;league=nfl;sport=football;team=DAL;opponent=PHI;team_score=7;"
                             "opponent_score=0;my_team=true;team_color=#002A5C;team_rgb=0,42,92;"
                             "opponent_color=#004C54;opponent_rgb=0,76,84;game_id=1;play=touchdown;points=7;"));
  f = ha_event_fields(e[1]);
  CHECK_EQ(std::string(f.back().first), std::string("previous_leader"));
  CHECK_EQ(ha_webhook_json(e[0], "gameday-\"x\""),
           std::string("{\"event\":\"gameday_score\",\"panel\":\"gameday-\\\"x\\\"\",\"kind\":\"score\","
                       "\"league\":\"nfl\",\"sport\":\"football\",\"team\":\"DAL\",\"opponent\":\"PHI\","
                       "\"team_score\":\"7\",\"opponent_score\":\"0\",\"my_team\":\"true\","
                       "\"team_color\":\"#002A5C\",\"team_rgb\":\"0,42,92\",\"opponent_color\":\"#004C54\","
                       "\"opponent_rgb\":\"0,76,84\",\"game_id\":\"1\",\"play\":\"touchdown\",\"points\":\"7\"}"));
  CHECK_EQ(ha_kind_name(HaKind::TEST), std::string("test"));
  CHECK_EQ(ha_rgb("#FFb612"), (uint32_t) 0xFFB612);
  CHECK_EQ(ha_rgb(""), (uint32_t) 0xFFFFFF);
  CHECK_EQ(ha_rgb("12345"), (uint32_t) 0xFFFFFF);
  CHECK_EQ(ha_rgb("zz0000"), (uint32_t) 0xFFFFFF);
  std::string esc;
  ha_json_escape(esc, std::string("a\\b\n"));
  CHECK_EQ(esc, std::string("a\\\\b\\u000a"));

  // The Period and Game Clock sensors.
  GameSnapshot g = live(3, 0);
  g.period = 2;
  g.display_clock = "4:32";
  g.short_detail = "4:32 - 2nd";
  CHECK_EQ(ha_period_text(g), std::string("2nd quarter"));
  CHECK_EQ(ha_clock_text(g), std::string("4:32"));
  g.short_detail = "Halftime";
  CHECK_EQ(ha_period_text(g), std::string("Halftime"));
  CHECK_EQ(ha_clock_text(g), std::string(""));
  g.state = GameState::POST;
  g.completed = true;
  CHECK_EQ(ha_period_text(g), std::string("Final"));
  CHECK_EQ(ha_clock_text(g), std::string(""));
  g.state = GameState::PRE;
  CHECK_EQ(ha_period_text(g), std::string(""));
  g = live(1, 0);
  g.sport = Sport::BASEBALL;
  g.period = 5;
  g.mlb.half = Half::TOP;
  CHECK_EQ(ha_period_text(g), std::string("Top of the 5th"));
  CHECK_EQ(ha_clock_text(g), std::string(""));
  g.sport = Sport::HOCKEY;
  g.period = 1;
  g.short_detail = "End of 1st";
  CHECK_EQ(ha_period_text(g), std::string("End of 1st"));

  // The webhook's address and the reminder's lead.
  CHECK(hook_url_ok(""));
  CHECK(hook_url_ok("http://192.168.1.20:1880/gameday"));
  CHECK(hook_url_ok("http://homeassistant.local:8123/api/webhook/gameday-abc"));
  CHECK(!hook_url_ok("https://example.com/hook"));
  CHECK(!hook_url_ok("http://"));
  CHECK(!hook_url_ok("http:///path"));
  CHECK(!hook_url_ok("ftp://192.168.1.20/"));
  CHECK(!hook_url_ok("192.168.1.20/hook"));
  CHECK(!hook_url_ok("http://exa mple.com/"));
  CHECK(!hook_url_ok("http://example.com/\xC3\xA9"));
  CHECK(!hook_url_ok("http://" + std::string(kHookUrlMax, 'a')));
  // A .local name is looked up over mDNS, by its name alone; the port stays
  // out of it.
  CHECK_EQ(hook_mdns_name("http://homeassistant.local:8123/api/webhook/x"), std::string("homeassistant"));
  CHECK_EQ(hook_mdns_name("http://HomeAssistant.LOCAL/"), std::string("HomeAssistant"));
  CHECK_EQ(hook_mdns_name("http://192.168.1.20:1880/gameday"), std::string(""));
  CHECK_EQ(hook_mdns_name("http://nas.home.arpa/hook"), std::string(""));
  CHECK_EQ(hook_mdns_name("http://.local/"), std::string(""));
  size_t hb = 0, he = 0;
  CHECK(hook_host_span("http://homeassistant.local:8123/api", hb, he));
  CHECK_EQ(hb, (size_t) 7);
  CHECK_EQ(he, (size_t) 26);
  CHECK(hook_host_span("http://192.168.1.20", hb, he));
  CHECK_EQ(he, (size_t) 19);
  CHECK(!hook_host_span("http:///x", hb, he));
  CHECK_EQ(clamp_pregame_minutes(-5), 0);
  CHECK_EQ(clamp_pregame_minutes(30), 30);
  CHECK_EQ(clamp_pregame_minutes(500), kPregameMax);
}

static void test_splash() {
  CHECK_EQ(decide_splash(live(0, 0), live(6, 0), true).text, std::string("TOUCHDOWN!"));
  CHECK_EQ(decide_splash(live(0, 0), live(7, 0), true).text, std::string("TOUCHDOWN!"));
  CHECK_EQ(decide_splash(live(0, 0), live(8, 0), true).text, std::string("TOUCHDOWN!"));
  CHECK_EQ(decide_splash(live(7, 0), live(10, 0), true).text, std::string("FIELD GOAL!"));
  CHECK_EQ(decide_splash(live(6, 0), live(7, 0), true).text, std::string("EXTRA POINT!"));
  CHECK_EQ(decide_splash(live(6, 0), live(8, 0), true).text, std::string("2-POINT!"));
  CHECK_EQ(decide_splash(live(0, 0), live(5, 0), true).text, std::string("SCORE!"));
  CHECK_EQ(decide_splash(live(0, 0), live(6, 0), true).color, (uint32_t) 0x002a5c);
  // Opponent
  CHECK_EQ(decide_splash(live(0, 0), live(0, 6), true).text, std::string("PHI TOUCHDOWN"));
  CHECK_EQ(decide_splash(live(0, 0), live(0, 3), true).text, std::string("PHI FIELD GOAL"));
  CHECK_EQ(decide_splash(live(0, 6), live(0, 7), true).text, std::string("PHI EXTRA POINT"));
  CHECK_EQ(decide_splash(live(0, 6), live(0, 8), true).text, std::string("PHI 2-POINT"));
  CHECK_EQ(decide_splash(live(0, 0), live(0, 6), true).color, (uint32_t) 0x004c54);
  CHECK_EQ(decide_splash(live(0, 0), live(0, 6), false).text, std::string(""));
  // Both scored between polls: ours wins
  CHECK_EQ(decide_splash(live(0, 0), live(7, 3), true).text, std::string("TOUCHDOWN!"));
  // No splash cases
  CHECK_EQ(decide_splash(GameSnapshot{}, live(7, 0), true).text, std::string(""));  // boot mid-game
  CHECK_EQ(decide_splash(live(0, 0, GameState::PRE), live(7, 0), true).text, std::string(""));
  CHECK_EQ(decide_splash(live(7, 0), live(7, 0), true).text, std::string(""));
  CHECK_EQ(decide_splash(live(7, 0), live(0, 0), true).text, std::string(""));  // correction
  GameSnapshot other = live(0, 0);
  other.event_id = "2";
  CHECK_EQ(decide_splash(other, live(7, 0), true).text, std::string(""));
  // Victory
  CHECK_EQ(decide_splash(live(21, 20), live(21, 20, GameState::POST), true).text, std::string("DAL WINS!"));
  CHECK_EQ(decide_splash(live(20, 21), live(20, 21, GameState::POST), true).text, std::string(""));
  CHECK_EQ(decide_splash(live(21, 20, GameState::POST), live(21, 20, GameState::POST), true).text, std::string(""));
  // Suspended or canceled mid-game: post, but no winner
  GameSnapshot halted = live(21, 20, GameState::POST);
  halted.completed = false;
  halted.short_detail = "Suspended";
  CHECK_EQ(decide_splash(live(21, 20), halted, true).text, std::string(""));
  CHECK_EQ(decide_splash(live(20, 21), halted, true, true).text, std::string(""));
}

static void test_status_text() {
  TickerOptions all;
  GameSnapshot s = live(24, 24);
  s.down_distance = "3rd & 10 at BOIS 25";
  s.short_detail = "15:00 - 4th";
  s.last_play = "End of 3rd quarter.";
  CHECK_EQ(status_text(s, all, ""), std::string("3rd & 10 at BOIS 25 | 15:00 - 4th | End of 3rd quarter."));
  TickerOptions no_clock = all;
  no_clock.clock = false;
  CHECK_EQ(status_text(s, no_clock, ""), std::string("3rd & 10 at BOIS 25 | End of 3rd quarter."));
  TickerOptions none;
  none.clock = none.down_distance = none.last_play = false;
  CHECK_EQ(status_text(s, none, ""), std::string("15:00 - 4th"));
  // Baseball's and soccer's switches don't touch football's.
  TickerOptions others_off = all;
  others_off.baseball_count = others_off.soccer_stats = false;
  CHECK_EQ(status_text(s, others_off, ""), status_text(s, all, ""));
  // Long play is truncated
  s.last_play = std::string(200, 'x');
  TickerOptions only_play;
  only_play.clock = only_play.down_distance = false;
  std::string t = status_text(s, only_play, "");
  CHECK_EQ(t.size(), (size_t) 160);
  CHECK(t.substr(157) == "...");
  // A scoring play's drive follows Last play; the win chance follows Odds.
  GameSnapshot sc = live(10, 7);
  sc.last_play = "G.Spetic field goal GOOD";
  sc.drive = "12 plays, 50 yards, 5:48";
  sc.team_win_pct = 72;
  CHECK_EQ(status_text(sc, only_play, ""),
           std::string("G.Spetic field goal GOOD | Drive: 12 plays, 50 yards, 5:48 | DAL 72% to win"));
  TickerOptions play_no_odds = only_play;
  play_no_odds.odds = false;
  CHECK_EQ(status_text(sc, play_no_odds, ""), std::string("G.Spetic field goal GOOD | Drive: 12 plays, 50 yards, 5:48"));
  TickerOptions odds_only = only_play;
  odds_only.last_play = false;
  CHECK_EQ(status_text(sc, odds_only, ""), std::string("DAL 72% to win"));
  sc.team_win_pct = 31;  // the other side is ahead in the model
  CHECK_EQ(win_chance(sc), std::string("PHI 69% to win"));
  sc.team_win_pct = 50;
  CHECK_EQ(win_chance(sc), std::string("DAL 50% to win"));
  sc.team_win_pct = -1;
  CHECK_EQ(status_text(sc, odds_only, ""), std::string("In Progress"));
  // Halftime
  GameSnapshot h = live(14, 7);
  h.short_detail = "Halftime";
  h.team_record = "2-0";
  h.opp_record = "1-1";
  CHECK_EQ(status_text(h, all, ""), std::string("Halftime | DAL 2-0 | PHI 1-1"));
  // Pre
  GameSnapshot p = live(0, 0, GameState::PRE);
  p.odds = "SEA -3.5";
  p.over_under = "44.5";
  p.tv = "NBC";
  p.venue = "Lumen Field";
  CHECK_EQ(status_text(p, all, "Wed 7:20 PM"), std::string("Wed 7:20 PM | SEA -3.5 | O/U 44.5 | NBC | Lumen Field"));
  TickerOptions no_odds = all;
  no_odds.odds = false;
  CHECK_EQ(status_text(p, no_odds, "Wed 7:20 PM"), std::string("Wed 7:20 PM | Lumen Field"));
  // A named game leads, and a venue that repeats the name is dropped
  p.game_name = "Super Bowl LX";
  CHECK_EQ(status_text(p, no_odds, "Sun 5:30 PM"), std::string("Super Bowl LX | Sun 5:30 PM | Lumen Field"));
  p.game_name = p.venue = "Rose Bowl";
  CHECK_EQ(status_text(p, no_odds, "Thu 3:00 PM"), std::string("Rose Bowl | Thu 3:00 PM"));
  // The playoff round leads the other sports' pre-game tickers too
  for (Sport sport : {Sport::BASEBALL, Sport::BASKETBALL, Sport::HOCKEY}) {
    GameSnapshot r;
    r.valid = true;
    r.sport = sport;
    r.state = GameState::PRE;
    r.team_abbr = "LAD";
    r.opp_abbr = "NYM";
    r.venue = "Dodger Stadium";
    r.game_name = "NLCS - Game 2";
    CHECK_EQ(status_text(r, no_odds, "Tue 7:08 PM"), std::string("NLCS - Game 2 | Tue 7:08 PM | Dodger Stadium"));
    r.game_name.clear();
    CHECK_EQ(status_text(r, no_odds, "Tue 7:08 PM"), std::string("Tue 7:08 PM | Dodger Stadium"));
  }
  // Leader lines: last name, yards with a comma, touchdowns when there are any
  CHECK_EQ(espn::detail::leader_line("D. Prescott", "129/192, 1381 YDS, 9 TD, 3 INT"), std::string("Prescott 1,381 yds 9 TD"));
  CHECK_EQ(espn::detail::leader_line("M. Penix Jr.", "52/77, 719 YDS, 3 TD, 1 INT"), std::string("Penix Jr. 719 yds 3 TD"));
  CHECK_EQ(espn::detail::leader_line("B. Robinson", "102 CAR, 580 YDS, 4 TD"), std::string("Robinson 580 yds 4 TD"));
  CHECK_EQ(espn::detail::leader_line("D. London", "27 REC, 488 YDS"), std::string("London 488 yds"));
  CHECK_EQ(espn::detail::leader_line("C. Gauthier", "2"), std::string(""));  // no yards: nothing to say
  CHECK_EQ(espn::detail::leader_line("", "100 YDS"), std::string(""));
  // ESPN writes the yards with no thousands separator today (pinned from the
  // recorded document below); one written "1,381" reads the same.
  CHECK_EQ(espn::detail::leader_line("D. Prescott", "129/192, 1,381 YDS, 9 TD, 3 INT"),
           std::string("Prescott 1,381 yds 9 TD"));
  CHECK_EQ(espn::detail::leader_line("D. Prescott", "12,345 YDS"), std::string("Prescott 12,345 yds"));
  CHECK_EQ(espn::detail::leader_line("D. Prescott", "-- YDS"), std::string(""));
  {
    std::string raw = slurp("fixtures/event_nfl_pre_dal.json");
    CHECK(raw.find("\"112/185, 1408 YDS, 10 TD, 3 INT\"") != std::string::npos);
  }
  // Pre-game on a single panel: both records after the kickoff, the leaders
  // after the odds, and the leaders switch takes only the leaders away
  GameSnapshot pr = live(0, 0, GameState::PRE);
  pr.team_record = "2-3";
  pr.opp_record = "3-2";
  pr.team_leader = "Prescott 1,381 yds 9 TD";
  pr.opp_leader = "Love 1,408 yds 10 TD";
  pr.venue = "Lambeau Field";
  CHECK_EQ(status_text(pr, no_odds, "Sun 7:20 PM"),
           std::string("Sun 7:20 PM | Prescott 1,381 yds 9 TD | Love 1,408 yds 10 TD | Lambeau Field"));
  TickerOptions narrow = no_odds;
  narrow.records = true;
  CHECK_EQ(status_text(pr, narrow, "Sun 7:20 PM"),
           std::string("Sun 7:20 PM | DAL 2-3, PHI 3-2 | Prescott 1,381 yds 9 TD | Love 1,408 yds 10 TD | Lambeau Field"));
  TickerOptions no_leaders = narrow;
  no_leaders.leaders = false;
  CHECK_EQ(status_text(pr, no_leaders, "Sun 7:20 PM"), std::string("Sun 7:20 PM | DAL 2-3, PHI 3-2 | Lambeau Field"));
  pr.opp_record = "";
  CHECK_EQ(status_text(pr, no_leaders, "Sun 7:20 PM"), std::string("Sun 7:20 PM | DAL 2-3 | Lambeau Field"));
  // Post
  GameSnapshot f = live(21, 20, GameState::POST);
  f.team_record = "3-0";
  f.opp_record = "2-1";
  CHECK_EQ(status_text(f, all, ""), std::string("Final | DAL 3-0 | PHI 2-1"));
  f.short_detail = "Final/OT";
  CHECK_EQ(status_text(f, all, ""), std::string("Final | DAL 3-0 | PHI 2-1"));
  // Postponed or canceled: post too, but never called a final
  GameSnapshot pp = live(0, 0, GameState::POST);
  pp.completed = false;
  pp.short_detail = "Postponed";
  pp.team_record = "3-0";
  pp.opp_record = "2-1";
  CHECK_EQ(status_text(pp, all, ""), std::string("Postponed | DAL 3-0 | PHI 2-1"));
  pp.short_detail = "Canceled";
  CHECK_EQ(status_text(pp, none, ""), std::string("Canceled | DAL 3-0 | PHI 2-1"));
  pp.short_detail = "";
  CHECK_EQ(status_text(pp, all, ""), std::string("Final | DAL 3-0 | PHI 2-1"));
  GameSnapshot nf;
  CHECK_EQ(status_text(nf, all, ""), std::string("No upcoming game"));
}

// The words for an empty board. "No upcoming game" (the empty answer) is only
// right once ESPN said so; until then a failed read is ESPN out of reach.
static void test_no_game_text() {
  // Favorites: ESPN said "no game" only once every favorite's read answered.
  // An off-season first favorite read before ESPN went away says nothing of
  // the three unread ones, so the board still says Can't reach ESPN.
  CHECK(!no_game_said(4, 1, true));
  CHECK_EQ(no_game_text(true, false, no_game_said(4, 1, true), 2), std::string("Can't reach ESPN"));
  CHECK(no_game_said(4, 4, false));
  CHECK_EQ(no_game_text(true, false, no_game_said(4, 4, false), 2), std::string(""));
  CHECK(no_game_said(0, 0, true));  // My team, and the Live fallback's card
  CHECK(!no_game_said(0, 0, false));
  // The miss count stops at 255 instead of wrapping to "ESPN answered".
  CHECK_EQ(miss_count_after(0), (uint8_t) 1);
  CHECK_EQ(miss_count_after(254), (uint8_t) 255);
  CHECK_EQ(miss_count_after(255), (uint8_t) 255);
  // No clock yet: nothing can have been read, whatever else is true
  CHECK_EQ(no_game_text(false, false, false, 0), std::string("Getting ready"));
  CHECK_EQ(no_game_text(false, true, true, 5), std::string("Getting ready"));
  // Clock set, first read still out
  CHECK_EQ(no_game_text(true, false, false, 0), std::string(""));
  // Reads failing, ESPN never answered for this team: after boot or a team change
  CHECK_EQ(no_game_text(true, false, false, 1), std::string("Can't reach ESPN"));
  CHECK_EQ(no_game_text(true, false, false, 7), std::string("Can't reach ESPN"));
  // ESPN said there is no game: stays so when later reads fail (the ticker adds the age)
  CHECK_EQ(no_game_text(true, false, true, 0), std::string(""));
  CHECK_EQ(no_game_text(true, false, true, 3), std::string(""));
  // Live modes keep their own "Looking for a live game"
  CHECK_EQ(no_game_text(true, true, false, 1), std::string(""));
  CHECK_EQ(no_game_text(true, true, false, 3), std::string(""));
  // A sign-in Wi-Fi says so in place of all of them, live modes too
  CHECK_EQ(no_game_text(false, false, false, 0, true), std::string("Wi-Fi needs a sign-in"));
  CHECK_EQ(no_game_text(true, false, false, 2, true), std::string("Wi-Fi needs a sign-in"));
  CHECK_EQ(no_game_text(true, true, false, 1, true), std::string("Wi-Fi needs a sign-in"));
  CHECK_EQ(no_game_text(true, false, false, 2, false), std::string("Can't reach ESPN"));
  // No clock: "Getting ready" for the first 30 s on Wi-Fi, then "No internet"
  // (no time server and no ESPN Date header answered), live modes too
  CHECK_EQ(kClockGraceMs, (uint32_t) 30000);
  CHECK_EQ(no_game_text(false, false, false, 0, false, 0), std::string("Getting ready"));  // off Wi-Fi
  CHECK_EQ(no_game_text(false, false, false, 0, false, 29999), std::string("Getting ready"));
  CHECK_EQ(no_game_text(false, false, false, 0, false, 30000), std::string("No internet"));
  CHECK_EQ(no_game_text(false, true, false, 0, false, 30000), std::string("No internet"));
  CHECK_EQ(no_game_text(false, false, true, 5, false, 600000), std::string("No internet"));
  // A sign-in page answered: that, before or after the 30 s
  CHECK_EQ(no_game_text(false, false, false, 0, true, 1000), std::string("Wi-Fi needs a sign-in"));
  CHECK_EQ(no_game_text(false, false, false, 0, true, 30000), std::string("Wi-Fi needs a sign-in"));
  // The clock is set: the time on Wi-Fi no longer matters
  CHECK_EQ(no_game_text(true, false, false, 0, false, 600000), std::string(""));
  CHECK_EQ(no_game_text(true, false, false, 1, false, 600000), std::string("Can't reach ESPN"));
  CHECK_EQ(no_game_text(true, true, false, 3, false, 600000), std::string(""));
  CHECK(!clock_late(true, 600000));
  CHECK(!clock_late(false, 29999));
  CHECK(clock_late(false, 30000));
}

// The plain HTTP read of ESPN after joining Wi-Fi (captive.h). Only a clear
// portal answer is a sign-in network; anything ESPN could have sent is not.
static void test_captive() {
  // ESPN today: 200, JSON, "{}"
  CHECK(classify_probe(200, "", "application/json;charset=UTF-8", "{}") == Probe::OPEN);
  CHECK(classify_probe(200, "", "", "  \r\n[1]") == Probe::OPEN);
  CHECK(classify_probe(200, "", "application/json", "") == Probe::OPEN);
  CHECK(classify_probe(200, "", "", "\xEF\xBB\xBF{\"a\":1}") == Probe::OPEN);
  // ESPN's JSON error for a URL it no longer knows still means ESPN answered
  CHECK(classify_probe(404, "", "application/json", "{\"code\":404}") == Probe::OPEN);
  // Portals: a redirect to their sign-in page, over HTTP or HTTPS
  CHECK(classify_probe(302, "http://192.168.10.1:8002/index.php?zone=guest", "text/html", "") == Probe::CAPTIVE);
  CHECK(classify_probe(302, "https://login.hotelwifi.example/portal?mac=aa:bb", "", "") == Probe::CAPTIVE);
  CHECK(classify_probe(307, "HTTPS://Splash.Example.NET/", "", "") == Probe::CAPTIVE);
  CHECK(classify_probe(301, "http://user@portal.example/x", "", "") == Probe::CAPTIVE);
  // or their page in ESPN's place, or the status made for it
  CHECK(classify_probe(200, "", "text/html", "<!DOCTYPE html><html><head><title>Sign in") == Probe::CAPTIVE);
  CHECK(classify_probe(200, "", "", "\n  <html>") == Probe::CAPTIVE);
  CHECK(classify_probe(200, "", "text/html; charset=utf-8", "") == Probe::CAPTIVE);
  CHECK(classify_probe(511, "", "text/html", "<html>") == Probe::CAPTIVE);
  CHECK(classify_probe(511, "", "", "") == Probe::CAPTIVE);
  // ESPN sending plain HTTP on to HTTPS one day is still ESPN
  CHECK(classify_probe(301, "https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/6/record", "", "") ==
        Probe::OPEN);
  CHECK(classify_probe(302, "https://secure.espn.com:443/x", "", "") == Probe::OPEN);
  CHECK(classify_probe(302, "https://espn.com", "", "") == Probe::OPEN);
  // but a host that only ends in the same letters is not
  CHECK(classify_probe(302, "http://notespn.com/login", "", "") == Probe::CAPTIVE);
  CHECK(classify_probe(302, "http://espn.com.portal.example/login", "", "") == Probe::CAPTIVE);
  // Proves nothing: no answer, a relative or missing redirect, an error page
  // (ESPN's own Akamai "Access Denied" is HTML), an empty or odd body
  CHECK(classify_probe(0, "", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(-1, "", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(302, "/login", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(302, "", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(403, "", "text/html", "<HTML><HEAD>\n<TITLE>Access Denied</TITLE>") == Probe::UNKNOWN);
  CHECK(classify_probe(503, "", "text/html", "<html>") == Probe::UNKNOWN);
  CHECK(classify_probe(200, "", "", "") == Probe::UNKNOWN);
  CHECK(classify_probe(200, "", "text/plain", "OK") == Probe::UNKNOWN);
  CHECK(classify_probe(204, "", "", "") == Probe::UNKNOWN);

  // When to probe: once per join
  CHECK(probe_due(false, false, false, 0));
  CHECK(probe_due(false, true, true, 0));
  CHECK(!probe_due(true, false, false, 10 * 60 * 1000));
  // again every minute on a sign-in network
  CHECK(!probe_due(true, true, false, kProbeCaptiveRecheckMs - 1));
  CHECK(probe_due(true, true, false, kProbeCaptiveRecheckMs));
  // every two while reads fail on one that looked open
  CHECK(!probe_due(true, false, true, kProbeFailingRecheckMs - 1));
  CHECK(probe_due(true, false, true, kProbeFailingRecheckMs));
}

static struct tm mk(int year, int yday, int wday, int hour, int min, int mon = 8, int mday = 5) {
  struct tm t{};
  t.tm_year = year - 1900;
  t.tm_yday = yday;
  t.tm_wday = wday;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_mon = mon;
  t.tm_mday = mday;
  return t;
}

static void test_kickoff_label() {
  struct tm now = mk(2026, 247, 6, 10, 0);  // Sat Sep 5 10:00
  CHECK_EQ(kickoff_label(mk(2026, 247, 6, 14, 30), now), std::string("Today 2:30 PM"));
  CHECK_EQ(kickoff_label(mk(2026, 248, 0, 12, 0), now), std::string("Tomorrow 12:00 PM"));
  CHECK_EQ(kickoff_label(mk(2026, 251, 3, 19, 20), now), std::string("Wed 7:20 PM"));
  CHECK_EQ(kickoff_label(mk(2026, 256, 1, 0, 5, 8, 14), now), std::string("Sep 14 12:05 AM"));
  // ESPTime{}.to_c_tm(), what the panel gets for an epoch it can't convert:
  // no label rather than a read before the month table (found by fuzz_espn).
  CHECK_EQ(kickoff_label(mk(1900, -1, -1, 0, 0, -1, 0), now), std::string(""));
  // A year far enough out to overflow the day count in int.
  CHECK_EQ(kickoff_label(mk(2147481747, 0, 4, 19, 30, 0, 1), now), std::string("Jan 1 7:30 PM"));
  // Across New Year: a Jan 1 bowl game seen on Dec 31 is "Tomorrow", in a
  // year that isn't a leap year (2025, Dec 31 is day 364) and one that is
  // (2028, day 365). It read "Fri" while local_day counted 366 days a year.
  struct tm nye25 = mk(2025, 364, 3, 20, 0, 11, 31);  // Wed Dec 31 2025 8:00 PM
  CHECK_EQ(kickoff_label(mk(2026, 0, 4, 12, 0, 0, 1), nye25), std::string("Tomorrow 12:00 PM"));
  CHECK_EQ(kickoff_label(mk(2026, 1, 5, 19, 30, 0, 2), nye25), std::string("Fri 7:30 PM"));
  struct tm nye28 = mk(2028, 365, 0, 20, 0, 11, 31);  // Sun Dec 31 2028
  CHECK_EQ(kickoff_label(mk(2029, 0, 1, 12, 0, 0, 1), nye28), std::string("Tomorrow 12:00 PM"));
  // A week out across New Year: six days is a weekday, seven is the date.
  struct tm dec28 = mk(2025, 361, 0, 10, 0, 11, 28);  // Sun Dec 28 2025
  CHECK_EQ(kickoff_label(mk(2026, 2, 6, 15, 0, 0, 3), dec28), std::string("Sat 3:00 PM"));
  CHECK_EQ(kickoff_label(mk(2026, 3, 0, 15, 0, 0, 4), dec28), std::string("Jan 4 3:00 PM"));
  // Still one day apart on every other night of the year.
  CHECK_EQ(local_day(mk(2026, 59, 0, 0, 0, 2, 1)) - local_day(mk(2026, 58, 6, 0, 0, 1, 28)), (int64_t) 1);
}

// The label counts days in local time and the panel redraws it when
// local_day changes (gameday.cpp, day_tick_). A London panel still said
// "Tomorrow 1:30 PM" at midnight on 10/25, the night the clocks went back.
// Each zone: a kickoff, the first instant of its local day, and the label
// either side of that instant.
static void test_kickoff_midnight() {
  const char *was = getenv("TZ");
  std::string saved = was != nullptr ? was : "";
  struct Case {
    const char *tz, *kick, *midnight, *before, *after;
  } cases[] = {
      // London: the clocks go back at 2 AM BST, after that midnight.
      {"GMT0BST,M3.5.0/1,M10.5.0", "2026-10-25T13:30Z", "2026-10-24T23:00Z", "Tomorrow 1:30 PM", "Today 1:30 PM"},
      {"GMT0BST,M3.5.0/1,M10.5.0", "2026-10-26T19:00Z", "2026-10-26T00:00Z", "Tomorrow 7:00 PM", "Today 7:00 PM"},
      // US Central, falling back and springing forward.
      {"CST6CDT,M3.2.0,M11.1.0", "2026-11-01T18:00Z", "2026-11-01T05:00Z", "Tomorrow 12:00 PM", "Today 12:00 PM"},
      {"CST6CDT,M3.2.0,M11.1.0", "2027-03-14T17:00Z", "2027-03-14T06:00Z", "Tomorrow 12:00 PM", "Today 12:00 PM"},
      // Chile springs forward at midnight itself: Sunday starts at 1 AM.
      {"<-04>4<-03>,M9.1.6/24,M4.1.6/24", "2026-09-06T18:00Z", "2026-09-06T04:00Z", "Tomorrow 3:00 PM",
       "Today 3:00 PM"},
  };
  auto local = [](int64_t epoch) {
    time_t t = (time_t) epoch;
    struct tm out{};
    localtime_r(&t, &out);
    return out;
  };
  for (const auto &c : cases) {
    setenv("TZ", c.tz, 1);
    tzset();
    int64_t mid = parse_iso8601_z(c.midnight);
    struct tm kick = local(parse_iso8601_z(c.kick));
    struct tm before = local(mid - 1), after = local(mid);
    CHECK_EQ(kickoff_label(kick, before), std::string(c.before));
    CHECK_EQ(kickoff_label(kick, after), std::string(c.after));
    CHECK(after.tm_hour <= 1 && after.tm_min == 0);
    // The redraw comes at that instant, once: not in the evening before, and
    // not again when the clocks change later in the night.
    CHECK(local_day(before) != local_day(after));
    CHECK(local_day(local(mid - 6 * 3600)) == local_day(before));
    CHECK(local_day(local(mid + 6 * 3600)) == local_day(after));
  }
  if (saved.empty())
    unsetenv("TZ");
  else
    setenv("TZ", saved.c_str(), 1);
  tzset();
}

static void test_upcoming() {
  std::vector<Upcoming> up;
  CHECK(parse_upcoming_str(slurp("fixtures/schedule_nfl_dal.json"), 6, 3, up));
  CHECK_EQ(up.size(), (size_t) 3);
  if (up.size() == 3) {
    CHECK_EQ(up[0].event_id, std::string("401872930"));
    CHECK_EQ(up[0].opp_abbr, std::string("NYG"));
    CHECK_EQ(up[0].opp_id, (uint32_t) 19);
    CHECK(!up[0].home);
    CHECK_EQ(up[0].tv, std::string("NBC"));
    CHECK_EQ(up[0].kickoff_epoch, parse_iso8601_z("2026-09-14T00:20Z"));
    CHECK_EQ(up[1].opp_abbr, std::string("WSH"));
    CHECK(up[1].home);
    CHECK_EQ(up[2].opp_abbr, std::string("BAL"));
    CHECK(up[2].neutral);
  }
  // College: the first event is already final and must be skipped
  up.clear();
  CHECK(parse_upcoming_str(slurp("fixtures/schedule_ncaa_bc.json"), 103, 4, up));
  CHECK_EQ(up.size(), (size_t) 4);
  if (up.size() >= 3) {
    CHECK_EQ(up[0].event_id, std::string("401858214"));
    CHECK_EQ(up[0].opp_abbr, std::string("RUTG"));
    CHECK(up[0].home);
    CHECK_EQ(up[0].tv, std::string("ESPN2"));
    CHECK_EQ(up[2].tv, std::string(""));  // no broadcast listed yet
  }
  CHECK_EQ(schedule_url(League::NFL, 6), std::string("https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/6/schedule"));
}

static void test_live_games() {
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  LiveList games;
  CHECK(parse_live_games(json, games));
  CHECK(games.size() >= 1);
  bool found = false;
  for (const auto &g : games) {
    if (g.event_id == kLiveEvent) {
      found = true;
      CHECK_EQ(g.away_abbr, std::string("BOIS"));
      CHECK_EQ(g.home_abbr, std::string("ORE"));
      CHECK_EQ(g.away_id, kBoise);
      CHECK_EQ(g.home_id, kOregon);
      CHECK(g.group != 0);
    }
  }
  CHECK(found);
  LiveList none;
  std::string nfl = slurp("fixtures/scoreboard_nfl.json");
  CHECK(parse_live_games(nfl, none));
  CHECK_EQ(none.size(), (size_t) 0);
  // The earliest kickoff still to come, for the wait when nothing is live.
  int64_t next = -1;
  games.clear();
  CHECK(parse_live_games(json, games, &next));
  CHECK_EQ(next, parse_iso8601_z("2026-09-05T23:00Z"));
  none.clear();
  CHECK(parse_live_games(nfl, none, &next));
  CHECK_EQ(next, parse_iso8601_z("2026-09-10T00:20Z"));
  std::string finals = slurp("fixtures/scoreboard_ncaa_conf_titles.json");
  CHECK(parse_live_games(finals, none, &next));
  CHECK_EQ(next, (int64_t) 0);  // every game final: nothing left to wait for
  // A TBD kickoff sits at midnight Eastern with timeValid false and must not
  // count, or it would hold the wait at its floor all day.
  std::string tbd = R"({"events":[)"
                    R"({"id":"1","date":"2026-10-24T04:00Z","status":{"type":{"state":"pre"}},"competitions":[{"timeValid":false}]},)"
                    R"({"id":"2","date":"2026-10-24T19:30Z","status":{"type":{"state":"pre"}},"competitions":[{"timeValid":true}]},)"
                    R"({"id":"3","date":"2026-10-24T16:00Z","status":{"type":{"state":"post"}},"competitions":[{}]}]})";
  CHECK(parse_live_games(tbd, none, &next));
  CHECK_EQ(next, parse_iso8601_z("2026-10-24T19:30Z"));
  CHECK(scan_url(League::NFL, parse_iso8601_z("2026-09-14T01:00Z")).find("dates=20260913") != std::string::npos);
  CHECK(scan_url(League::NCAA, 0).find("groups=80") != std::string::npos);
  // College basketball needs its group too, or the scan sees the Top 25 only.
  CHECK_EQ(scan_url(League::MCBB, parse_iso8601_z("2026-02-14T20:00Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard"
                       "?groups=50&limit=300&dates=20260214"));
  CHECK(scan_url(League::NBA, 0).find("groups=") == std::string::npos);
  // A panel with no team picked reads college basketball's Top 25 slate.
  CHECK_EQ(scan_url(League::MCBB, parse_iso8601_z("2026-02-14T20:00Z"), true),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard"
                       "?dates=20260214"));
  CHECK(scan_url(League::NCAA, 0, true).find("groups=80") != std::string::npos);
  CHECK_EQ(scan_url(League::NHL, 0, true), scan_url(League::NHL, 0));
  // neutral splashes name both sides
  CHECK_EQ(decide_splash(live(0, 0), live(6, 0), true, true).text, std::string("DAL TOUCHDOWN"));
  CHECK_EQ(decide_splash(live(0, 0), live(0, 3), true, true).text, std::string("PHI FIELD GOAL"));
  CHECK_EQ(decide_splash(live(20, 21), live(20, 21, GameState::POST), true, true).text, std::string("PHI WINS!"));
}

static void test_clock_text() {
  GameSnapshot s = live(0, 0);
  s.display_clock = "0:42";
  s.period = 2;
  s.short_detail = "1:10 - 2nd";  // stale formatted text
  CHECK_EQ(clock_text(s), std::string("0:42 2nd"));
  s.period = 5;
  CHECK_EQ(clock_text(s), std::string("0:42 OT"));
  s.period = 6;
  CHECK_EQ(clock_text(s), std::string("0:42 2OT"));
  s.period = 2;
  s.display_clock = "0:00";
  s.short_detail = "Halftime";
  CHECK_EQ(clock_text(s), std::string("Halftime"));
  s.short_detail = "End of 3rd";
  CHECK_EQ(clock_text(s), std::string("End of 3rd"));
  s.display_clock = "";
  s.short_detail = "12:34 - 4th";
  CHECK_EQ(clock_text(s), std::string("12:34 4th"));
}

static void test_color() {
  CHECK_EQ(parse_color("002a5c"), (uint32_t) 0x002a5c);
  CHECK_EQ(parse_color(""), (uint32_t) 0xFFFFFF);
  CHECK_EQ(parse_color("zzzzzz"), (uint32_t) 0xFFFFFF);
}

// ---- favorites mode: lock-on, release, collisions, playlist -------------------
static FavGame fav(GameState st, int64_t kick, int64_t final_at = 0) {
  FavGame g;
  g.valid = true;
  g.state = st;
  g.kickoff_epoch = kick;
  g.final_epoch = final_at;
  return g;
}

static void test_favorites() {
  FavRules r;
  r.lockon_s = 15 * 60;
  r.release_s = 30;
  r.alternate = false;
  r.rotate_s = 5 * 60;
  r.dwell_s = 10;
  const int64_t T = 1000000;

  // lock window: only within lockon_s before kickoff, while IN, or release_s after the final
  CHECK(!fav_locked(fav(GameState::PRE, T + 16 * 60), T, r));
  CHECK(fav_locked(fav(GameState::PRE, T + 15 * 60), T, r));
  CHECK(fav_locked(fav(GameState::PRE, T - 60), T, r));  // ESPN lag: still PRE past kickoff
  CHECK(fav_locked(fav(GameState::IN, T - 3600), T, r));
  CHECK(fav_locked(fav(GameState::POST, T - 3 * 3600, T - 29), T, r));
  CHECK(!fav_locked(fav(GameState::POST, T - 3 * 3600, T - 30), T, r));
  CHECK(!fav_locked(fav(GameState::POST, T - 3 * 3600, 0), T, r));  // already final when fetched
  CHECK(!fav_locked(FavGame{}, T, r));

  // playlist: nothing locked, cycle valid entries in slot order every dwell_s
  std::vector<FavGame> idle = {fav(GameState::PRE, T + 86400), FavGame{}, fav(GameState::PRE, T + 2 * 86400),
                               fav(GameState::PRE, T + 3 * 86400)};
  FavChoice c = pick_favorite(idle, T, r, -1, 0, -1);
  CHECK_EQ(c.index, 0);
  CHECK(!c.locked);
  c = pick_favorite(idle, T + 9, r, 0, T, -1);
  CHECK_EQ(c.index, 0);  // dwell not over
  c = pick_favorite(idle, T + 10, r, 0, T, -1);
  CHECK_EQ(c.index, 2);  // skips the empty slot
  c = pick_favorite(idle, T + 30, r, 3, T + 20, -1);
  CHECK_EQ(c.index, 0);  // wraps
  // a current entry that became invalid moves on at once
  c = pick_favorite(idle, T + 1, r, 1, T, -1);
  CHECK_EQ(c.index, 2);
  // nothing valid at all
  std::vector<FavGame> none = {FavGame{}, FavGame{}, FavGame{}, FavGame{}};
  c = pick_favorite(none, T, r, -1, 0, -1);
  CHECK_EQ(c.index, -1);

  // one locked: hold it regardless of dwell
  std::vector<FavGame> one = {fav(GameState::PRE, T + 86400), fav(GameState::IN, T - 600), fav(GameState::PRE, T + 86400),
                              FavGame{}};
  c = pick_favorite(one, T + 100, r, 0, T, -1);
  CHECK_EQ(c.index, 1);
  CHECK(c.locked);
  c = pick_favorite(one, T + 5000, r, 1, T + 100, -1);
  CHECK_EQ(c.index, 1);

  // collision, stick: the higher slot wins even when the lower one is shown
  std::vector<FavGame> two = {fav(GameState::IN, T - 600), fav(GameState::IN, T - 600), fav(GameState::PRE, T + 86400),
                              FavGame{}};
  c = pick_favorite(two, T + 5000, r, 1, T, -1);
  CHECK_EQ(c.index, 0);
  CHECK(c.locked);
  // collision, alternate: hold for rotate_s, then the next locked one, wrapping
  r.alternate = true;
  c = pick_favorite(two, T + 299, r, 0, T, -1);
  CHECK_EQ(c.index, 0);
  c = pick_favorite(two, T + 300, r, 0, T, -1);
  CHECK_EQ(c.index, 1);
  c = pick_favorite(two, T + 600, r, 1, T + 300, -1);
  CHECK_EQ(c.index, 0);
  // alternate with the shown entry not locked jumps to a locked one at once
  c = pick_favorite(two, T + 1, r, 2, T, -1);
  CHECK_EQ(c.index, 0);
  r.alternate = false;

  // pin: a pinned locked entry beats priority; a pinned idle entry only seeds the playlist
  c = pick_favorite(two, T + 5000, r, 0, T, 1);
  CHECK_EQ(c.index, 1);
  CHECK(c.locked);
  c = pick_favorite(idle, T + 5, r, 2, T, 2);
  CHECK_EQ(c.index, 2);
  CHECK(!c.locked);
  c = pick_favorite(idle, T + 10, r, 2, T, 2);
  CHECK_EQ(c.index, 3);  // dwell over, playlist continues past the pin
  // a pin on an empty slot is ignored
  c = pick_favorite(two, T + 5000, r, 0, T, 3);
  CHECK_EQ(c.index, 0);

  // a playlist: only its own teams cycle while idle, and the rules relax
  // inside it, never out to the other favorites
  std::vector<FavGame> list = idle;
  list[0].in_list = false;
  list[3].in_list = false;
  c = pick_favorite(list, T, r, -1, 0, -1);
  CHECK_EQ(c.index, 2);
  c = pick_favorite(list, T + 10, r, 2, T, -1);
  CHECK_EQ(c.index, 2);  // the only one in the playlist comes round again
  r.today_only = true;
  r.day_start = T - 3600;
  r.day_end = T + 3600;
  c = pick_favorite(list, T, r, -1, 0, -1);
  CHECK_EQ(c.index, 2);  // today only would leave nothing, so it relaxes
  r.today_only = false;
  std::vector<FavGame> out = {fav(GameState::PRE, T + 86400), FavGame{}};
  out[0].in_list = false;
  c = pick_favorite(out, T, r, -1, 0, -1);
  CHECK_EQ(c.index, -1);  // no card in the playlist: none from outside it
  // a favorite outside the playlist still locks on near its kickoff
  std::vector<FavGame> takes = {fav(GameState::PRE, T + 86400), fav(GameState::IN, T - 600)};
  takes[1].in_list = false;
  c = pick_favorite(takes, T, r, 0, T, -1);
  CHECK_EQ(c.index, 1);
  CHECK(c.locked);
}

// A schedule parsed from the team endpoint does not know its own league, so
// the caller has to stamp it. Without the stamp a college favorite polls the
// NFL scoreboard and never finds its game.
// The favorite Home Assistant follows while the idle playlist rotates.
static void test_followed_favorite() {
  const int64_t T = 1000000;
  CHECK_EQ(followed_favorite({}), -1);
  CHECK_EQ(followed_favorite({FavGame{}, FavGame{}}), -1);
  // Only the playlist being played: the sooner kickoff outside it doesn't count
  std::vector<FavGame> list = {fav(GameState::PRE, T + 86400), fav(GameState::PRE, T + 3 * 86400)};
  list[0].in_list = false;
  CHECK_EQ(followed_favorite(list), 1);
  // The next kickoff, whatever its slot
  CHECK_EQ(followed_favorite({fav(GameState::PRE, T + 3 * 86400), FavGame{}, fav(GameState::PRE, T + 86400),
                              fav(GameState::PRE, T + 2 * 86400)}),
           2);
  // A card that is already final loses to any upcoming game
  CHECK_EQ(followed_favorite({fav(GameState::POST, T - 3600), fav(GameState::PRE, T + 86400)}), 1);
  // No upcoming game: the first slot with a card
  CHECK_EQ(followed_favorite({FavGame{}, fav(GameState::POST, T - 3600), fav(GameState::POST, T - 60)}), 1);
}

static void test_schedule_league() {
  std::string json = slurp("fixtures/team_ncaa_bc.json");
  Schedule s;
  CHECK(parse_team_str(json, s));
  CHECK(s.valid);
  CHECK_EQ((int) s.league, 0);  // documents the gap: 0 is League::NFL

  adopt_league(s, League::NCAA);
  CHECK_EQ((int) s.league, (int) League::NCAA);

  std::string url = scoreboard_url((League) s.league, s.group, s.kickoff_epoch);
  CHECK(url.find("/college-football/") != std::string::npos);
  CHECK(url.find("/nfl/") == std::string::npos);
}

// Refresh Now, and the six hour schedule cycle. SCHEDULE_INTERVAL is 6 hours;
// the helper takes it as an argument so the test does not depend on the device
// constant.
static const uint32_t kSix = 6u * 60u * 60u * 1000u;

static void test_no_live_rescan() {
  const uint32_t lo = 2 * 60 * 1000, hi = 3 * 60 * 60 * 1000;
  int64_t now = parse_iso8601_z("2026-10-17T13:00Z");
  CHECK_EQ(no_live_rescan_ms(now, now + 45 * 60, lo, hi), (uint32_t) 45 * 60 * 1000);  // waits for kickoff
  CHECK_EQ(no_live_rescan_ms(now, now + 8 * 3600, lo, hi), hi);                        // capped
  CHECK_EQ(no_live_rescan_ms(now, now + 30, lo, hi), lo);                              // never under the floor
  CHECK_EQ(no_live_rescan_ms(now, now - 600, lo, hi), lo);  // due but not under way yet
  CHECK_EQ(no_live_rescan_ms(now, 0, lo, hi), hi);          // nothing left today
  CHECK_EQ(no_live_rescan_ms(0, now + 3600, lo, hi), lo);   // no clock yet
}

// The scan clock in the Live modes: when the wait for a live game is over,
// and what a failed scan does to it.
static void test_live_scan_clock() {
  const uint32_t min = 60 * 1000, three_h = 3 * 60 * min;
  // The wait.
  CHECK(live_rescan_due(5000, 0, three_h, false));                 // no wait running
  CHECK(!live_rescan_due(5000 + 20 * 1000, 5000, three_h, false));  // 20 s into 3 h
  CHECK(live_rescan_due(5000 + three_h, 5000, three_h, false));     // over, exactly
  CHECK(live_rescan_due(5000 + 20 * 1000, 5000, three_h, true));    // a favorite's kickoff
  CHECK(!live_rescan_due(100, 0xFFFFFF00u, three_h, false));        // across millis()' wrap
  CHECK(live_rescan_due(three_h, 0xFFFFFF00u, three_h, false));

  // After a failed scan. The saved team's card and the finals rotation start
  // over, so the retry scans; a followed game keeps its rotate clock.
  CHECK_EQ(live_clock_after_failed_scan(5000, true, false), (uint32_t) 0);
  CHECK_EQ(live_clock_after_failed_scan(5000, false, true), (uint32_t) 0);
  CHECK_EQ(live_clock_after_failed_scan(5000, false, false), (uint32_t) 5000);

  // The fallback's day: a scan at 1:00 finds nothing and waits 3 h for the
  // next kickoff. At 4:00 the scan fails (stamped at its start). The back-off
  // retry 20 s later must scan again, not wait until 7:00.
  uint32_t started = 4 * 60 * min;  // stamped when the 4:00 scan started
  started = live_clock_after_failed_scan(started, true, false);
  CHECK(live_rescan_due(4 * 60 * min + 20 * 1000, started, three_h, false));
  // Before the fix the stamp stayed, and the retry polled the card instead.
  CHECK(!live_rescan_due(4 * 60 * min + 20 * 1000, 4 * 60 * min, three_h, false));
}

static void test_schedule_due() {
  // Nothing fetched yet: the panel has to read the team endpoint.
  CHECK(schedule_due(false, false, 1000, 0, kSix));

  // Fresh schedule, nothing asked for: poll the game we already know about.
  CHECK(!schedule_due(true, false, kSix, kSix - 1000, kSix));

  // Older than the cycle: read it again.
  CHECK(schedule_due(true, false, 2 * kSix, kSix - 1000, kSix));
  CHECK(schedule_due(true, false, kSix, 0, kSix));  // exactly due

  // Refresh Now forces the re-read however fresh the schedule is, and whatever
  // the uptime. The second case is the v1.3.x bug: zeroing the timestamp on a
  // still valid schedule left the test as millis() >= 6 h, so Refresh Now did
  // nothing for the first six hours after a restart.
  CHECK(schedule_due(true, true, 1000, 500, kSix));
  CHECK(schedule_due(true, true, 60u * 1000u, 0, kSix));

  // Without the force flag that same state is not due: this is what made the
  // old refresh_now() a no-op, and it stays true so the force flag is the only
  // thing carrying Refresh Now.
  CHECK(!schedule_due(true, false, 60u * 1000u, 0, kSix));

  // millis() wraps every 49 days. Unsigned subtraction still gives the real
  // age, so a wrap must not force a spurious re-read.
  uint32_t before_wrap = 0xFFFFFFFFu - 1000u;
  CHECK(!schedule_due(true, false, 2000, before_wrap, kSix));  // ~3 s old
  CHECK(schedule_due(true, false, before_wrap + 1000u + kSix, before_wrap, kSix));
}

static void test_team_cache() {
  Schedule d;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_dal.json"), d));
  // Saved the evening before the game.
  int64_t saved = d.kickoff_epoch - 20 * 3600;
  TeamCache c;
  CHECK(team_cache_save(d, League::NFL, 6, saved, c));

  // Next morning, same team: straight to the scoreboard.
  Schedule s;
  CHECK(team_cache_load(c, League::NFL, 6, saved + 12 * 3600, s));
  CHECK(s.valid);
  CHECK_EQ(s.event_id, std::string("401872930"));
  CHECK_EQ(s.kickoff_epoch, d.kickoff_epoch);
  CHECK_EQ(s.group, d.group);
  CHECK_EQ(s.league, (uint8_t) League::NFL);

  // Another team, or the same id in the other league: read the team endpoint.
  CHECK(!team_cache_load(c, League::NFL, 21, saved + 60, s));
  CHECK(!team_cache_load(c, League::NCAA, 6, saved + 60, s));

  // More than a day old, or saved "in the future" (a clock that jumped back).
  CHECK(team_cache_load(c, League::NFL, 6, saved + TEAM_CACHE_MAX_AGE, s));
  CHECK(!team_cache_load(c, League::NFL, 6, saved + TEAM_CACHE_MAX_AGE + 1, s));
  CHECK(!team_cache_load(c, League::NFL, 6, saved - 1, s));

  // Kicked off more than four hours ago: that game is over.
  TeamCache late;
  CHECK(team_cache_save(d, League::NFL, 6, d.kickoff_epoch + 3600, late));
  CHECK(team_cache_load(late, League::NFL, 6, d.kickoff_epoch + TEAM_CACHE_GAME_OVER, s));
  CHECK(!team_cache_load(late, League::NFL, 6, d.kickoff_epoch + TEAM_CACHE_GAME_OVER + 1, s));

  // College keeps its conference group for the scoreboard request.
  Schedule bc;
  CHECK(parse_team_str(slurp("fixtures/team_ncaa_bc.json"), bc));
  TeamCache cc;
  CHECK(team_cache_save(bc, League::NCAA, 103, bc.kickoff_epoch - 3600, cc));
  Schedule sc;
  CHECK(team_cache_load(cc, League::NCAA, 103, bc.kickoff_epoch, sc));
  CHECK_EQ(sc.event_id, std::string("401856777"));
  CHECK_EQ(sc.group, (uint32_t) 1);
  CHECK_EQ(sc.league, (uint8_t) League::NCAA);

  // Nothing to keep: no next game, an id too long for the record, no clock.
  Schedule none;
  none.valid = true;
  TeamCache n;
  CHECK(!team_cache_save(none, League::NFL, 6, saved, n));
  Schedule longid = d;
  longid.event_id = "1234567890123456";  // 16 characters, no room for the end
  CHECK(!team_cache_save(longid, League::NFL, 6, saved, n));
  CHECK(!team_cache_save(d, League::NFL, 6, 0, n));

  // A blank record (nothing saved yet reads back as zeros) is never used.
  TeamCache blank{};
  CHECK(!team_cache_load(blank, League::NFL, 0, 0, s));
}

static void test_boot_hold() {
  // Holding: no board yet, inside the cap.
  CHECK(boot_hold(false, 1000, 1000));
  CHECK(boot_hold(false, 1000, 1000 + BOOT_HOLD_CAP_MS - 1));
  // The cap ends it whether or not a board came.
  CHECK(!boot_hold(false, 1000, 1000 + BOOT_HOLD_CAP_MS));
  // The first scoreboard ends it at once.
  CHECK(!boot_hold(true, 1000, 1500));
  // No hold running.
  CHECK(!boot_hold(false, 0, 1500));
  // millis() wraps every 49 days: the age is still right.
  uint32_t before_wrap = 0xFFFFFFFFu - 2000u;
  CHECK(boot_hold(false, before_wrap, 3000));
  CHECK(!boot_hold(false, before_wrap, before_wrap + BOOT_HOLD_CAP_MS));
}

static void test_rotate_minutes() {
  // 1 minute is a real choice on the device page and in the app. Treating it
  // as corrupt at boot reset the mode to My team.
  CHECK(espn::rotate_minutes_valid(1));
  CHECK(espn::rotate_minutes_valid(30));
  CHECK(!espn::rotate_minutes_valid(0));
  CHECK(!espn::rotate_minutes_valid(31));
  CHECK(espn::rotate_minutes_valid(espn::kRotateDefaultMinutes));
  CHECK(espn::kRotateDefaultMinutes == 1);
  CHECK(espn::clamp_rotate_minutes(0) == 1);
  CHECK(espn::clamp_rotate_minutes(-5) == 1);
  CHECK(espn::clamp_rotate_minutes(1) == 1);
  CHECK(espn::clamp_rotate_minutes(45) == 30);
}

// The root "id" of a recorded single-event document, so the event id check
// runs on every sport's document, not only football's.
static std::string root_id(const std::string &json) {
  size_t at = json.find("\"id\":\"");
  if (at == std::string::npos)
    return "";
  at += 6;
  return json.substr(at, json.find('"', at) - at);
}

// MLB single-event documents recorded 2026-10-07 (see fixtures/README.md).
static const uint32_t kBraves = 15, kDodgers = 19, kWhiteSox = 4, kBrewers = 8;

static GameSnapshot mlb_event(const char *file, uint32_t team) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK(parse_event_str(json, Sport::BASEBALL, team, s, root_id(json)));
  return s;
}

static void test_mlb_urls() {
  CHECK_EQ(team_url(League::MLB, 15), std::string("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/teams/15"));
  CHECK_EQ(event_url(League::MLB, "401908016"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/scoreboard/401908016"));
  CHECK_EQ(team_logo_url(League::MLB, 15, "ATL"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/mlb/500-dark/scoreboard/atl.png&w=64&h=64"));
  CHECK_EQ(std::string(league_key(League::MLB)), std::string("mlb"));
  CHECK(league_by_key("mlb") != nullptr);
  CHECK(league_by_key("cfl") == nullptr);
  // A West Coast game still on at 2:30 AM Eastern is listed under the day it
  // started; the next morning's scan moves on to the new day.
  CHECK(scan_url(League::MLB, parse_iso8601_z("2026-10-08T06:30Z")).find("dates=20261007") != std::string::npos);
  CHECK(scan_url(League::MLB, parse_iso8601_z("2026-10-08T13:00Z")).find("dates=20261008") != std::string::npos);
  CHECK_EQ(scan_url(League::MLB, parse_iso8601_z("2026-10-08T13:00Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/scoreboard?dates=20261008"));
  CHECK(league_sport(League::MLB) == Sport::BASEBALL);
  CHECK(league_sport(League::NCAA) == Sport::FOOTBALL);
  bool found = false;
  for (size_t i = 0; i < kTeamCount; i++) {
    if (kTeams[i].league == League::MLB && kTeams[i].espn_id == kBraves) {
      CHECK_EQ(team_option(kTeams[i]), std::string("MLB: Atlanta Braves"));
      found = true;
    }
  }
  CHECK(found);
}

static void test_mlb_team() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_mlb_atl.json"), s));
  CHECK_EQ(s.event_id, std::string("401908016"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-10-07T22:00Z"));
  CHECK_EQ(s.team_color, std::string("0c2340"));
  CHECK_EQ(s.team_record, std::string("94-68"));
}

static void test_mlb_live() {
  // Top 7th, LAD batting, runner on second, 2-1 count, two out. ATL is home.
  GameSnapshot t = mlb_event("fixtures/event_mlb_top.json", kBraves);
  CHECK(t.valid);
  CHECK(t.sport == Sport::BASEBALL);
  CHECK(t.state == GameState::IN);
  CHECK(t.team_home);
  CHECK_EQ(t.team_abbr, std::string("ATL"));
  CHECK_EQ(t.opp_abbr, std::string("LAD"));
  CHECK_EQ(t.team_score, 1);
  CHECK_EQ(t.opp_score, 1);
  CHECK(t.mlb.half == Half::TOP);
  CHECK_EQ((int) t.mlb.balls, 2);
  CHECK_EQ((int) t.mlb.strikes, 1);
  CHECK_EQ((int) t.mlb.outs, 2);
  CHECK_EQ((int) t.mlb.bases, (int) Baseball::SECOND);
  CHECK_EQ(t.possession, 2);  // the visitors bat in the top half
  CHECK_EQ(t.mlb.batter, std::string("M. Muncy"));
  CHECK_EQ(t.mlb.pitcher, std::string("D. Fuentes"));
  CHECK_EQ(t.mlb.team_hits, 2);
  CHECK_EQ(t.mlb.opp_hits, 3);
  CHECK_EQ(t.mlb.team_errors, 2);
  CHECK_EQ(t.mlb.opp_errors, 0);
  CHECK_EQ(t.mlb.series, std::string("LAD lead series 2-1"));
  CHECK_EQ(t.mlb.play_type, std::string("Ball"));
  CHECK_EQ(t.last_play, std::string("Pitch 3 : Ball 2"));
  CHECK_EQ(t.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/mlb/500-dark/scoreboard/atl.png&w=64&h=64"));
  CHECK_EQ(clock_text(t), std::string("Top 7th"));
  CHECK_EQ(baseball_count(t), std::string("2-1"));
  TickerOptions o;
  o.clock = false;
  CHECK_EQ(status_text(t, o, ""), std::string("2-1, 2 outs | P D. Fuentes, AB M. Muncy | Pitch 3 : Ball 2"));
  TickerOptions quiet = o;
  quiet.baseball_count = quiet.last_play = false;
  CHECK_EQ(status_text(t, quiet, ""), std::string("Top 7th"));
  // Football's switch doesn't touch the count.
  TickerOptions no_football = o;
  no_football.down_distance = false;
  CHECK_EQ(status_text(t, no_football, ""), status_text(t, o, ""));

  // The same game from the Dodgers' side: they bat.
  GameSnapshot d = mlb_event("fixtures/event_mlb_top.json", kDodgers);
  CHECK(!d.team_home);
  CHECK_EQ(d.possession, 1);
  CHECK_EQ(d.mlb.team_hits, 3);

  // Bottom 6th: the Braves bat, one out.
  GameSnapshot b = mlb_event("fixtures/event_mlb_bottom.json", kBraves);
  CHECK(b.mlb.half == Half::BOTTOM);
  CHECK_EQ(b.possession, 1);
  CHECK_EQ((int) b.mlb.outs, 1);
  CHECK_EQ(status_text(b, o, ""), std::string("1-1, 1 out | P E. Henriquez, AB L. Thomas | Pitch 2 : Ball 1"));

  // End of an inning: ESPN keeps a stale count, the panel must not show it.
  GameSnapshot e = mlb_event("fixtures/event_mlb_end.json", kBraves);
  CHECK(e.mlb.half == Half::END);
  CHECK_EQ((int) e.mlb.balls, -1);
  CHECK_EQ((int) e.mlb.outs, -1);
  CHECK_EQ((int) e.mlb.bases, 0);
  CHECK_EQ(e.possession, 0);
  CHECK_EQ(baseball_count(e), std::string(""));
  CHECK_EQ(clock_text(e), std::string("End 5th"));
  CHECK_EQ(status_text(e, o, ""), std::string("ATL 1 H 2 E, LAD 2 H 0 E | LAD lead series 2-1"));
}

static void test_mlb_pre_post() {
  GameSnapshot p = mlb_event("fixtures/event_mlb_pre.json", kBrewers);
  CHECK(p.state == GameState::PRE);
  CHECK_EQ(p.mlb.team_probable, std::string("R. Gasser"));
  CHECK_EQ(p.mlb.opp_probable, std::string("W. Buehler"));
  CHECK_EQ((int) p.mlb.balls, -1);
  CHECK_EQ(p.possession, 0);
  CHECK_EQ(p.odds, std::string("SD -112"));
  CHECK_EQ(p.tv, std::string("FS1"));
  TickerOptions o;
  std::string pre = status_text(p, o, "Today 10:00 PM");
  // The round from the fixture's notes leads, as on the device page
  CHECK_EQ(p.game_name, std::string("NLDS - Game 4"));
  CHECK(pre.rfind("NLDS - Game 4 | Today 10:00 PM | MIL leads series 2-1 | MIL R. Gasser vs SD W. Buehler | SD -112 | ",
                  0) == 0);
  // Each side's home run leader, with the RBIs when he leads those too, then
  // the forecast at an open-air park
  CHECK_EQ(p.team_leader, std::string("Bauers 26 HR"));
  CHECK_EQ(p.opp_leader, std::string("Machado 30 HR 91 RBI"));
  CHECK(pre.find(" | FS1 | Bauers 26 HR | Machado 30 HR 91 RBI | 82F Mostly clear at Petco Park") !=
        std::string::npos);
  TickerOptions no_leaders;
  no_leaders.leaders = false;
  CHECK(status_text(p, no_leaders, "Today 10:00 PM").find(" | FS1 | 82F Mostly clear at Petco Park") !=
        std::string::npos);

  GameSnapshot f = mlb_event("fixtures/event_mlb_final.json", kWhiteSox);
  CHECK(f.state == GameState::POST);
  CHECK(!f.team_winner);
  CHECK_EQ(f.team_score, 3);
  CHECK_EQ(f.opp_score, 9);
  CHECK_EQ(status_text(f, o, ""), std::string("Final | CHW 6 H 0 E, CLE 11 H 0 E | CHW lead series 2-1"));
  f.short_detail = "Final/10";
  CHECK(status_text(f, o, "").rfind("Final/10 | ", 0) == 0);

  // A real rainout (CHC @ CLE, 2026-04-04): ESPN sends post with completed false.
  GameSnapshot ppd = mlb_event("fixtures/event_mlb_postponed.json", 5);
  CHECK(ppd.state == GameState::POST);
  CHECK(!ppd.completed);
  CHECK_EQ(status_text(ppd, o, ""), std::string("Postponed"));
  GameSnapshot ppd_was = ppd;
  ppd_was.state = GameState::IN;
  ppd.team_score = 2;
  CHECK_EQ(decide_splash(ppd_was, ppd, true, true).text, std::string(""));

  GameSnapshot x;
  CHECK(!parse_event_str(slurp("fixtures/event_mlb_top.json"), Sport::BASEBALL, kWhiteSox, x));  // not in it
  CHECK(!parse_event_str(slurp("fixtures/event_mlb_top.json"), Sport::BASEBALL, kBraves, x, "401908017"));  // some other event
  CHECK(!parse_event_str("{nope", Sport::BASEBALL, kBraves, x));
}

static void test_mlb_splash() {
  GameSnapshot prev = mlb_event("fixtures/event_mlb_top.json", kBraves);
  auto after = [&](int us, int them, const char *play) {
    GameSnapshot c = prev;
    c.team_score += us;
    c.opp_score += them;
    c.mlb.play_type = play;
    return c;
  };
  CHECK_EQ(decide_splash(prev, after(1, 0, "Play Result"), true).text, std::string("RUN!"));
  CHECK_EQ(decide_splash(prev, after(3, 0, "Play Result"), true).text, std::string("3 RUNS!"));
  CHECK_EQ(decide_splash(prev, after(4, 0, "Home Run"), true).text, std::string("HOME RUN!"));
  CHECK_EQ(decide_splash(prev, after(1, 0, "Play Result"), true).color, (uint32_t) 0x0c2340);
  CHECK_EQ(decide_splash(prev, after(0, 1, "Play Result"), true).text, std::string("LAD SCORES"));
  CHECK_EQ(decide_splash(prev, after(0, 2, "Play Result"), true).text, std::string("LAD 2 RUNS"));
  CHECK_EQ(decide_splash(prev, after(0, 1, "Home Run"), true).text, std::string("LAD HOME RUN"));
  CHECK_EQ(decide_splash(prev, after(0, 1, "Home Run"), false).text, std::string(""));
  GameSnapshot hr = after(0, 2, "Play Result");
  hr.last_play = "Muncy homered to right (402 feet), Freeman scored.";
  CHECK_EQ(decide_splash(prev, hr, true).text, std::string("LAD HOME RUN"));
  // Live modes: neither side is ours.
  CHECK_EQ(decide_splash(prev, after(1, 0, "Play Result"), true, true).text, std::string("ATL SCORES"));
  CHECK_EQ(decide_splash(prev, after(0, 0, "Ball"), true).text, std::string(""));
  GameSnapshot fin = after(2, 0, "Play Result");
  fin.state = GameState::POST;
  fin.completed = true;
  GameSnapshot was = fin;
  was.state = GameState::IN;
  CHECK_EQ(decide_splash(was, fin, true).text, std::string("ATL WINS!"));
}

// Favorites list: the today-only filter and the season horizon.
static void test_favorites_filters() {
  const int64_t day = 86400;
  const int64_t now = 1791000000;  // a Wednesday afternoon, say
  FavRules r;
  r.day_start = now - 15 * 3600;
  r.day_end = r.day_start + day;
  auto card = [](GameState st, int64_t kick, int64_t horizon) {
    FavGame g;
    g.valid = true;
    g.state = st;
    g.kickoff_epoch = kick;
    g.horizon_s = horizon;
    return g;
  };
  const int64_t h = 10 * day;
  FavGame tonight = card(GameState::PRE, now + 4 * 3600, h);
  FavGame saturday = card(GameState::PRE, now + 3 * day, h);
  FavGame spring = card(GameState::PRE, now + 150 * day, h);       // MLB in December
  FavGame bye_week = card(GameState::PRE, now + 13 * day, 0);      // football: no horizon
  FavGame old_final = card(GameState::POST, now - 40 * day, h);    // last season's final
  FavGame live = card(GameState::IN, now - 3600, h);
  FavGame final_today = card(GameState::POST, r.day_start + 3600, h);

  CHECK(fav_idle_ok(tonight, now, r, false));
  CHECK(fav_idle_ok(saturday, now, r, false));
  CHECK(!fav_idle_ok(spring, now, r, false));
  CHECK(fav_idle_ok(bye_week, now, r, false));
  CHECK(!fav_idle_ok(old_final, now, r, false));
  CHECK(fav_idle_ok(tonight, now, r, true));
  CHECK(!fav_idle_ok(saturday, now, r, true));
  CHECK(fav_idle_ok(live, now, r, true));
  CHECK(fav_idle_ok(final_today, now, r, true));
  CHECK(!fav_idle_ok(FavGame{}, now, r, false));

  // The playlist skips the out-of-season card.
  std::vector<FavGame> games = {saturday, spring, tonight};
  FavChoice c = pick_favorite(games, now, r, 0, now - 20, -1);
  CHECK_EQ(c.index, 2);
  c = pick_favorite(games, now, r, 2, now - 20, -1);
  CHECK_EQ(c.index, 0);
  // Today only: just tonight's game, and it stays up past the dwell.
  r.today_only = true;
  c = pick_favorite(games, now, r, 2, now - 20, -1);
  CHECK_EQ(c.index, 2);
  CHECK(!c.locked);
  // Nothing today: the filter relaxes to the season horizon...
  std::vector<FavGame> quiet = {saturday, spring};
  c = pick_favorite(quiet, now, r, -1, 0, -1);
  CHECK_EQ(c.index, 0);
  c = pick_favorite(quiet, now, r, 0, now - 20, -1);
  CHECK_EQ(c.index, 0);
  // ...and with every team out of season, any card beats a blank panel.
  std::vector<FavGame> winter = {spring, old_final};
  c = pick_favorite(winter, now, r, -1, 0, -1);
  CHECK_EQ(c.index, 0);
  c = pick_favorite(winter, now, r, 0, now - 20, -1);
  CHECK_EQ(c.index, 1);
  // A remote press shows its card for the dwell even when today-only would
  // skip it, then the playlist moves on.
  c = pick_favorite(games, now, r, 0, now, 0);
  CHECK_EQ(c.index, 0);
  c = pick_favorite(games, now, r, 0, now - 20, 0);
  CHECK_EQ(c.index, 2);
  // A locked game still wins over everything.
  r.today_only = false;
  std::vector<FavGame> mixed = {saturday, live};
  c = pick_favorite(mixed, now, r, 0, now, -1);
  CHECK_EQ(c.index, 1);
  CHECK(c.locked);
}

// Soccer single-event documents (see fixtures/README.md): Brazilian Serie A
// recorded live 2026-10-08, MLS and Premier League pre-game and finals, a
// Champions League final on penalties and a World Cup game after extra time.
static const uint32_t kArsenal = 359, kLeeds = 357, kSunderland = 366, kMiami = 20232, kDcUnited = 193,
                      kInter = 1936, kCorinthians = 874, kVasco = 3454, kMirassol = 9169, kBragantino = 6079,
                      kPsg = 160, kArgentina = 202, kCruzeiro = 2022;

static GameSnapshot soccer_event(const char *file, uint32_t team) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK(parse_event_str(json, Sport::SOCCER, team, s, root_id(json)));
  return s;
}

static void test_soccer_urls() {
  CHECK_EQ(team_url(League::EPL, kArsenal), std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/eng.1/teams/359"));
  CHECK_EQ(team_url(League::MLS, kMiami), std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/usa.1/teams/20232"));
  CHECK_EQ(event_url(League::EPL, "401879268"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/eng.1/scoreboard/401879268"));
  // A soccer schedule lists past results unless asked for fixtures.
  CHECK_EQ(schedule_url(League::EPL, kArsenal),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/eng.1/teams/359/schedule?fixture=true"));
  CHECK_EQ(schedule_url(League::MLB, 15), std::string("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/teams/15/schedule"));
  CHECK_EQ(team_logo_url(League::EPL, kArsenal, "ARS"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/soccer/500-dark/359.png&w=64&h=64"));
  CHECK_EQ(team_logo_url(League::MLS, kMiami, "MIA"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/soccer/500-dark/20232.png&w=64&h=64"));
  CHECK_EQ(std::string(league_key(League::EPL)), std::string("epl"));
  CHECK_EQ(std::string(league_key(League::MLS)), std::string("mls"));
  CHECK(league_by_key("epl") != nullptr && league_by_key("epl")->season_list);
  CHECK(league_by_key("mls") != nullptr && league_by_key("mls")->logo_by_id);
  CHECK(league_sport(League::EPL) == Sport::SOCCER);
  CHECK(league_sport(League::MLS) == Sport::SOCCER);
  int epl = 0, mls = 0;
  for (size_t i = 0; i < kTeamCount; i++) {
    const Team &t = kTeams[i];
    epl += t.league == League::EPL;
    mls += t.league == League::MLS;
    if (t.league == League::EPL && t.espn_id == kArsenal)
      CHECK_EQ(team_option(t), std::string("EPL: Arsenal"));
    if (t.league == League::MLS && t.espn_id == kMiami)
      CHECK_EQ(team_option(t), std::string("MLS: Inter Miami CF"));
  }
  CHECK_EQ(epl, 20);
  CHECK_EQ(mls, 30);
}

static void test_soccer_team() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_epl_ars.json"), s));
  CHECK_EQ(s.event_id, std::string("401879268"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-10-10T11:30Z"));
  CHECK_EQ(s.team_color, std::string("e20520"));
  CHECK_EQ(s.team_record, std::string("4-0-1"));
  CHECK(parse_team_str(slurp("fixtures/team_mls_mia.json"), s));
  CHECK_EQ(s.event_id, std::string("761847"));
  CHECK_EQ(s.team_record, std::string("12-10-5"));

  // The fixture list feeds Up next the same way football's schedule does.
  std::vector<Upcoming> up;
  CHECK(parse_upcoming_str(slurp("fixtures/schedule_mls_mia.json"), kMiami, 4, up));
  CHECK_EQ(up.size(), (size_t) 2);
  CHECK_EQ(up[0].event_id, std::string("761847"));
  CHECK_EQ(up[0].opp_id, kDcUnited);
  CHECK_EQ(up[0].opp_abbr, std::string("DC"));
  CHECK(up[0].home);
  CHECK_EQ(up[0].tv, std::string("Apple TV"));
  CHECK_EQ(up[1].opp_abbr, std::string("NYC"));
  CHECK_EQ(up[1].kickoff_epoch, parse_iso8601_z("2026-10-14T23:30Z"));
}

static void test_soccer_pre() {
  GameSnapshot a = soccer_event("fixtures/event_epl_pre.json", kArsenal);
  CHECK(a.valid);
  CHECK(a.sport == Sport::SOCCER);
  CHECK(a.state == GameState::PRE);
  CHECK(a.team_home);
  CHECK_EQ(a.opp_abbr, std::string("LEE"));
  CHECK_EQ(a.team_record, std::string("4-0-1"));  // W-D-L, the order of ESPN's standings
  // ESPN sends the newest result first ("LWWWW": Arsenal lost their last
  // game); the ticker prints it oldest first like a league table.
  CHECK_EQ(a.soc.team_form, std::string("WWWWL"));
  CHECK_EQ(a.soc.opp_form, std::string("DDLWD"));
  CHECK_EQ(a.soc.team_line, std::string("-275"));
  CHECK_EQ(a.soc.draw_line, std::string("+425"));
  CHECK_EQ(a.soc.opp_line, std::string("+750"));
  CHECK(a.soc.goals.empty());
  CHECK_EQ(a.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/soccer/500-dark/359.png&w=64&h=64"));
  TickerOptions o;
  CHECK_EQ(status_text(a, o, "Sat 7:30 AM"),
           std::string("Sat 7:30 AM | Form ARS WWWWL, LEE DDLWD | ARS -275, draw +425, LEE +750 | O/U 2.5 | USA Net | "
                       "Emirates Stadium"));
  // From the visitors' side the line turns around.
  GameSnapshot l = soccer_event("fixtures/event_epl_pre.json", kLeeds);
  CHECK(!l.team_home);
  CHECK_EQ(l.soc.team_line, std::string("+750"));
  CHECK_EQ(l.soc.opp_line, std::string("-275"));

  GameSnapshot m = soccer_event("fixtures/event_mls_pre.json", kMiami);
  CHECK_EQ(m.team_record, std::string("12-10-5"));
  CHECK_EQ(status_text(m, o, "Sat 7:30 PM"),
           std::string("Sat 7:30 PM | Form MIA DDWDL, DC DWDLD | MIA -270, draw +425, DC +500 | O/U 3.5 | Apple TV | "
                       "Nu Stadium"));
  TickerOptions no_odds = o;
  no_odds.odds = false;
  CHECK_EQ(status_text(m, no_odds, ""),
           std::string("Scheduled | Form MIA DDWDL, DC DWDLD | Nu Stadium"));
  // Only the favorite's line without the three-way one.
  GameSnapshot partial = m;
  partial.soc.opp_line.clear();
  CHECK_EQ(status_text(partial, o, "Sat 7:30 PM").find("MIA -270, draw +425 | O/U 3.5") != std::string::npos, true);

  GameSnapshot none;
  none.sport = Sport::SOCCER;
  CHECK_EQ(status_text(none, o, ""), std::string("No upcoming game"));
}

static void test_soccer_live() {
  TickerOptions o;
  o.clock = false;  // as the component asks: the clock has its own row

  // First half, no goals yet: an empty situation row.
  GameSnapshot fh = soccer_event("fixtures/event_soccer_first_half.json", kCruzeiro);
  CHECK(fh.state == GameState::IN);
  CHECK_EQ(fh.soc.status, std::string("STATUS_FIRST_HALF"));
  CHECK_EQ(clock_text(fh), std::string("25'"));
  CHECK_EQ(soccer_situation(fh), std::string(""));
  CHECK_EQ(status_text(fh, o, ""),
           std::string("Cards CRU 1Y, SAO 1Y | Possession CRU 64.1%, SAO 35.9% | Shots (on target) CRU 4 (0), SAO 4 (1) | "
                       "24' Yellow Card: G. Rojas (CRU)"));
  TickerOptions with_clock;
  with_clock.soccer_stats = with_clock.last_play = false;
  CHECK_EQ(status_text(fh, with_clock, ""), std::string("25'"));
  // Football's and baseball's switches don't touch soccer's.
  TickerOptions others_off = o;
  others_off.down_distance = others_off.baseball_count = false;
  CHECK_EQ(status_text(fh, others_off, ""), status_text(fh, o, ""));

  // Half time, Vasco (away) lead 1-0.
  GameSnapshot h = soccer_event("fixtures/event_soccer_halftime.json", kVasco);
  CHECK(h.state == GameState::IN);
  CHECK_EQ(h.soc.status, std::string("STATUS_HALFTIME"));
  CHECK_EQ(h.display_clock, std::string("45'+5'"));  // the clock stops where the half ended
  CHECK_EQ(clock_text(h), std::string("HT"));
  CHECK_EQ(soccer_situation(h), std::string("28' Lescano"));
  CHECK_EQ(status_text(h, o, ""),
           std::string("Half time | Goals VAS 28' Lescano | Possession VAS 54.8%, BOT 45.2% | "
                       "Shots (on target) VAS 8 (4), BOT 6 (1) | W-D-L VAS 8-7-12, BOT 9-8-11"));

  // Stoppage time, Internacional (home) 2-1 up. ESPN's status name flips
  // between STATUS_SECOND_HALF and STATUS_IN_PROGRESS from poll to poll.
  GameSnapshot s = soccer_event("fixtures/event_soccer_stoppage.json", kInter);
  CHECK_EQ(s.soc.status, std::string("STATUS_IN_PROGRESS"));
  CHECK_EQ(clock_text(s), std::string("90'+2'"));
  CHECK_EQ(s.team_score, 2);
  CHECK_EQ(s.opp_score, 1);
  CHECK_EQ(s.soc.goals.size(), (size_t) 3);
  CHECK(s.soc.goals[0].ours);
  CHECK(!s.soc.goals[2].ours);
  CHECK_EQ(s.soc.goals[2].name, std::string("Andre"));  // "André", folded for the panel font
  CHECK_EQ(s.soc.team_yellows, 3);
  CHECK_EQ(s.soc.opp_yellows, 4);
  CHECK_EQ(s.soc.team_reds + s.soc.opp_reds, 0);
  CHECK_EQ(soccer_situation(s), std::string("80' Andre"));
  CHECK_EQ(s.soc.last_event, std::string("85' Yellow Card: R. Garro (COR)"));
  CHECK_EQ(status_text(s, o, ""),
           std::string("Goals INT 1' Vitinho, 77' Alan Patrick; COR 80' Andre | Cards INT 3Y, COR 4Y | "
                       "Possession INT 28%, COR 72% | Shots (on target) INT 17 (7), COR 21 (6) | "
                       "85' Yellow Card: R. Garro (COR)"));
  TickerOptions quiet = o;
  quiet.soccer_stats = quiet.last_play = false;
  CHECK_EQ(status_text(s, quiet, ""), std::string("90'+2'"));

  // Second half with a red card: Mirassol (away) down to ten.
  GameSnapshot r = soccer_event("fixtures/event_soccer_second_half.json", kMirassol);
  CHECK_EQ(clock_text(r), std::string("90'"));
  CHECK_EQ(r.soc.team_reds, 1);
  CHECK_EQ(r.soc.opp_reds, 0);
  CHECK_EQ(r.soc.last_event, std::string("70' Goal: Andre Luis (MIR)"));
  // 14 characters is too wide for the row: the last word of the name stays.
  CHECK_EQ(soccer_situation(r), std::string("70' Luis"));
  CHECK(status_text(r, o, "").rfind("Goals MIR 70' Andre Luis; BRA 51' Eduardo Sasha | Cards MIR 1R | ", 0) == 0);
  GameSnapshot b = soccer_event("fixtures/event_soccer_second_half.json", kBragantino);
  CHECK_EQ(b.soc.team_reds, 0);
  CHECK_EQ(b.soc.opp_reds, 1);

  // Full time in the same game as the stoppage-time poll.
  GameSnapshot f = soccer_event("fixtures/event_soccer_full_time.json", kInter);
  CHECK(f.state == GameState::POST);
  CHECK(f.team_winner);
  CHECK_EQ(clock_text(f), std::string("FT"));
  CHECK_EQ(status_text(f, o, ""),
           std::string("Full time | Goals INT 1' Vitinho, 77' Alan Patrick; COR 80' Andre | W-D-L INT 6-10-12, COR 8-8-12"));
  CHECK_EQ(decide_splash(s, f, true).text, std::string("INT WINS!"));
  CHECK_EQ(decide_splash(s, f, true).color, (uint32_t) 0xC60000);
  GameSnapshot sc = soccer_event("fixtures/event_soccer_stoppage.json", kCorinthians);
  GameSnapshot fc = soccer_event("fixtures/event_soccer_full_time.json", kCorinthians);
  CHECK_EQ(decide_splash(sc, fc, true).text, std::string(""));
  CHECK_EQ(decide_splash(sc, fc, true, true).text, std::string("INT WINS!"));  // live modes: the winner, either side
}

static void test_soccer_finals() {
  TickerOptions o;
  // Premier League: Arsenal win 2-0 away with a stoppage-time penalty; Sunderland had a man sent off.
  GameSnapshot a = soccer_event("fixtures/event_epl_final.json", kArsenal);
  CHECK(a.state == GameState::POST);
  CHECK(a.team_winner);
  CHECK_EQ(a.soc.opp_reds, 1);
  CHECK(a.soc.goals.back().penalty);
  CHECK_EQ(status_text(a, o, ""),
           std::string("Full time | Goals ARS 58' Guimaraes, 90'+7' Saka (P) | Red cards SUN 1 | W-D-L ARS 4-0-0, SUN 1-1-2"));
  // "90'+7' Saka (P)" is 15 characters: the penalty mark gives way.
  CHECK_EQ(soccer_situation(a), std::string("90'+7' Saka"));
  CHECK_EQ(soccer_situation(a, 16), std::string("90'+7' Saka (P)"));
  GameSnapshot sun = soccer_event("fixtures/event_epl_final.json", kSunderland);
  CHECK_EQ(sun.soc.team_reds, 1);

  // MLS: a 2-2 draw. Both sides have winner false and nobody gets a splash.
  GameSnapshot d = soccer_event("fixtures/event_mls_final.json", kMiami);
  CHECK(!d.team_winner);
  CHECK_EQ(d.team_score, 2);
  CHECK_EQ(d.opp_score, 2);
  CHECK_EQ(clock_text(d), std::string("FT"));
  CHECK_EQ(status_text(d, o, ""),
           std::string("Full time | Goals MIA 24' Messi, 74' Suarez; SD 12' Dreyer, 82' Dreyer | W-D-L MIA 12-10-4, SD 8-7-11"));
  GameSnapshot live = d;
  live.state = GameState::IN;
  CHECK_EQ(decide_splash(live, d, true).text, std::string(""));
  CHECK_EQ(decide_splash(live, d, true, true).text, std::string(""));

  // Champions League final on penalties: 1-1, PSG win the shootout 4-3.
  GameSnapshot p = soccer_event("fixtures/event_soccer_pens.json", kArsenal);
  CHECK_EQ(p.soc.status, std::string("STATUS_FINAL_PEN"));
  CHECK_EQ(p.soc.team_shootout, 3);
  CHECK_EQ(p.soc.opp_shootout, 4);
  CHECK_EQ(p.soc.goals.size(), (size_t) 2);  // shootout kicks are not goals
  CHECK_EQ(clock_text(p), std::string("PENS"));
  CHECK_EQ(p.soc.last_event, std::string("Shootout Penalty - Scored: L. Beraldo (PSG)"));
  CHECK_EQ(status_text(p, o, ""),
           std::string("PSG win 4-3 on penalties | UEFA Champions League, Final | Goals ARS 6' Havertz; PSG 65' Dembele (P)"));
  GameSnapshot pl = p;
  pl.state = GameState::IN;
  pl.soc.status = "STATUS_SHOOTOUT";
  CHECK_EQ(clock_text(pl), std::string("PENS 3-4"));
  CHECK_EQ(decide_splash(pl, p, true).text, std::string(""));
  CHECK_EQ(decide_splash(pl, p, true, true).text, std::string("PSG WINS!"));
  GameSnapshot psg = soccer_event("fixtures/event_soccer_pens.json", kPsg);
  GameSnapshot psg_live = psg;
  psg_live.state = GameState::IN;
  CHECK_EQ(decide_splash(psg_live, psg, true).text, std::string("PSG WINS!"));

  // World Cup round of 32 after extra time, with an own goal at 111'.
  GameSnapshot e = soccer_event("fixtures/event_soccer_aet.json", kArgentina);
  CHECK_EQ(clock_text(e), std::string("AET"));
  CHECK(e.soc.goals.back().own_goal);
  CHECK(e.soc.goals.back().ours);  // an own goal counts for the side it helped
  CHECK_EQ(e.soc.goals.back().name, std::string("Borges"));
  CHECK_EQ(status_text(e, o, ""),
           std::string("After extra time | FIFA World Cup, Round of 32 | Goals ARG 29' Messi, 92' Martinez, "
                       "111' Borges (OG); CPV 59' Duarte, 103' Lopes Cabral"));
  CHECK_EQ(soccer_situation(e), std::string("111' Borges"));
  GameSnapshot og = e;
  og.soc.goals.back().clock = "55'";
  CHECK_EQ(soccer_situation(og), std::string("55' Borges"));  // "55' Borges OG" is 13
  CHECK_EQ(soccer_situation(og, 13), std::string("55' Borges OG"));
  og.soc.goals.back().name = "Hany";
  CHECK_EQ(soccer_situation(og), std::string("55' Hany OG"));

  // Extra time while it is played (no recording yet: built from the final).
  GameSnapshot et = e;
  et.state = GameState::IN;
  et.soc.status = "STATUS_SECOND_HALF_EXTRA_TIME";
  et.period = 3;
  et.display_clock = "105'+1'";
  CHECK_EQ(clock_text(et), std::string("ET 105'+1'"));
  et.period = 4;
  et.display_clock = "120'+12'";
  CHECK_EQ(clock_text(et), std::string("120'+12'"));
  et.period = 3;
  et.soc.status = "STATUS_HALFTIME_ET";
  CHECK_EQ(clock_text(et), std::string("ET HT"));
  GameSnapshot x;
  CHECK(!parse_event_str(slurp("fixtures/event_soccer_aet.json"), Sport::SOCCER, kArsenal, x));  // not in it
  CHECK(!parse_event_str(slurp("fixtures/event_soccer_aet.json"), Sport::SOCCER, kArgentina, x, "760501"));  // some other event
}

static void test_soccer_splash() {
  GameSnapshot prev = soccer_event("fixtures/event_soccer_stoppage.json", kInter);
  auto after = [&](int us, int them) {
    GameSnapshot c = prev;
    c.team_score += us;
    c.opp_score += them;
    return c;
  };
  CHECK_EQ(decide_splash(prev, after(1, 0), true).text, std::string("GOAL!"));
  CHECK_EQ(decide_splash(prev, after(1, 0), true).color, (uint32_t) 0xC60000);
  CHECK_EQ(decide_splash(prev, after(0, 1), true).text, std::string("COR GOAL"));
  CHECK_EQ(decide_splash(prev, after(0, 1), false).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(1, 0), true, true).text, std::string("INT GOAL"));
  CHECK_EQ(decide_splash(prev, after(0, 0), true).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(-1, 0), true).text, std::string(""));  // a goal taken back
  GameSnapshot pre = prev;
  pre.state = GameState::PRE;
  CHECK_EQ(decide_splash(pre, after(1, 0), true).text, std::string(""));
  for (const char *word : {"GOAL!", "REMO GOAL", "INT WINS!"})
    CHECK(strlen(word) <= 10);
}

static void test_soccer_text_helpers() {
  CHECK_EQ(ascii_fold("Guimar\xC3\xA3" "es"), std::string("Guimaraes"));
  CHECK_EQ(ascii_fold("Gy\xC3\xB6keres"), std::string("Gyokeres"));
  CHECK_EQ(ascii_fold("\xC3\x98" "degaard"), std::string("Odegaard"));
  CHECK_EQ(ascii_fold("Demb\xC3\xA9l\xC3\xA9"), std::string("Dembele"));
  CHECK_EQ(ascii_fold("\xC5\x81ukasz \xC5\x9E" "ahin"), std::string("Lukasz Sahin"));
  CHECK_EQ(ascii_fold("\xC8\x98tefan"), std::string("Stefan"));
  CHECK_EQ(ascii_fold("O\xE2\x80\x99Reilly"), std::string("O'Reilly"));
  CHECK_EQ(ascii_fold("Stra\xC3\x9F" "e \xC3\x86"), std::string("Strasse AE"));
  CHECK_EQ(ascii_fold("Plain ASCII 1-0"), std::string("Plain ASCII 1-0"));
  CHECK_EQ(ascii_fold("bad\xFF\xC3"), std::string("bad"));
  CHECK_EQ(ascii_fold("\xE6\x97\xA5\xE6\x9C\xAC"), std::string(""));
  CHECK_EQ(soccer_board_record("6-10-12"), std::string("6-10-12"));
  CHECK_EQ(soccer_board_record("12-10-12"), std::string("46 pts"));  // too wide under a logo
  CHECK_EQ(soccer_board_record(""), std::string(""));
  CHECK_EQ(soccer_board_record("999-999-999"), std::string("3996 pts"));
  // A count past three digits (the fuzzer's 1068893704 overflowed 3 * w).
  CHECK_EQ(soccer_board_record("1068893704-1-1"), std::string("1068893704-1-1"));
  CHECK_EQ(soccer_board_record("12-2147483648-1"), std::string("12-2147483648-1"));
}

// A club's next game across its league and cups (team endpoints recorded
// 2026-10-08: Arsenal's league game is on 10/10, its Champions League game
// on 10/13, and it is not in the Europa League).
static void test_soccer_cups() {
  CHECK_EQ(team_url(League::EPL, kArsenal, "uefa.champions"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/uefa.champions/teams/359"));
  CHECK_EQ(event_url(League::EPL, "401915417", "uefa.champions"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/uefa.champions/scoreboard/401915417"));
  CHECK_EQ(team_url(League::MLS, kMiami, "concacaf.champions"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/concacaf.champions/teams/20232"));
  CHECK_EQ(event_url(League::EPL, "401879268", nullptr), event_url(League::EPL, "401879268"));
  CHECK_EQ(team_url(League::NFL, 6, nullptr), team_url(League::NFL, 6));
  CHECK_EQ(std::string(league_cup(League::EPL, 0)), std::string("uefa.champions"));
  CHECK_EQ(std::string(league_cup(League::EPL, 2)), std::string("uefa.europa.conf"));
  CHECK(league_cup(League::EPL, 3) == nullptr);
  CHECK_EQ(std::string(league_cup(League::MLS, 0)), std::string("concacaf.champions"));
  CHECK_EQ(league_cup_count(League::EPL), (size_t) 3);
  CHECK_EQ(league_cup_count(League::MLS), (size_t) 1);
  CHECK_EQ(league_cup_count(League::NFL), (size_t) 0);
  CHECK_EQ(league_cup_count(League::MLB), (size_t) 0);

  std::vector<Schedule> comps(4);
  CHECK(parse_team_str(slurp("fixtures/team_epl_ars.json"), comps[0]));
  CHECK(parse_team_str(slurp("fixtures/team_ucl_ars.json"), comps[1]));
  CHECK(parse_team_str(slurp("fixtures/team_uel_ars.json"), comps[2]));
  for (size_t i = 0; i < comps.size(); i++) {
    adopt_league(comps[i], League::EPL);
    adopt_comp(comps[i], i == 0 ? nullptr : league_cup(League::EPL, i - 1));
  }
  CHECK_EQ(comps[1].event_id, std::string("401915417"));
  CHECK_EQ(comps[1].kickoff_epoch, parse_iso8601_z("2026-10-13T19:00Z"));
  CHECK(comps[2].valid);  // not in the Europa League: a good read with no game
  CHECK(comps[2].event_id.empty());
  CHECK(!comps[3].valid);  // the Conference League not read yet

  int64_t league_ko = parse_iso8601_z("2026-10-10T11:30Z");
  // Before the league game: it is first.
  CHECK_EQ(pick_schedule(comps, parse_iso8601_z("2026-10-08T00:00Z"), ""), 0);
  // While it is on, and just after, ESPN still names it: it stays.
  CHECK_EQ(pick_schedule(comps, league_ko + 2 * 3600, ""), 0);
  // Seen to finish, or long over: the cup game is next.
  CHECK_EQ(pick_schedule(comps, league_ko + 2 * 3600, "401879268"), 1);
  CHECK_EQ(pick_schedule(comps, league_ko + 5 * 3600, ""), 1);
  // A cup game first in the week.
  std::vector<Schedule> early = comps;
  early[1].kickoff_epoch = league_ko - 3 * 86400;
  CHECK_EQ(pick_schedule(early, parse_iso8601_z("2026-10-06T00:00Z"), ""), 1);
  // Same kickoff: the league's read wins.
  early[1].kickoff_epoch = league_ko;
  CHECK_EQ(pick_schedule(early, 0, ""), 0);
  // Nothing else: a game that is over is still better than none.
  std::vector<Schedule> only_over = {comps[0], comps[2]};
  CHECK_EQ(pick_schedule(only_over, league_ko + 10 * 3600, ""), 0);
  // No read names a game (an off season): none.
  std::vector<Schedule> none = {comps[2], comps[3]};
  CHECK_EQ(pick_schedule(none, 0, ""), -1);
  CHECK_EQ(pick_schedule(std::vector<Schedule>{}, 0, ""), -1);
  // The league read had no game but a cup does (MLS's winter, the CONCACAF Champions Cup).
  std::vector<Schedule> cup_only = {comps[2], comps[1]};
  CHECK_EQ(pick_schedule(cup_only, 0, ""), 1);

  // The boot cache has no room for the cup: a cup game saves a blank record
  // that never loads, so an older league game cannot come back either.
  TeamCache c{};
  c.team_id = 99;
  CHECK(team_cache_save(comps[1], League::EPL, kArsenal, league_ko, c));
  CHECK_EQ(c.team_id, (uint32_t) 0);
  Schedule loaded;
  CHECK(!team_cache_load(c, League::EPL, kArsenal, league_ko, loaded));
  CHECK(team_cache_save(comps[0], League::EPL, kArsenal, league_ko - 86400, c));
  CHECK(team_cache_load(c, League::EPL, kArsenal, league_ko - 3600, loaded));
  CHECK_EQ(loaded.event_id, std::string("401879268"));
  CHECK(loaded.comp_slug == nullptr);
}

// NBA (preseason) and WNBA (semifinal) single-event documents recorded
// 2026-10-07 (see fixtures/README.md).
static const uint32_t kPacers = 11, kWolves = 16, kThunder = 25, kBlazers = 22, kDream = 20, kLiberty = 9;

static GameSnapshot hoops_event(const char *file, uint32_t team) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK(parse_event_str(json, Sport::BASKETBALL, team, s, root_id(json)));
  return s;
}

static void test_nba_urls() {
  CHECK_EQ(team_url(League::NBA, 11), std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/nba/teams/11"));
  CHECK_EQ(event_url(League::WNBA, "401918297"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/wnba/scoreboard/401918297"));
  CHECK_EQ(team_logo_url(League::NBA, 11, "IND"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nba/500-dark/scoreboard/ind.png&w=64&h=64"));
  CHECK_EQ(team_logo_url(League::WNBA, 129689, "GS"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/wnba/500-dark/gs.png&w=64&h=64"));
  CHECK_EQ(std::string(league_key(League::NBA)), std::string("nba"));
  CHECK_EQ(std::string(league_key(League::WNBA)), std::string("wnba"));
  for (const char *key : {"nba", "wnba"}) {
    const LeagueInfo *l = league_by_key(key);
    CHECK(l != nullptr && !l->season_list && l->linger_min == 30);
    CHECK(l != nullptr && l->sport == Sport::BASKETBALL);
  }
  int nba = 0, wnba = 0;
  for (size_t i = 0; i < kTeamCount; i++) {
    const Team &t = kTeams[i];
    nba += t.league == League::NBA;
    wnba += t.league == League::WNBA;
    if (t.league == League::NBA && t.espn_id == kPacers)
      CHECK_EQ(team_option(t), std::string("NBA: Indiana Pacers"));
    if (t.league == League::WNBA && t.espn_id == kLiberty)
      CHECK_EQ(team_option(t), std::string("WNBA: New York Liberty"));
  }
  CHECK_EQ(nba, 30);
  CHECK(wnba >= 13);
}

static void test_nba_team() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_nba_ind.json"), s));
  CHECK_EQ(s.event_id, std::string("401914123"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-10-07T23:00Z"));
  CHECK_EQ(s.team_color, std::string("0c2340"));
  CHECK_EQ(s.team_record, std::string("0-0"));
  Schedule w;
  CHECK(parse_team_str(slurp("fixtures/team_wnba_ny.json"), w));
  CHECK_EQ(w.event_id, std::string("401918297"));
  CHECK_EQ(w.kickoff_epoch, parse_iso8601_z("2026-10-07T23:30Z"));
  CHECK_EQ(w.team_color, std::string("86cebc"));
  CHECK_EQ(w.team_record, std::string("26-18"));
}

static void test_nba_live() {
  // 8:27 in the 3rd, IND 74 MIN 73. IND is home.
  GameSnapshot g = hoops_event("fixtures/event_nba_live.json", kPacers);
  CHECK(g.valid);
  CHECK(g.sport == Sport::BASKETBALL);
  CHECK(g.state == GameState::IN);
  CHECK(g.team_home);
  CHECK_EQ(g.team_abbr, std::string("IND"));
  CHECK_EQ(g.opp_abbr, std::string("MIN"));
  CHECK_EQ(g.team_score, 74);
  CHECK_EQ(g.opp_score, 73);
  CHECK_EQ(g.period, 3);
  CHECK_EQ((int) g.nba.regulation, 4);
  CHECK(g.nba.phase == Phase::PLAY);
  CHECK_EQ(g.nba.series, std::string(""));
  CHECK_EQ((int) g.nba.team_wins, -1);
  CHECK_EQ(g.possession, 0);
  CHECK_EQ(g.team_timeouts, 0);
  CHECK_EQ(g.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nba/500-dark/scoreboard/ind.png&w=64&h=64"));
  CHECK_EQ(clock_text(g), std::string("8:27 3rd"));
  CHECK_EQ(basketball_series(g), std::string(""));
  const std::string play = "Jaden McDaniels makes 14-foot driving floating jump shot (Jonathan Kuminga assists)";
  CHECK_EQ(g.last_play, play);
  TickerOptions o;
  o.clock = false;
  CHECK_EQ(status_text(g, o, ""), play);
  TickerOptions clock = o;
  clock.clock = true;
  CHECK_EQ(status_text(g, clock, ""), "8:27 - 3rd | " + play);
  TickerOptions quiet = o;
  quiet.last_play = false;
  CHECK_EQ(status_text(g, quiet, ""), std::string("8:27 - 3rd"));

  // Under a minute ESPN sends tenths and no minutes.
  GameSnapshot c = hoops_event("fixtures/event_nba_clock.json", kThunder);
  CHECK_EQ(c.display_clock, std::string("12.3"));
  CHECK_EQ(clock_text(c), std::string("12.3 1st"));
  CHECK_EQ(status_text(c, o, ""), std::string("Brayden Burries makes 17-foot pullup jump shot"));

  // Between quarters: the clock row names the break, the ticker the records.
  GameSnapshot e = hoops_event("fixtures/event_nba_end.json", kThunder);
  CHECK(e.nba.phase == Phase::END_PERIOD);
  CHECK_EQ(clock_text(e), std::string("End 1st"));
  CHECK_EQ(status_text(e, o, ""), std::string("OKC 0-1 | MIL 0-1"));

  // Overtime, from the period number: OT is period 5 with four quarters.
  GameSnapshot ot = g;
  ot.period = 5;
  CHECK_EQ(clock_text(ot), std::string("8:27 OT"));
  ot.period = 6;
  CHECK_EQ(clock_text(ot), std::string("8:27 2OT"));
  ot.nba.phase = Phase::END_PERIOD;
  CHECK_EQ(clock_text(ot), std::string("End 2OT"));
  ot.period = 4;
  CHECK_EQ(clock_text(ot), std::string("End 4th"));
  ot.nba.phase = Phase::OTHER;
  ot.short_detail = "Delayed";
  CHECK_EQ(clock_text(ot), std::string("Delayed"));
}

static void test_wnba_live() {
  // A semifinal, ATL leads the series 1-0: 4.5 left in the 2nd.
  GameSnapshot g = hoops_event("fixtures/event_wnba_live.json", kLiberty);
  CHECK(g.state == GameState::IN);
  CHECK(!g.team_home);
  CHECK_EQ(g.team_abbr, std::string("NY"));
  CHECK_EQ(g.team_score, 54);
  CHECK_EQ(g.opp_score, 56);
  CHECK_EQ(clock_text(g), std::string("4.5 2nd"));
  CHECK_EQ(g.nba.series, std::string("ATL leads series 1-0"));
  CHECK_EQ((int) g.nba.team_wins, 0);
  CHECK_EQ((int) g.nba.opp_wins, 1);
  CHECK_EQ(basketball_series(g), std::string("ATL lead 1-0"));
  CHECK_EQ(g.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/wnba/500-dark/ny.png&w=64&h=64"));
  TickerOptions o;
  o.clock = false;
  CHECK_EQ(status_text(g, o, ""), std::string("Rhyne Howard makes 23-foot three point jumper (Jordin Canada assists)"));
  GameSnapshot a = hoops_event("fixtures/event_wnba_live.json", kDream);
  CHECK_EQ((int) a.nba.team_wins, 1);
  CHECK_EQ(basketball_series(a), std::string("ATL lead 1-0"));

  GameSnapshot h = hoops_event("fixtures/event_wnba_half.json", kLiberty);
  CHECK(h.nba.phase == Phase::HALFTIME);
  CHECK_EQ(clock_text(h), std::string("Halftime"));
  CHECK_EQ(status_text(h, o, ""), std::string("ATL leads series 1-0"));

  // The game went to overtime: period 5, "20.8 - OT".
  GameSnapshot ot = hoops_event("fixtures/event_wnba_ot.json", kDream);
  CHECK_EQ(ot.period, 5);
  CHECK_EQ(clock_text(ot), std::string("20.8 OT"));
  CHECK_EQ(basketball_series(ot), std::string("ATL lead 1-0"));

  // ESPN had not counted this game in the series yet when it went final.
  GameSnapshot f = hoops_event("fixtures/event_wnba_final.json", kDream);
  CHECK(f.state == GameState::POST);
  CHECK_EQ(f.team_score, 101);
  CHECK_EQ(f.opp_score, 98);
  CHECK_EQ(status_text(f, o, ""), std::string("Final/OT | ATL leads series 1-0"));
  CHECK_EQ(decide_splash(ot, f, true).text, std::string("ATL WINS!"));
  CHECK_EQ(decide_splash(ot, f, true).color, (uint32_t) 0xe31837);
  GameSnapshot ny_ot = hoops_event("fixtures/event_wnba_ot.json", kLiberty);
  GameSnapshot ny_f = hoops_event("fixtures/event_wnba_final.json", kLiberty);
  CHECK_EQ(decide_splash(ny_ot, ny_f, true).text, std::string(""));
  CHECK_EQ(decide_splash(ny_ot, ny_f, true, true).text, std::string("ATL WINS!"));

  // The short series line fits the 12-character situation row.
  GameSnapshot s = g;
  s.nba.team_wins = s.nba.opp_wins = 2;
  CHECK_EQ(basketball_series(s), std::string("Series 2-2"));
  s.team_abbr = "UTAH";
  s.nba.team_wins = 3;
  CHECK_EQ(basketball_series(s), std::string("UTAH up 3-2"));
}

static void test_nba_pre_post() {
  GameSnapshot p = hoops_event("fixtures/event_nba_pre.json", kBlazers);
  CHECK(p.state == GameState::PRE);
  CHECK_EQ(p.odds, std::string("POR -14.5"));
  CHECK_EQ(p.over_under, std::string("216.5"));
  CHECK_EQ(p.tv, std::string("NBA TV"));
  TickerOptions o;
  CHECK_EQ(status_text(p, o, "Today 10:00 PM"),
           std::string("Today 10:00 PM | POR -14.5 | O/U 216.5 | NBA TV | Moda Center"));
  TickerOptions no_odds;
  no_odds.odds = false;
  CHECK_EQ(status_text(p, no_odds, ""), std::string("10/7 - 10:00 PM EDT | Moda Center"));
  // A single 64-wide panel has no room for the records, so the ticker carries them
  TickerOptions narrow = no_odds;
  narrow.records = true;
  CHECK_EQ(status_text(p, narrow, ""), std::string("10/7 - 10:00 PM EDT | POR 0-0, GS 1-1 | Moda Center"));
  CHECK_EQ(basketball_series(p), std::string(""));
  // A preseason game lists no leaders; a regular-season one lists points per
  // game, and an arena has no forecast
  CHECK_EQ(p.team_leader, std::string(""));
  GameSnapshot m = hoops_event("fixtures/event_nba_pre_leaders.json", 14);  // MIA at CHA
  CHECK_EQ(m.team_leader, std::string("Antetokounmpo 35.0 PPG"));
  CHECK_EQ(m.opp_leader, std::string("Allen 21.0 PPG"));
  CHECK_EQ(m.weather, std::string(""));
  CHECK_EQ((int) m.week, 0);
  CHECK_EQ(status_text(m, no_odds, ""),
           std::string("10/24 - 6:00 PM EDT | Antetokounmpo 35.0 PPG | Allen 21.0 PPG | Spectrum Center"));

  GameSnapshot f = hoops_event("fixtures/event_nba_final.json", kPacers);
  CHECK(f.state == GameState::POST);
  CHECK(f.team_winner);
  CHECK_EQ(f.team_score, 123);
  CHECK_EQ(f.opp_score, 112);
  CHECK_EQ(status_text(f, o, ""), std::string("Final | IND 0-0 | MIN 1-0"));
  f.short_detail = "Final/OT";
  CHECK_EQ(status_text(f, o, ""), std::string("Final/OT | IND 0-0 | MIN 1-0"));

  GameSnapshot x;
  CHECK(!parse_event_str(slurp("fixtures/event_nba_live.json"), Sport::BASKETBALL, kBlazers, x));  // not in it
  CHECK(!parse_event_str(slurp("fixtures/event_nba_live.json"), Sport::BASKETBALL, kPacers, x, "401914124"));  // some other event
  CHECK(!parse_event_str("{nope", Sport::BASKETBALL, kPacers, x));
}

static void test_nba_splash() {
  GameSnapshot prev = hoops_event("fixtures/event_nba_live.json", kPacers);
  auto after = [&](int us, int them, GameState st) {
    GameSnapshot c = prev;
    c.team_score += us;
    c.opp_score += them;
    c.state = st;
    c.completed = st == GameState::POST;
    return c;
  };
  // Baskets never splash, whoever scores.
  CHECK_EQ(decide_splash(prev, after(3, 0, GameState::IN), true).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(0, 2, GameState::IN), true).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(2, 0, GameState::IN), true, true).text, std::string(""));
  Splash win = decide_splash(prev, after(10, 0, GameState::POST), true);
  CHECK_EQ(win.text, std::string("IND WINS!"));
  CHECK_EQ(win.color, (uint32_t) 0x0c2340);
  CHECK_EQ(decide_splash(prev, after(0, 10, GameState::POST), true).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(0, 10, GameState::POST), true, true).text, std::string("MIN WINS!"));
  GameSnapshot fin = after(10, 0, GameState::POST);
  CHECK_EQ(decide_splash(fin, fin, true).text, std::string(""));
  // The recorded final after the recorded live poll: IND won 123-112.
  GameSnapshot final_doc = hoops_event("fixtures/event_nba_final.json", kPacers);
  CHECK_EQ(decide_splash(prev, final_doc, true).text, std::string("IND WINS!"));
  CHECK_EQ(decide_splash(hoops_event("fixtures/event_nba_live.json", kWolves),
                         hoops_event("fixtures/event_nba_final.json", kWolves), true)
               .text,
           std::string(""));
}

// Men's college basketball, recorded 2026-10-07 in the off season: last
// season's finals (ESPN serves only the final state of a past game) and the
// coming season's opener (see fixtures/README.md).
static const uint32_t kMichigan = 130, kUConn = 41, kPenn = 219;

static void test_mcbb_urls() {
  CHECK_EQ(team_url(League::MCBB, 130),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/teams/130"));
  CHECK_EQ(event_url(League::MCBB, "401856600"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard/401856600"));
  CHECK_EQ(team_logo_url(League::MCBB, 130, "MICH"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/130.png&w=64&h=64"));
  const LeagueInfo *l = league_by_key("mcbb");
  CHECK(l != nullptr && !l->season_list && l->logo_by_id && l->sport == Sport::BASKETBALL);
  CHECK_EQ(std::string(league_path(League::MCBB)), std::string("mens-college-basketball"));
  int n = 0;
  for (size_t i = 0; i < kTeamCount; i++) {
    const Team &t = kTeams[i];
    if (t.league != League::MCBB)
      continue;
    n++;
    if (t.espn_id == kMichigan)
      CHECK_EQ(team_option(t), std::string("NCAAM: Michigan Wolverines"));
  }
  CHECK(n >= 350);
}

static void test_mcbb() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_mcbb_mich.json"), s));
  CHECK_EQ(s.event_id, std::string("401925733"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-11-02T23:00Z"));
  CHECK_EQ(s.team_color, std::string("00274c"));

  GameSnapshot p = hoops_event("fixtures/event_mcbb_pre.json", kMichigan);
  CHECK(p.state == GameState::PRE);
  CHECK_EQ((int) p.nba.regulation, 2);
  CHECK_EQ(p.opp_abbr, std::string("OAK"));
  CHECK_EQ(p.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/130.png&w=64&h=64"));
  TickerOptions o;
  CHECK_EQ(status_text(p, o, "Nov 2 6:00 PM"), std::string("Nov 2 6:00 PM | BTN | Crisler Center"));

  // The national championship: two halves, no series.
  GameSnapshot f = hoops_event("fixtures/event_mcbb_final.json", kUConn);
  CHECK(f.state == GameState::POST);
  CHECK_EQ((int) f.nba.regulation, 2);
  CHECK_EQ(f.team_score, 63);
  CHECK_EQ(f.opp_score, 69);
  CHECK_EQ(status_text(f, o, ""), std::string("Final | CONN 34-6 | MICH 37-3"));
  CHECK_EQ(basketball_series(f), std::string(""));

  // Overtime is period 3 after two halves.
  GameSnapshot ot = hoops_event("fixtures/event_mcbb_ot.json", kPenn);
  CHECK_EQ(ot.period, 3);
  CHECK(ot.team_winner);
  CHECK_EQ(status_text(ot, o, "").rfind("Final/OT | PENN ", 0), (size_t) 0);

  // Past games come back final only, so the live clock is checked on a copy.
  GameSnapshot live = f;
  live.state = GameState::IN;
  live.nba.phase = Phase::PLAY;
  live.period = 1;
  live.display_clock = "12:34";
  CHECK_EQ(clock_text(live), std::string("12:34 1st"));
  live.period = 2;
  CHECK_EQ(clock_text(live), std::string("12:34 2nd"));
  live.period = 3;
  CHECK_EQ(clock_text(live), std::string("12:34 OT"));
  live.period = 4;
  CHECK_EQ(clock_text(live), std::string("12:34 2OT"));
  live.nba.phase = Phase::HALFTIME;
  live.period = 1;
  CHECK_EQ(clock_text(live), std::string("Halftime"));
  live.nba.phase = Phase::END_PERIOD;
  live.period = 2;
  CHECK_EQ(clock_text(live), std::string("End 2nd"));
}

// A real home run (TB @ NYY, 2026-10-07, Top 6th): ESPN types the play
// "Play Result", so the splash relies on the play text.
static void test_mlb_home_run() {
  const uint32_t kRays = 30, kYankees = 10;
  GameSnapshot before = mlb_event("fixtures/event_mlb_hr_before.json", kRays);
  GameSnapshot after = mlb_event("fixtures/event_mlb_hr.json", kRays);
  CHECK_EQ(after.mlb.play_type, std::string("Play Result"));
  CHECK(after.last_play.find("homered") != std::string::npos);
  CHECK_EQ(after.team_score - before.team_score, 2);
  CHECK_EQ(decide_splash(before, after, true).text, std::string("HOME RUN!"));
  GameSnapshot nyy_before = mlb_event("fixtures/event_mlb_hr_before.json", kYankees);
  GameSnapshot nyy_after = mlb_event("fixtures/event_mlb_hr.json", kYankees);
  CHECK_EQ(decide_splash(nyy_before, nyy_after, true).text, std::string("TB HOME RUN"));
}

// NHL single-event documents (see fixtures/README.md)
static const uint32_t kJets = 28, kAvalanche = 17, kPenguins = 16, kCapitals = 23, kOilers = 6, kHurricanes = 7;

static GameSnapshot nhl_event(const char *file, uint32_t team) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK(parse_event_str(json, Sport::HOCKEY, team, s, root_id(json)));
  return s;
}

static void test_nhl_urls() {
  CHECK_EQ(team_url(League::NHL, 6), std::string("https://site.api.espn.com/apis/site/v2/sports/hockey/nhl/teams/6"));
  CHECK_EQ(event_url(League::NHL, "401891830"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/hockey/nhl/scoreboard/401891830"));
  CHECK_EQ(team_logo_url(League::NHL, 129764, "UTAH"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nhl/500-dark/scoreboard/utah.png&w=64&h=64"));
  CHECK_EQ(std::string(league_key(League::NHL)), std::string("nhl"));
  const LeagueInfo *nhl = league_by_key("nhl");
  CHECK(nhl != nullptr && !nhl->season_list && nhl->linger_min == 30);
  CHECK(league_sport(League::NHL) == Sport::HOCKEY);
  int count = 0;
  for (size_t i = 0; i < kTeamCount; i++) {
    if (kTeams[i].league != League::NHL)
      continue;
    count++;
    if (kTeams[i].espn_id == kPenguins)
      CHECK_EQ(team_option(kTeams[i]), std::string("NHL: Pittsburgh Penguins"));
  }
  CHECK_EQ(count, 32);
}

static void test_nhl_team() {
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_nhl_edm.json"), s));
  CHECK_EQ(s.event_id, std::string("401892456"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-10-08T02:00Z"));
  CHECK_EQ(s.team_color, std::string("00205b"));
  CHECK_EQ(s.team_record, std::string("2-0-1"));  // W-L-OTL
}

static void test_nhl_live() {
  // COL @ WPG, 3:21 left in the 1st, a delay of game penalty the last play
  GameSnapshot g = nhl_event("fixtures/event_nhl_live.json", kJets);
  CHECK(g.valid);
  CHECK(g.sport == Sport::HOCKEY);
  CHECK(g.state == GameState::IN);
  CHECK(g.team_home);
  CHECK(!g.nhl.playoffs);
  CHECK_EQ(g.nhl.series, std::string(""));
  CHECK_EQ(g.team_abbr, std::string("WPG"));
  CHECK_EQ(g.opp_abbr, std::string("COL"));
  CHECK_EQ(g.team_score, 0);
  CHECK_EQ(g.opp_score, 0);
  CHECK_EQ(g.period, 1);
  CHECK_EQ(g.possession, 0);
  CHECK_EQ(g.team_timeouts, 0);
  CHECK_EQ(g.down_distance, std::string(""));
  CHECK_EQ(g.team_record, std::string("2-0-1"));
  CHECK_EQ(g.opp_record, std::string("2-0-0"));
  CHECK_EQ(g.team_logo, std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nhl/500-dark/scoreboard/wpg.png&w=64&h=64"));
  CHECK_EQ(clock_text(g), std::string("3:21 1st"));
  const std::string penalty = "Delaying Game - Puck over glass served by Fedor Svechkov";
  CHECK_EQ(g.last_play, penalty);
  TickerOptions o;
  o.clock = false;
  CHECK_EQ(status_text(g, o, ""), penalty);
  TickerOptions with_clock;
  CHECK_EQ(status_text(g, with_clock, ""), "3:21 - 1st | " + penalty);
  TickerOptions quiet = o;
  quiet.last_play = false;
  CHECK_EQ(status_text(g, quiet, ""), std::string("3:21 - 1st"));
  GameSnapshot nameless = g;  // seen live: a goal before ESPN had the scorer
  nameless.last_play = ", assists: Neal Pionk (1), Dylan Samberg (1)";
  CHECK_EQ(status_text(nameless, o, ""), std::string("assists: Neal Pionk (1), Dylan Samberg (1)"));

  // The same game from the visitors' side
  GameSnapshot c = nhl_event("fixtures/event_nhl_live.json", kAvalanche);
  CHECK(!c.team_home);
  CHECK_EQ(c.team_abbr, std::string("COL"));
  CHECK_EQ(c.team_record, std::string("2-0-0"));

  // Intermission: PIT @ WSH after the 1st, the ticker shows the records
  GameSnapshot e = nhl_event("fixtures/event_nhl_end.json", kPenguins);
  CHECK(e.state == GameState::IN);
  CHECK(!e.team_home);
  CHECK_EQ(e.team_score, 1);
  CHECK_EQ(e.opp_score, 2);
  CHECK_EQ(clock_text(e), std::string("End of 1st"));
  CHECK_EQ(status_text(e, o, ""), std::string("PIT 2-1-0 | WSH 1-1-0"));
  CHECK_EQ(status_text(e, quiet, ""), std::string("PIT 2-1-0 | WSH 1-1-0"));
  GameSnapshot w = nhl_event("fixtures/event_nhl_end.json", kCapitals);
  CHECK_EQ(w.team_score, 2);
  CHECK_EQ(status_text(w, o, ""), std::string("WSH 1-1-0 | PIT 2-1-0"));

  // Overtime, then the shootout (regular season) or more overtime (playoffs)
  GameSnapshot ot = g;
  ot.period = 4;
  ot.display_clock = "3:10";
  ot.short_detail = "3:10 - OT";
  CHECK_EQ(clock_text(ot), std::string("3:10 OT"));
  ot.display_clock = "0:00";
  ot.short_detail = "End of OT";
  CHECK_EQ(clock_text(ot), std::string("End of OT"));
  GameSnapshot so = ot;
  so.period = 5;
  so.short_detail = "Shootout";
  CHECK_EQ(clock_text(so), std::string("SO"));
  so.short_detail = "0:00 - SO";
  CHECK_EQ(clock_text(so), std::string("SO"));
  GameSnapshot ot2 = so;
  ot2.nhl.playoffs = true;
  ot2.display_clock = "12:00";
  ot2.short_detail = "12:00 - 2OT";
  CHECK_EQ(clock_text(ot2), std::string("12:00 2OT"));
  ot2.period = 6;
  CHECK_EQ(clock_text(ot2), std::string("12:00 3OT"));
}

// The pre-game ticker's week, forecast and leaders, and what keeps them out.
static void test_pregame_extras() {
  // RUTG at NU on 2026-10-24: ESPN sent "33" as the forecast's words and
  // "Clear" as its condition id
  GameSnapshot nu;
  CHECK(parse_event_str(slurp("fixtures/event_ncaa_pre_weather.json"), Sport::FOOTBALL, 77, nu, "401858498"));
  CHECK(nu.state == GameState::PRE);
  CHECK_EQ((int) nu.week, 8);
  CHECK_EQ(nu.weather, std::string("54F Clear"));
  CHECK_EQ(nu.team_leader, std::string("Chiles 1,398 yds 9 TD"));
  CHECK_EQ(nu.opp_leader, std::string("Lonergan 1,066 yds 10 TD"));
  TickerOptions o;
  o.odds = false;
  CHECK_EQ(status_text(nu, o, "Sat 2:30 PM"),
           std::string("Week 8 | Sat 2:30 PM | Chiles 1,398 yds 9 TD | Lonergan 1,066 yds 10 TD | 54F Clear at Ryan Field"));
  // A named game leads instead of the week, and a venue that repeats the
  // name leaves the forecast on its own
  GameSnapshot bowl = nu;
  bowl.game_name = bowl.venue = "Rose Bowl";
  CHECK_EQ(espn::detail::pregame_name(bowl), std::string("Rose Bowl"));
  CHECK_EQ(espn::detail::venue_line(bowl), std::string("54F Clear"));
  bowl.weather.clear();
  CHECK_EQ(espn::detail::venue_line(bowl), std::string(""));
  // A dome's forecast is left out (ATL's indoor stadium, "Sunny" in the feed)
  GameSnapshot atl;
  CHECK(parse_event_str(slurp("fixtures/event_nfl_pre.json"), Sport::FOOTBALL, 1, atl));
  CHECK_EQ(atl.weather, std::string(""));
  CHECK_EQ((int) atl.week, 6);
  // Once the game is on: no week, forecast or leaders
  GameSnapshot live;
  CHECK(parse_event_str(slurp("fixtures/event_ncaa_live.json"), Sport::FOOTBALL, 2229, live, "401871053"));
  CHECK_EQ((int) live.week, 0);
  CHECK_EQ(live.weather, std::string(""));
  GameSnapshot top = mlb_event("fixtures/event_mlb_top.json", kBraves);
  CHECK_EQ(top.team_leader, std::string(""));
  CHECK_EQ(top.opp_leader, std::string(""));
  // A postseason game has no week
  CHECK_EQ((int) mlb_event("fixtures/event_mlb_pre.json", kBrewers).week, 0);

  CHECK_EQ(espn::detail::stat_leader("G. Antetokounmpo", "35.0", "PPG"), std::string("Antetokounmpo 35.0 PPG"));
  CHECK_EQ(espn::detail::stat_leader("M. Penix Jr.", "3", "pts"), std::string("Penix Jr. 3 pts"));
  CHECK_EQ(espn::detail::stat_leader("C. McDavid", "0", "pts"), std::string(""));
  CHECK_EQ(espn::detail::stat_leader("C. McDavid", "0.0", "PPG"), std::string(""));
  CHECK_EQ(espn::detail::stat_leader("C. McDavid", "--", "pts"), std::string(""));
  CHECK_EQ(espn::detail::stat_leader("C. McDavid", "1.2.3", "PPG"), std::string(""));
  CHECK_EQ(espn::detail::stat_leader("C. McDavid", "", "pts"), std::string(""));
  CHECK_EQ(espn::detail::stat_leader("", "9", "pts"), std::string(""));

  // The forecast's words and temperature, and what makes them unusable
  auto wx = [](const char *json) {
    JsonDocument d;
    deserializeJson(d, json);
    return espn::detail::weather_text(d.as<JsonObjectConst>());
  };
  CHECK_EQ(wx(R"({"temperature":76,"displayValue":"Cloudy","conditionId":"7"})"), std::string("76F Cloudy"));
  CHECK_EQ(wx(R"({"temperature":-4,"displayValue":"Snow","conditionId":"22"})"), std::string("-4F Snow"));
  CHECK_EQ(wx(R"({"temperature":71.6,"displayValue":"7","conditionId":"7"})"), std::string("72F"));
  CHECK_EQ(wx(R"({"temperature":"hot","displayValue":"Sunny"})"), std::string(""));
  CHECK_EQ(wx(R"({"displayValue":"Sunny"})"), std::string(""));
  CHECK_EQ(wx(R"({"temperature":400,"displayValue":"Sunny"})"), std::string(""));
  CHECK_EQ(wx(R"({"temperature":80,"displayValue":"Mostly sunny with a chance of afternoon thunderstorms"})"),
           std::string("80F Mostly sunny with a chance of"));
}

static void test_nhl_pre_post() {
  GameSnapshot p = nhl_event("fixtures/event_nhl_pre.json", kOilers);
  CHECK(p.state == GameState::PRE);
  CHECK(!p.team_home);
  CHECK_EQ(p.odds, std::string("EDM -135"));
  CHECK_EQ(p.over_under, std::string("6.5"));
  CHECK_EQ(p.tv, std::string("ESPN+"));
  TickerOptions o;
  CHECK_EQ(status_text(p, o, "Today 10:00 PM"),
           std::string("Today 10:00 PM | EDM -135 | O/U 6.5 | ESPN+ | McDavid 9 pts | Carlsson 5 pts | Honda Center"));
  TickerOptions no_odds;
  no_odds.odds = false;
  CHECK_EQ(status_text(p, no_odds, ""), std::string("10/7 - 10:00 PM EDT | McDavid 9 pts | Carlsson 5 pts | Honda Center"));
  TickerOptions narrow = no_odds;
  narrow.records = true;
  narrow.leaders = false;
  CHECK_EQ(status_text(p, narrow, ""), std::string("10/7 - 10:00 PM EDT | EDM 2-0-1, ANA 2-0-0 | Honda Center"));

  // Tonight's final, and the win splash from the intermission document of the same game
  GameSnapshot f = nhl_event("fixtures/event_nhl_final.json", kCapitals);
  CHECK(f.state == GameState::POST);
  CHECK(f.team_winner);
  CHECK_EQ(f.team_score, 5);
  CHECK_EQ(f.opp_score, 3);
  CHECK_EQ(status_text(f, o, ""), std::string("Final | WSH 2-1-0 | PIT 2-2-0"));
  GameSnapshot w = nhl_event("fixtures/event_nhl_end.json", kCapitals);
  CHECK_EQ(decide_splash(w, f, true).text, std::string("WSH WINS!"));
  CHECK_EQ(decide_splash(w, f, true).color, (uint32_t) 0xd71830);
  GameSnapshot lost_prev = nhl_event("fixtures/event_nhl_end.json", kPenguins);
  GameSnapshot lost = nhl_event("fixtures/event_nhl_final.json", kPenguins);
  CHECK_EQ(decide_splash(lost_prev, lost, true).text, std::string(""));
  CHECK_EQ(decide_splash(lost_prev, lost, true, true).text, std::string("WSH WINS!"));

  // A regular-season shootout: CAR won it 5-4
  GameSnapshot so = nhl_event("fixtures/event_nhl_final_so.json", kPenguins);
  CHECK(so.state == GameState::POST);
  CHECK(!so.team_winner);
  CHECK_EQ(so.period, 5);
  CHECK_EQ(so.team_score, 4);
  CHECK_EQ(so.opp_score, 5);
  CHECK_EQ(clock_text(so), std::string("Final/SO"));
  CHECK_EQ(status_text(so, o, ""), std::string("Final/SO | PIT 32-17-15 | CAR 41-17-6"));

  // A playoff game in double overtime: the series line stands in for records
  GameSnapshot po = nhl_event("fixtures/event_nhl_final_2ot.json", kHurricanes);
  CHECK(po.nhl.playoffs);
  CHECK(po.team_winner);
  CHECK_EQ(po.nhl.series, std::string("CAR leads series 2-0"));
  CHECK_EQ(status_text(po, o, ""), std::string("Final/2OT | CAR leads series 2-0"));
  GameSnapshot pre = po;
  pre.state = GameState::PRE;
  pre.odds = pre.over_under = pre.tv = "";
  CHECK_EQ(status_text(pre, o, "Today 7:00 PM"),
           std::string("East 1st Round - Game 2 | Today 7:00 PM | CAR leads series 2-0 | Lenovo Center"));

  GameSnapshot x;
  CHECK(!parse_event_str(slurp("fixtures/event_nhl_live.json"), Sport::HOCKEY, kPenguins, x));  // not in it
  CHECK(!parse_event_str(slurp("fixtures/event_nhl_live.json"), Sport::HOCKEY, kJets, x, "401892456"));  // some other event
  CHECK(!parse_event_str("{nope", Sport::HOCKEY, kJets, x));
}

static void test_nhl_splash() {
  GameSnapshot prev = nhl_event("fixtures/event_nhl_live.json", kJets);
  auto after = [&](int us, int them) {
    GameSnapshot c = prev;
    c.team_score += us;
    c.opp_score += them;
    return c;
  };
  CHECK_EQ(decide_splash(prev, after(1, 0), true).text, std::string("GOAL!"));
  CHECK_EQ(decide_splash(prev, after(1, 0), false).color, (uint32_t) 0x002d62);
  CHECK_EQ(decide_splash(prev, after(0, 1), true).text, std::string("COL GOAL"));
  CHECK_EQ(decide_splash(prev, after(0, 1), true).color, (uint32_t) 0x860038);
  CHECK_EQ(decide_splash(prev, after(0, 1), false).text, std::string(""));
  CHECK_EQ(decide_splash(prev, after(0, 0), true).text, std::string(""));
  CHECK_EQ(decide_splash(after(1, 0), prev, true).text, std::string(""));  // goal taken back on review
  // Live modes: neither side is ours
  CHECK_EQ(decide_splash(prev, after(1, 0), true, true).text, std::string("WPG GOAL"));
  CHECK_EQ(decide_splash(prev, after(0, 1), true, true).text, std::string("COL GOAL"));
  // The win, including a shootout decided between two polls
  GameSnapshot fin = after(1, 0);
  fin.state = GameState::POST;
  fin.completed = true;
  CHECK_EQ(decide_splash(prev, fin, true).text, std::string("WPG WINS!"));
  GameSnapshot lost = after(0, 1);
  lost.state = GameState::POST;
  lost.completed = true;
  CHECK_EQ(decide_splash(prev, lost, true).text, std::string(""));
  CHECK_EQ(decide_splash(prev, lost, true, true).text, std::string("COL WINS!"));
  GameSnapshot other = after(1, 0);
  other.event_id = "1";
  CHECK_EQ(decide_splash(prev, other, true).text, std::string(""));
  GameSnapshot pre = prev;
  pre.state = GameState::PRE;
  CHECK_EQ(decide_splash(pre, after(1, 0), true).text, std::string(""));
}

// ESPN's team endpoint keeps naming a finished game for hours (PIT's still
// named the 10/7 final at 11 AM Eastern on 10/8). Leagues without a season
// list find the next game in the coming days' scoreboards.
static void test_lookahead() {
  Schedule pit;
  CHECK(parse_team_str(slurp("fixtures/team_nhl_pit_post.json"), pit));
  CHECK_EQ(pit.event_id, std::string("401891830"));
  CHECK(pit.next_final);
  Schedule atl;
  CHECK(parse_team_str(slurp("fixtures/team_mlb_atl.json"), atl));
  CHECK(!atl.next_final);  // its next event was in progress
  Schedule dal;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_dal.json"), dal));
  CHECK(!dal.next_final);

  Schedule next;
  CHECK(parse_team_game_str(slurp("fixtures/day_nhl_20261009.json"), 16, next));
  CHECK_EQ(next.event_id, std::string("401892467"));  // PIT @ CBJ
  CHECK_EQ(next.kickoff_epoch, parse_iso8601_z("2026-10-09T23:00Z"));
  Schedule none;
  CHECK(!parse_team_game_str(slurp("fixtures/day_nhl_20261009.json"), 17, none));  // COL is off
  CHECK(!parse_team_game_str(slurp("fixtures/day_mlb_20261009.json"), 15, none));   // no games at all
  // Game 2 of a doubleheader, after game 1's final.
  const char *dh = R"({"events":[
    {"id":"1","date":"2026-07-04T17:05Z","status":{"type":{"state":"post"}},
     "competitions":[{"competitors":[{"id":"15"},{"id":"19"}]}]},
    {"id":"2","date":"2026-07-04T22:10Z","status":{"type":{"state":"pre"}},
     "competitions":[{"competitors":[{"id":"19"},{"id":"15"}]}]}]})";
  CHECK(parse_team_game_str(dh, 15, next));
  CHECK_EQ(next.event_id, std::string("2"));
  // The scoreboard can still call game 1 live after the panel saw it end.
  const char *dh_live = R"({"events":[
    {"id":"1","date":"2026-07-04T17:05Z","status":{"type":{"state":"in"}},
     "competitions":[{"competitors":[{"id":"15"},{"id":"19"}]}]},
    {"id":"2","date":"2026-07-04T22:10Z","status":{"type":{"state":"pre"}},
     "competitions":[{"competitors":[{"id":"19"},{"id":"15"}]}]}]})";
  CHECK(parse_team_game_str(dh_live, 15, next, "1"));
  CHECK_EQ(next.event_id, std::string("2"));
  CHECK(!parse_team_game_str(slurp("fixtures/day_nhl_20261009.json"), 16, none, "401892467"));
  // A document that does not parse is a failed read, not an off day.
  std::string bad = "{\"events\":[";
  CHECK_EQ(parse_team_game(bad, 16, std::string(), none), -1);
  std::string off = slurp("fixtures/day_mlb_20261009.json");
  CHECK_EQ(parse_team_game(off, 15, std::string(), none), 0);

  // Mid-game, the team endpoint already names the next game; today's
  // scoreboard still has the one on now (LAD @ MIL, Bot 3rd, 2026-10-12).
  Schedule mil;
  CHECK(parse_team_str(slurp("fixtures/team_mlb_mil_live.json"), mil));
  CHECK_EQ(mil.event_id, std::string("401908032"));  // MIL @ LAD, 10/14
  Schedule playing;
  CHECK(parse_team_game_str(slurp("fixtures/day_mlb_20261012.json"), 8, playing));
  CHECK_EQ(playing.event_id, std::string("401908031"));
  CHECK(today_game_first(mil, playing));
  // CLE's game tonight hasn't started: both name it, nothing changes.
  Schedule cle, cle_today;
  cle.event_id = "401908023";
  cle.kickoff_epoch = parse_iso8601_z("2026-10-13T00:00Z");
  CHECK(parse_team_game_str(slurp("fixtures/day_mlb_20261012.json"), 5, cle_today));
  CHECK(!today_game_first(cle, cle_today));
  // No game today, or no next game named at all.
  CHECK(!today_game_first(mil, Schedule{}));
  Schedule unnamed;
  CHECK(today_game_first(unnamed, playing));
  // Doubleheader: game 1 on now, the team endpoint names game 2.
  Schedule game2;
  game2.event_id = "2";
  game2.kickoff_epoch = parse_iso8601_z("2026-07-04T22:10Z");
  CHECK(parse_team_game_str(dh_live, 15, next));
  CHECK(today_game_first(game2, next));

  // Soccer jumps the same way: CRU @ BRA at 25' (Brazil's Serie A, the same
  // documents as MLS and the Premier League, 2026-10-12).
  Schedule cru;
  CHECK(parse_team_str(slurp("fixtures/team_bra_cru_live.json"), cru));
  CHECK_EQ(cru.event_id, std::string("401841276"));  // CRU @ GRE, 10/18
  Schedule cru_now;
  CHECK(parse_team_game_str(slurp("fixtures/day_bra_20261012.json"), 2022, cru_now));
  CHECK_EQ(cru_now.event_id, std::string("401841260"));
  CHECK(today_game_first(cru, cru_now));
  // CHA's game that day is over: nothing on now.
  CHECK(!parse_team_game_str(slurp("fixtures/day_bra_20261012.json"), 9318, none));

  // College football too: FLA @ TEX at halftime, 2026-10-17. Texas's team
  // endpoint names next week's game; its conference's scoreboard (the group
  // the team endpoint gives) has the one on now.
  Schedule tex;
  CHECK(parse_team_str(slurp("fixtures/team_ncaa_tex_live.json"), tex));
  CHECK_EQ(tex.event_id, std::string("401856732"));  // MISS @ TEX, 10/24
  CHECK_EQ(tex.group, 8u);
  CHECK_EQ(team_day_url(League::NCAA, tex.group, parse_iso8601_z("2026-10-17T17:40Z") - 4 * 3600),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard?groups=8&dates=20261017"));
  Schedule tex_now;
  CHECK(parse_team_game_str(slurp("fixtures/day_ncaa_20261017.json"), 251, tex_now));
  CHECK_EQ(tex_now.event_id, std::string("401856724"));
  CHECK(today_game_first(tex, tex_now));
  // Ole Miss plays later that day: both name the same game, nothing changes.
  Schedule miss, miss_today;
  CHECK(parse_team_game_str(slurp("fixtures/day_ncaa_20261017.json"), 145, miss_today));
  miss.event_id = miss_today.event_id;
  miss.kickoff_epoch = miss_today.kickoff_epoch;
  CHECK(!today_game_first(miss, miss_today));
  // A game the panel saw finish is passed over even while ESPN lists it live,
  // and a team that is off that day (Iowa) has nothing on now.
  CHECK(!parse_team_game_str(slurp("fixtures/day_ncaa_20261017.json"), 251, none, "401856724"));
  CHECK(!parse_team_game_str(slurp("fixtures/day_ncaa_20261017.json"), 2294, none));

  // The NFL keeps the game: NE @ CHI was final on 2026-10-22 and Chicago's
  // team endpoint still named it, while Memphis's (college, in overtime two
  // minutes later) named next week's. So the NFL skips the day read.
  Schedule chi;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_chi_post.json"), chi));
  CHECK_EQ(chi.event_id, std::string("401873010"));  // NE @ CHI, final
  CHECK(chi.next_final);
  Schedule mem;
  CHECK(parse_team_str(slurp("fixtures/team_ncaa_mem_live.json"), mem));
  CHECK_EQ(mem.event_id, std::string("401862814"));  // ARMY @ MEM, 10/31
  CHECK(team_names_game_on_now(League::NFL));
  for (League l : {League::NCAA, League::MLB, League::NBA, League::WNBA, League::NHL, League::MLS, League::EPL,
                   League::MCBB})
    CHECK(!team_names_game_on_now(l));

  CHECK_EQ(team_day_url(League::NHL, 0, parse_iso8601_z("2026-10-09T15:00Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/hockey/nhl/scoreboard?dates=20261009"));
  CHECK_EQ(team_day_url(League::MCBB, 8, parse_iso8601_z("2026-12-06T18:00Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/basketball/mens-college-basketball/scoreboard?groups=8&dates=20261206"));
  CHECK_EQ(team_day_url(League::MLS, 0, parse_iso8601_z("2026-10-15T00:30Z")),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/usa.1/scoreboard?dates=20261014"));
  CHECK_EQ(team_day_url(League::EPL, 0, parse_iso8601_z("2026-10-21T19:00Z"), "uefa.champions"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/soccer/uefa.champions/scoreboard?dates=20261021"));
}

int run_football_golden();  // golden_football.cpp
int run_set_tests(int &checks);  // test_set.cpp

static void test_logo_lru() {
  using espn::LogoSlot;
  // Oldest idle, off-screen slot goes first.
  CHECK_EQ(espn::pick_evict({{300, false, false}, {100, false, false}, {200, false, false}}), 1);
  // On screen or still downloading: never dropped, even when oldest.
  CHECK_EQ(espn::pick_evict({{100, false, true}, {150, true, false}, {200, false, false}}), 2);
  // Nothing droppable.
  CHECK_EQ(espn::pick_evict({{100, true, false}, {200, false, true}}), -1);
  CHECK_EQ(espn::pick_evict({}), -1);
  // A failed logo is due once its wait has passed, including across the millis() wrap.
  CHECK(!espn::logo_retry_due(1000, 30000, 1000 + 30000 - 1));
  CHECK(espn::logo_retry_due(1000, 30000, 1000 + 30000));
  CHECK(espn::logo_retry_due(0xFFFFF000u, 30000, 0xFFFFF000u + 30000));
  CHECK(!espn::logo_retry_due(0xFFFFF000u, 30000, 0xFFFFF000u + 29999));
  // A board with new logos waits for both, then goes up; a cap ends the wait
  // for a logo that is slow, and one that failed is not waited for at all.
  CHECK(espn::logo_hold_over(false, false, 1000, 1000));
  CHECK(!espn::logo_hold_over(true, false, 1000, 1000));
  CHECK(!espn::logo_hold_over(false, true, 1000, 1000 + espn::LOGO_HOLD_MS - 1));
  CHECK(espn::logo_hold_over(true, true, 1000, 1000 + espn::LOGO_HOLD_MS));
  CHECK(espn::logo_hold_over(true, false, 0xFFFFF000u, 0xFFFFF000u + espn::LOGO_HOLD_MS));
  CHECK(!espn::logo_hold_over(true, false, 0xFFFFF000u, 0xFFFFF000u + espn::LOGO_HOLD_MS - 1));
  // A dark logo that 404s falls back to the plain one, the URL dark_logo made it from.
  std::string plain = "https://a.espncdn.com/i/teamlogos/soccer/500/436.png";
  std::string dark = espn::dark_logo(plain);
  CHECK_EQ(espn::plain_logo_url(dark),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/soccer/500/436.png&w=64&h=64"));
  CHECK_EQ(espn::plain_logo_url(espn::dark_logo("https://a.espncdn.com/i/teamlogos/nfl/500/scoreboard/dal.png")),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/nfl/500/scoreboard/dal.png&w=64&h=64"));
  CHECK_EQ(espn::plain_logo_url("https://a.espncdn.com/combiner/i?img=/i/teamlogos/soccer/500/436.png&w=64&h=64"),
           std::string());
}

// The wait before a failed logo is asked for again: per logo, growing with each
// failure in a row, longer after a refusal, cleared when it loads.
static void test_logo_backoff() {
  uint8_t fails = 0;
  uint32_t wait = 0;
  // Ordinary failures: 30 s doubling to 10 min, jittered into the upper half.
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(fails, 1);
  CHECK_EQ(wait, 15000u);
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(wait, 30000u);
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(wait, 60000u);
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(fails, 4);
  CHECK_EQ(wait, 120000u);
  for (int i = 0; i < 20; i++)
    espn::logo_failed(fails, wait, false, 0, 300000);
  CHECK_EQ(wait, 600000u);
  // Each failure waits at least as long as the one before it, with no jitter.
  uint8_t f = 0;
  uint32_t w = 0, prev_floor = 0;
  for (int i = 0; i < 12; i++) {
    espn::logo_failed(f, w, false, 0, 0);
    CHECK(w >= prev_floor);
    prev_floor = w;
  }
  // A 403 or 429 starts at 5 min and stops at an hour.
  fails = 0;
  espn::logo_failed(fails, wait, true, 0, 0);
  CHECK_EQ(wait, 150000u);
  espn::logo_failed(fails, wait, true, 0, 300000);
  CHECK_EQ(wait, 600000u);
  for (int i = 0; i < 20; i++)
    espn::logo_failed(fails, wait, true, 0, 1800000);
  CHECK_EQ(wait, 3600000u);
  // Retry-After longer than the schedule is honored, up to the hour.
  fails = 0;
  espn::logo_failed(fails, wait, true, 900000, 0);
  CHECK_EQ(wait, 900000u);
  fails = 0;
  espn::logo_failed(fails, wait, true, 9000000, 0);
  CHECK_EQ(wait, 3600000u);
  // A refusal after plain failures lands on the refusal schedule for its place in the run.
  fails = 2;
  espn::logo_failed(fails, wait, true, 0, 300000);
  CHECK_EQ(wait, 900000u);
  // The count saturates instead of wrapping back to a short wait.
  fails = 255;
  wait = 0;
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(fails, 255);
  CHECK_EQ(wait, 300000u);
  // A logo that loads clears its own wait and count, and the next failure starts over.
  espn::logo_loaded(fails, wait);
  CHECK_EQ(fails, 0);
  CHECK_EQ(wait, 0u);
  espn::logo_failed(fails, wait, false, 0, 0);
  CHECK_EQ(fails, 1);
  CHECK_EQ(wait, 15000u);
  // Two logos keep separate runs: one failing for the fifth time does not slow the other's first retry.
  uint8_t fails_a = 4, fails_b = 0;
  uint32_t wait_a = 0, wait_b = 0;
  espn::logo_failed(fails_a, wait_a, false, 0, 0);
  espn::logo_failed(fails_b, wait_b, false, 0, 0);
  CHECK_EQ(wait_a, 240000u);
  CHECK_EQ(wait_b, 15000u);
}

// Counts what the parsers allocate through the hook the firmware points at PSRAM
struct CountingAllocator : ArduinoJson::Allocator {
  int calls = 0;
  int live = 0;
  void *allocate(size_t size) override {
    calls++;
    live++;
    return malloc(size);
  }
  void deallocate(void *ptr) override {
    if (ptr != nullptr)
      live--;
    free(ptr);
  }
  void *reallocate(void *ptr, size_t size) override {
    if (ptr == nullptr) {
      calls++;
      live++;
    }
    return realloc(ptr, size);
  }
};

static void test_json_allocator() {
  CountingAllocator counting;
  espn::set_json_allocator(&counting);
  std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
  GameSnapshot s;
  CHECK(parse_scoreboard_str(json, kPostEvent, kBallState, s));
  CHECK_EQ(s.team_score, 3);
  Schedule sch;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_dal.json"), sch));
  CHECK(counting.calls > 0);
  CHECK_EQ(counting.live, 0);  // every document freed its memory
  espn::set_json_allocator(nullptr);
  int before = counting.calls;
  GameSnapshot again;
  CHECK(parse_scoreboard_str(json, kPostEvent, kBallState, again));
  CHECK_EQ(counting.calls, before);  // back on the default heap
}

static void test_timezone_index() {
  // Names first: the app sends one, so a reordered list can't pick a wrong zone.
  CHECK_EQ(espn::timezone_index("US Central"), 1);
  CHECK_EQ(espn::timezone_index("America/Chicago"), 1);
  CHECK_EQ(espn::timezone_index("Australia Eastern"), 12);
  CHECK_EQ(espn::timezone_index("Australia/Sydney"), 12);
  CHECK_EQ(espn::timezone_index("UTC"), 13);
  // Positions still work for the device page, which is built from this list.
  CHECK_EQ(espn::timezone_index("0"), 0);
  CHECK_EQ(espn::timezone_index("13"), 13);
  CHECK_EQ(espn::timezone_index("14"), -1);
  CHECK_EQ(espn::timezone_index("99999999999999999999"), -1);
  // Nothing close enough to guess at.
  CHECK_EQ(espn::timezone_index("us central"), -1);
  CHECK_EQ(espn::timezone_index("America/Detroit"), -1);
  CHECK_EQ(espn::timezone_index("-1"), -1);
  CHECK_EQ(espn::timezone_index("1a"), -1);
  CHECK_EQ(espn::timezone_index(""), -1);
  CHECK_EQ(espn::timezone_index(nullptr), -1);
  // Every row answers to its own name and IANA name.
  for (size_t i = 0; i < espn::kTimezoneCount; i++) {
    CHECK_EQ(espn::timezone_index(espn::kTimezones[i].name), (int) i);
    CHECK_EQ(espn::timezone_index(espn::kTimezones[i].iana), (int) i);
  }
}

// Recorded 2026-10-08 from the 2025-26 postseason, see fixtures/README.md
static void test_postseason() {
  TickerOptions all;
  // CFP quarterfinal at the Rose Bowl, a neutral site, from both sides
  std::string qf = slurp("fixtures/scoreboard_ncaa_cfp_quarterfinals.json");
  GameSnapshot ala;
  CHECK(parse_scoreboard_str(qf, "401769072", 333, ala));
  CHECK(ala.state == GameState::POST);
  CHECK(ala.completed);
  CHECK_EQ(ala.team_score, 3);
  CHECK_EQ(ala.opp_score, 38);
  CHECK(!ala.team_winner);
  CHECK_EQ(ala.venue, std::string("Rose Bowl"));
  CHECK_EQ(status_text(ala, all, ""), std::string("Final | ALA 11-4 | IU 14-0"));
  GameSnapshot iu;
  CHECK(parse_scoreboard_str(qf, "401769072", 84, iu));
  CHECK(iu.team_winner);
  CHECK_EQ(iu.opp_abbr, std::string("ALA"));
  // The My team poll filters by our conference. ESPN lists a game under
  // both teams' conferences, so SEC (8) finds Alabama's Rose Bowl.
  CHECK_EQ(scoreboard_url(League::NCAA, 8, ala.kickoff_epoch),
           std::string("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard"
                       "?groups=8&dates=20260101"));

  // National championship: 7:30 PM Eastern is 00:30 UTC the next day
  std::string final_json = slurp("fixtures/scoreboard_ncaa_cfp_final.json");
  GameSnapshot mia;
  CHECK(parse_scoreboard_str(final_json, "401769076", 2390, mia));
  CHECK(mia.completed);
  CHECK_EQ(mia.team_score, 21);
  CHECK_EQ(mia.opp_score, 27);
  CHECK(!mia.team_winner);
  CHECK(scoreboard_url(League::NCAA, 1, mia.kickoff_epoch).find("&dates=20260119") != std::string::npos);

  // Conference title game that went to overtime
  GameSnapshot duke;
  CHECK(parse_scoreboard_str(slurp("fixtures/scoreboard_ncaa_conf_titles.json"), "401777328", 150, duke));
  CHECK(duke.completed);
  CHECK(duke.team_winner);
  CHECK_EQ(duke.short_detail, std::string("Final/OT"));
  CHECK_EQ(status_text(duke, all, ""), std::string("Final | DUKE 8-5 | UVA 10-3"));

  // NFL wild card and the Super Bowl
  GameSnapshot chi;
  CHECK(parse_scoreboard_str(slurp("fixtures/scoreboard_nfl_wild_card.json"), "401772981", 3, chi));
  CHECK(chi.completed);
  CHECK(chi.team_winner);
  CHECK_EQ(chi.team_score, 31);
  CHECK_EQ(chi.opp_score, 27);
  GameSnapshot sea;
  CHECK(parse_scoreboard_str(slurp("fixtures/scoreboard_nfl_super_bowl.json"), "401772988", 26, sea));
  CHECK(sea.completed);
  CHECK(sea.team_winner);
  CHECK_EQ(sea.opp_abbr, std::string("NE"));
  CHECK_EQ(sea.venue, std::string("Levi's Stadium"));
  CHECK(scoreboard_url(League::NFL, 0, sea.kickoff_epoch).find("?dates=20260208") != std::string::npos);

  // Game names from competitions[0].notes[0].headline
  CHECK_EQ(ala.game_name, std::string("Rose Bowl"));
  CHECK_EQ(mia.game_name, std::string("CFP Championship"));
  CHECK_EQ(duke.game_name, std::string("ACC Championship"));
  CHECK_EQ(chi.game_name, std::string("NFC Wild Card"));
  CHECK_EQ(sea.game_name, std::string("Super Bowl LX"));
}

// Home Assistant's line: no per-play parts, so it only moves on state and score.
static void test_ha_status_text() {
  GameSnapshot s = live(24, 21);
  s.down_distance = "3rd & 10 at BOIS 25";
  s.short_down_distance = "3rd & 10";
  s.short_detail = "15:00 - 4th";
  s.last_play = "End of 3rd quarter.";
  CHECK_EQ(ha_status_text(s, ""), std::string("Live | DAL 24, PHI 21"));
  s.last_play = "Prescott pass to Lamb for 12 yards";
  s.short_detail = "8:12 - 4th";
  CHECK_EQ(ha_status_text(s, ""), std::string("Live | DAL 24, PHI 21"));
  GameSnapshot h = live(14, 7);
  h.short_detail = "Halftime";
  CHECK_EQ(ha_status_text(h, ""), std::string("Halftime | DAL 14, PHI 7"));
  GameSnapshot p = live(0, 0, GameState::PRE);
  p.odds = "SEA -3.5";
  p.venue = "Lumen Field";
  p.short_detail = "10/15 - 8:20 PM EDT";
  CHECK_EQ(ha_status_text(p, "Wed 7:20 PM"), std::string("DAL vs PHI | Wed 7:20 PM"));
  CHECK_EQ(ha_status_text(p, ""), std::string("DAL vs PHI | 10/15 - 8:20 PM EDT"));
  p.game_name = "Rose Bowl";
  CHECK_EQ(ha_status_text(p, "Jan 1 4:00 PM"), std::string("Rose Bowl | DAL vs PHI | Jan 1 4:00 PM"));
  GameSnapshot f = live(31, 28, GameState::POST);
  f.team_record = "5-1";
  CHECK_EQ(ha_status_text(f, ""), std::string("Final | DAL 31, PHI 28"));
  GameSnapshot pp = live(0, 0, GameState::POST);
  pp.completed = false;
  pp.short_detail = "Postponed";
  CHECK_EQ(ha_status_text(pp, ""), std::string("Postponed | DAL 0, PHI 0"));
  CHECK_EQ(ha_status_text(GameSnapshot{}, ""), std::string("No upcoming game"));
}

static void test_short_game_name() {
  // Every headline in the recorded fixtures
  CHECK_EQ(short_game_name("College Football Playoff Quarterfinal at the Rose Bowl Presented by Prudential"),
           std::string("Rose Bowl"));
  CHECK_EQ(short_game_name("College Football Playoff Quarterfinal at the Allstate Sugar Bowl"),
           std::string("Sugar Bowl"));
  CHECK_EQ(short_game_name("College Football Playoff Quarterfinal at the Capital One Orange Bowl"),
           std::string("Orange Bowl"));
  CHECK_EQ(short_game_name("College Football Playoff National Championship Presented by AT&T"),
           std::string("CFP Championship"));
  CHECK_EQ(short_game_name("SEC Championship"), std::string("SEC Championship"));
  CHECK_EQ(short_game_name("Big 12 Championship"), std::string("Big 12 Championship"));
  CHECK_EQ(short_game_name("FCS Championship - Second Round"), std::string("FCS Championship - Second Round"));
  CHECK_EQ(short_game_name("NFC Wild Card Playoffs"), std::string("NFC Wild Card"));
  CHECK_EQ(short_game_name("Super Bowl LX"), std::string("Super Bowl LX"));
  CHECK_EQ(short_game_name("NFL Melbourne Game"), std::string("NFL Melbourne Game"));
  CHECK_EQ(short_game_name("Aflac Kickoff"), std::string("Aflac Kickoff"));
  CHECK_EQ(short_game_name("Liberty Mutual Music City Kickoff"), std::string("Liberty Mutual Music City Kickoff"));
  // Bowl names as ESPN has listed them in past seasons
  CHECK_EQ(short_game_name("Rose Bowl Game Presented by Prudential"), std::string("Rose Bowl"));
  CHECK_EQ(short_game_name("Goodyear Cotton Bowl Classic"), std::string("Cotton Bowl"));
  CHECK_EQ(short_game_name("College Football Playoff Semifinal at the Vrbo Fiesta Bowl"), std::string("Fiesta Bowl"));
  CHECK_EQ(short_game_name("SRS Distribution Las Vegas Bowl"), std::string("Las Vegas Bowl"));
  CHECK_EQ(short_game_name("Tony the Tiger Sun Bowl"), std::string("Sun Bowl"));
  CHECK_EQ(short_game_name("Snoop Dogg Arizona Bowl presented by Gin & Juice by Dre and Snoop"),
           std::string("Arizona Bowl"));
  CHECK_EQ(short_game_name("LA Bowl Hosted by Gronk"), std::string("LA Bowl"));
  // ESPN abbreviates the sponsor too (event_mcbb_ot.json)
  CHECK_EQ(short_game_name("Ivy League Tournament pres. by TIAA - Final"), std::string("Ivy League Tournament"));
  // Notes that are a result or a schedule change are no name
  CHECK_EQ(short_game_name("Paris Saint-Germain win 4-3 on penalties"), std::string(""));
  CHECK_EQ(short_game_name("Rain - Makeup date April 5"), std::string(""));
  CHECK_EQ(short_game_name("Sun Bowl presented by Win Inc"), std::string("Sun Bowl"));  // the sponsor goes first
  CHECK_EQ(short_game_name("NLCS - Game 2"), std::string("NLCS - Game 2"));
  CHECK_EQ(short_game_name("East 1st Round - Game 2"), std::string("East 1st Round - Game 2"));
  CHECK_EQ(short_game_name("Duke's Mayo Bowl"), std::string("Duke's Mayo Bowl"));
  CHECK_EQ(short_game_name("College Football Playoff First Round Game"), std::string("CFP First Round Game"));
  CHECK_EQ(short_game_name("AFC Divisional Playoffs"), std::string("AFC Divisional"));
  CHECK_EQ(short_game_name("AFC Championship"), std::string("AFC Championship"));
  CHECK_EQ(short_game_name("Pro Bowl Games"), std::string("Pro Bowl Games"));
  CHECK_EQ(short_game_name("NFL London Games"), std::string("NFL London Games"));
  CHECK_EQ(short_game_name(""), std::string(""));
  // Kickoff windows for TBD college games come in the same notes (2026-10-24 SEC slate)
  CHECK_EQ(short_game_name("FLEX: 3:30 - 4:30pm OR 6 - 8pm ET start"), std::string(""));
  CHECK_EQ(short_game_name("AFTERNOON: 3:30 - 4:30pm ET start"), std::string(""));
  CHECK_EQ(short_game_name("NIGHT: 6 - 8pm ET start"), std::string(""));
  CHECK_EQ(short_game_name("Kickoff 7 PM ET"), std::string(""));
}

// The abbreviation badge's colors (badge.h), the app's rules.
static void test_badge() {
  // Luminance: black, white, and the app's 0.6 text threshold.
  CHECK_EQ(badge_luminance(0x000000), 0);
  CHECK_EQ(badge_luminance(0xFFFFFF), 1000);
  CHECK(badge_luminance(0xF2A900) > kBadgeDarkTextLum);   // gold: black text
  CHECK(badge_luminance(0x1E5AA8) < kBadgeDarkTextLum);   // blue: white text
  CHECK(badge_luminance(0xC8102E) < kBadgeDarkTextLum);   // red: white text
  // A second color that differs from the fill is the ring.
  BadgeStyle b = badge_style(0x1E5AA8, 0xF4F6F8);
  CHECK(b.fill == 0x1E5AA8 && b.has_ring && b.ring == 0xF4F6F8 && !b.dark_text);
  // The same color twice is no ring, and a mid-tone fill needs none.
  b = badge_style(0x1E5AA8, 0x1E5AA8);
  CHECK(!b.has_ring);
  // A near-black fill would vanish on the panel: a gray ring stands in when
  // it has no ring or its ring is as dark.
  b = badge_style(0x000000, 0x000000);
  CHECK(b.has_ring && b.ring == kBadgeNeutralRing);
  b = badge_style(0x101010, 0x080808);
  CHECK(b.has_ring && b.ring == kBadgeNeutralRing);
  // A near-black fill with a visible ring keeps that ring.
  b = badge_style(0x000000, 0xC8102E);
  CHECK(b.has_ring && b.ring == 0xC8102E && !b.dark_text);
  // A light fill takes black text and keeps its own ring.
  b = badge_style(0xF4F6F8, 0x1E5AA8);
  CHECK(b.dark_text && b.has_ring && b.ring == 0x1E5AA8);
}

// The demo build's matchups, made-up teams and script (demo.h).
static void test_demo() {
  auto abbrs = [](DemoMatchup m) { return std::string(m.home->abbr) + "-" + m.away->abbr; };
  // No picks: favorite #1 at home when it is in the league, else the stock game.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 0, nullptr)) == "DAL-PHI");
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 0, find_team(League::NFL, 12))) == "KC-PHI");
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 0, find_team(League::NCAA, 333))) == "DAL-PHI");
  CHECK(abbrs(demo_matchup(DemoKind::COLLEGE, 0, 0, find_team(League::NCAA, 333))) == "ALA-MICH");
  CHECK(abbrs(demo_matchup(DemoKind::COLLEGE, 0, 0, find_team(League::NFL, 6))) == "OSU-MICH");
  // A panel that follows the stock opponent plays the stock home team instead.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 0, find_team(League::NFL, 21))) == "PHI-DAL");
  CHECK(abbrs(demo_matchup(DemoKind::COLLEGE, 0, 0, find_team(League::NCAA, 130))) == "MICH-OSU");
  // Picks win, and a pick from the other league is no pick.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 26, 25, find_team(League::NFL, 6))) == "SEA-SF");
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 333, 0, find_team(League::NFL, 6))) == "DAL-PHI");
  // The same team on both sides never happens.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 26, 26, find_team(League::NFL, 6))) == "SEA-PHI");
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 6, find_team(League::NFL, 6))) == "DAL-PHI");

  // Made-up teams: unique, and never a real team's abbreviation.
  for (size_t i = 0; i < kMadeUpTeamCount; i++) {
    for (size_t j = i + 1; j < kMadeUpTeamCount; j++)
      CHECK(std::string(kMadeUpTeams[i].abbr) != kMadeUpTeams[j].abbr);
    for (size_t t = 0; t < kTeamCount; t++)
      CHECK(std::string(kMadeUpTeams[i].abbr) != kTeams[t].abbr);
  }
  CHECK(std::string(demo_made_up(DemoKind::PRO, true).abbr) != demo_made_up(DemoKind::COLLEGE, true).abbr);
  // Nothing real anywhere in the demo's teams: no real team's name, and a
  // second color that gives every badge a ring (badge.h never needs the gray
  // fallback for them).
  for (size_t i = 0; i < kMadeUpTeamCount; i++) {
    for (size_t t = 0; t < kTeamCount; t++)
      CHECK(std::string(kMadeUpTeams[i].name) != kTeams[t].name);
    CHECK(kMadeUpTeams[i].alt != kMadeUpTeams[i].rgb);
    BadgeStyle b = badge_style(kMadeUpTeams[i].rgb, kMadeUpTeams[i].alt);
    CHECK(b.has_ring && b.ring == kMadeUpTeams[i].alt && b.fill == kMadeUpTeams[i].rgb);
  }
  // White letters on the blue, red and green fills; black on the gold one.
  CHECK(!badge_style(demo_made_up(DemoKind::PRO, true).rgb, 0).dark_text);
  CHECK(!badge_style(demo_made_up(DemoKind::PRO, false).rgb, 0).dark_text);
  CHECK(!badge_style(demo_made_up(DemoKind::COLLEGE, true).rgb, 0).dark_text);
  CHECK(badge_style(demo_made_up(DemoKind::COLLEGE, false).rgb, 0).dark_text);
  // One pass of the script is what the shot list is timed against.
  uint32_t total_ms = 0;
  for (size_t i = 0; i < kDemoStepCount; i++)
    total_ms += kDemoSteps[i].hold_ms;
  CHECK(total_ms == 52500);

  // Every football team has its color (the demo is football only), and the
  // color table matches teams.h, where football comes first.
  size_t football = 0;
  while (football < kTeamCount && league_sport(kTeams[football].league) == Sport::FOOTBALL)
    football++;
  CHECK(kTeamColorCount == football);
  for (size_t t = 0; t < football && t < kTeamColorCount; t++)
    CHECK(kTeamColors[t].league == kTeams[t].league && kTeamColors[t].espn_id == kTeams[t].espn_id);
  CHECK(team_rgb(League::NFL, 6) == 0x002A5C);
  CHECK(team_rgb(League::NFL, 999999) == 0xFFFFFF);

  // The script: starts before kickoff, ends final with the home side ahead,
  // scores never go down, and the splashes fire along the way.
  CHECK(kDemoSteps[0].state == GameState::PRE);
  const DemoStep &last = kDemoSteps[kDemoStepCount - 1];
  CHECK(last.state == GameState::POST && last.us > last.them);
  GameSnapshot prev, cur;
  int touchdowns = 0, wins = 0;
  for (size_t i = 0; i < kDemoStepCount; i++) {
    if (i > 0) {
      CHECK(kDemoSteps[i].us >= kDemoSteps[i - 1].us && kDemoSteps[i].them >= kDemoSteps[i - 1].them);
    }
    cur = GameSnapshot{};
    cur.valid = true;
    cur.team_abbr = "HBR";
    cur.opp_abbr = "IRN";
    cur.state = kDemoSteps[i].state;
    cur.completed = cur.state == GameState::POST;
    cur.team_score = kDemoSteps[i].us;
    cur.opp_score = kDemoSteps[i].them;
    cur.team_winner = cur.completed && cur.team_score > cur.opp_score;
    if (i > 0) {
      Splash sp = decide_splash(prev, cur, true, false);
      if (sp.text.find("TOUCHDOWN") != std::string::npos)
        touchdowns++;
      if (sp.text == "HBR WINS!")
        wins++;
    }
    prev = cur;
  }
  CHECK(touchdowns == 4);
  CHECK(wins == 1);
}

static Final mkfinal(uint8_t league, int rank, int64_t kick, const char *abbr) {
  Final f;
  f.league = league;
  f.rank = (uint8_t) rank;
  f.kickoff_epoch = kick;
  f.away_abbr = abbr;
  return f;
}

// The finals rotation's walks through ESPN days, one league at a time.
static void test_recap_walk() {
  const int32_t mon = 20000;  // any ESPN day
  int32_t day = 0;

  // Back, on a Monday with no NFL final today: Sunday has 13, so the walk
  // reads Sunday and stops.
  RecapWalk back;
  CHECK(back.back_due(mon, 0, day));
  CHECK_EQ(day, mon - 1);
  back.back_read(mon, 13);
  CHECK_EQ(back.step, RecapWalk::kDone);
  CHECK(!back.back_due(mon, 13, day));  // done for today
  CHECK(!back.back_due(mon + 1, 13, day));  // a new day with finals held: still done

  // An empty Sunday (a bye week), then Saturday has three, then Friday one:
  // four, enough.
  back = RecapWalk{};
  CHECK(back.back_due(mon, 0, day) && day == mon - 1);
  back.back_read(mon, 0);
  CHECK(back.back_due(mon, 0, day) && day == mon - 2);
  back.back_read(mon, 3);
  CHECK(back.back_due(mon, 3, day) && day == mon - 3);
  back.back_read(mon, 4);
  CHECK(!back.back_due(mon, 4, day));

  // Today's scan already had enough: no read at all.
  back = RecapWalk{};
  CHECK(!back.back_due(mon, 5, day));
  CHECK_EQ(back.step, RecapWalk::kDone);

  // A week back with nothing: it stops after kRecapMaxDays reads, stays
  // stopped for the rest of the day, and walks again the next day.
  back = RecapWalk{};
  int reads = 0;
  while (back.back_due(mon, 0, day)) {
    CHECK_EQ(day, mon - 1 - reads);
    back.back_read(mon, 0);
    reads++;
  }
  CHECK_EQ(reads, kRecapMaxDays);
  CHECK(!back.back_due(mon, 0, day));
  CHECK(back.back_due(mon + 1, 0, day) && day == mon);

  // Forward, no game left today: tomorrow has none, the day after has a
  // kickoff at 7 PM. The walk stops there until that kickoff passes.
  const int64_t now = 1792000000, seven_pm = now + 2 * 86400;
  RecapWalk fwd;
  CHECK(fwd.fwd_due(mon, 0, now, day) && day == mon + 1);
  CHECK(fwd.fwd_read(mon, false));
  CHECK(fwd.fwd_due(mon, 0, now, day) && day == mon + 2);
  CHECK(fwd.fwd_read(mon, true));
  CHECK(!fwd.fwd_due(mon, seven_pm, now, day));
  CHECK(!fwd.fwd_due(mon + 1, seven_pm, now + 86400, day));  // a new day, kickoff still ahead
  // The kickoff passed: look again from the day it is now.
  CHECK(fwd.fwd_due(mon + 2, seven_pm, seven_pm + 60, day) && day == mon + 3);

  // A week ahead with no kickoff: it gives up, and tries again tomorrow,
  // not on every scan today.
  fwd = RecapWalk{};
  bool walking = true;
  int ahead = 0;
  while (walking && fwd.fwd_due(mon, 0, now, day)) {
    walking = fwd.fwd_read(mon, false);
    ahead++;
  }
  CHECK(!walking);
  CHECK_EQ(ahead, kRecapMaxDays);
  CHECK(!fwd.fwd_due(mon, 0, now + 3600, day));
  CHECK(fwd.fwd_due(mon + 1, 0, now + 86400, day) && day == mon + 2);
}

static void test_recap() {
  const uint8_t NFL = (uint8_t) League::NFL, NCAA = (uint8_t) League::NCAA;
  // Finals ride along with the scan only when asked for
  std::string titles = slurp("fixtures/scoreboard_ncaa_conf_titles.json");
  LiveList live;
  FinalList finals;
  int64_t next = -1;
  CHECK(parse_live_games(titles, live, &next, &finals));
  CHECK_EQ(live.size(), (size_t) 0);
  CHECK_EQ(next, (int64_t) 0);
  CHECK_EQ(finals.size(), (size_t) 6);
  if (finals.size() == 6) {
    // Big Ten title: IU (away, #2) 13, OSU (home, #1) 10
    const Final &f = finals[0];
    CHECK_EQ(f.away_abbr, std::string("IU"));
    CHECK_EQ(f.away_id, (uint32_t) 84);
    CHECK_EQ(f.away_score, 13);
    CHECK_EQ(f.home_abbr, std::string("OSU"));
    CHECK_EQ(f.home_id, kOhioState);
    CHECK_EQ(f.home_score, 10);
    CHECK_EQ((int) f.rank, 1);
    CHECK_EQ(f.detail, std::string("Final"));
    CHECK_EQ(f.kickoff_epoch, parse_iso8601_z("2025-12-07T01:00Z"));
    CHECK(!f.away_color.empty());
    CHECK_EQ(finals[3].detail, std::string("Final/OT"));
    CHECK_EQ((int) finals[3].rank, 17);  // UVA #17, Duke unranked (99)
    CHECK_EQ((int) finals[4].rank, 99);  // neither ranked
    // The card: away side first like a broadcast, logos from the ids
    Final c = f;
    c.league = (uint8_t) League::NCAA;
    GameSnapshot g = final_snapshot(c);
    CHECK(g.valid);
    CHECK(g.state == GameState::POST);
    CHECK_EQ(g.team_abbr, std::string("IU"));
    CHECK_EQ(g.opp_score, 10);
    CHECK_EQ(g.team_logo, team_logo_url(League::NCAA, 84, "IU"));
    CHECK_EQ(g.opp_logo, team_logo_url(League::NCAA, kOhioState, "OSU"));
    CHECK_EQ(status_text(g, TickerOptions{}, "").substr(0, 5), std::string("Final"));
  }
  // Postponed and canceled games are "post" but not completed: no card
  std::string mixed = R"({"events":[)"
                      R"({"id":"1","date":"2026-10-10T16:00Z","status":{"type":{"state":"post","completed":false,"shortDetail":"Postponed"}},"competitions":[{"competitors":[{"id":"1","homeAway":"home","score":"0","team":{"abbreviation":"AAA"}},{"id":"2","homeAway":"away","score":"0","team":{"abbreviation":"BBB"}}]}]},)"
                      R"({"id":"2","date":"2026-10-10T19:30Z","status":{"type":{"state":"post","completed":true,"shortDetail":"Final"}},"competitions":[{"competitors":[{"id":"3","homeAway":"home","score":"21","team":{"abbreviation":"CCC"}},{"id":"4","homeAway":"away","score":"24","team":{"abbreviation":"DDD"}}]}]}]})";
  finals.clear();
  CHECK(parse_live_games(mixed, live, nullptr, &finals));
  CHECK_EQ(finals.size(), (size_t) 1);
  if (finals.size() == 1) {
    CHECK_EQ(finals[0].away_abbr, std::string("DDD"));
    CHECK_EQ(finals[0].away_score, 24);
    CHECK_EQ((int) finals[0].rank, 99);
  }
  // Without the finals list nothing changes for the plain scan
  CHECK(parse_live_games(mixed, live, &next));
  CHECK_EQ(live.size(), (size_t) 0);

  // Days, Eastern like scan_url()
  int32_t sat = espn_day(parse_iso8601_z("2025-12-06T21:00Z"), NCAA);
  CHECK_EQ(espn_day(parse_iso8601_z("2025-12-07T01:00Z"), NCAA), sat);  // 8 PM Eastern Saturday
  // 1 AM Eastern, a 10:30 PM West Coast kickoff still on: still Saturday's
  // scoreboard, which is the only one that lists it. 5 AM is Sunday.
  CHECK_EQ(espn_day(parse_iso8601_z("2025-12-07T06:00Z"), NCAA), sat);
  CHECK_EQ(espn_day(parse_iso8601_z("2025-12-07T10:00Z"), NCAA), sat + 1);
  CHECK(scan_url(League::NCAA, espn_day_epoch(sat)).find("dates=20251206") != std::string::npos);
  CHECK_EQ(recap_day_word(sat, sat), std::string("Today's"));
  CHECK_EQ(recap_day_word(sat, sat + 1), std::string("Yesterday's"));
  CHECK_EQ(recap_day_word(sat, sat + 2), std::string("Saturday's"));
  CHECK_EQ(recap_day_word(sat - 1, sat + 2), std::string("Friday's"));
  RecapDay hd;
  hd.league = NCAA;
  hd.day = sat;
  CHECK_EQ(recap_header(hd, espn_day_epoch(sat + 2)), std::string("Saturday's college scores"));
  hd.league = NFL;
  CHECK_EQ(recap_header(hd, espn_day_epoch(sat)), std::string("Today's NFL scores"));

  // Merge: newest day first, ranked games first, a rescan replaces the day
  std::vector<RecapDay> days;
  RecapDay d1;
  d1.league = NCAA;
  d1.day = sat;
  d1.finals = {mkfinal(NCAA, 99, 100, "U1"), mkfinal(NCAA, 5, 200, "R5"), mkfinal(NCAA, 2, 300, "R2"),
               mkfinal(NCAA, 99, 50, "U0")};
  recap_merge(days, d1);
  CHECK_EQ(days.size(), (size_t) 1);
  if (days.size() == 1 && days[0].finals.size() == 4) {
    CHECK_EQ(days[0].finals[0].away_abbr, std::string("R2"));
    CHECK_EQ(days[0].finals[1].away_abbr, std::string("R5"));
    CHECK_EQ(days[0].finals[2].away_abbr, std::string("U0"));
    CHECK_EQ(days[0].finals[3].away_abbr, std::string("U1"));
  }
  RecapDay n1;
  n1.league = NFL;
  n1.day = sat + 2;  // Monday night
  n1.finals = {mkfinal(NFL, 99, 400, "MNF")};
  recap_merge(days, n1);
  RecapDay n0;
  n0.league = NFL;
  n0.day = sat + 1;  // Sunday
  for (int i = 0; i < 13; i++)
    n0.finals.push_back(mkfinal(NFL, 99, 300 + i, "SUN"));
  recap_merge(days, n0);
  CHECK_EQ(days.size(), (size_t) 3);
  if (days.size() == 3) {
    CHECK_EQ(days[0].day, sat + 2);
    CHECK_EQ(days[1].day, sat + 1);
    CHECK_EQ(days[2].day, sat);
  }
  RecapDay again = n1;
  again.finals.push_back(mkfinal(NFL, 99, 500, "MNF2"));
  recap_merge(days, again);
  CHECK_EQ(days.size(), (size_t) 3);
  CHECK_EQ(days[0].finals.size(), (size_t) 2);
  RecapDay empty;
  empty.league = NFL;
  empty.day = sat + 3;
  recap_merge(days, empty);
  CHECK_EQ(days.size(), (size_t) 3);  // an empty day is not stored
  CHECK_EQ(recap_count(days, NFL), (size_t) 15);
  CHECK_EQ(recap_count(days, NCAA), (size_t) 4);

  // Trim: Monday's 2 are short of 4, so Sunday stays; a new NFL day with 4
  // pushes both out. Anything a week old goes.
  recap_trim(days, espn_day_epoch(sat + 3));
  CHECK_EQ(days.size(), (size_t) 3);
  RecapDay thu;
  thu.league = NFL;
  thu.day = sat + 5;
  for (int i = 0; i < 4; i++)
    thu.finals.push_back(mkfinal(NFL, 99, 600 + i, "THU"));
  recap_merge(days, thu);
  recap_trim(days, espn_day_epoch(sat + 5));
  CHECK_EQ(recap_count(days, NFL), (size_t) 4);
  CHECK_EQ(recap_count(days, NCAA), (size_t) 4);
  recap_trim(days, espn_day_epoch(sat + 7));
  CHECK_EQ(recap_count(days, NCAA), (size_t) 0);
  CHECK_EQ(recap_count(days, NFL), (size_t) 4);

  // Cards: header, finals, then the next kickoff. The cap is split between
  // the leagues that have finals.
  std::vector<RecapDay> two;
  RecapDay c;
  c.league = NCAA;
  c.day = sat;
  for (int i = 0; i < 30; i++)
    c.finals.push_back(mkfinal(NCAA, 99, i, "C"));
  recap_merge(two, c);
  CHECK_EQ(two[0].finals.size(), (size_t) 22);  // a day keeps what one loop can show
  const uint8_t both = kRecapNfl | kRecapCollege;
  std::vector<RecapCard> cards = recap_cards(two, both, true);
  CHECK_EQ(cards.size(), (size_t) (1 + 22 + 1));
  CHECK(cards.front().kind == RecapKind::HEADER);
  CHECK(cards[1].kind == RecapKind::FINAL);
  CHECK(cards.back().kind == RecapKind::NEXT);
  RecapDay mon;
  mon.league = NFL;
  mon.day = sat + 2;
  mon.finals = {mkfinal(NFL, 99, 1, "N")};
  RecapDay sun;
  sun.league = NFL;
  sun.day = sat + 1;
  for (int i = 0; i < 13; i++)
    sun.finals.push_back(mkfinal(NFL, 99, i, "N"));
  recap_merge(two, mon);
  recap_merge(two, sun);
  cards = recap_cards(two, both, false);
  size_t headers = 0, nfl = 0, college = 0;
  for (const auto &k : cards) {
    if (k.kind == RecapKind::HEADER)
      headers++;
    else if (k.kind == RecapKind::FINAL)
      (two[k.day].league == NFL ? nfl : college)++;
  }
  CHECK_EQ(headers, (size_t) 3);
  CHECK_EQ(nfl, (size_t) 11);
  CHECK_EQ(college, (size_t) 11);
  CHECK(cards.back().kind == RecapKind::FINAL);
  // A league the mode does not read gets no cards, and the other one the
  // whole cap
  cards = recap_cards(two, kRecapNfl, false);
  CHECK_EQ(cards.size(), (size_t) (2 + 14));
  for (const auto &k : cards)
    CHECK_EQ((int) two[k.day].league, (int) NFL);
  CHECK_EQ(recap_cards(two, kRecapCollege, false).size(), (size_t) 23);
  CHECK_EQ(recap_cards({}, both, true).size(), (size_t) 1);
  CHECK_EQ(recap_cards({}, both, false).size(), (size_t) 0);
  CHECK_EQ(recap_card_ms(RecapKind::FINAL), (uint32_t) 7000);
}

static Pending mkpending(uint8_t league, int rank, int64_t kick, const char *abbr) {
  Pending p;
  p.league = league;
  p.rank = (uint8_t) rank;
  p.kickoff_epoch = kick;
  p.away_abbr = abbr;
  p.home_abbr = "HOM";
  return p;
}

static bool sorted_by_kickoff(const PendingList &v) {
  for (size_t i = 1; i < v.size(); i++)
    if (v[i - 1].kickoff_epoch > v[i].kickoff_epoch)
      return false;
  return true;
}

// The rotation's later-today cards (card 160) and finals for every league.
static void test_later_today() {
  const uint8_t NFL = (uint8_t) League::NFL, NCAA = (uint8_t) League::NCAA, NBA = (uint8_t) League::NBA,
                NHL = (uint8_t) League::NHL, MLB = (uint8_t) League::MLB, MCBB = (uint8_t) League::MCBB;
  const uint32_t lo = 2 * 60 * 1000, hi = 3 * 3600 * 1000;

  // NHL, 10/9: four games to come, in kickoff order, with what a card needs
  std::string nhl_day = slurp("fixtures/day_nhl_20261009.json");
  LiveList live;
  FinalList finals;
  PendingList nhl;
  int64_t next = -1;
  CHECK(parse_live_games(nhl_day, live, &next, &finals, &nhl));
  CHECK_EQ(live.size(), (size_t) 0);
  CHECK_EQ(finals.size(), (size_t) 0);
  CHECK_EQ(nhl.size(), (size_t) 4);
  CHECK(sorted_by_kickoff(nhl));
  if (nhl.size() == 4) {
    CHECK_EQ(nhl[0].event_id, std::string("401892465"));
    CHECK_EQ(nhl[0].away_abbr, std::string("SEA"));
    CHECK_EQ(nhl[0].home_abbr, std::string("DET"));
    CHECK_EQ(nhl[0].away_id, (uint32_t) 124292);
    CHECK(nhl[0].home_id != 0);
    CHECK_EQ(nhl[0].away_record, std::string("2-2-0"));
    CHECK(!nhl[0].home_record.empty());
    CHECK_EQ(nhl[0].away_color, std::string("000d33"));
    CHECK(!nhl[0].home_color.empty());
    CHECK_EQ(nhl[0].tv, std::string("ESPN+"));
    CHECK_EQ((int) nhl[0].rank, 99);
    CHECK_EQ(nhl[0].kickoff_epoch, parse_iso8601_z("2026-10-09T23:00Z"));
    CHECK_EQ(nhl[1].away_abbr, std::string("NYR"));
    CHECK_EQ(nhl[1].tv, std::string("NHL Net"));
    CHECK_EQ(nhl[3].away_abbr, std::string("ANA"));
    CHECK_EQ(nhl[3].kickoff_epoch, parse_iso8601_z("2026-10-10T00:00Z"));
    // The rescan is unchanged: it still waits for the first of them
    CHECK_EQ(next, nhl[0].kickoff_epoch);
    int64_t five_pm = parse_iso8601_z("2026-10-09T21:00Z");
    CHECK_EQ(no_live_rescan_ms(five_pm, next, lo, hi), (uint32_t) ((nhl[0].kickoff_epoch - five_pm) * 1000));
    CHECK_EQ(no_live_rescan_ms(five_pm, next, lo, hi), (uint32_t) 2 * 3600 * 1000);
    // The card: away side first, logos from the ids, the league's sport
    Pending p = nhl[0];
    p.league = NHL;
    GameSnapshot g = pending_snapshot(p);
    CHECK(g.valid);
    CHECK(g.state == GameState::PRE);
    CHECK(g.sport == Sport::HOCKEY);
    CHECK_EQ(g.team_abbr, std::string("SEA"));
    CHECK_EQ(g.opp_abbr, std::string("DET"));
    CHECK_EQ(g.kickoff_epoch, p.kickoff_epoch);
    CHECK_EQ(g.tv, std::string("ESPN+"));
    CHECK_EQ(g.team_logo, team_logo_url(League::NHL, 124292, "SEA"));
    CHECK_EQ(g.opp_logo, team_logo_url(League::NHL, p.home_id, "DET"));
  }
  // Without the list the scan's wait is the same
  next = -1;
  CHECK(parse_live_games(nhl_day, live, &next));
  CHECK_EQ(next, parse_iso8601_z("2026-10-09T23:00Z"));

  // MLB, 10/12: the live game is not one to come; the one after it is
  std::string mlb_day = slurp("fixtures/day_mlb_20261012.json");
  PendingList mlb;
  live.clear();
  CHECK(parse_live_games(mlb_day, live, &next, &finals, &mlb));
  CHECK_EQ(live.size(), (size_t) 1);
  CHECK_EQ(mlb.size(), (size_t) 1);
  if (mlb.size() == 1 && live.size() == 1) {
    CHECK_EQ(live[0].event_id, std::string("401908031"));
    CHECK_EQ(mlb[0].event_id, std::string("401908023"));
    CHECK_EQ(mlb[0].away_abbr, std::string("CLE"));
    CHECK_EQ(mlb[0].home_abbr, std::string("TB"));
    CHECK_EQ(mlb[0].tv, std::string("TBS"));
    CHECK_EQ(next, mlb[0].kickoff_epoch);
    int64_t two_pm = parse_iso8601_z("2026-10-12T18:00Z");
    CHECK_EQ(no_live_rescan_ms(two_pm, next, lo, hi), hi);  // six hours out: the cap
  }

  // NBA, 10/15: two to come, one with no TV listed
  std::string nba_day = slurp("fixtures/day_nba_20261015.json");
  PendingList nba;
  live.clear();
  CHECK(parse_live_games(nba_day, live, &next, nullptr, &nba));
  CHECK_EQ(nba.size(), (size_t) 2);
  if (nba.size() == 2) {
    CHECK_EQ(nba[0].away_abbr, std::string("TOR"));
    CHECK_EQ(nba[0].home_abbr, std::string("NY"));
    CHECK_EQ(nba[0].tv, std::string(""));
    CHECK_EQ(nba[1].away_abbr, std::string("OKC"));
    CHECK_EQ(nba[1].tv, std::string("NBA TV"));
    CHECK_EQ(nba[1].kickoff_epoch, parse_iso8601_z("2026-10-16T00:30Z"));
    CHECK_EQ(next, nba[0].kickoff_epoch);
    // Before TOR at NY the walk forward waits for it
    int64_t six_pm = parse_iso8601_z("2026-10-15T23:00Z");
    CHECK_EQ(recap_scan_next(next, nba, six_pm), nba[0].kickoff_epoch);
    // 6:35 PM Central, ESPN still showing TOR at NY to come: OKC at HOU is
    // still ahead, so no read for tomorrow (bench, 10/15). The rescan's
    // floor while ESPN lags is unchanged.
    int64_t lagging = parse_iso8601_z("2026-10-15T23:35Z");
    CHECK_EQ(recap_scan_next(next, nba, lagging), nba[1].kickoff_epoch);
    CHECK_EQ(no_live_rescan_ms(lagging, next, lo, hi), lo);
    // Both kickoffs past: nothing left today, so the walk may look ahead
    CHECK_EQ(recap_scan_next(next, nba, parse_iso8601_z("2026-10-16T00:35Z")), (int64_t) 0);
    CHECK_EQ(recap_scan_next(0, {}, lagging), (int64_t) 0);
  }

  // NFL week 1, sixteen to come: only the first eight are kept, ties in
  // ESPN's order
  std::string nfl_week = slurp("fixtures/scoreboard_nfl.json");
  PendingList nfl;
  CHECK(parse_live_games(nfl_week, live, &next, nullptr, &nfl));
  CHECK_EQ(nfl.size(), kPendingKeep);
  CHECK(sorted_by_kickoff(nfl));
  if (nfl.size() == kPendingKeep) {
    CHECK_EQ(nfl[0].away_abbr, std::string("NE"));
    CHECK_EQ(nfl[1].away_abbr, std::string("SF"));
    CHECK_EQ(nfl[2].away_abbr, std::string("TB"));
    CHECK_EQ(nfl[7].away_abbr, std::string("CHI"));
    CHECK_EQ(nfl[7].kickoff_epoch, parse_iso8601_z("2026-09-13T17:00Z"));
    CHECK_EQ(next, nfl[0].kickoff_epoch);
  }

  // College, a Saturday with 36 to come: the ranked ones are kept first,
  // and none of them is a game on now or a final. The rescan still waits
  // for the earliest kickoff, ranked or not.
  std::string ncaa = slurp("fixtures/scoreboard_ncaa_live.json");
  PendingList college;
  live.clear();
  finals.clear();
  CHECK(parse_live_games(ncaa, live, &next, &finals, &college));
  CHECK_EQ(college.size(), kPendingKeep);
  CHECK_EQ(next, parse_iso8601_z("2026-09-05T23:00Z"));
  for (size_t i = 0; i < college.size(); i++) {
    CHECK(college[i].rank < 99);
    if (i > 0)
      CHECK(!pending_before(college[i], college[i - 1]));
    for (const auto &l : live)
      CHECK(l.event_id != college[i].event_id);
    CHECK(college[i].event_id != kPostEvent);
  }
  if (college.size() == kPendingKeep) {
    CHECK_EQ(college[0].away_abbr, std::string("WIS"));
    CHECK_EQ((int) college[0].rank, 4);
    CHECK_EQ((int) college[7].rank, 17);
  }

  // TBD, postponed and canceled games are not to come; a live game and a
  // final are not either
  std::string mixed = R"({"events":[)"
                      R"({"id":"1","date":"2026-10-24T04:00Z","status":{"type":{"state":"pre"}},"competitions":[{"timeValid":false,"competitors":[{"id":"1","homeAway":"home","team":{"abbreviation":"AAA"}},{"id":"2","homeAway":"away","team":{"abbreviation":"BBB"}}]}]},)"
                      R"({"id":"2","date":"2026-10-24T16:00Z","status":{"type":{"state":"post","completed":false,"shortDetail":"Postponed"}},"competitions":[{"timeValid":true,"competitors":[{"id":"3","homeAway":"home","team":{"abbreviation":"CCC"}},{"id":"4","homeAway":"away","team":{"abbreviation":"DDD"}}]}]},)"
                      R"({"id":"3","date":"2026-10-24T17:00Z","status":{"type":{"state":"post","completed":false,"shortDetail":"Canceled"}},"competitions":[{"timeValid":true,"competitors":[{"id":"5","homeAway":"home","team":{"abbreviation":"EEE"}},{"id":"6","homeAway":"away","team":{"abbreviation":"FFF"}}]}]},)"
                      R"({"id":"4","date":"2026-10-24T19:30Z","status":{"type":{"state":"pre"}},"competitions":[{"timeValid":true,"competitors":[{"id":"7","homeAway":"home","team":{"abbreviation":"GGG"}},{"id":"8","homeAway":"away","team":{"abbreviation":"HHH"}}],"broadcasts":[{"names":["ESPN","ABC"]}]}]},)"
                      R"({"id":"5","date":"2026-10-24T18:00Z","status":{"type":{"state":"in"}},"competitions":[{"timeValid":true,"competitors":[{"id":"9","homeAway":"home","team":{"abbreviation":"III"}},{"id":"10","homeAway":"away","team":{"abbreviation":"JJJ"}}]}]},)"
                      R"({"id":"6","date":"2026-10-24T15:00Z","status":{"type":{"state":"post","completed":true,"shortDetail":"Final"}},"competitions":[{"timeValid":true,"competitors":[{"id":"11","homeAway":"home","score":"3","team":{"abbreviation":"KKK"}},{"id":"12","homeAway":"away","score":"1","team":{"abbreviation":"LLL"}}]}]}]})";
  PendingList one;
  live.clear();
  finals.clear();
  CHECK(parse_live_games(mixed, live, &next, &finals, &one));
  CHECK_EQ(one.size(), (size_t) 1);
  CHECK_EQ(live.size(), (size_t) 1);
  CHECK_EQ(finals.size(), (size_t) 1);
  if (one.size() == 1) {
    CHECK_EQ(one[0].event_id, std::string("4"));
    CHECK_EQ(one[0].away_abbr, std::string("HHH"));
    CHECK_EQ(one[0].away_id, (uint32_t) 8);
    CHECK_EQ(one[0].tv, std::string("ESPN"));
    CHECK_EQ(next, one[0].kickoff_epoch);
  }

  // Days per sport. Thursday 10/15 2026, Eastern daylight time: at 1 AM and
  // 4 AM Friday no league has turned over (a West Coast game ending then is
  // still Thursday's, football included); by 5 AM all of them have.
  int64_t noon = parse_iso8601_z("2026-10-15T16:00Z");
  int64_t one_am = parse_iso8601_z("2026-10-16T05:00Z");
  int64_t four_am = parse_iso8601_z("2026-10-16T08:00Z");
  int64_t five_am = parse_iso8601_z("2026-10-16T09:00Z");
  for (const auto &info : kLeagues) {
    uint8_t l = (uint8_t) info.league;
    int32_t thu = espn_day(noon, l);
    CHECK_EQ(espn_day(espn_day_epoch(thu), l), thu);
    CHECK_EQ(espn_day(one_am, l), thu);
    CHECK_EQ(espn_day(four_am, l), thu);
    CHECK_EQ(espn_day(five_am, l), thu + 1);
    // The same day the scan reads at that moment
    CHECK(scan_url(info.league, one_am).find("dates=20261015") != std::string::npos);
    CHECK(scan_url(info.league, five_am).find("dates=20261016") != std::string::npos);
  }
  const int32_t thu = espn_day(noon, NBA);
  CHECK_EQ(espn_day(noon, NFL), thu);

  // Header words per league, each in its own day
  CHECK_EQ(std::string(recap_league_word(NFL)), std::string("NFL"));
  CHECK_EQ(std::string(recap_league_word(NCAA)), std::string("college"));
  CHECK_EQ(std::string(recap_league_word(NBA)), std::string("NBA"));
  CHECK_EQ(std::string(recap_league_word(NHL)), std::string("NHL"));
  CHECK_EQ(std::string(recap_league_word(MLB)), std::string("MLB"));
  CHECK_EQ(std::string(recap_league_word((uint8_t) League::WNBA)), std::string("WNBA"));
  CHECK_EQ(std::string(recap_league_word((uint8_t) League::MLS)), std::string("MLS"));
  CHECK_EQ(std::string(recap_league_word((uint8_t) League::EPL)), std::string("EPL"));
  CHECK_EQ(std::string(recap_league_word(MCBB)), std::string("NCAAM"));
  RecapDay hd;
  hd.league = NHL;
  hd.day = thu - 1;
  CHECK_EQ(recap_header(hd, noon), std::string("Yesterday's NHL scores"));
  hd.league = MLB;
  hd.day = thu - 5;
  CHECK_EQ(recap_header(hd, noon), std::string("Saturday's MLB scores"));
  // At 1 AM Friday a late NBA final is still today's, and so is a late
  // college game's; by 5 AM both are yesterday's
  hd.league = NBA;
  hd.day = thu;
  CHECK_EQ(recap_header(hd, one_am), std::string("Today's NBA scores"));
  hd.league = NCAA;
  CHECK_EQ(recap_header(hd, one_am), std::string("Today's college scores"));
  CHECK_EQ(recap_header(hd, five_am), std::string("Yesterday's college scores"));
  Pending tv = mkpending(NHL, 99, 0, "BOS");
  tv.tv = "ESPN+";
  CHECK_EQ(recap_later_ticker(tv, "Today 7:00 PM"), std::string("Later today's NHL games | Today 7:00 PM | ESPN+"));
  tv.tv.clear();
  CHECK_EQ(recap_later_ticker(tv, "Today 7:00 PM"), std::string("Later today's NHL games | Today 7:00 PM"));
  tv.home_abbr = "NYR";
  CHECK_EQ(recap_matchup(tv), std::string("BOS at NYR"));
  CHECK_EQ(recap_next_text(recap_matchup(tv), "Thu 6:00 PM"), std::string("Next: BOS at NYR Thu 6:00 PM"));
  CHECK_EQ(recap_next_text("", "Thu 6:00 PM"), std::string("Next: Thu 6:00 PM"));
  Final hockey = mkfinal(NHL, 99, 0, "BOS");
  CHECK(final_snapshot(hockey).sport == Sport::HOCKEY);
  CHECK(final_snapshot(mkfinal(NCAA, 99, 0, "OSU")).sport == Sport::FOOTBALL);

  // Three leagues at noon Thursday: seven games to come and yesterday's
  // finals.
  const int64_t h = 3600;
  std::vector<RecapDay> days;
  auto finals_day = [](uint8_t league, int32_t day, int n) {
    RecapDay d;
    d.league = league;
    d.day = day;
    for (int i = 0; i < n; i++)
      d.finals.push_back(mkfinal(league, 99, i, "F"));
    return d;
  };
  recap_merge(days, finals_day(NBA, thu - 1, 3));
  recap_merge(days, finals_day(NHL, thu - 1, 13));
  recap_merge(days, finals_day(MLB, thu - 1, 5));
  PendingList later = {
      mkpending(NBA, 99, noon + 7 * h + 1800, "N1"), mkpending(NBA, 99, noon + 8 * h + 1800, "N2"),
      mkpending(NHL, 99, noon + 7 * h, "H1"),        mkpending(NHL, 99, noon + 7 * h, "H2"),
      mkpending(NHL, 99, noon + 7 * h, "H3"),        mkpending(NHL, 99, noon + 8 * h, "H4"),
      mkpending(MLB, 99, noon + 4 * h, "B1"),
  };
  recap_absorb(days, {}, later, noon);
  const uint16_t three = recap_bit(NBA) | recap_bit(NHL) | recap_bit(MLB);
  std::vector<RecapCard> cards = recap_cards(days, three, true, noon);
  // "Later today" and the seven in kickoff order, then the finals: the
  // other 15 of the 22 shared out one by one (NBA has only 3), no Next card
  CHECK_EQ(cards.size(), (size_t) (1 + 7 + 3 + 15));
  size_t later_n = 0, finals_n[kLeagueCount] = {}, headers = 0, first_header = cards.size();
  int64_t last_kick = 0;
  bool ordered = true;
  for (size_t i = 0; i < cards.size(); i++) {
    const RecapCard &k = cards[i];
    if (k.kind == RecapKind::LATER) {
      later_n++;
      int64_t kick = days[k.day].later[k.final].kickoff_epoch;
      if (kick < last_kick || i > first_header)
        ordered = false;
      last_kick = kick;
    } else if (k.kind == RecapKind::HEADER) {
      headers++;
      if (first_header == cards.size())
        first_header = i;
    } else if (k.kind == RecapKind::FINAL) {
      finals_n[days[k.day].league]++;
    }
    CHECK(k.kind != RecapKind::NEXT);
  }
  CHECK(cards[0].kind == RecapKind::LATER_HEADER);
  CHECK_EQ(later_n, (size_t) 7);
  CHECK(ordered);
  CHECK_EQ(first_header, (size_t) 8);
  CHECK_EQ(headers, (size_t) 3);
  CHECK_EQ(finals_n[NBA], (size_t) 3);
  CHECK_EQ(finals_n[NHL], (size_t) 7);
  CHECK_EQ(finals_n[MLB], (size_t) 5);
  CHECK_EQ(recap_card_ms(RecapKind::LATER_HEADER), (uint32_t) 4000);
  CHECK_EQ(recap_card_ms(RecapKind::LATER), (uint32_t) 7000);
  if (cards.size() > 1)
    CHECK_EQ(days[cards[1].day].later[cards[1].final].away_abbr, std::string("B1"));
  // A league the mode does not read has no cards
  cards = recap_cards(days, recap_bit(NHL), false, noon);
  for (const auto &k : cards)
    if (k.kind == RecapKind::LATER || k.kind == RecapKind::FINAL || k.kind == RecapKind::HEADER)
      CHECK_EQ((int) days[k.day].league, (int) NHL);
  CHECK_EQ(cards.size(), (size_t) (1 + 4 + 1 + 13));

  // A game that has kicked off is gone the next time the cards are made
  cards = recap_cards(days, three, true, noon + 7 * h);
  later_n = 0;
  for (const auto &k : cards)
    if (k.kind == RecapKind::LATER)
      later_n++;
  CHECK_EQ(later_n, (size_t) 3);  // N1, N2 and H4 (B1 and H1-H3 have started)
  // With nothing left today the Next card comes back, after the finals
  cards = recap_cards(days, three, true, noon + 9 * h);
  CHECK(cards.front().kind == RecapKind::HEADER);
  CHECK(cards.back().kind == RecapKind::NEXT);
  CHECK_EQ(cards.size(), (size_t) (3 + 21 + 1));
  // A cap that leaves a league out gives it no header
  cards = recap_cards(days, three, false, noon + 9 * h, 2);
  CHECK_EQ(cards.size(), (size_t) 4);
  for (const auto &k : cards)
    CHECK((int) days[k.day].league != (int) MLB);

  // A later scan at 7:01 PM: B1 is final and H1 and H2 are under way. ESPN
  // still lists H3 as not started, but its time has passed, so it is not
  // kept; N1, N2 and H4 are. Today's MLB day now holds the final.
  FinalList today_finals = {mkfinal(MLB, 99, noon + 4 * h, "B1")};
  PendingList still = {later[0], later[1], later[4], later[5]};
  recap_absorb(days, today_finals, still, noon + 7 * h + 60);
  CHECK_EQ(recap_count(days, MLB), (size_t) 6);
  size_t stored = 0;
  for (const auto &d : days)
    stored += d.later.size();
  CHECK_EQ(stored, (size_t) 3);
  // The next day the games to come are gone; the finals stay
  recap_trim(days, noon + 24 * h);
  stored = 0;
  for (const auto &d : days)
    stored += d.later.size();
  CHECK_EQ(stored, (size_t) 0);
  CHECK_EQ(recap_count(days, NHL), (size_t) 13);
  CHECK_EQ(recap_count(days, NBA), (size_t) 3);

  // Every league with two to come and a full NFL day of finals: eight
  // games to come, one from each of the first eight leagues, and the
  // finals come down to the 14 left of the 22
  std::vector<RecapDay> all;
  PendingList two_each;
  uint16_t every = 0;
  for (const auto &info : kLeagues) {
    uint8_t l = (uint8_t) info.league;
    every |= recap_bit(l);
    two_each.push_back(mkpending(l, 99, noon + 2 * h, "A"));
    two_each.push_back(mkpending(l, 99, noon + 3 * h, "B"));
  }
  recap_merge(all, finals_day(NFL, espn_day(noon, NFL) - 4, 20));
  recap_absorb(all, {}, two_each, noon);
  stored = 0;
  for (const auto &d : all) {
    stored += d.later.size();
    CHECK(d.league != MCBB);  // the one the eight left out is not kept
  }
  CHECK_EQ(stored, kRecapLaterCap);
  cards = recap_cards(all, every, true, noon);
  later_n = 0;
  size_t nfl_finals = 0;
  for (const auto &k : cards) {
    if (k.kind == RecapKind::LATER)
      later_n++;
    else if (k.kind == RecapKind::FINAL)
      nfl_finals++;
  }
  CHECK_EQ(later_n, kRecapLaterCap);
  CHECK_EQ(nfl_finals, kRecapCap - kRecapLaterCap);
  CHECK_EQ(nfl_finals, (size_t) 14);
}

static void test_backoff() {
  // Jitter lands in the upper half of the doubled delay.
  CHECK_EQ(backoff_ms(1, false, 0, 0), 15000u);
  CHECK_EQ(backoff_ms(1, false, 0, 15000), 30000u);
  CHECK_EQ(backoff_ms(1, false, 0, 15001), 15000u);  // wraps, never past the delay
  CHECK_EQ(backoff_ms(2, false, 0, 30000), 60000u);
  CHECK_EQ(backoff_ms(3, false, 0, 60000), 120000u);
  // Ordinary failures stop doubling at 10 minutes, even after a long run.
  CHECK_EQ(backoff_ms(6, false, 0, 300000), 600000u);
  CHECK_EQ(backoff_ms(200, false, 0, 300000), 600000u);
  CHECK_EQ(backoff_ms(200, false, 0, 0), 300000u);
  // 403/429 start at 5 minutes and stop at an hour.
  CHECK_EQ(backoff_ms(1, true, 0, 150000), 300000u);
  CHECK_EQ(backoff_ms(1, true, 0, 0), 150000u);
  CHECK_EQ(backoff_ms(2, true, 0, 300000), 600000u);
  CHECK_EQ(backoff_ms(250, true, 0, 1800000), 3600000u);
  // Every jitter value stays inside its window.
  for (uint32_t r = 0; r < 100000; r += 977) {
    uint32_t w = backoff_ms(4, false, 0, r * 7919u);
    CHECK(w >= 120000u && w <= 240000u);
  }
  // Retry-After is honored when it is longer, capped at the hour, with up to
  // a quarter on top. A shorter one does not cut the backoff.
  CHECK_EQ(backoff_ms(1, true, 900000, 0), 900000u);
  CHECK_EQ(backoff_ms(1, true, 900000, 225000), 1125000u);
  CHECK_EQ(backoff_ms(1, true, 9000000, 0), 3600000u);
  CHECK_EQ(backoff_ms(1, true, 10000, 0), 150000u);
  CHECK_EQ(backoff_ms(1, false, 900000, 0), 15000u);  // only a refusal carries it
  CHECK_EQ(parse_retry_after_ms("120"), 120000u);
  CHECK_EQ(parse_retry_after_ms("0"), 0u);
  CHECK_EQ(parse_retry_after_ms(""), 0u);
  CHECK_EQ(parse_retry_after_ms("Wed, 21 Oct 2015 07:28:00 GMT"), 0u);
  CHECK_EQ(parse_retry_after_ms("1234567"), 0u);
  CHECK(is_throttle_status(403));
  CHECK(is_throttle_status(429));
  CHECK(!is_throttle_status(404));
  CHECK(!is_throttle_status(503));
}

// Favorites' side polls (another favorite's game has the board) keep out of
// the board's miss count, freshness and back-off.
static void test_fav_side_polls() {
  const uint32_t base = 30 * 1000, min = 60 * 1000;
  CHECK_EQ(side_poll_wait_ms(0, base), base);
  CHECK_EQ(side_poll_wait_ms(1, base), 5 * min);
  CHECK_EQ(side_poll_wait_ms(2, base), 10 * min);
  CHECK_EQ(side_poll_wait_ms(4, base), 40 * min);
  CHECK_EQ(side_poll_wait_ms(5, base), 60 * min);
  CHECK_EQ(side_poll_wait_ms(255, base), 60 * min);

  // A side poll that worked clears nothing: the board's own poll may be the
  // one failing, and its "Can't reach ESPN" must still come.
  FavPollEffect fx = fav_poll_effect(true, true, false, false);
  CHECK(!fx.mark_good && !fx.clear_backoff && !fx.count_miss && !fx.back_off);
  // One that failed counts no miss and starts no back-off, so the board's
  // 10 s poll keeps its pace.
  fx = fav_poll_effect(false, true, false, false);
  CHECK(!fx.count_miss && !fx.back_off);
  // A refusal is everyone's: back off.
  fx = fav_poll_effect(false, true, true, false);
  CHECK(!fx.count_miss && fx.back_off);
  // The board's own game, or a read with nothing on the board: as before.
  fx = fav_poll_effect(true, false, false, true);
  CHECK(fx.mark_good && fx.clear_backoff);
  fx = fav_poll_effect(false, false, false, true);
  CHECK(fx.count_miss && fx.back_off && !fx.mark_good);
  // Another favorite's refresh read worked: ESPN answered, but the board's
  // game wasn't read, so its freshness stays.
  fx = fav_poll_effect(true, false, false, false);
  CHECK(!fx.mark_good && fx.clear_backoff && !fx.count_miss && !fx.back_off);
  // One that failed (its team read or its game read) leaves the board's miss
  // count and pace alone, as a side poll does; it retries on its own clock.
  fx = fav_poll_effect(false, false, false, false);
  CHECK(!fx.count_miss && !fx.back_off && !fx.mark_good && !fx.clear_backoff);
  // Except a refusal.
  fx = fav_poll_effect(false, false, true, false);
  CHECK(!fx.count_miss && fx.back_off);
  // The board's own read refused: a miss and a back-off.
  fx = fav_poll_effect(false, false, true, true);
  CHECK(fx.count_miss && fx.back_off);
}

// After a My team game poll failed: read the team again now, or back off.
static void test_reread_after_poll() {
  // A transport failure (timeout, 5xx): back off, as before.
  CHECK(!reread_team_after_poll(false, false, false, false));
  // ESPN served no such game (a 404, another game's document): read the team
  // once; if it names the same game again, back off.
  CHECK(reread_team_after_poll(false, true, false, false));
  CHECK(!reread_team_after_poll(false, true, false, true));
  // The boot cache's game didn't poll: read the team, whatever the failure.
  CHECK(reread_team_after_poll(true, false, false, false));
  CHECK(reread_team_after_poll(true, true, false, true));
  // A refusal waits out the back-off in every case.
  CHECK(!reread_team_after_poll(true, true, true, false));
  CHECK(!reread_team_after_poll(false, true, true, false));
}

static void test_cup_backoff() {
  // A cup job backs off when its last read failed or was refused.
  CHECK(cup_read_backs_off(true, true, false, false));   // the poll failed
  CHECK(cup_read_backs_off(true, true, false, true));    // the poll was refused
  CHECK(!cup_read_backs_off(true, true, true, false));   // the poll landed
  CHECK(!cup_read_backs_off(false, true, true, false));  // a good poll after a failed cup read
  CHECK(cup_read_backs_off(false, false, false, false)); // the cup read failed, no poll
  CHECK(cup_read_backs_off(true, false, false, true));   // today's scoreboard refused, no poll
  CHECK(!cup_read_backs_off(true, false, false, false)); // a clean cup read with no poll

  // A run of failed cup polls doubles from 30 s to the 10 minute cap.
  // rnd 0 is the low end of each window, half the doubled delay.
  Backoff b;
  const uint32_t plain[] = {15000, 30000, 60000, 120000, 240000, 300000, 300000};
  for (uint32_t want : plain) {
    CHECK(cup_read_backs_off(true, true, false, false));
    CHECK_EQ(b.fail(false, 0, 0), want);
  }
  CHECK_EQ(b.streak, (uint8_t) 7);
  CHECK(!b.throttled);
  // A 403 mid-run moves the rest of the run to the refusal schedule and keeps
  // it there while later reads fail plainly.
  CHECK_EQ(b.fail(true, 0, 0), 1800000u);
  CHECK(b.throttled);
  CHECK_EQ(b.fail(false, 0, 0), 1800000u);
  // A good poll starts the next run over.
  b.clear();
  CHECK_EQ(b.fail(false, 0, 0), 15000u);
  // A refused cup read with no poll starts at 5 minutes and honors
  // Retry-After.
  b.clear();
  CHECK(cup_read_backs_off(true, false, false, true));
  CHECK_EQ(b.fail(true, 0, 0), 150000u);
  CHECK_EQ(b.fail(true, 0, 0), 300000u);
  b.clear();
  CHECK_EQ(b.fail(true, 900000, 0), 900000u);
  // The streak stops counting at 255 and the wait stays at the cap.
  b.clear();
  for (int i = 0; i < 300; i++)
    b.fail(false, 0, 300000);
  CHECK_EQ(b.streak, (uint8_t) 255);
  CHECK_EQ(b.fail(false, 0, 300000), 600000u);
}

static void test_auto_update() {
  CHECK(version_newer("2026.10.0", "1.5.5"));
  CHECK(version_newer("1.5.6", "1.5.5"));
  CHECK(version_newer("1.10.0", "1.9.9"));
  CHECK(version_newer("2026.10.1", "2026.10.0"));
  CHECK(!version_newer("1.5.5", "1.5.5"));
  CHECK(!version_newer("1.5.5", "2026.10.0"));  // a test build ahead of the release
  CHECK(!version_newer("", "1.5.5"));
  CHECK(!version_newer("v1.5.6", "1.5.5"));
  CHECK(version_newer("1.5.5.1", "1.5.5"));
  CHECK(!version_newer("1.5.5-beta", "1.5.5"));

  CHECK(auto_update_slot(0, 0) == 0);
  CHECK(auto_update_slot(0xA9, 0x94) == (0xA994 % 60));
  for (int i = 0; i < 256; i++)
    CHECK(auto_update_slot(0x12, (uint8_t) i) < kAutoUpdateSpreadMinutes);
  CHECK(auto_update_day(2026, 286) != auto_update_day(2026, 287));
  CHECK(auto_update_day(2026, 365) != auto_update_day(2027, 1));

  // 3:20 AM, slot minute 15, tonight's read done, a newer release out
  AutoUpdateInputs in;
  in.enabled = true;
  in.clock_valid = true;
  in.hour = 3;
  in.minute = 20;
  in.today = auto_update_day(2026, 286);
  in.tried_day = auto_update_day(2026, 280);
  in.checked_day = in.today;
  in.slot = 15;
  in.check_done = true;
  in.available = true;
  in.newer = true;
  CHECK(auto_update_step(in) == AutoUpdateStep::INSTALL);

  AutoUpdateInputs t = in;
  t.enabled = false;  // off: nothing, ever
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.clock_valid = false;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.live = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::WAIT_GAME);
  t.hour = 5;  // the game ran past the window: not tonight
  t.minute = 0;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.tried_day = in.today;  // tried tonight already (failed, or cut off)
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t.today = auto_update_day(2026, 287);  // the next night it goes again
  t.checked_day = t.today;
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL);
  t = in;
  t.minute = 14;  // before this panel's minute
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.hour = 2;
  t.minute = 59;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t.hour = 4;  // the second hour still counts
  t.minute = 59;
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL);
  t.hour = 5;
  t.minute = 0;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t.hour = 15;  // 3 PM is not 3 AM
  t.minute = 30;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.checked_day = auto_update_day(2026, 285);  // not read tonight yet
  CHECK(auto_update_step(t) == AutoUpdateStep::CHECK);
  t = in;
  t.check_done = false;  // read still out
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.available = false;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.newer = false;  // a different version, but not a newer one
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);

  // Beta channel versions: the build number after "-beta." is the next part.
  CHECK(version_newer("2026.10.0-beta.813", "2026.10.0-beta.812"));
  CHECK(!version_newer("2026.10.0-beta.812", "2026.10.0-beta.812"));
  CHECK(!version_newer("2026.10.0-beta.811", "2026.10.0-beta.812"));
  CHECK(version_newer("2026.10.0-beta.812", "2026.10.0"));  // a hand-flashed bar panel takes the beta
  CHECK(version_newer("2026.10.1-beta.900", "2026.10.0-beta.950"));
  CHECK(version_newer("2026.10.0-beta.1000", "2026.10.0-beta.999"));
  CHECK(!version_newer("2026.10.0-beta.", "2026.10.0"));
  CHECK(!version_newer("2026.10.0-dev", "2026.10.0"));

  // Install now: names in the release summary.
  const std::string me = "gameday-aba970";
  CHECK(install_now_listed("Beta 2026.10.0-beta.812. Install now: gameday-aba970", me));
  CHECK(install_now_listed("Install now: gameday-500ebc, gameday-aba970", me));
  CHECK(install_now_listed("Install now: gameday-500ebc,gameday-aba970 ", me));
  CHECK(install_now_listed("Beta 2026.10.0-beta.812. Install now: all", me));
  CHECK(!install_now_listed("Install now: gameday-500ebc", me));
  CHECK(!install_now_listed("Install now: gameday-aba9700", me));
  CHECK(!install_now_listed("Install now: gameday-aba97", me));
  CHECK(!install_now_listed("Install now:", me));
  CHECK(!install_now_listed("Beta 2026.10.0-beta.812", me));
  CHECK(!install_now_listed("gameday-aba970", me));  // named, but not in the list
  CHECK(!install_now_listed("Install now: all", ""));

  // Listed: installs at any hour, even off, unless a game is live.
  t = in;
  t.hour = 15;
  t.now = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL_NOW);
  t.clock_valid = false;  // no clock needed
  t.tried_day = in.today;  // and a night try doesn't block it
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL_NOW);
  t.live = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::WAIT_GAME);
  t.live = false;
  t.newer = false;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t.newer = true;
  t.available = false;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t.available = true;
  t.enabled = false;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.now = true;  // in the window too, the try counts for this version, not the night
  CHECK(auto_update_step(t) == AutoUpdateStep::INSTALL_NOW);

  // A release that rolled back here: skipped every night, live game or not,
  // and Install now doesn't override it.
  t = in;
  t.failed = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::SKIP_FAILED);
  t.live = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::SKIP_FAILED);
  t.live = false;
  t.today = auto_update_day(2026, 293);  // a week later, still skipped
  t.checked_day = t.today;
  CHECK(auto_update_step(t) == AutoUpdateStep::SKIP_FAILED);
  t.check_done = false;  // nothing to say before tonight's read is back
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
  t = in;
  t.failed = true;
  t.now = true;
  CHECK(auto_update_step(t) == AutoUpdateStep::SKIP_FAILED);
  t.hour = 15;
  CHECK(auto_update_step(t) == AutoUpdateStep::IDLE);
}

static void test_update_mark() {
  UpdateMark m{};
  CHECK(update_mark_boot(m, "2026.10.0", false) == UpdateOutcome::NONE);
  CHECK(!update_failed_here(m, "2026.10.0"));
  CHECK(!update_failed_here(m, ""));

  // Installed and running it: settled only once the boot is good.
  update_mark_set(m.installing, "2026.10.1");
  CHECK(update_mark_boot(m, "2026.10.1", false) == UpdateOutcome::PENDING);
  CHECK_EQ(std::string(m.installing), std::string("2026.10.1"));
  CHECK(m.failed[0] == '\0');
  // The bootloader's flag can be left over from an older rollback; running
  // the new version still means it took.
  CHECK(update_mark_boot(m, "2026.10.1", true) == UpdateOutcome::PENDING);

  // Rolled back: the old firmware boots and remembers the version.
  m = UpdateMark{};
  update_mark_set(m.installing, "2026.10.1");
  CHECK(update_mark_boot(m, "2026.10.0", true) == UpdateOutcome::ROLLED_BACK);
  CHECK(m.installing[0] == '\0');
  CHECK_EQ(std::string(m.failed), std::string("2026.10.1"));
  CHECK(update_failed_here(m, "2026.10.1"));
  CHECK(!update_failed_here(m, "2026.10.2"));  // a later release still installs
  CHECK(!update_failed_here(m, "2026.10.1.1"));
  // Later boots keep it while the panel stays behind it.
  CHECK(update_mark_boot(m, "2026.10.0", true) == UpdateOutcome::NONE);
  CHECK(update_failed_here(m, "2026.10.1"));

  // A manual install of it that takes, or anything newer, drops it.
  UpdateMark k = m;
  CHECK(update_mark_boot(k, "2026.10.1", false) == UpdateOutcome::NONE);
  CHECK(k.failed[0] == '\0');
  k = m;
  update_mark_set(k.installing, "2026.10.2");
  CHECK(update_mark_boot(k, "2026.10.2", false) == UpdateOutcome::PENDING);
  CHECK(k.failed[0] == '\0');
  // A later release that rolls back too replaces it.
  k = m;
  update_mark_set(k.installing, "2026.10.2");
  CHECK(update_mark_boot(k, "2026.10.0", true) == UpdateOutcome::ROLLED_BACK);
  CHECK_EQ(std::string(k.failed), std::string("2026.10.2"));

  // Still on the old firmware with no rollback (power cut mid-download): not a failure.
  m = UpdateMark{};
  update_mark_set(m.installing, "2026.10.1");
  CHECK(update_mark_boot(m, "2026.10.0", false) == UpdateOutcome::NOT_INSTALLED);
  CHECK(m.installing[0] == '\0');
  CHECK(m.failed[0] == '\0');

  // A rollback from a version that isn't newer (a manual downgrade) is
  // nothing automatic installs anyway.
  m = UpdateMark{};
  update_mark_set(m.installing, "1.5.5");
  CHECK(update_mark_boot(m, "2026.10.0", true) == UpdateOutcome::ROLLED_BACK);
  CHECK(m.failed[0] == '\0');

  // Beta versions fit, a longer string is cut and still ends.
  m = UpdateMark{};
  update_mark_set(m.installing, "2026.10.0-beta.812");
  CHECK(update_mark_boot(m, "2026.10.0-beta.811", true) == UpdateOutcome::ROLLED_BACK);
  CHECK(update_failed_here(m, "2026.10.0-beta.812"));
  CHECK(!update_failed_here(m, "2026.10.0-beta.813"));
  update_mark_set(m.installing, std::string(40, '9'));
  CHECK_EQ(strlen(m.installing), kUpdateVersionLen - 1);
  // Flash that holds garbage with no end still reads as a string.
  memset(m.failed, '7', sizeof(m.failed));
  m.installing[0] = '\0';
  update_mark_boot(m, "1.0", false);
  CHECK(strlen(m.failed) < kUpdateVersionLen);
}

// Every byte of a panel_text() result is printable ASCII or part of a Latin-1
// character the fonts list (U+00A1 to U+00FF without the soft hyphen), the
// only glyphs the panel's fonts carry.
static bool panel_drawable(const std::string &s) {
  for (size_t i = 0; i < s.size(); i++) {
    const uint8_t c = (uint8_t) s[i];
    if (c >= 0x20 && c < 0x7F)
      continue;
    if ((c == 0xC2 || c == 0xC3) && i + 1 < s.size() && ((uint8_t) s[i + 1] & 0xC0) == 0x80) {
      if (c == 0xC2 && ((uint8_t) s[i + 1] < 0xA1 || (uint8_t) s[i + 1] == 0xAD))
        return false;
      i++;
      continue;
    }
    return false;
  }
  return true;
}

static void test_boot_log() {
  const int64_t now = 1792000000;  // 2026-10-16
  const uint32_t build_a = 111, build_b = 222;
  // RTC memory after a power cut is noise; the log starts over.
  BootLog log;
  memset(&log, 0xA5, sizeof(log));
  CHECK(!boot_log_valid(log));
  CHECK(boot_log_start(log, kResetPowerOn, build_a, 0) == Restart::POWER);
  CHECK(boot_log_valid(log));
  CHECK_EQ(log.boots, 1u);
  CHECK_EQ((int) log.fault, 0);
  CHECK_EQ(boot_log_fault_ago(log, now, 50), (int64_t) -1);

  // A crash: kept, and as old as the uptime while it's this start.
  CHECK(boot_log_start(log, kResetPanic, build_a, now) == Restart::CRASH);
  CHECK_EQ(log.boots, 2u);
  CHECK(log.fault == (uint8_t) Restart::CRASH);
  CHECK_EQ(boot_log_fault_ago(log, now + 600, 600), (int64_t) 600);
  // The owner presses Reboot: the start says restart, the crash stays.
  boot_log_planned(log);
  CHECK(boot_log_start(log, kResetSoftware, build_a, now + 3600) == Restart::RESTART);
  CHECK(log.fault == (uint8_t) Restart::CRASH);
  CHECK_EQ(boot_log_fault_ago(log, now + 7200, 3600), (int64_t) 7200);
  // Without a clock, an earlier fault's age isn't known.
  CHECK_EQ(boot_log_fault_ago(log, 0, 3600), (int64_t) -1);

  // The S3 sometimes reports esp_restart() as a watchdog; the mark says not.
  boot_log_planned(log);
  CHECK(boot_log_start(log, kResetWdt, build_a, now) == Restart::RESTART);
  CHECK(boot_log_start(log, kResetWdt, build_a, now) == Restart::WATCHDOG);  // unmarked
  CHECK(log.fault == (uint8_t) Restart::WATCHDOG);

  // A planned restart into a different build is an update, either way.
  boot_log_planned(log);
  CHECK(boot_log_start(log, kResetSoftware, build_b, now) == Restart::UPDATE);
  boot_log_planned(log);
  CHECK(boot_log_start(log, kResetWdt, build_a, now) == Restart::UPDATE);
  // An unmarked software reset is still a restart, and the mark is used once.
  CHECK(boot_log_start(log, kResetSoftware, build_b, now) == Restart::RESTART);
  CHECK_EQ((int) log.planned, 0);

  // A brownout at a start with no clock yet gets its time once it's set.
  CHECK(boot_log_start(log, kResetBrownout, build_b, 0) == Restart::BROWNOUT);
  CHECK_EQ(log.fault_at, (int64_t) 0);
  boot_log_clock(log, 1000);  // not a real time
  CHECK_EQ(log.fault_at, (int64_t) 0);
  boot_log_clock(log, now);
  CHECK_EQ(log.fault_at, (int64_t) now);
  boot_log_clock(log, now + 5);  // only the first
  CHECK_EQ(log.fault_at, (int64_t) now);
  CHECK(boot_log_start(log, kResetSoftware, build_b, now + 60) == Restart::RESTART);
  boot_log_clock(log, now + 60);  // not this start's fault
  CHECK_EQ(log.fault_at, (int64_t) now);
  CHECK_EQ(boot_log_fault_ago(log, now + 90, 30), (int64_t) 90);
  CHECK(boot_log_start(log, kResetPowerGlitch, build_b, now) == Restart::BROWNOUT);

  // Power: a log that survived it is cleared anyway, faults and all.
  CHECK(boot_log_start(log, kResetPowerOn, build_b, now) == Restart::POWER);
  CHECK_EQ(log.boots, 1u);
  CHECK_EQ((int) log.fault, 0);
  // A flipped bit and the log starts over.
  CHECK(boot_log_start(log, kResetPanic, build_b, now) == Restart::CRASH);
  log.boots ^= 4;
  CHECK(!boot_log_valid(log));
  CHECK_EQ(boot_log_fault_ago(log, now, 10), (int64_t) -1);
  CHECK(boot_log_start(log, kResetSoftware, build_b, now) == Restart::RESTART);
  CHECK_EQ(log.boots, 1u);
  CHECK_EQ((int) log.fault, 0);
  memset(&log, 0, sizeof(log));
  boot_log_planned(log);  // no effect on an invalid log
  CHECK(!boot_log_valid(log));

  CHECK(classify_restart(kResetTaskWdt, true, false) == Restart::WATCHDOG);
  CHECK(classify_restart(kResetIntWdt, false, false) == Restart::WATCHDOG);
  CHECK(classify_restart(kResetCpuLockup, false, false) == Restart::CRASH);
  CHECK(classify_restart(kResetUsb, false, false) == Restart::USB);
  CHECK(classify_restart(kResetExternal, false, false) == Restart::POWER);
  CHECK(classify_restart(kResetDeepSleep, false, false) == Restart::OTHER);
  CHECK(classify_restart(99, false, false) == Restart::OTHER);
  CHECK_EQ(std::string(restart_key(Restart::BROWNOUT)), std::string("brownout"));
  CHECK_EQ(std::string(restart_key(Restart::OTHER)), std::string("other"));
}

static void test_health_json() {
  Health h{};
  h.uptime_s = 7260;
  h.start = Restart::RESTART;
  h.boots = 3;
  h.fault = Restart::BROWNOUT;
  h.fault_ago = 9000;
  h.heap_kb = 143.24f;
  h.block_kb = 40.0f;
  h.psram_kb = 4100.5f;
  h.wifi = true;
  h.rssi = -58;
  h.espn_status = 200;
  h.espn_ago = 9;
  h.version = "2026.10.0";
  h.built = 1792285260;  // 2026-10-18 1:01 AM UTC
  h.channel = "beta";
  char buf[512];
  int n = health_json(buf, sizeof(buf), h);
  CHECK(n > 0 && n < (int) sizeof(buf));
  CHECK_EQ(std::string(buf),
           std::string("{\"up\":7260,\"start\":\"restart\",\"boots\":3,\"fault\":\"brownout\",\"fault_ago\":9000,"
                       "\"heap_kb\":143.2,\"block_kb\":40.0,\"psram_kb\":4100.5,\"rssi\":-58,\"espn\":200,"
                       "\"espn_ago\":9,\"version\":\"2026.10.0\",\"built\":1792285260,"
                       "\"channel\":\"beta\"}"));
  // Parses, and the optional keys go when they mean nothing.
  JsonDocument doc;
  CHECK(!deserializeJson(doc, buf));
  CHECK_EQ(doc["rssi"].as<int>(), -58);
  h.fault_ago = -1;
  h.wifi = false;
  h.espn_ago = -1;
  health_json(buf, sizeof(buf), h);
  doc.clear();
  CHECK(!deserializeJson(doc, buf));
  CHECK_EQ(std::string(doc["fault"].as<const char *>()), std::string("brownout"));
  CHECK(doc["fault_ago"].isNull());
  CHECK(doc["rssi"].isNull());
  CHECK(doc["espn"].isNull());
  h.fault = Restart::NONE;
  h.espn_status = 0;  // no answer
  h.espn_ago = 30;
  health_json(buf, sizeof(buf), h);
  doc.clear();
  CHECK(!deserializeJson(doc, buf));
  CHECK(doc["fault"].isNull());
  CHECK_EQ(doc["espn"].as<int>(), 0);
  CHECK_EQ(doc["espn_ago"].as<int>(), 30);
}

static void test_heat_guard() {
  HeatGuard g;
  CHECK(!g.feed(NAN));
  CHECK(std::isnan(g.last_c));
  CHECK(!g.feed(60.0f));
  CHECK(!g.hot);
  // The thermometer goes up at kHotC and stays up between the two limits,
  // however long.
  CHECK(!g.feed(kHotC - 0.1f));
  CHECK(g.feed(kHotC));
  CHECK(g.hot);
  CHECK(!g.feed(kHotC + 10));
  CHECK(!g.feed(kCoolC + 1));
  CHECK(!g.feed(kCoolC));
  CHECK(g.hot);
  // Down once it reads under kCoolC
  CHECK(g.feed(kCoolC - 0.1f));
  CHECK(!g.hot);
  CHECK(!g.feed(20.0f));
  CHECK(g.last_c == 20.0f);
  // A NAN in between changes nothing
  CHECK(g.feed(kHotC));
  CHECK(!g.feed(NAN));
  CHECK(g.hot);
  CHECK(g.last_c == kHotC);
}

static void test_power_log() {
  PowerLog log{};
  CHECK_EQ(sizeof(PowerLog), (size_t) 8);  // the flash blob from the earlier guard
  CHECK(!power_log_start(log, false));
  CHECK_EQ(log.brownouts, 0u);
  CHECK(power_log_start(log, true));
  CHECK(power_log_start(log, true));
  CHECK_EQ(log.brownouts, 2u);
  CHECK(!power_log_start(log, false));
  CHECK_EQ(log.brownouts, 2u);
}

static void test_power_dip() {
  // A normal start: never dims, whatever the clock says.
  PowerDip none;
  CHECK(!none.light(0));
  CHECK(!none.light(1000));
  CHECK(!none.tick(5000));
  // A brownout start: the window opens when the display first lights, not
  // at boot, and holds for kDipMs.
  PowerDip d;
  d.pending = true;
  CHECK(!d.tick(400));  // nothing lit yet
  CHECK(d.light(500));
  CHECK(!d.pending);
  CHECK(d.light(500 + kDipMs - 1));
  CHECK(!d.tick(500 + kDipMs - 1));
  CHECK(d.tick(500 + kDipMs));  // once, when it closes
  CHECK(!d.tick(500 + kDipMs));
  CHECK(!d.light(500 + kDipMs));
  CHECK(!d.light(10 * kDipMs));
  // A brightness change inside the window stays dimmed; the window closes
  // on the next light() too, when the loop hasn't ticked yet.
  PowerDip e;
  e.pending = true;
  CHECK(e.light(100));
  CHECK(e.light(1100));
  CHECK(!e.light(100 + kDipMs));
  CHECK(!e.tick(100 + kDipMs + 1));
  // millis() wrapping doesn't stall it.
  PowerDip w;
  w.pending = true;
  const uint32_t m = 0xFFFFFFFFu - 1000;
  CHECK(w.light(m));
  CHECK(w.light(m + 2000));
  CHECK(w.tick(m + kDipMs));
}

static void test_guarded_brightness() {
  CHECK_EQ((int) dipped_brightness(128, false), 128);
  CHECK_EQ((int) dipped_brightness(255, false), 255);
  CHECK_EQ((int) dipped_brightness(128, true), 64);
  CHECK_EQ((int) dipped_brightness(255, true), 127);
  CHECK_EQ((int) dipped_brightness(1, true), 1);  // never off
  CHECK_EQ((int) dipped_brightness(0, true), 0);
  CHECK(guard_mark(false, false) == GuardMark::NONE);
  CHECK(guard_mark(true, false) == GuardMark::HEAT);
  CHECK(guard_mark(false, true) == GuardMark::POWER);
  CHECK(guard_mark(true, true) == GuardMark::POWER);

  char buf[96];
  guard_json_keys(buf, sizeof(buf), 47.25f, true, 4, true);
  CHECK_EQ(std::string(buf), std::string(",\"temp\":47.2,\"heat\":1,\"brownouts\":4,\"power\":\"dipped\""));
  guard_json_keys(buf, sizeof(buf), NAN, false, 4, false);
  CHECK_EQ(std::string(buf), std::string(",\"heat\":0,\"brownouts\":4,\"power\":\"ok\""));
  guard_json_keys(buf, sizeof(buf), 80.0f, true, 0, false);
  CHECK_EQ(std::string(buf), std::string(",\"temp\":80.0,\"heat\":1,\"brownouts\":0,\"power\":\"ok\""));
}

static void test_countdown() {
  const int64_t m = 60, h = 3600, d = 24 * 3600;
  // Days and hours a day or more out, hours and minutes under a day, then
  // minutes. Minutes round up, so the last minute reads 1m.
  CHECK_EQ(countdown_text(2 * d + 14 * h + 30 * m), std::string("2d 14h"));
  CHECK_EQ(countdown_text(d), std::string("1d 0h"));
  CHECK_EQ(countdown_text(d - 1), std::string("1d 0h"));
  CHECK_EQ(countdown_text(d - m), std::string("23h 59m"));
  CHECK_EQ(countdown_text(14 * h + 5 * m), std::string("14h 05m"));
  CHECK_EQ(countdown_text(14 * h + 4 * m + 1), std::string("14h 05m"));
  CHECK_EQ(countdown_text(h), std::string("1h 00m"));
  CHECK_EQ(countdown_text(45 * m), std::string("45m"));
  CHECK_EQ(countdown_text(1), std::string("1m"));
  CHECK_EQ(countdown_text(0), std::string(""));
  CHECK_EQ(countdown_text(-5), std::string(""));
  CHECK_EQ(countdown_text(245 * d + 3 * h), std::string("245d 3h"));
  CHECK_EQ(countdown_text(1000 * d + 3 * h), std::string("1000d"));

  // The card's digits: counting down, "Today" through the custom day, then over.
  const int64_t at = 1797724800;  // any midnight
  CHECK_EQ(countdown_card_text(at, at + d, at - 2 * h), std::string("2h 00m"));
  CHECK_EQ(countdown_card_text(at, at + d, at), std::string("Today"));
  CHECK_EQ(countdown_card_text(at, at + d, at + d - 1), std::string("Today"));
  CHECK_EQ(countdown_card_text(at, at + d, at + d), std::string(""));
  // A kickoff ends the card at once.
  CHECK_EQ(countdown_card_text(at, at, at), std::string(""));

  CHECK_EQ(matchup_line("DAL", "PHI", true), std::string("DAL vs PHI"));
  CHECK_EQ(matchup_line("DAL", "PHI", false), std::string("DAL at PHI"));
  CHECK_EQ(matchup_line("", "PHI", false), std::string("at PHI"));
  CHECK_EQ(matchup_line("DAL", "", true), std::string("Next game"));

  uint32_t ymd = 1;
  CHECK(parse_countdown_date("2026-12-25", ymd) && ymd == 20261225u);
  CHECK(parse_countdown_date("", ymd) && ymd == 0u);
  CHECK(parse_countdown_date("none", ymd) && ymd == 0u);
  CHECK(parse_countdown_date("2028-02-29", ymd) && ymd == 20280229u);
  CHECK(!parse_countdown_date("2027-02-29", ymd));
  CHECK(!parse_countdown_date("2026-04-31", ymd));
  CHECK(!parse_countdown_date("2026-13-01", ymd));
  CHECK(!parse_countdown_date("2026-00-10", ymd));
  CHECK(!parse_countdown_date("1999-12-31", ymd));
  CHECK(!parse_countdown_date("2026-1-5", ymd));
  CHECK(!parse_countdown_date("2026/12/25", ymd));
  CHECK(!parse_countdown_date("2026-12-2x", ymd));
  CHECK_EQ(countdown_date_text(20261225), std::string("2026-12-25"));
  CHECK_EQ(countdown_date_text(20270305), std::string("2027-03-05"));
  CHECK_EQ(countdown_date_text(0), std::string(""));
  CHECK_EQ(countdown_day_label(20261225), std::string("Dec 25"));
  CHECK_EQ(countdown_day_label(20270305), std::string("Mar 5"));

  CHECK_EQ(countdown_name("  Christmas  "), std::string("Christmas"));
  CHECK_EQ(countdown_name("Line\nbreak"), std::string("Linebreak"));
  CHECK_EQ(countdown_name("   "), std::string(""));
  // Emoji are left out, not drawn as '?', and leave no double space.
  CHECK_EQ(countdown_name("Xmas \xf0\x9f\x8e\x84 Eve"), std::string("Xmas Eve"));
  CHECK_EQ(countdown_name("\xf0\x9f\x8e\x82 Birthday"), std::string("Birthday"));
  CHECK_EQ(countdown_name("Go \xe2\x9a\xbd\xef\xb8\x8f"), std::string("Go"));
  CHECK_EQ(countdown_name("Fianc\xc3\xa9 day"), std::string("Fianc\xc3\xa9 day"));
  CHECK_EQ(countdown_name("The day the season finally starts"), std::string("The day the season f"));
  CHECK_EQ(countdown_name("Graduation day Rena\xc3\xa9"), std::string("Graduation day Rena"));
  CHECK_EQ(countdown_name("Graduation day Ren\xc3\xa9"), std::string("Graduation day Ren\xc3\xa9"));
  CHECK_EQ(countdown_name("Nineteen characters x"), std::string("Nineteen characters"));
  CHECK_EQ(countdown_name("Caf\xe9 day"), std::string("Caf day"));
  CHECK_EQ(countdown_name("Half \xc3"), std::string("Half"));
  CHECK_EQ(countdown_name("\x80\xbf" "Opening Day"), std::string("Opening Day"));

  // My team: the next game's card and its countdown take 10 s turns, the
  // custom countdown the last turn of each minute, on the wall clock.
  const int64_t min0 = 1797724800;  // a whole minute
  CHECK(countdown_turn(min0, true, false) == CountdownCard::NONE);
  CHECK(countdown_turn(min0 + 9, true, false) == CountdownCard::NONE);
  CHECK(countdown_turn(min0 + 10, true, false) == CountdownCard::NEXT);
  CHECK(countdown_turn(min0 + 19, true, false) == CountdownCard::NEXT);
  CHECK(countdown_turn(min0 + 20, true, false) == CountdownCard::NONE);
  CHECK(countdown_turn(min0 + 55, true, false) == CountdownCard::NEXT);
  CHECK(countdown_turn(min0 + 55, true, true) == CountdownCard::CUSTOM);
  CHECK(countdown_turn(min0 + 50, false, true) == CountdownCard::CUSTOM);
  CHECK(countdown_turn(min0 + 49, false, true) == CountdownCard::NONE);
  CHECK(countdown_turn(min0 + 10, false, false) == CountdownCard::NONE);
  CHECK(custom_turn(min0 + 59) && !custom_turn(min0 + 60));

  // Favorites: a card with a countdown keeps its turn for both halves.
  FavRules r;
  r.dwell_s = kCountdownTurnS;
  r.countdown_s = kCountdownTurnS;
  const int64_t T = 1000000;
  std::vector<FavGame> idle = {fav(GameState::PRE, T + 86400), fav(GameState::PRE, T + 2 * 86400)};
  idle[0].countdown = true;
  CHECK_EQ(pick_favorite(idle, T + 10, r, 0, T, -1).index, 0);
  CHECK_EQ(pick_favorite(idle, T + 19, r, 0, T, -1).index, 0);
  CHECK_EQ(pick_favorite(idle, T + 20, r, 0, T, -1).index, 1);
  // One without keeps the plain dwell.
  CHECK_EQ(pick_favorite(idle, T + 10, r, 1, T, -1).index, 0);
  CHECK(!pick_favorite(idle, T + 20, r, 0, T, -1).again);
  // A playlist of one comes back to its own card when the turn is over, and
  // says so, so the card goes back up before its countdown.
  std::vector<FavGame> one = {idle[0]};
  CHECK(!pick_favorite(one, T + 19, r, 0, T, -1).again);
  FavChoice back = pick_favorite(one, T + 20, r, 0, T, -1);
  CHECK_EQ(back.index, 0);
  CHECK(back.again);
  CHECK(!pick_favorite(one, T + 25, r, 0, T + 20, -1).again);
}

static void test_night() {
  // Times as the page's fields send them
  CHECK_EQ(night_parse_time("23:00"), 23 * 60);
  CHECK_EQ(night_parse_time("7:05"), 7 * 60 + 5);
  CHECK_EQ(night_parse_time("00:00"), 0);
  CHECK_EQ(night_parse_time("23:59"), 23 * 60 + 59);
  CHECK_EQ(night_parse_time("24:00"), -1);
  CHECK_EQ(night_parse_time("12:60"), -1);
  CHECK_EQ(night_parse_time("12:5"), -1);
  CHECK_EQ(night_parse_time("123:00"), -1);
  CHECK_EQ(night_parse_time(":30"), -1);
  CHECK_EQ(night_parse_time("1a:00"), -1);
  CHECK_EQ(night_parse_time("11 PM"), -1);
  CHECK_EQ(night_parse_time(""), -1);
  CHECK_EQ(night_time_text(7 * 60), std::string("07:00"));
  CHECK_EQ(night_time_text(23 * 60 + 30), std::string("23:30"));
  CHECK_EQ(night_clock12(7 * 60), std::string("7:00 AM"));
  CHECK_EQ(night_clock12(0), std::string("12:00 AM"));
  CHECK_EQ(night_clock12(12 * 60 + 5), std::string("12:05 PM"));
  CHECK_EQ(night_clock12(23 * 60 + 30), std::string("11:30 PM"));

  // Across midnight, and inside one day
  CHECK(night_window(23 * 60, 7 * 60, 23 * 60));
  CHECK(night_window(23 * 60, 7 * 60, 2 * 60));
  CHECK(!night_window(23 * 60, 7 * 60, 7 * 60));  // over at the end minute
  CHECK(!night_window(23 * 60, 7 * 60, 12 * 60));
  CHECK(night_window(60, 6 * 60, 3 * 60));
  CHECK(!night_window(60, 6 * 60, 23 * 60));
  CHECK(!night_window(8 * 60, 8 * 60, 8 * 60));  // same start and end: never
  {
    // A change to the same start and end keeps the hours the schedule had.
    NightPrefs now = night_defaults(), next = now;
    next.start = 23 * 60;
    next.end = 23 * 60;
    next.on = 1;
    CHECK(night_keep_hours(next, now));
    CHECK_EQ(next.start, now.start);
    CHECK_EQ(next.end, now.end);
    CHECK_EQ(next.on, 1);  // the rest of the change stands
    next.start = 22 * 60;
    next.end = 6 * 60;
    CHECK(!night_keep_hours(next, now));
    CHECK_EQ(next.start, 22 * 60);
    CHECK_EQ(next.end, 6 * 60);
  }

  NightPrefs p = night_defaults();
  CHECK(!p.on);
  CHECK_EQ((int) p.start, 23 * 60);
  CHECK_EQ((int) p.end, 7 * 60);
  CHECK(!p.screen_off);
  CHECK_EQ((int) p.level, (int) kNightLevelDefault);
  const int late = 2 * 60;
  CHECK(night_state(p, true, late, false) == Night::DAY);  // off out of the box
  p.on = 1;
  CHECK(night_state(p, true, late, false) == Night::DIM);
  CHECK(night_state(p, true, 12 * 60, false) == Night::DAY);
  CHECK(night_state(p, false, late, false) == Night::DAY);  // no clock yet: never dark
  CHECK(night_state(p, true, late, true) == Night::GAME);
  p.screen_off = 1;
  CHECK(night_state(p, true, late, false) == Night::DARK);
  CHECK(night_state(p, true, late, true) == Night::GAME);  // a live game lights it

  p.level = 20;
  CHECK_EQ((int) night_brightness(128, Night::DIM, p), 20);
  CHECK_EQ((int) night_brightness(128, Night::DARK, p), 0);
  CHECK_EQ((int) night_brightness(128, Night::GAME, p), 128);
  CHECK_EQ((int) night_brightness(128, Night::DAY, p), 128);
  // The dim start after a brownout halves the night level too
  CHECK_EQ((int) dipped_brightness(night_brightness(128, Night::DIM, p), true), 10);
  CHECK_EQ(std::string(night_word(Night::DAY)), std::string(""));
  CHECK_EQ(std::string(night_word(Night::DARK)), std::string("off"));

  // A damaged blob goes back to the defaults field by field
  NightPrefs bad{};
  bad.on = 7;
  bad.start = 5000;
  bad.end = 6 * 60;
  bad.screen_off = 9;
  bad.level = 0;
  night_sanitize(bad);
  CHECK_EQ((int) bad.on, 1);
  CHECK_EQ((int) bad.start, (int) kNightStartDefault);
  CHECK_EQ((int) bad.end, 6 * 60);
  CHECK_EQ((int) bad.screen_off, 1);
  CHECK_EQ((int) bad.level, (int) kNightLevelDefault);
  static_assert(sizeof(NightPrefs) == 10, "NightPrefs is saved in flash: its size can't change");
}

static void test_offseason() {
  const int64_t now = 1792080000;  // any fixed instant
  const int64_t day = 24 * 3600;
  const int64_t football = season_horizon(Sport::FOOTBALL);
  const int64_t hockey = season_horizon(Sport::HOCKEY);
  CHECK_EQ(football, (int64_t) 0);
  CHECK_EQ(hockey, kSeasonHorizon);
  CHECK_EQ(season_horizon(Sport::BASEBALL), kSeasonHorizon);
  // The team endpoint named no game: the clock, whatever the sport.
  CHECK(off_season(GameState::NOT_FOUND, 0, now, football));
  CHECK(off_season(GameState::NOT_FOUND, 0, now, hockey));
  // Football has no horizon: a game 20 days out stays on its card, and so
  // does an old final.
  CHECK(!off_season(GameState::PRE, now + 20 * day, now, football));
  CHECK(!off_season(GameState::POST, now - 60 * day, now, football));
  // An NHL team 11 days out reaches the clock; 9 days, or exactly 10, does not.
  CHECK(off_season(GameState::PRE, now + 11 * day, now, hockey));
  CHECK(!off_season(GameState::PRE, now + 9 * day, now, hockey));
  CHECK(!off_season(GameState::PRE, now + 10 * day, now, hockey));
  // A final older than the horizon is the end of the season.
  CHECK(off_season(GameState::POST, now - 11 * day, now, hockey));
  CHECK(!off_season(GameState::POST, now - 2 * day, now, hockey));
  CHECK(!off_season(GameState::IN, now - 11 * day, now, hockey));

  // Favorites, one favorite at a time: the clock needs every one at 1.
  CHECK_EQ(fav_off_season(false, false, false, GameState::NOT_FOUND, 0, now, hockey), -1);  // not read yet
  CHECK_EQ(fav_off_season(true, false, false, GameState::NOT_FOUND, 0, now, hockey), 1);    // no game named
  // Next season's opener named, as ESPN lists it in November for an MLB
  // team: past the horizon, the same as no game.
  CHECK_EQ(fav_off_season(true, true, true, GameState::PRE, now + 120 * day, now, hockey), 1);
  CHECK_EQ(fav_off_season(true, true, false, GameState::NOT_FOUND, now + 120 * day, now, hockey), 1);  // not polled yet
  CHECK_EQ(fav_off_season(true, true, true, GameState::PRE, now + 2 * day, now, hockey), 0);  // a game this week
  CHECK_EQ(fav_off_season(true, true, true, GameState::POST, now - 2 * day, now, hockey), 0);
  CHECK_EQ(fav_off_season(true, true, true, GameState::POST, now - 40 * day, now, hockey), 1);  // the last final
  CHECK_EQ(fav_off_season(true, true, true, GameState::IN, now - 11 * day, now, hockey), 0);
  // Football keeps its card however far the game is.
  CHECK_EQ(fav_off_season(true, true, true, GameState::PRE, now + 200 * day, now, football), 0);

  // The Next line. Wed Sep 9 2026, 10:00 AM.
  struct tm wed = mk(2026, 251, 3, 10, 0, 8, 9);
  CHECK_EQ(next_line("DAL", "PHI", true, mk(2026, 255, 0, 15, 25, 8, 13), wed),
           std::string("Next: DAL vs PHI Sun 3:25 PM"));
  CHECK_EQ(next_line("DAL", "PHI", true, mk(2026, 251, 3, 19, 0, 8, 9), wed),
           std::string("Next: DAL vs PHI Today 7:00 PM"));
  CHECK_EQ(next_line("BOS", "NYR", false, mk(2026, 252, 4, 0, 5, 8, 10), wed),
           std::string("Next: at NYR Tomorrow 12:05 AM"));
  // Months out, the date: Thu Oct 15 2026 to Fri Mar 26 2027.
  struct tm oct15 = mk(2026, 287, 4, 21, 30, 9, 15);
  struct tm mar26 = mk(2027, 84, 5, 19, 5, 2, 26);
  CHECK_EQ(next_line("COL", "PHI", false, mar26, oct15), std::string("Next: at PHI Mar 26 7:05 PM"));
  // The board puts the kickoff on its own line.
  CHECK_EQ(next_line("COL", "PHI", false, mar26, oct15, "\n"), std::string("Next: at PHI\nMar 26 7:05 PM"));
  CHECK_EQ(next_line("COL", "LAD", true, mar26, oct15, "\n"), std::string("Next: COL vs LAD\nMar 26 7:05 PM"));
  // An opponent ESPN has not named yet.
  CHECK_EQ(next_line("COL", "", true, mar26, oct15), std::string("Next game Mar 26 7:05 PM"));
  // A game with no time yet shows TBD, not ESPN's midnight placeholder.
  CHECK_EQ(next_line("COL", "PHI", false, mar26, oct15, " ", true), std::string("Next: at PHI Mar 26 TBD"));
  CHECK_EQ(next_line("COL", "PHI", false, mar26, oct15, "\n", true), std::string("Next: at PHI\nMar 26 TBD"));

  // The clock itself: 12-hour, no seconds, no leading zero.
  CHECK_EQ(clock_time_text(mk(2026, 287, 4, 0, 5, 9, 15)), std::string("12:05 AM"));
  CHECK_EQ(clock_time_text(mk(2026, 287, 4, 9, 7, 9, 15)), std::string("9:07 AM"));
  CHECK_EQ(clock_time_text(mk(2026, 287, 4, 12, 0, 9, 15)), std::string("12:00 PM"));
  CHECK_EQ(clock_time_text(oct15), std::string("9:30 PM"));
  CHECK_EQ(clock_date_text(oct15), std::string("Thu, Oct 15"));
  CHECK_EQ(clock_date_text(mar26), std::string("Fri, Mar 26"));

  // Team colors, lifted when dark; neutral gray with no team.
  CHECK_EQ(clock_color(false, 0xE31837), kClockNeutral);
  CHECK_EQ(clock_color(false, 0xFFFFFF), kClockNeutral);
  CHECK_EQ(clock_color(true, 0xE31837), (uint32_t) 0xE31837);
  CHECK_EQ(clock_color(true, 0x002A5C), (uint32_t) 0x0057C0);
  CHECK_EQ(clock_color(true, 0x000000), (uint32_t) 0xFFFFFF);
  CHECK_EQ(clock_color(true, parse_color("")), (uint32_t) 0xFFFFFF);  // a team with no color from ESPN
}

// Reads text through MsgReader in pieces of `step` bytes, as a socket hands it over.
static MsgReader<std::string> read_list(const std::string &file, uint32_t from, uint32_t max, size_t step) {
  MsgReader<std::string> r(from, max);
  bool more = true;
  for (size_t i = 0; i < file.size() && more; i += step)
    more = r.feed(file.data() + i, std::min(step, file.size() - i));
  if (more)
    r.finish();
  return r;
}

// The FCC screen (regulatory.h): every page fits its board, and the words
// come out whole.
static void test_regulatory() {
  CHECK(reg_wrap("Contains FCC~ID:", 13) == (std::vector<std::string>{"Contains", "FCC ID:"}));
  CHECK(reg_wrap("Contains FCC~ID:", 21) == (std::vector<std::string>{"Contains FCC ID:"}));
  CHECK(reg_wrap("2AC7Z-ESPS3WROOM1", 13) == (std::vector<std::string>{"2AC7Z-", "ESPS3WROOM1"}));
  CHECK(reg_wrap("2AC7Z-ESPS3WROOM1", 21) == (std::vector<std::string>{"2AC7Z-ESPS3WROOM1"}));
  CHECK(reg_wrap("abcdefghij", 4) == (std::vector<std::string>{"abcd", "efgh", "ij"}));
  CHECK(reg_wrap("  a  b ", 4) == (std::vector<std::string>{"a b"}));
  CHECK(reg_wrap("text", 0).empty());
  CHECK(reg_pages(0, 7).empty());
  CHECK_EQ(reg_layout(64).cols, (size_t) 13);
  CHECK_EQ(reg_layout(128).cols, (size_t) 21);
  CHECK(reg_layout(128).big && !reg_layout(64).big);
  for (int width : {64, 128}) {
    const RegLayout lay = reg_layout(width);
    // Room for the page number in the bottom 8 px
    CHECK(lay.rows * (lay.big ? 12 : 8) <= 56);
    CHECK(lay.cols * (lay.big ? 6 : 5) <= (size_t) width + 1);
    const auto pages = reg_pages(lay.cols, lay.rows);
    CHECK_EQ(pages.size(), (size_t) 5);
    std::string statement;
    for (size_t p = 0; p < pages.size(); p++) {
      size_t rows = 0;
      size_t start = 0;
      while (start <= pages[p].size()) {
        size_t nl = pages[p].find('\n', start);
        const std::string line = pages[p].substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        rows++;
        CHECK(!line.empty() && line.size() <= lay.cols);
        // A 13th letter on 64 px loses its fifth column, which only these use
        if (line.size() * 5 > (size_t) width && !lay.big)
          CHECK(std::string("T#&+^~").find(line.back()) == std::string::npos);
        if (p > 0)
          statement += (statement.empty() ? "" : " ") + line;
        if (nl == std::string::npos)
          break;
        start = nl + 1;
      }
      CHECK(rows <= lay.rows);
    }
    // The statement word for word, and the label on the first page
    CHECK_EQ(statement, std::string(kRegPart15));
    CHECK(pages[0].find("Model GDS-1") != std::string::npos);
    CHECK(pages[0].find("Game Day") != std::string::npos);
    CHECK(pages[0].find("FCC ID:") != std::string::npos);
  }
  const auto small = reg_pages(13, 7);
  CHECK_EQ(small[0], std::string("Game Day\nScoreboard\nModel GDS-1\nContains\nFCC ID:\n2AC7Z-\nESPS3WROOM1"));
  const auto wide = reg_pages(21, 4);
  CHECK_EQ(wide[0], std::string("Game Day Scoreboard\nModel GDS-1\nContains FCC ID:\n2AC7Z-ESPS3WROOM1"));
}

static void test_message() {
  // Cleaning: the saved text keeps its lines, a list entry is one line.
  CHECK_EQ(message_clean("  Wings $5 \r\n\n\tPints $3  \n", kMsgTextMax, true), std::string("Wings $5\nPints $3"));
  CHECK_EQ(message_clean("a\nb", kMsgTextMax, false), std::string("a b"));
  CHECK_EQ(message_clean("Happy birthday \xF0\x9F\x8E\x82 Sam", kMsgTextMax, true), std::string("Happy birthday Sam"));
  CHECK_EQ(message_clean("\xE2\x9D\xA4\xEF\xB8\x8F Go team", kMsgTextMax, true), std::string("Go team"));
  CHECK_EQ(message_clean("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x8D\xB3", kMsgTextMax, true), std::string(""));
  CHECK_EQ(message_clean("bell\x07 rings", kMsgTextMax, true), std::string("bell rings"));
  // Latin-1 and the rest stay for panel_text() to map.
  CHECK_EQ(message_clean("Jos\xC3\xA9\xE2\x80\x99s", kMsgTextMax, true), std::string("Jos\xC3\xA9\xE2\x80\x99s"));
  // Cut at the cap, never inside a character, and no trailing space.
  CHECK_EQ(message_clean("abc\xC3\xA9", 4, true), std::string("abc"));
  CHECK_EQ(message_clean("ab cd", 3, true), std::string("ab"));
  CHECK_EQ(message_clean(std::string(300, 'x'), kMsgTextMax, true).size(), kMsgTextMax);
  // A broken byte is kept for panel_text() to turn into '?'.
  CHECK_EQ(message_clean("a\x80" "b", kMsgTextMax, true), std::string("a\x80" "b"));

  CHECK(msg_url_ok(""));
  CHECK(msg_url_ok("http://192.168.1.20/names.txt"));
  CHECK(msg_url_ok("https://gist.githubusercontent.com/x/raw/plates.txt"));
  CHECK(!msg_url_ok("https://"));
  CHECK(!msg_url_ok("ftp://example.com/a.txt"));
  CHECK(!msg_url_ok("example.com/a.txt"));
  CHECK(!msg_url_ok("http://exa mple.com/"));
  CHECK(!msg_url_ok("http://example.com/\xC3\xA9"));
  CHECK(!msg_url_ok("http://" + std::string(kMsgUrlMax, 'a')));

  CHECK_EQ(clamp_say_secs(0), kSayDefaultSecs);
  CHECK_EQ(clamp_say_secs(-4), kSayDefaultSecs);
  CHECK_EQ(clamp_say_secs(30), 30);
  CHECK_EQ(clamp_say_secs(100000), kSaySecsMax);
  CHECK_EQ(clamp_msg_every(-1), 0);
  CHECK_EQ(clamp_msg_every(99), kMsgEveryMax);

  std::string list = "Ann\nBo\nCy\nDee\nEd";
  CHECK_EQ(msg_count(list), 5u);
  CHECK_EQ(msg_count(std::string()), 0u);
  CHECK_EQ(msg_entries(list, 0, 2), std::string("Ann\nBo"));
  CHECK_EQ(msg_entries(list, 3, 4), std::string("Dee\nEd"));
  CHECK_EQ(msg_entries(list, 5, 4), std::string(""));

  // The reader: every piece size gives the same list.
  std::string file = "\xEF\xBB\xBFJane Doe\r\n\r\n  John Smith  \r\n# not a comment\nZo\xC3\xAB\n" + std::string(100, 'L') + "\nlast";
  for (size_t step : {1, 3, 7, 512}) {
    auto r = read_list(file, 0, kMsgChunkLines, step);
    CHECK(r.eof());
    CHECK_EQ(r.kept(), 6u);
    CHECK_EQ(msg_entries(r.list(), 0, 4), std::string("Jane Doe\nJohn Smith\n# not a comment\nZo\xC3\xAB"));
    CHECK_EQ(msg_entries(r.list(), 4, 1), std::string(kMsgLineMax, 'L'));
    CHECK_EQ(msg_entries(r.list(), 5, 1), std::string("last"));
  }
  // A window into a long list: entries 250-349 of 1000, and it stops reading
  // as soon as it has them.
  std::string names;
  for (int i = 0; i < 1000; i++)
    names += "Name " + std::to_string(i) + "\n";
  auto w = read_list(names, 250, kMsgChunkLines, 64);
  CHECK(!w.eof());
  CHECK(w.full());
  CHECK_EQ(w.seen(), 350u);
  CHECK_EQ(msg_entries(w.list(), 0, 1), std::string("Name 250"));
  CHECK_EQ(msg_entries(w.list(), 99, 1), std::string("Name 349"));
  CHECK(w.bytes() < names.size() / 2);
  // The last window runs to the end.
  auto tail = read_list(names, 950, kMsgChunkLines, 64);
  CHECK(tail.eof());
  CHECK_EQ(tail.kept(), 50u);
  // A window past the end keeps nothing.
  auto past = read_list(names, 5000, kMsgChunkLines, 64);
  CHECK(past.eof());
  CHECK_EQ(past.kept(), 0u);
  CHECK_EQ(past.seen(), 1000u);

  // Paging: a short list from the top, wrapping round.
  MsgPage p = msg_next_page(0, 0, 5, true, false, kMsgPageLines);
  CHECK(!p.read);
  CHECK_EQ(p.first, 0u);
  CHECK_EQ(p.count, 4u);
  CHECK_EQ(p.next, 4u);
  p = msg_next_page(4, 0, 5, true, false, kMsgPageLines);
  CHECK_EQ(p.count, 1u);
  CHECK_EQ(p.next, 0u);  // back to the top
  // Stale at the top: read it again.
  p = msg_next_page(0, 0, 5, true, true, kMsgPageLines);
  CHECK(p.read);
  CHECK_EQ(p.next, 0u);
  // Not stale in the middle: keep going.
  p = msg_next_page(4, 0, 5, true, true, kMsgPageLines);
  CHECK(!p.read);
  // A long list: the window ends, read the next one from where it stopped.
  p = msg_next_page(96, 0, 100, false, false, kMsgPageLines);
  CHECK_EQ(p.count, 4u);
  CHECK_EQ(p.next, 100u);
  p = msg_next_page(100, 0, 100, false, false, kMsgPageLines);
  CHECK(p.read);
  CHECK_EQ(p.next, 100u);
  // The last window of a long list ends: back to the top, which needs a read.
  p = msg_next_page(950, 900, 50, true, false, kMsgPageLines);
  CHECK(p.read);
  CHECK_EQ(p.next, 0u);
  // Nothing read yet.
  p = msg_next_page(0, 0, 0, false, false, kMsgPageLines);
  CHECK(p.read);
  // An empty file: nothing to show until it is stale.
  p = msg_next_page(0, 0, 0, true, false, kMsgPageLines);
  CHECK(!p.read);
  CHECK_EQ(p.count, 0u);
  CHECK(msg_next_page(0, 0, 0, true, true, kMsgPageLines).read);

  // Turns: every 2 minutes for a page, or holding the board.
  CHECK(msg_turn(false, true, false, 999999, 2, kMsgPageMs) == MsgTurn::NONE);
  CHECK(msg_turn(false, true, true, 0, 2, kMsgPageMs) == MsgTurn::HIDE);
  CHECK(msg_turn(true, false, true, 0, 2, kMsgPageMs) == MsgTurn::HIDE);  // a game went live
  CHECK(msg_turn(true, true, false, 119999, 2, kMsgPageMs) == MsgTurn::NONE);
  CHECK(msg_turn(true, true, false, 120000, 2, kMsgPageMs) == MsgTurn::SHOW);
  CHECK(msg_turn(true, true, true, kMsgPageMs - 1, 2, kMsgPageMs) == MsgTurn::NONE);
  CHECK(msg_turn(true, true, true, kMsgPageMs, 2, kMsgPageMs) == MsgTurn::HIDE);
  CHECK(msg_turn(true, true, false, 0, 0, kMsgPageMs) == MsgTurn::SHOW);
  CHECK(msg_turn(true, true, true, kMsgPageMs, 0, kMsgPageMs) == MsgTurn::NEXT);
  // The panel's trouble line wins over the card: three missed ESPN reads in a
  // row, a sign-in Wi-Fi, or no internet. Not two misses, which the ticker
  // doesn't call stale either.
  CHECK(!msg_trouble(0, false, false));
  CHECK(!msg_trouble(2, false, false));
  CHECK(msg_trouble(3, false, false));
  CHECK(msg_trouble(255, false, false));
  CHECK(msg_trouble(0, true, false));
  CHECK(msg_trouble(0, false, true));
  // Trouble starts with the card up (every 0, holding the board): it comes
  // down; while trouble lasts it stays down; once it clears it comes straight
  // back on every 0, and after its wait on every N
  CHECK(msg_turn(true, !msg_trouble(3, false, false), true, 0, 0, kMsgPageMs) == MsgTurn::HIDE);
  CHECK(msg_turn(true, !msg_trouble(3, false, false), false, 999999, 0, kMsgPageMs) == MsgTurn::NONE);
  CHECK(msg_turn(true, !msg_trouble(0, true, false), false, 999999, 0, kMsgPageMs) == MsgTurn::NONE);
  CHECK(msg_turn(true, !msg_trouble(0, false, true), false, 999999, 0, kMsgPageMs) == MsgTurn::NONE);
  CHECK(msg_turn(true, !msg_trouble(0, false, false), false, 0, 0, kMsgPageMs) == MsgTurn::SHOW);
  CHECK(msg_turn(true, !msg_trouble(0, false, false), false, 60000, 2, kMsgPageMs) == MsgTurn::NONE);
  CHECK(msg_turn(true, !msg_trouble(0, false, false), false, 120000, 2, kMsgPageMs) == MsgTurn::SHOW);
}

// A settings change writes flash at once, except while a /gameday/set request
// or a settings restore applies its keys: those fold into one write when the
// outermost batch ends.
static void test_prefs_sync() {
  PrefsSyncGate g;
  CHECK(g.request());  // a lone change writes now
  CHECK(!g.batching());
  g.begin();
  CHECK(g.batching());
  CHECK(!g.request());  // inside a request: owed, not written yet
  CHECK(!g.request());
  CHECK(g.end());  // one write for the whole request
  CHECK(!g.batching());
  g.begin();
  CHECK(!g.end());  // a request that changed nothing writes nothing
  CHECK(g.request());  // and the gate is open again
  // A restore runs the set keys inside its own batch: the inner end owes
  // nothing yet, the outer one writes once.
  g.begin();
  g.begin();
  CHECK(!g.request());
  CHECK(!g.end());
  CHECK(g.batching());
  CHECK(!g.request());
  CHECK(g.end());
  CHECK(!g.batching());
  CHECK(!g.end());  // an extra end is harmless
  CHECK(g.request());
}

static void test_panel_text() {
  using esphome::gameday::panel_text;
  // An iPhone hotspot name (backlog #144)
  CHECK_EQ(panel_text("Sam\xE2\x80\x99s iPhone"), std::string("Sam's iPhone"));
  CHECK_EQ(panel_text("\xE2\x80\x98Hi\xE2\x80\x99"), std::string("'Hi'"));
  CHECK_EQ(panel_text("Hawai\xCA\xBBi"), std::string("Hawai'i"));
  CHECK_EQ(panel_text("\xE2\x80\x9CHail Mary\xE2\x80\x9D"), std::string("\"Hail Mary\""));
  CHECK_EQ(panel_text("10\xE2\x80\x93" "7 \xE2\x80\x94 run \xE2\x88\x92" "3"), std::string("10-7 - run -3"));
  CHECK_EQ(panel_text("Wait\xE2\x80\xA6"), std::string("Wait..."));
  CHECK_EQ(panel_text("a\xE2\x80\xA2" "b"), std::string("a\xC2\xB7" "b"));
  // Latin-1 letters are in the fonts and pass through untouched.
  CHECK_EQ(panel_text("S\xC3\xA3o Paulo"), std::string("S\xC3\xA3o Paulo"));
  CHECK_EQ(panel_text("Atl\xC3\xA9tico"), std::string("Atl\xC3\xA9tico"));
  CHECK_EQ(panel_text("San Jos\xC3\xA9 State, M\xC3\xBCnchen, Ni\xC3\xB1o, \xC3\x85" "s"),
           std::string("San Jos\xC3\xA9 State, M\xC3\xBCnchen, Ni\xC3\xB1o, \xC3\x85" "s"));
  // Letters past Latin-1 lose the accent.
  CHECK_EQ(panel_text("Don\xC4\x8D" "i\xC4\x87"), std::string("Doncic"));
  CHECK_EQ(panel_text("Be\xC5\x9Fikta\xC5\x9F"), std::string("Besiktas"));
  CHECK_EQ(panel_text("\xC5\x81\xC3\xB3" "d\xC5\xBA"), std::string("L\xC3\xB3" "dz"));
  CHECK_EQ(panel_text("Kad\xC4\xB1k\xC3\xB6y"), std::string("Kadik\xC3\xB6y"));
  CHECK_EQ(panel_text("\xC5\x92uvre"), std::string("OEuvre"));
  CHECK_EQ(panel_text("Ia\xC8\x99i"), std::string("Iasi"));
  // An accent sent as its own character is dropped, keeping the letter.
  CHECK_EQ(panel_text("Pe\xCC\x81rez"), std::string("Perez"));
  // Spaces and invisible characters
  CHECK_EQ(panel_text("a\xC2\xA0" "b\xE2\x80\x89" "c"), std::string("a b c"));
  CHECK_EQ(panel_text("a\xE2\x80\x8B" "b\xC2\xAD" "c\xEF\xBB\xBF"), std::string("abc"));
  CHECK_EQ(panel_text("line\none\ttwo"), std::string("line one two"));
  // Anything else is a '?', never a box.
  CHECK_EQ(panel_text("Home \xF0\x9F\x8F\x88"), std::string("Home ?"));
  CHECK_EQ(panel_text("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD"), std::string("?"));
  CHECK_EQ(panel_text("\xD0\x9C\xD0\xB8\xD1\x80"), std::string("???"));
  // Broken UTF-8 is one '?' per bad byte and doesn't swallow what follows.
  CHECK_EQ(panel_text("a\x80" "b"), std::string("a?b"));
  CHECK_EQ(panel_text("a\xC3"), std::string("a?"));
  CHECK_EQ(panel_text("a\xE2\x80" "b"), std::string("a??b"));
  CHECK_EQ(panel_text("\xC0\xAF"), std::string("??"));
  CHECK_EQ(panel_text("\xED\xA0\x80"), std::string("???"));
  CHECK_EQ(panel_text(""), std::string(""));
  CHECK_EQ(panel_text("TOUCHDOWN!"), std::string("TOUCHDOWN!"));
  // Every code point up to U+FFFF, and some past it, comes out drawable.
  for (uint32_t cp = 0; cp < 0x20000; cp++) {
    if (cp >= 0xD800 && cp <= 0xDFFF)
      continue;
    std::string s;
    if (cp < 0x80) {
      s += (char) cp;
    } else if (cp < 0x800) {
      s += (char) (0xC0 | (cp >> 6));
      s += (char) (0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      s += (char) (0xE0 | (cp >> 12));
      s += (char) (0x80 | ((cp >> 6) & 0x3F));
      s += (char) (0x80 | (cp & 0x3F));
    } else {
      s += (char) (0xF0 | (cp >> 18));
      s += (char) (0x80 | ((cp >> 12) & 0x3F));
      s += (char) (0x80 | ((cp >> 6) & 0x3F));
      s += (char) (0x80 | (cp & 0x3F));
    }
    const std::string out = panel_text(s);
    if (!panel_drawable(out)) {
      CHECK(panel_drawable(out));
      printf("  U+%04X\n", (unsigned) cp);
      break;
    }
  }
}

// A game with no kickoff time yet (fixtures recorded 2026-10-14): ESPN lists
// it at midnight Eastern with timeValid false. Every parse carries the flag,
// so the panel says "TBD" and never schedules off the placeholder.
static struct tm utc_tm(int64_t epoch) {
  time_t t = (time_t) epoch;
  return *gmtime(&t);
}

static void test_tbd_kickoff() {
  const uint32_t kIowa = 2294, kMichigan = 130;
  const int64_t placeholder = parse_iso8601_z("2026-10-24T04:00Z");  // midnight Eastern, Sat 10/24

  // The team endpoint: Iowa's next game has no time yet.
  Schedule s;
  CHECK(parse_team_str(slurp("fixtures/team_ncaa_iowa_tbd.json"), s));
  CHECK_EQ(s.event_id, std::string("401858497"));
  CHECK_EQ(s.kickoff_epoch, placeholder);
  CHECK(s.time_tbd);
  CHECK_EQ(s.group, (uint32_t) 5);
  Schedule dal;
  CHECK(parse_team_str(slurp("fixtures/team_nfl_dal.json"), dal));
  CHECK(!dal.time_tbd);

  // The game's scoreboard is the Eastern day of the game, not the day before
  // (20261023 has no Big Ten games at all).
  CHECK(scoreboard_url(League::NCAA, s.group, s.kickoff_epoch, s.time_tbd).find("dates=20261024") !=
        std::string::npos);
  CHECK(scoreboard_url(League::NCAA, s.group, s.kickoff_epoch).find("dates=20261023") != std::string::npos);
  // Standard time: the placeholder is 05:00Z, still the same day.
  CHECK(scoreboard_url(League::NCAA, 5, parse_iso8601_z("2026-11-07T05:00Z"), true).find("dates=20261107") !=
        std::string::npos);

  // The event in the day's scoreboard, and on its own.
  std::string sb = slurp("fixtures/scoreboard_ncaa_tbd.json");
  GameSnapshot g;
  CHECK(parse_scoreboard_str(sb, "401858497", kIowa, g));
  CHECK(g.valid);
  CHECK(g.state == GameState::PRE);
  CHECK(g.time_tbd);
  CHECK_EQ(g.kickoff_epoch, placeholder);
  CHECK_EQ(g.short_detail, std::string("TBD"));
  CHECK_EQ(g.opp_abbr, std::string("MINN"));
  GameSnapshot mich;
  CHECK(parse_scoreboard_str(sb, "401858496", kMichigan, mich));
  CHECK(!mich.time_tbd);
  CHECK_EQ(mich.kickoff_epoch, parse_iso8601_z("2026-10-24T16:00Z"));
  GameSnapshot ev;
  CHECK(parse_event_str(slurp("fixtures/event_ncaa_tbd.json"), Sport::FOOTBALL, kIowa, ev));
  CHECK(ev.valid);
  CHECK(ev.time_tbd);
  CHECK_EQ(ev.event_id, std::string("401858497"));

  // The day's scoreboard read for the team's game, and the live scan's wait.
  Schedule day;
  CHECK(parse_team_game_str(sb, kIowa, day));
  CHECK_EQ(day.event_id, std::string("401858497"));
  CHECK(day.time_tbd);
  Schedule mday;
  CHECK(parse_team_game_str(sb, kMichigan, mday));
  CHECK(!mday.time_tbd);
  LiveList live;
  int64_t next = -1;
  CHECK(parse_live_games(sb, live, &next));
  CHECK_EQ(next, parse_iso8601_z("2026-10-24T16:00Z"));

  // The season schedule: five TBD games, then one with a time.
  std::vector<Upcoming> up;
  CHECK(parse_upcoming_str(slurp("fixtures/schedule_ncaa_iowa_tbd.json"), kIowa, 8, up));
  CHECK_EQ(up.size(), (size_t) 6);
  if (up.size() == 6) {
    for (size_t i = 0; i < 5; i++)
      CHECK(up[i].time_tbd);
    CHECK_EQ(up[0].event_id, std::string("401858497"));
    CHECK_EQ(up[0].opp_abbr, std::string("MINN"));
    CHECK(!up[5].time_tbd);
    CHECK_EQ(up[5].kickoff_epoch, parse_iso8601_z("2026-11-27T17:00Z"));
  }

  // The label: the game's day and "TBD". The panel takes the day from noon
  // Eastern; in US Central (UTC-5 in October) the placeholder itself is
  // Friday 11 PM, which is what the ticker used to say.
  const int64_t central = -5 * 3600;
  struct tm wed = utc_tm(parse_iso8601_z("2026-10-14T20:00Z") + central);
  struct tm tue = utc_tm(parse_iso8601_z("2026-10-20T15:00Z") + central);
  struct tm fri = utc_tm(parse_iso8601_z("2026-10-23T15:00Z") + central);
  struct tm kick = utc_tm(tbd_day_epoch(placeholder) + central);
  CHECK_EQ(kickoff_label(utc_tm(placeholder + central), tue), std::string("Fri 11:00 PM"));
  CHECK_EQ(kickoff_label(kick, wed, true), std::string("Oct 24 TBD"));
  CHECK_EQ(kickoff_label(kick, tue, true), std::string("Sat TBD"));
  CHECK_EQ(kickoff_label(kick, fri, true), std::string("Tomorrow TBD"));
  TickerOptions o;
  std::string text = status_text(g, o, kickoff_label(kick, tue, true));
  CHECK(text.find("Sat TBD") != std::string::npos);
  CHECK(text.find(" PM") == std::string::npos);
  CHECK(text.find(" AM") == std::string::npos);
  CHECK_EQ(status_text(g, o, ""), status_text(g, o, "TBD"));  // ESPN's own text when there is no label

  // Favorites: no lock-on 15 minutes before the placeholder, and the today
  // filter goes by the game's day, not Friday night.
  FavRules r;
  r.lockon_s = 15 * 60;
  FavGame f;
  f.valid = true;
  f.state = GameState::PRE;
  f.kickoff_epoch = placeholder;
  CHECK(fav_locked(f, placeholder - 10 * 60, r));
  f.time_tbd = true;
  CHECK(!fav_locked(f, placeholder - 10 * 60, r));
  CHECK(!fav_locked(f, placeholder + 3600, r));
  f.state = GameState::IN;
  CHECK(fav_locked(f, placeholder + 16 * 3600, r));
  f.state = GameState::PRE;
  FavRules friday = r;  // Friday 10/23 in US Central
  friday.day_start = parse_iso8601_z("2026-10-23T05:00Z");
  friday.day_end = friday.day_start + 86400;
  FavRules saturday = r;
  saturday.day_start = friday.day_end;
  saturday.day_end = saturday.day_start + 86400;
  int64_t fri_night = parse_iso8601_z("2026-10-24T02:00Z");
  CHECK(!fav_idle_ok(f, fri_night, friday, true));
  CHECK(fav_idle_ok(f, fri_night, saturday, true));
  f.time_tbd = false;
  CHECK(fav_idle_ok(f, fri_night, friday, true));  // the old reading of the placeholder

  // The boot cache has no room for the flag: a TBD game saves a blank record,
  // so the next boot reads the team endpoint.
  TeamCache c;
  CHECK(team_cache_save(s, League::NCAA, kIowa, placeholder - 5 * 86400, c));
  Schedule back;
  CHECK(!team_cache_load(c, League::NCAA, kIowa, placeholder - 5 * 86400 + 60, back));
}

static void test_wifi_history() {
  CHECK(wifi_cause(200) == WifiCause::SIGNAL);  // beacon timeout
  CHECK(wifi_cause(3) == WifiCause::ROUTER);    // deauth from the router
  CHECK(wifi_cause(206) == WifiCause::ROUTER);  // the router restarted
  CHECK(wifi_cause(15) == WifiCause::PASSWORD);
  CHECK(wifi_cause(207) == WifiCause::ROAM);
  CHECK(wifi_cause(8) == WifiCause::PANEL);
  CHECK(wifi_cause(1) == WifiCause::OTHER);
  CHECK_EQ(std::string(wifi_cause_key(WifiCause::PASSWORD)), std::string("password"));

  static int8_t rssi[kWifiMinutes];
  static WifiDrop drops[kWifiDrops];

  // No buffers (no PSRAM): no history, only the total.
  WifiHistory none;
  none.sample(0, true, -60);
  none.up(0);
  none.down(10, WifiCause::SIGNAL);
  none.up(15);
  CHECK_EQ(none.json(20, true, -60), std::string("{\"rssi\":-60,\"step\":300,\"hist\":[],\"weak\":false,"
                                                 "\"drops\":1,\"day\":0,\"causes\":{}}"));

  // A panel that drops all the time still answers whole JSON.
  {
    static int8_t r2[kWifiMinutes];
    static WifiDrop d2[kWifiDrops];
    WifiHistory flap;
    flap.attach(r2, d2);
    for (uint32_t i = 0; i < 1000000; i++) {
      flap.up(i * 2);
      flap.down(i * 2 + 1, WifiCause::OTHER);
    }
    std::string fj = flap.json(2000000, false, -127);
    CHECK(fj.find("\"weak\":false,\"drops\":1000000,\"day\":64,\"causes\":{\"other\":64},\"last\":\"other\",\"last_ago\":1}") !=
          std::string::npos);
    CHECK(fj.back() == '}');
  }

  // A minute keeps the mean of its readings; a minute off Wi-Fi keeps 0.
  WifiHistory h;
  h.attach(rssi, drops);
  h.up(0);
  h.sample(0, true, -60);
  h.sample(10, true, -62);
  h.sample(20, true, -65);
  CHECK_EQ(h.minutes(), 0u);
  h.sample(60, false, -127);
  CHECK_EQ(h.minutes(), 1u);
  CHECK_EQ((int) rssi[0], -62);
  h.sample(120, true, -70);
  CHECK_EQ((int) rssi[1], 0);
  CHECK_EQ(h.json(130, false, -127), std::string("{\"step\":300,\"hist\":[-62],\"weak\":false,"
                                                  "\"drops\":0,\"day\":0,\"causes\":{}}"));

  // A loop that stalled past several minutes fills them in as off Wi-Fi.
  h.sample(400, true, -70);
  CHECK_EQ(h.minutes(), 6u);
  CHECK_EQ((int) rssi[2], -70);
  CHECK_EQ((int) rssi[3], 0);
  // Points end at the newest minute: [-62] then [off, -70, off, off, off]
  CHECK(h.json(400, true, -70).find("\"hist\":[-62,-70]") != std::string::npos);

  // Drops: the cause, how long it lasted, and the panel leaving on purpose
  // or a roam not counted as trouble.
  h.down(1000, WifiCause::SIGNAL);
  std::string j = h.json(1010, false, -127);
  CHECK(j.find("\"drops\":1,\"day\":1,\"causes\":{\"signal\":1},\"last\":\"signal\",\"last_ago\":10}") !=
        std::string::npos);
  h.up(1042);
  CHECK(h.json(1100, true, -60).find("\"last_ago\":100,\"last_down\":42}") != std::string::npos);
  h.up(1050);  // already up
  CHECK(h.json(1100, true, -60).find("\"last_down\":42}") != std::string::npos);
  h.down(1200, WifiCause::PANEL);
  h.up(1210);
  h.down(1300, WifiCause::ROAM);
  h.up(1301);
  h.down(1400, WifiCause::ROUTER);
  h.down(1401, WifiCause::SIGNAL);  // already down: the first cause stands
  h.up(1460);
  CHECK_EQ(h.total(), 2u);
  j = h.json(1500, true, -60);
  CHECK(j.find("\"drops\":2,\"day\":2,\"causes\":{\"signal\":1,\"router\":1,\"roam\":1},"
               "\"last\":\"router\",\"last_ago\":100,\"last_down\":60}") != std::string::npos);
  // A day later the drops are history: still in the total, out of the day.
  j = h.json(1400 + 24 * 3600, true, -60);
  CHECK(j.find("\"drops\":2,\"day\":0,\"causes\":{},\"last\":\"router\"") != std::string::npos);

  // The weak-signal hint: a median under -75 dBm over the last half hour,
  // from at least ten minutes on Wi-Fi.
  WifiHistory w;
  w.attach(rssi, drops);
  uint32_t t = 0;
  // n minutes of one reading each, then a reading-less sample that closes
  // the last of them
  auto run = [&](int n, bool on, int8_t v) {
    for (int m = 0; m < n; m++, t += 60)
      w.sample(t, on, v);
    w.sample(t, false, -127);
  };
  run(9, true, -80);
  CHECK(!w.weak());  // nine minutes so far
  run(1, true, -80);
  CHECK(w.weak());
  run(9, true, -60);
  CHECK(w.weak());  // 10 weak of 19
  run(2, true, -60);
  CHECK(!w.weak());  // 10 weak of 21
  run(30, true, -60);
  CHECK(!w.weak());
  run(25, false, -127);
  run(5, true, -80);
  CHECK(!w.weak());  // five readings in the last half hour: too few to say
  run(30, false, -127);
  CHECK(!w.weak());  // off Wi-Fi: no readings, no hint
  CHECK(w.json(t, false, -127).find("-80,null,null,null,null,null,null]") != std::string::npos);

  // The ring holds a day: after 25 hours the JSON has 288 points.
  WifiHistory d;
  d.attach(rssi, drops);
  for (uint32_t m = 0; m <= 25 * 60; m++)
    d.sample(m * 60, true, (int8_t) (m < 24 * 60 ? -50 : -77));
  CHECK_EQ(d.minutes(), 25u * 60);
  j = d.json(25 * 3600, true, -77);
  size_t points = 1;
  for (char c : j.substr(j.find("\"hist\":["), j.find(']') - j.find("\"hist\":[")))
    if (c == ',')
      points++;
  CHECK_EQ(points, (size_t) (kWifiMinutes / kWifiStep));
  CHECK(j.find(",-77]") != std::string::npos);
  CHECK(d.weak());
}

// The line score at halftime and the final (line_score.h), from the recorded
// single-event documents.
static void test_line_score() {
  // NHL final, WSH 5-3 over PIT, read as Washington's panel
  GameSnapshot f = nhl_event("fixtures/event_nhl_final.json", kCapitals);
  CHECK_EQ((int) f.team_line_count, 3);
  CHECK_EQ((int) f.team_lines[0], 2);
  CHECK_EQ((int) f.opp_lines[2], 0);
  {
    // A period value past int's range (found by the fuzzer) is capped, not cast
    std::string json = slurp("fixtures/event_nhl_final.json");
    const std::string first = "\"linescores\":[{\"value\":2.0,";
    size_t at = json.find(first);
    CHECK(at != std::string::npos);
    json.replace(at, first.size(), "\"linescores\":[{\"value\":1.40187e+10,");
    GameSnapshot big;
    CHECK(parse_event_str(json, Sport::HOCKEY, kCapitals, big, root_id(json)));
    CHECK(big.team_lines[0] == 255 || big.opp_lines[0] == 255);
  }
  CHECK(line_score_due(f));
  LineScoreText t = line_score_text(f, 128);
  CHECK_EQ((int) t.font, 12);
  CHECK(!t.columns);
  CHECK_EQ((int) t.abbr_cols, 4);
  CHECK_EQ(t.head, std::string(" 1  2  3  T"));
  CHECK_EQ(t.body, std::string(" 2  2  1  5\n 1  2  0  3"));
  CHECK_EQ(t.title, std::string("Final"));
  t = line_score_text(f, 64);
  CHECK_EQ((int) t.font, 8);
  CHECK(t.columns);
  CHECK_EQ(t.body, std::string("1     2    1\n2     2    2\n3     1    0\nT     5    3"));

  // A regular-season shootout: periods 4 and 5 fold into one SO column
  GameSnapshot so = nhl_event("fixtures/event_nhl_final_so.json", kPenguins);
  t = line_score_text(so, 128);
  CHECK_EQ(t.head, std::string(" 1  2  3 SO  T"));
  CHECK_EQ(t.body, std::string(" 1  1  2  0  4\n 1  0  3  1  5"));
  CHECK_EQ(t.title, std::string("Final/SO"));
  t = line_score_text(so, 64);
  CHECK_EQ(t.body, std::string("1     1    1\n2     1    0\n3     2    3\nSO    0    1\nT     4    5"));

  // Live hockey is not halftime: no line score
  GameSnapshot live = nhl_event("fixtures/event_nhl_live.json", kAvalanche);
  CHECK(!line_score_due(live));
  CHECK_EQ((int) line_score_text(live, 128).font, 0);

  // WNBA halftime: the quarters still to play are blank
  GameSnapshot h = hoops_event("fixtures/event_wnba_half.json", kLiberty);
  CHECK(at_halftime(h));
  CHECK(line_score_due(h));
  t = line_score_text(h, 128);
  CHECK_EQ((int) t.font, 12);
  CHECK_EQ(t.head, std::string(" 1  2  3  4   T"));
  CHECK_EQ(t.body, std::string("26 28        54\n26 30        56"));
  CHECK_EQ(t.title, std::string("Halftime"));
  t = line_score_text(h, 64);
  CHECK_EQ(t.body, std::string("1H   54   56\n2H          \nT    54   56"));

  // MLB final, CLE 9-3 at CHW: innings in threes, then R H E
  GameSnapshot m = mlb_event("fixtures/event_mlb_final.json", kWhiteSox);
  t = line_score_text(m, 128);
  CHECK_EQ((int) t.font, 8);
  CHECK_EQ(t.head, std::string("123 456 789  R  H E"));
  CHECK_EQ(t.body.substr(0, t.body.find('\n')), std::string("100 101 000  3  " + std::to_string(m.mlb.team_hits) +
                                                             " " + std::to_string(m.mlb.team_errors)));
  t = line_score_text(m, 64);
  CHECK_EQ(t.body.substr(0, 12), std::string("R     3    9"));

  // The home side ahead after the top of the 9th never bats in it: an x
  GameSnapshot x = m;
  x.team_line_count = 8;
  t = line_score_text(x, 128);
  CHECK_EQ(t.body.substr(0, 12), std::string("100 101 00x "));
  // Extra innings that no longer fit in threes, then a 10-run inning
  GameSnapshot ex = m;
  ex.team_line_count = ex.opp_line_count = 12;
  t = line_score_text(ex, 128);
  CHECK_EQ(t.head.substr(0, 13), std::string("123456789012 "));
  ex.opp_lines[2] = 11;
  t = line_score_text(ex, 128);
  CHECK(t.head.find("R") != std::string::npos);
  CHECK((int) (t.abbr_cols + t.head.size()) * 5 <= 128);

  // Football final with overtime folded into one column, at 128 in the small font
  GameSnapshot sea;
  CHECK(parse_event_str(slurp("fixtures/event_nfl_super_bowl.json"), Sport::FOOTBALL, 26, sea, "401772988"));
  t = line_score_text(sea, 128);
  CHECK_EQ(t.head, std::string(" 1  2  3  4   T"));
  CHECK_EQ(t.body, std::string(" 3  6  3 17  29\n 0  0  0 13  13"));
  GameSnapshot ot = sea;
  ot.team_line_count = ot.opp_line_count = 6;
  ot.team_lines[4] = 0;
  ot.team_lines[5] = 6;
  ot.opp_lines[4] = 3;
  ot.opp_lines[5] = 0;
  t = line_score_text(ot, 128);
  CHECK_EQ(t.head, std::string(" 1  2  3  4 OT   T"));
  CHECK_EQ((int) t.font, 8);  // an OT column takes the small font
  t = line_score_text(ot, 64);
  CHECK_EQ(t.body, std::string("1H    9    0\n2H   20   13\nOT    6    3\nT    29   13"));

  // A soccer final has no line score
  GameSnapshot epl;
  CHECK(parse_event_str(slurp("fixtures/event_epl_final.json"), Sport::SOCCER, 359, epl, ""));
  CHECK(!line_score_due(epl));
  CHECK_EQ(epl.headline, std::string("Guimar\xc3\xa3" "es stunner helps Arsenal keep perfect start"));
  CHECK_EQ(so.headline, std::string("Blake's shootout goal rescues Hurricanes in 5-4 win over Penguins"));
  CHECK_EQ(f.headline, std::string(""));
}

// A live football snapshot for the scoring log, DAL (away, ours) at PHI.
static GameSnapshot log_read(int us, int them, int period, const char *clock, const char *type = "",
                             int value = 0, uint32_t team = 0, const char *who = "", int yards = 0,
                             const char *text = "") {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::IN;
  g.event_id = "401";
  g.team_abbr = "DAL";
  g.opp_abbr = "PHI";
  g.team_id = 6;
  g.opp_id = 21;
  g.team_score = us;
  g.opp_score = them;
  g.period = period;
  g.display_clock = clock;
  g.play_type = type;
  g.play_value = (int8_t) value;
  g.play_team = team;
  g.play_athlete = who;
  g.play_yards = (int16_t) yards;
  g.last_play = text;
  return g;
}

static void test_score_log() {
  CHECK_EQ(espn::detail::last_name("J. Grier"), std::string("Grier"));
  CHECK_EQ(espn::detail::last_name("O. Mabson II"), std::string("Mabson II"));
  CHECK_EQ(espn::detail::last_name("G.Kittle"), std::string("Kittle"));
  CHECK_EQ(espn::detail::pass_target("(Shotgun) B.Purdy pass short left to G.Kittle for 11 yards, TOUCHDOWN."),
           std::string("Kittle"));
  CHECK_EQ(espn::detail::pass_target("(00:38) No Huddle-Shotgun #17 J.Kohl pass complete short right to #4 J.Resar "
                               "caught at FIU31, for 6 yards"),
           std::string("Resar"));
  CHECK_EQ(espn::detail::pass_target("#13 J.Grier rush left for 28 yards gain to the MNE00 TOUCHDOWN"), std::string(""));

  ScoreLog log;
  // The first read of a game logs nothing; repeats add nothing.
  log.update(log_read(0, 0, 1, "15:00"));
  log.update(log_read(0, 0, 1, "15:00"));
  CHECK(log.lines("401").empty());
  // A touchdown pass seen as the last play, then the extra point finishes its line
  log.update(log_read(6, 0, 1, "7:12", "Passing Touchdown", 6, 6, "B. Smith", 12,
                      "(Shotgun) D.Prescott pass short right to C.Lamb for 12 yards, TOUCHDOWN."));
  log.update(log_read(6, 0, 1, "7:12", "Passing Touchdown", 6, 6, "B. Smith", 12));
  CHECK_EQ(log.lines("401").size(), (size_t) 1);
  CHECK_EQ(log.lines("401")[0], std::string("Q1 7:12 Lamb 12 yd pass, DAL 6-0"));
  log.update(log_read(7, 0, 1, "7:12", "Extra Point Good", 1, 6, "B. Aubrey"));
  CHECK_EQ(log.lines("401").size(), (size_t) 1);
  CHECK_EQ(log.lines("401")[0], std::string("Q1 7:12 Lamb 12 yd pass, DAL 7-0"));
  // Their field goal; a touchdown run with the try already in (NFL text)
  log.update(log_read(7, 3, 2, "11:02", "Field Goal Good", 3, 21, "J. Elliott", 48));
  log.update(log_read(7, 10, 2, "3:40", "Rushing Touchdown", 7, 21, "S. Barkley", 28));
  // A read that missed the play: the score alone
  log.update(log_read(14, 10, 3, "9:00", "Kickoff", 0, 21));
  // Both sides between reads: neither is described
  log.update(log_read(17, 12, 4, "5:00", "Safety", 2, 6));
  // A safety right after a touchdown does not finish the touchdown's line
  log.update(log_read(23, 12, 4, "4:00", "Rushing Touchdown", 6, 6, "J. Williams", 1));
  log.update(log_read(23, 14, 4, "3:50", "Safety", 2, 21, "N. Smith"));
  // Overtime, and a final whose score came with it
  GameSnapshot fin = log_read(26, 14, 5, "6:01", "Field Goal Good", 3, 6, "B. Aubrey", 33);
  fin.state = GameState::POST;
  log.update(fin);
  std::vector<std::string> l = log.lines("401");
  CHECK_EQ(l.size(), (size_t) 9);
  if (l.size() == 9) {
    CHECK_EQ(l[1], std::string("Q2 11:02 Elliott 48 yd FG, PHI 3-7"));
    CHECK_EQ(l[2], std::string("Q2 3:40 Barkley 28 yd run, PHI 10-7"));
    CHECK_EQ(l[3], std::string("Q3 9:00 DAL TD, DAL 14-10"));
    CHECK_EQ(l[4], std::string("Q4 5:00 DAL FG, DAL 17-12"));
    CHECK_EQ(l[5], std::string("Q4 5:00 PHI safety, PHI 12-17"));
    CHECK_EQ(l[6], std::string("Q4 4:00 Williams 1 yd run, DAL 23-12"));
    CHECK_EQ(l[7], std::string("Q4 3:50 Safety, PHI 14-23"));
    CHECK_EQ(l[8], std::string("OT 6:01 Aubrey 33 yd FG, DAL 26-14"));
  }

  // A second game, then a third: the first is forgotten, the second kept
  GameSnapshot other = log_read(0, 0, 1, "15:00");
  other.event_id = "402";
  log.update(other);
  other.team_score = 3;
  log.update(other);
  CHECK_EQ(log.lines("402").size(), (size_t) 1);
  CHECK_EQ(log.lines("401").size(), (size_t) 9);
  GameSnapshot third = other;
  third.event_id = "403";
  log.update(third);
  CHECK(log.lines("401").empty());
  CHECK_EQ(log.lines("402").size(), (size_t) 1);
  // A final read for the first time (the finals rotation) takes no slot
  GameSnapshot rot = other;
  rot.event_id = "404";
  rot.state = GameState::POST;
  log.update(rot);
  CHECK_EQ(log.lines("402").size(), (size_t) 1);
  // The same game from the other side (two favorites) keeps its scores straight
  GameSnapshot flip = other;
  std::swap(flip.team_abbr, flip.opp_abbr);
  std::swap(flip.team_score, flip.opp_score);
  std::swap(flip.team_id, flip.opp_id);
  log.update(flip);
  CHECK_EQ(log.lines("402").size(), (size_t) 1);
  // Only the latest twelve lines stay
  for (int i = 1; i <= 14; i++) {
    other.team_score = 3 + 3 * i;
    log.update(other);
  }
  l = log.lines("402");
  CHECK_EQ(l.size(), ScoreLog::kLines);
  CHECK_EQ(l.back(), std::string("Q1 15:00 DAL FG, DAL 45-0"));

  // Hockey: the scorer when the last play is the goal; the shootout
  ScoreLog hl;
  GameSnapshot hk = log_read(0, 0, 1, "20:00");
  hk.sport = Sport::HOCKEY;
  hl.update(hk);
  hk.team_score = 1;
  hk.period = 2;
  hk.display_clock = "12:04";
  hk.play_type = "Goal";
  hk.play_value = 1;
  hk.play_team = 6;
  hk.play_athlete = "S. Crosby";
  hl.update(hk);
  hk.opp_score = 1;
  hk.period = 3;
  hk.display_clock = "1:10";
  hk.play_value = 0;
  hl.update(hk);
  hk.team_score = 2;
  hk.period = 5;
  hk.state = GameState::POST;
  hl.update(hk);
  l = hl.lines("401");
  CHECK_EQ(l.size(), (size_t) 3);
  if (l.size() == 3) {
    CHECK_EQ(l[0], std::string("P2 12:04 Crosby goal, DAL 1-0"));
    CHECK_EQ(l[1], std::string("P3 1:10 PHI goal, PHI 1-1"));
    CHECK_EQ(l[2], std::string("SO DAL wins shootout, DAL 2-1"));
  }

  // Baseball: ESPN's play text, trimmed, with the half inning
  ScoreLog bl;
  GameSnapshot b = log_read(0, 0, 6, "0:00");
  b.sport = Sport::BASEBALL;
  b.short_detail = "Top 6th";
  bl.update(b);
  b.team_score = 2;
  b.play_value = 2;
  b.play_team = 6;
  b.last_play = "Mesa Jr. homered to right center (401 feet), Caminero scored.";
  bl.update(b);
  b.opp_score = 1;
  b.short_detail = "Bot 6th";
  b.play_value = 0;
  bl.update(b);
  l = bl.lines("401");
  CHECK_EQ(l.size(), (size_t) 2);
  if (l.size() == 2) {
    CHECK_EQ(l[0], std::string("T6 Mesa Jr. homered to right center, Caminero scored, DAL 2-0"));
    CHECK_EQ(l[1], std::string("B6 PHI 1 run, PHI 1-2"));
  }

  // Basketball keeps no log
  ScoreLog nl;
  GameSnapshot n = log_read(0, 0, 1, "12:00");
  n.sport = Sport::BASKETBALL;
  nl.update(n);
  n.team_score = 2;
  nl.update(n);
  CHECK(nl.lines("401").empty());

  // The ticker: headline and log after the first part
  CHECK_EQ(with_score_log("Final | DAL 5-2, PHI 4-3", "Cowboys win it", {"Q1 DAL TD, DAL 7-0"}),
           std::string("Final | Cowboys win it | Q1 DAL TD, DAL 7-0 | DAL 5-2, PHI 4-3"));
  CHECK_EQ(with_score_log("Halftime", "", {"Q1 DAL TD, DAL 7-0"}), std::string("Halftime | Q1 DAL TD, DAL 7-0"));
  CHECK_EQ(with_score_log("Final | X", "", {}), std::string("Final | X"));
  CHECK_EQ(with_score_log("Final", "Guimar\xc3\xa3" "es stunner", {}), std::string("Final | Guimaraes stunner"));
}

// Favorites' Jump to the action and the banner when another favorite scores.
static GameSnapshot live_snap(Sport sport, int period, const char *clock, int us, int them) {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::IN;
  g.event_id = "1";
  g.sport = sport;
  g.period = period;
  g.display_clock = clock;
  g.team_abbr = "KC";
  g.opp_abbr = "LV";
  g.team_score = us;
  g.opp_score = them;
  g.short_detail = std::string(clock) + " - " + std::to_string(period) + "th";
  return g;
}

// Favorites take over in Live games (favorites.h).
static LiveGame take_lg(League l, uint32_t away, uint32_t home, const char *id) {
  LiveGame g;
  g.league = (uint8_t) l;
  g.event_id = id;
  g.away_id = away;
  g.home_id = home;
  return g;
}

static void test_take_over() {
  // Cowboys, Mavericks, Stars: ids are per league, so NBA team 6 isn't the Cowboys.
  std::vector<FavRef> favs = {{(uint8_t) League::NFL, 6}, {(uint8_t) League::NBA, 6}, {(uint8_t) League::NHL, 9}};
  LiveList live = {take_lg(League::NFL, 21, 13, "phi-lv"), take_lg(League::NBA, 2, 6, "bos-dal"),
                                take_lg(League::NHL, 9, 4, "dal-chi"), take_lg(League::NFL, 19, 6, "nyg-dal")};
  CHECK_EQ(live_fav_rank(live[0], favs), -1);
  CHECK_EQ(live_fav_rank(live[1], favs), 1);  // the Mavericks at home
  CHECK_EQ(live_fav_rank(live[2], favs), 2);  // the Stars away
  CHECK_EQ(live_fav_rank(live[3], favs), 0);
  CHECK_EQ(live_fav_rank(take_lg(League::NBA, 19, 7, "x"), favs), -1);
  CHECK_EQ(live_fav_rank(take_lg(League::MLB, 6, 1, "x"), favs), -1);  // MLB 6 isn't a favorite

  // Stick: the favorite highest in the list, whatever is up now.
  CHECK_EQ(take_over_pick(live, favs, "", false, false), 3);
  CHECK_EQ(take_over_pick(live, favs, "bos-dal", false, false), 3);
  CHECK_EQ(take_over_pick(live, favs, "nyg-dal", false, false), 3);
  // Its game ended (the scan may still list it): the next favorite's.
  CHECK_EQ(take_over_pick(live, favs, "nyg-dal", true, false), 1);
  // No favorite on: the rotation picks.
  LiveList others = {live[0], take_lg(League::NBA, 19, 7, "x")};
  CHECK_EQ(take_over_pick(others, favs, "", false, false), -1);
  CHECK_EQ(take_over_pick(others, {}, "", false, true), -1);
  CHECK_EQ(take_over_pick({}, favs, "", false, false), -1);

  // Alternate: the next favorite's game in list order, wrapping.
  CHECK_EQ(take_over_pick(live, favs, "", false, true), 3);         // not on one yet: the highest
  CHECK_EQ(take_over_pick(live, favs, "phi-lv", false, true), 3);   // on another game: the highest
  CHECK_EQ(take_over_pick(live, favs, "nyg-dal", false, true), 1);  // Cowboys, then Mavericks
  CHECK_EQ(take_over_pick(live, favs, "bos-dal", false, true), 2);  // then Stars
  CHECK_EQ(take_over_pick(live, favs, "dal-chi", false, true), 3);  // and back to the Cowboys
  // A kickoff scan (not the Switch every look) keeps the favorite that's up.
  CHECK_EQ(take_over_pick(live, favs, "nyg-dal", false, true, false), 3);
  CHECK_EQ(take_over_pick(live, favs, "bos-dal", false, true, false), 1);
  CHECK_EQ(take_over_pick(live, favs, "phi-lv", false, true, false), 3);  // on another game: the highest
  CHECK_EQ(take_over_pick(live, favs, "bos-dal", true, true, false), 3);  // it ended: the highest
  // A lone favorite's game stays up.
  LiveList one = {live[0], live[2]};
  CHECK_EQ(take_over_pick(one, favs, "dal-chi", false, true), 1);
  CHECK_EQ(take_over_pick(one, favs, "dal-chi", true, true), -1);  // and when it ends, nothing
  // Two favorites in one game count once.
  std::vector<FavRef> rivals = {{(uint8_t) League::NFL, 19}, {(uint8_t) League::NFL, 6}};
  LiveList derby = {live[0], live[3]};
  CHECK_EQ(take_over_pick(derby, rivals, "nyg-dal", false, true), 1);
  CHECK_EQ(live_fav_rank(live[3], rivals), 0);

  // The kickoff scan: at the kickoff, then every 2 minutes while ESPN still
  // says "pre", every 10 after 20 minutes late.
  const int64_t k = 1'800'000'000;
  CHECK_EQ(take_over_rescan_at(k - 3600, 0), (int64_t) 0);
  CHECK_EQ(take_over_rescan_at(k - 3600, k), k);
  CHECK_EQ(take_over_rescan_at(k, k), k + 120);
  CHECK_EQ(take_over_rescan_at(k + 19 * 60, k), k + 19 * 60 + 120);
  CHECK_EQ(take_over_rescan_at(k + 20 * 60, k), k + 20 * 60 + 600);
  // A scan with a league's read failed keeps the rescan already set when the
  // new one is none or later, and moves one already due two minutes out.
  const int64_t n = k - 3600;
  CHECK_EQ(take_over_rescan_keep(n, 0, k), k);              // the failed league held the kickoff
  CHECK_EQ(take_over_rescan_keep(n, k + 600, k), k);        // a later favorite doesn't push it back
  CHECK_EQ(take_over_rescan_keep(n, k - 600, k), k - 600);  // an earlier one comes first
  CHECK_EQ(take_over_rescan_keep(n, k, 0), k);              // nothing set before
  CHECK_EQ(take_over_rescan_keep(n, 0, 0), (int64_t) 0);
  CHECK_EQ(take_over_rescan_keep(k, 0, k), k + 120);  // this scan was the due one
  CHECK_EQ(take_over_rescan_keep(k, 0, 1), k + 120);  // a "scan now" after a settings change
  CHECK_EQ(take_over_rescan_keep(k, k + 60, k), k + 60);

  // The scan reports the earliest favorite kickoff it saw, per league's ids.
  std::string nfl = slurp("fixtures/scoreboard_nfl.json");
  LiveList games;
  int64_t next = 0, fav_next = -1;
  std::vector<uint32_t> cowboys = {6};
  CHECK(parse_live_games(nfl, games, &next, nullptr, nullptr, &cowboys, &fav_next));
  CHECK_EQ(fav_next, parse_iso8601_z("2026-09-14T00:20Z"));  // NYG @ DAL, Sunday night
  CHECK_EQ(next, parse_iso8601_z("2026-09-10T00:20Z"));      // the week's first game
  std::vector<uint32_t> two = {6, 25};
  CHECK(parse_live_games(nfl, games, &next, nullptr, nullptr, &two, &fav_next));
  CHECK_EQ(fav_next, parse_iso8601_z("2026-09-11T00:35Z"));  // the 49ers come first
  std::vector<uint32_t> nobody = {9999};
  CHECK(parse_live_games(nfl, games, &next, nullptr, nullptr, &nobody, &fav_next));
  CHECK_EQ(fav_next, (int64_t) 0);
}

static void test_fav_action() {
  // Football: the red zone at any score, the last two minutes of the 4th or
  // overtime only when it is a one-score game.
  GameSnapshot f = live_snap(Sport::FOOTBALL, 2, "8:00", 0, 28);
  CHECK(!in_action(f, League::NFL));
  f.is_red_zone = true;
  CHECK(in_action(f, League::NFL));
  f = live_snap(Sport::FOOTBALL, 4, "1:59", 21, 14);
  CHECK(in_action(f, League::NFL));
  CHECK(in_action(f, League::NCAA));
  f.display_clock = "2:01";
  CHECK(!in_action(f, League::NFL));
  f = live_snap(Sport::FOOTBALL, 2, "1:00", 21, 14);
  CHECK(!in_action(f, League::NFL));  // the end of the first half is not the action
  f = live_snap(Sport::FOOTBALL, 4, "1:00", 30, 14);
  CHECK(!in_action(f, League::NFL));  // two scores up
  f = live_snap(Sport::FOOTBALL, 5, "6:00", 17, 17);
  CHECK(!in_action(f, League::NFL));
  f.display_clock = "0:45";
  CHECK(in_action(f, League::NFL));
  f.short_detail = "End of 4th";
  CHECK(!in_action(f, League::NFL));  // a break in play
  f.short_detail = "0:45 - OT";
  f.state = GameState::POST;
  CHECK(!in_action(f, League::NFL));

  // Baseball: the bases loaded during a half inning, whatever the score.
  GameSnapshot b = live_snap(Sport::BASEBALL, 7, "", 1, 9);
  b.mlb.half = Half::TOP;
  b.mlb.bases = Baseball::FIRST | Baseball::THIRD;
  CHECK(!in_action(b, League::MLB));
  b.mlb.bases |= Baseball::SECOND;
  CHECK(in_action(b, League::MLB));
  b.mlb.half = Half::MID;
  CHECK(!in_action(b, League::MLB));

  // Basketball: the last two minutes of the last period (a college game has
  // two halves) or overtime, within six.
  GameSnapshot h = live_snap(Sport::BASKETBALL, 4, "1:30", 100, 95);
  CHECK(in_action(h, League::NBA));
  h.team_score = 110;
  CHECK(!in_action(h, League::NBA));
  h = live_snap(Sport::BASKETBALL, 2, "0:58", 70, 70);
  h.nba.regulation = 2;
  CHECK(in_action(h, League::MCBB));
  h.nba.regulation = 4;
  CHECK(!in_action(h, League::NBA));  // only the 2nd quarter
  GameSnapshot ot = hoops_event("fixtures/event_wnba_ot.json", kDream);
  CHECK(in_action(ot, League::WNBA));  // 20.8 left in overtime

  // Hockey: the last two minutes of the 3rd or overtime, within a goal.
  GameSnapshot k = live_snap(Sport::HOCKEY, 3, "1:10", 2, 3);
  CHECK(in_action(k, League::NHL));
  k.team_score = 1;
  CHECK(!in_action(k, League::NHL));
  k = live_snap(Sport::HOCKEY, 2, "1:10", 2, 3);
  CHECK(!in_action(k, League::NHL));

  // Soccer: from the 88th minute of the second half on, stoppage and extra
  // time included, within a goal.
  GameSnapshot s = soccer_event("fixtures/event_soccer_stoppage.json", kInter);
  CHECK(in_action(s, League::MLS));  // 2-1 at 90'+2'
  GameSnapshot m = live_snap(Sport::SOCCER, 2, "87'", 1, 1);
  CHECK(!in_action(m, League::EPL));
  m.display_clock = "88'";
  CHECK(in_action(m, League::EPL));
  m.display_clock = "105'";
  m.period = 3;
  CHECK(in_action(m, League::EPL));
  m = live_snap(Sport::SOCCER, 1, "45'+2'", 0, 0);
  CHECK(!in_action(m, League::EPL));

  // Jump to the action: the game at its action goes up in place of the
  // pick, stays up while the action lasts, then the board goes back.
  auto g = [](bool action) {
    FavGame x;
    x.valid = true;
    x.state = GameState::IN;
    x.action = action;
    return x;
  };
  std::vector<FavGame> games = {g(false), g(false), g(false)};
  CHECK_EQ(action_favorite(games, 0, 0), -1);
  games[2].action = true;
  CHECK_EQ(action_favorite(games, 0, 0), 2);
  games[1].action = true;
  CHECK_EQ(action_favorite(games, 0, 2), 2);  // already up for its action: stays
  CHECK_EQ(action_favorite(games, 0, 0), 1);  // else the first slot at its action
  games[0].action = true;
  CHECK_EQ(action_favorite(games, 0, 2), -1);  // the pick is at its action itself
  games = {g(false), g(false), g(false)};
  CHECK_EQ(action_favorite(games, 0, 2), -1);  // action over: back to the pick
  games[1].action = true;
  games[1].state = GameState::POST;
  CHECK_EQ(action_favorite(games, 0, 0), -1);  // a final is never the action
  CHECK_EQ(action_favorite({}, -1, -1), -1);

  // The banner when another favorite scores: the favorite's score first.
  GameSnapshot a = live_snap(Sport::FOOTBALL, 3, "8:00", 14, 17);
  GameSnapshot a2 = a;
  a2.team_score = 21;
  CHECK_EQ(fav_score_banner(a, a2), std::string("KC TD, 21-17"));
  a2.team_score = 20;
  CHECK_EQ(fav_score_banner(a, a2), std::string("KC TD, 20-17"));
  a2.team_score = 17;
  CHECK_EQ(fav_score_banner(a, a2), std::string("KC FG, 17-17"));
  a2.team_score = 15;
  CHECK_EQ(fav_score_banner(a, a2), std::string(""));  // a safety, or the extra point after a TD
  a2.team_score = 14;
  a2.opp_score = 24;
  CHECK_EQ(fav_score_banner(a, a2), std::string(""));  // only the favorite's own scores
  a2 = a;
  a2.team_score = 21;
  a2.state = GameState::POST;
  CHECK_EQ(fav_score_banner(a, a2), std::string(""));
  GameSnapshot pre = a;
  pre.state = GameState::PRE;
  CHECK_EQ(fav_score_banner(pre, a), std::string(""));  // the first poll since kickoff is the baseline
  a2 = a;
  a2.team_score = 21;
  a2.event_id = "2";
  CHECK_EQ(fav_score_banner(a, a2), std::string(""));

  GameSnapshot r = live_snap(Sport::BASEBALL, 5, "", 2, 2), r2 = r;
  r2.team_score = 3;
  CHECK_EQ(fav_score_banner(r, r2), std::string("KC RUN, 3-2"));
  r2.team_score = 5;
  CHECK_EQ(fav_score_banner(r, r2), std::string("KC 3 RUNS, 5-2"));
  r2.mlb.play_type = "Home Run";
  CHECK_EQ(fav_score_banner(r, r2), std::string("KC HR, 5-2"));
  GameSnapshot q = live_snap(Sport::SOCCER, 2, "60'", 0, 1), q2 = q;
  q2.team_score = 1;
  CHECK_EQ(fav_score_banner(q, q2), std::string("KC GOAL, 1-1"));
  q.sport = q2.sport = Sport::HOCKEY;
  CHECK_EQ(fav_score_banner(q, q2), std::string("KC GOAL, 1-1"));
  q.sport = q2.sport = Sport::BASKETBALL;
  CHECK_EQ(fav_score_banner(q, q2), std::string(""));
}

// The boot preview's logo URL (team_logo_url) is the one ESPN's own game
// document names, for both sides, so the logo cache downloads each logo once.
// The single-event documents carry the scoreboard's event object as is.
static void test_logo_url_matches_game() {
  struct Case {
    League league;
    Sport sport;
    const char *file;
    uint32_t team;
  };
  const Case cases[] = {
      {League::MLB, Sport::BASEBALL, "fixtures/event_mlb_final.json", kWhiteSox},
      {League::MLB, Sport::BASEBALL, "fixtures/event_mlb_pre.json", kBrewers},
      {League::NBA, Sport::BASKETBALL, "fixtures/event_nba_final.json", kPacers},
      {League::WNBA, Sport::BASKETBALL, "fixtures/event_wnba_final.json", kDream},
      {League::MCBB, Sport::BASKETBALL, "fixtures/event_mcbb_final.json", kUConn},
      {League::EPL, Sport::SOCCER, "fixtures/event_epl_final.json", kArsenal},
      {League::MLS, Sport::SOCCER, "fixtures/event_mls_final.json", kMiami},
  };
  for (const auto &c : cases) {
    GameSnapshot s;
    std::string json = slurp(c.file);
    CHECK(parse_event_str(json, c.sport, c.team, s, root_id(json)));
    CHECK(!s.team_logo.empty() && !s.opp_logo.empty());
    CHECK_EQ(team_logo_url(c.league, s.team_id, s.team_abbr.c_str()), s.team_logo);
    CHECK_EQ(team_logo_url(c.league, s.opp_id, s.opp_abbr.c_str()), s.opp_logo);
  }
}

int main() {
  test_urls();
  test_iso();
  test_team_parse();
  test_scoreboard_live();
  test_scoreboard_post();
  test_event_live();
  test_event_matches_scoreboard();
  test_scoreboard_pre_nfl();
  test_splash();
  test_status_text();
  test_no_game_text();
  test_captive();
  test_ha_status_text();
  test_short_game_name();
  test_kickoff_label();
  test_live_games();
  test_upcoming();
  test_clock_text();
  test_color();
  test_favorites();
  test_fav_action();
  test_take_over();
  test_followed_favorite();
  test_schedule_league();
  test_schedule_due();
  test_no_live_rescan();
  test_live_scan_clock();
  test_team_cache();
  test_boot_hold();
  test_rotate_minutes();
  test_mlb_urls();
  test_mlb_team();
  test_mlb_live();
  test_mlb_pre_post();
  test_mlb_splash();
  test_mlb_home_run();
  test_favorites_filters();
  test_soccer_urls();
  test_soccer_team();
  test_soccer_pre();
  test_soccer_live();
  test_soccer_finals();
  test_soccer_splash();
  test_soccer_text_helpers();
  test_soccer_cups();
  test_lookahead();
  test_tbd_kickoff();
  test_kickoff_midnight();
  test_nhl_urls();
  test_nhl_team();
  test_nhl_live();
  test_nhl_pre_post();
  test_pregame_extras();
  test_nhl_splash();
  test_nba_urls();
  test_nba_team();
  test_nba_live();
  test_wnba_live();
  test_nba_pre_post();
  test_nba_splash();
  test_mcbb_urls();
  test_mcbb();
  test_logo_url_matches_game();
  checks++;
  failures += run_football_golden();
  test_logo_lru();
  test_logo_backoff();
  test_json_allocator();
  test_timezone_index();
  test_postseason();
  test_badge();
  test_demo();
  test_recap();
  test_recap_walk();
  test_later_today();
  test_backoff();
  test_cup_backoff();
  test_fav_side_polls();
  test_reread_after_poll();
  test_auto_update();
  test_update_mark();
  test_boot_log();
  test_health_json();
  test_heat_guard();
  test_power_log();
  test_power_dip();
  test_guarded_brightness();
  test_night();
  test_offseason();
  test_countdown();
  test_panel_text();
  test_prefs_sync();
  test_message();
  test_regulatory();
  test_wifi_history();
  test_ha_events();
  test_ha_game_events();
  test_ha_pregame();
  test_ha_events_stale();
  test_ha_fields();
  test_line_score();
  test_score_log();
  test_best_game();
  failures += run_set_tests(checks);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
