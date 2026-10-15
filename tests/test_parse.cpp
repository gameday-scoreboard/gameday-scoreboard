// Host tests for the ESPN parser and game logic. Build with `make` in tests/.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "auto_update.h"
#include "demo.h"
#include "espn_parse.h"
#include "favorites.h"
#include "logo_lru.h"
#include "panel_text.h"
#include "recap.h"
#include "poll_timing.h"
#include "startup.h"
#include "timezones.h"

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
  // Long play is truncated
  s.last_play = std::string(200, 'x');
  TickerOptions only_play;
  only_play.clock = only_play.down_distance = false;
  std::string t = status_text(s, only_play, "");
  CHECK_EQ(t.size(), (size_t) 160);
  CHECK(t.substr(157) == "...");
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
  std::vector<LiveGame> games;
  CHECK(parse_live_games(json, games));
  CHECK(games.size() >= 1);
  bool found = false;
  for (const auto &g : games) {
    if (g.event_id == kLiveEvent) {
      found = true;
      CHECK_EQ(g.away_abbr, std::string("BOIS"));
      CHECK_EQ(g.home_abbr, std::string("ORE"));
      CHECK_EQ(g.away_id, kBoise);
      CHECK(g.group != 0);
    }
  }
  CHECK(found);
  std::vector<LiveGame> none;
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
}

// A schedule parsed from the team endpoint does not know its own league, so
// the caller has to stamp it. Without the stamp a college favorite polls the
// NFL scoreboard and never finds its game.
// The favorite Home Assistant follows while the idle playlist rotates.
static void test_followed_favorite() {
  const int64_t T = 1000000;
  CHECK_EQ(followed_favorite({}), -1);
  CHECK_EQ(followed_favorite({FavGame{}, FavGame{}}), -1);
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

// MLB single-event documents recorded 2026-10-07 (see fixtures/README.md).
static const uint32_t kBraves = 15, kDodgers = 19, kWhiteSox = 4, kBrewers = 8;

static GameSnapshot mlb_event(const char *file, uint32_t team) {
  GameSnapshot s;
  CHECK(parse_event_str(slurp(file), Sport::BASEBALL, team, s));
  return s;
}

// A team change's placeholder is dropped on a failed read so the board says
// "Can't reach ESPN"; a game read from ESPN or a final card never is.
static void test_team_placeholder() {
  Team dal{League::NFL, 6, "DAL", "Dallas Cowboys", 0};
  GameSnapshot p = team_placeholder(dal);
  CHECK(p.valid);
  CHECK_EQ(p.team_abbr, std::string("DAL"));
  CHECK_EQ(p.team_logo, team_logo_url(League::NFL, 6, "DAL"));
  CHECK(is_team_placeholder(p));
  CHECK(!is_team_placeholder(GameSnapshot{}));
  GameSnapshot pre = mlb_event("fixtures/event_mlb_top.json", kBraves);
  CHECK(!pre.event_id.empty());
  CHECK(!is_team_placeholder(pre));
  Final f;
  f.away_abbr = "DAL";
  f.home_abbr = "PHI";
  CHECK(!is_team_placeholder(final_snapshot(f)));
}

static void test_mlb_urls() {
  CHECK_EQ(team_url(League::MLB, 15), std::string("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/teams/15"));
  CHECK_EQ(event_url(League::MLB, "401908016"),
           std::string("https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/scoreboard/401908016"));
  CHECK_EQ(team_logo_url(League::MLB, 15, "ATL"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/mlb/500-dark/scoreboard/atl.png&w=64&h=64"));
  CHECK_EQ(std::string(league_key(League::MLB)), std::string("mlb"));
  CHECK(league_by_key("mlb") != nullptr && league_by_key("mlb")->per_event);
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
  quiet.down_distance = quiet.last_play = false;
  CHECK_EQ(status_text(t, quiet, ""), std::string("Top 7th"));

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
  CHECK(pre.rfind("Today 10:00 PM | MIL leads series 2-1 | MIL R. Gasser vs SD W. Buehler | SD -112 | ", 0) == 0);
  CHECK(pre.find(" | FS1 | Petco Park") != std::string::npos);

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
  CHECK(parse_event_str(slurp(file), Sport::SOCCER, team, s));
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
  CHECK(league_by_key("epl") != nullptr && league_by_key("epl")->per_event && league_by_key("epl")->season_list);
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
  with_clock.down_distance = with_clock.last_play = false;
  CHECK_EQ(status_text(fh, with_clock, ""), std::string("25'"));

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
  quiet.down_distance = quiet.last_play = false;
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
  CHECK(parse_event_str(slurp(file), Sport::BASKETBALL, team, s));
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
    CHECK(l != nullptr && l->per_event && !l->season_list && l->linger_min == 30);
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
  CHECK_EQ(status_text(p, o, "Today 10:00 PM"), std::string("Today 10:00 PM | POR -14.5 | O/U 216.5 | NBA TV | Moda Center"));
  TickerOptions no_odds;
  no_odds.odds = false;
  CHECK_EQ(status_text(p, no_odds, ""), std::string("10/7 - 10:00 PM EDT | Moda Center"));
  CHECK_EQ(basketball_series(p), std::string(""));

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
  CHECK(l != nullptr && l->per_event && !l->season_list && l->logo_by_id && l->sport == Sport::BASKETBALL);
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
  CHECK(parse_event_str(slurp(file), Sport::HOCKEY, team, s));
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
  CHECK(nhl != nullptr && nhl->per_event && !nhl->season_list && nhl->linger_min == 30);
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

static void test_nhl_pre_post() {
  GameSnapshot p = nhl_event("fixtures/event_nhl_pre.json", kOilers);
  CHECK(p.state == GameState::PRE);
  CHECK(!p.team_home);
  CHECK_EQ(p.odds, std::string("EDM -135"));
  CHECK_EQ(p.over_under, std::string("6.5"));
  CHECK_EQ(p.tv, std::string("ESPN+"));
  TickerOptions o;
  CHECK_EQ(status_text(p, o, "Today 10:00 PM"), std::string("Today 10:00 PM | EDM -135 | O/U 6.5 | ESPN+ | Honda Center"));
  TickerOptions no_odds;
  no_odds.odds = false;
  CHECK_EQ(status_text(p, no_odds, ""), std::string("10/7 - 10:00 PM EDT | Honda Center"));

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
  CHECK_EQ(status_text(pre, o, "Today 7:00 PM"), std::string("Today 7:00 PM | CAR leads series 2-0 | Lenovo Center"));

  GameSnapshot x;
  CHECK(!parse_event_str(slurp("fixtures/event_nhl_live.json"), Sport::HOCKEY, kPenguins, x));  // not in it
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
  // Retry after 15 s, including across the millis() wrap.
  CHECK(!espn::logo_retry_due(1000, 1000 + espn::LOGO_RETRY_MS - 1));
  CHECK(espn::logo_retry_due(1000, 1000 + espn::LOGO_RETRY_MS));
  CHECK(espn::logo_retry_due(0xFFFFF000u, 0xFFFFF000u + espn::LOGO_RETRY_MS));
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

// The demo build's matchups, made-up teams and script (demo.h).
static void test_demo() {
  auto abbrs = [](DemoMatchup m) { return std::string(m.home->abbr) + "-" + m.away->abbr; };
  // No picks: the panel's own team at home when it is in the league, else the stock game.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 0, League::NFL, 12)) == "KC-PHI");
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 0, League::NCAA, 333)) == "DAL-PHI");
  CHECK(abbrs(demo_matchup(DemoKind::COLLEGE, 0, 0, League::NCAA, 333)) == "ALA-MICH");
  CHECK(abbrs(demo_matchup(DemoKind::COLLEGE, 0, 0, League::NFL, 6)) == "OSU-MICH");
  // A panel that follows the stock opponent plays the stock home team instead.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 0, League::NFL, 21)) == "PHI-DAL");
  CHECK(abbrs(demo_matchup(DemoKind::COLLEGE, 0, 0, League::NCAA, 130)) == "MICH-OSU");
  // Picks win, and a pick from the other league is no pick.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 26, 25, League::NFL, 6)) == "SEA-SF");
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 333, 0, League::NFL, 6)) == "DAL-PHI");
  // The same team on both sides never happens.
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 26, 26, League::NFL, 6)) == "SEA-PHI");
  CHECK(abbrs(demo_matchup(DemoKind::PRO, 0, 6, League::NFL, 6)) == "DAL-PHI");

  // Made-up teams: unique, and never a real team's abbreviation.
  for (size_t i = 0; i < kMadeUpTeamCount; i++) {
    for (size_t j = i + 1; j < kMadeUpTeamCount; j++)
      CHECK(std::string(kMadeUpTeams[i].abbr) != kMadeUpTeams[j].abbr);
    for (size_t t = 0; t < kTeamCount; t++)
      CHECK(std::string(kMadeUpTeams[i].abbr) != kTeams[t].abbr);
  }
  CHECK(std::string(demo_made_up(DemoKind::PRO, true).abbr) != demo_made_up(DemoKind::COLLEGE, true).abbr);

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

static void test_recap() {
  const uint8_t NFL = (uint8_t) League::NFL, NCAA = (uint8_t) League::NCAA;
  // Finals ride along with the scan only when asked for
  std::string titles = slurp("fixtures/scoreboard_ncaa_conf_titles.json");
  std::vector<LiveGame> live;
  std::vector<Final> finals;
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
  int32_t sat = espn_day(parse_iso8601_z("2025-12-06T21:00Z"));
  CHECK_EQ(espn_day(parse_iso8601_z("2025-12-07T01:00Z")), sat);  // 8 PM Eastern Saturday
  CHECK_EQ(espn_day(parse_iso8601_z("2025-12-07T06:00Z")), sat + 1);
  CHECK(scan_url(League::NCAA, espn_day_epoch(sat)).find("dates=20251206") != std::string::npos);
  CHECK_EQ(recap_day_word(sat, sat), std::string("Today's"));
  CHECK_EQ(recap_day_word(sat, sat + 1), std::string("Yesterday's"));
  CHECK_EQ(recap_day_word(sat, sat + 2), std::string("Saturday's"));
  CHECK_EQ(recap_day_word(sat - 1, sat + 2), std::string("Friday's"));
  RecapDay hd;
  hd.league = NCAA;
  hd.day = sat;
  CHECK_EQ(recap_header(hd, sat + 2), std::string("Saturday's college scores"));
  hd.league = NFL;
  CHECK_EQ(recap_header(hd, sat), std::string("Today's NFL scores"));

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
  recap_trim(days, sat + 3);
  CHECK_EQ(days.size(), (size_t) 3);
  RecapDay thu;
  thu.league = NFL;
  thu.day = sat + 5;
  for (int i = 0; i < 4; i++)
    thu.finals.push_back(mkfinal(NFL, 99, 600 + i, "THU"));
  recap_merge(days, thu);
  recap_trim(days, sat + 5);
  CHECK_EQ(recap_count(days, NFL), (size_t) 4);
  CHECK_EQ(recap_count(days, NCAA), (size_t) 4);
  recap_trim(days, sat + 7);
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

  // Panels from before "no team" saved Dallas on first boot
  CHECK(legacy_team_unpicked(NFL, 6, false, false));
  CHECK(legacy_team_unpicked(NFL, 6, true, true));
  CHECK(!legacy_team_unpicked(NFL, 6, true, false));
  CHECK(!legacy_team_unpicked(NFL, 17, false, true));
  CHECK(!legacy_team_unpicked(NCAA, 6, false, false));
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
  std::vector<LiveGame> live;
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

int main() {
  test_urls();
  test_iso();
  test_team_parse();
  test_scoreboard_live();
  test_scoreboard_post();
  test_scoreboard_pre_nfl();
  test_splash();
  test_status_text();
  test_no_game_text();
  test_ha_status_text();
  test_short_game_name();
  test_kickoff_label();
  test_live_games();
  test_upcoming();
  test_clock_text();
  test_color();
  test_favorites();
  test_followed_favorite();
  test_schedule_league();
  test_schedule_due();
  test_no_live_rescan();
  test_team_cache();
  test_boot_hold();
  test_rotate_minutes();
  test_mlb_urls();
  test_team_placeholder();
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
  test_nhl_urls();
  test_nhl_team();
  test_nhl_live();
  test_nhl_pre_post();
  test_nhl_splash();
  test_nba_urls();
  test_nba_team();
  test_nba_live();
  test_wnba_live();
  test_nba_pre_post();
  test_nba_splash();
  test_mcbb_urls();
  test_mcbb();
  checks++;
  failures += run_football_golden();
  test_logo_lru();
  test_json_allocator();
  test_timezone_index();
  test_postseason();
  test_demo();
  test_recap();
  test_backoff();
  test_cup_backoff();
  test_auto_update();
  test_panel_text();
  failures += run_set_tests(checks);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
