// Host tests for women's college volleyball (sport_volleyball.cpp and the
// rules other files keep per sport), on recorded ESPN documents. ESPN's score
// is sets won and its line score the points in each set. Build with `make`
// in tests/.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ArduinoJson.h"
#include "best_game.h"
#include "celebrate.h"
#include "favorites.h"
#include "game_leaders.h"
#include "ha_events.h"
#include "line_score.h"
#include "recap.h"
#include "row_text.h"
#include "score_log.h"
#include "season.h"
#include "set_request.h"

using namespace espn;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                          \
  do {                                                       \
    checks++;                                                \
    if (!(cond)) {                                           \
      failures++;                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                        \
  } while (0)
#define CHECK_EQ(a, b)                                                             \
  do {                                                                             \
    checks++;                                                                      \
    auto av = (a);                                                                 \
    decltype(av) bv = (b);                                                         \
    if (!(av == bv)) {                                                             \
      failures++;                                                                  \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #a);                          \
      show(av);                                                                    \
      show(bv);                                                                    \
    }                                                                              \
  } while (0)
static void show(const std::string &s) { printf("  [%s]\n", s.c_str()); }
static void show(int64_t v) { printf("  [%lld]\n", (long long) v); }

static std::string slurp(const std::string &path) {
  std::ifstream f("fixtures/" + path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

constexpr uint32_t kNebraska = 158, kWisconsin = 275, kGeorgia = 61, kMissSt = 344, kNotreDame = 87, kCincinnati = 2132,
                   kTexasTech = 2641;

static GameSnapshot event(const char *file, uint32_t team) {
  GameSnapshot s;
  CHECK(parse_event_str(slurp(file), Sport::VOLLEYBALL, team, s));
  return s;
}

// A match under way, by hand: our sets and theirs, and the points by set.
static GameSnapshot live(int sets, int opp_sets, int set, std::vector<int> us, std::vector<int> them) {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::IN;
  g.event_id = "1";
  g.sport = Sport::VOLLEYBALL;
  g.team_abbr = "NEB";
  g.opp_abbr = "WIS";
  g.team_record = "30-0";
  g.opp_record = "25-4";
  g.team_color = "fe0d04";
  g.opp_color = "c4012f";
  g.period = set;
  g.team_score = sets;
  g.opp_score = opp_sets;
  for (int p : us)
    g.team_lines[g.team_line_count++] = (uint8_t) p;
  for (int p : them)
    g.opp_lines[g.opp_line_count++] = (uint8_t) p;
  return g;
}

static void test_league() {
  const LeagueInfo *l = league_by_key("wcvb");
  CHECK(l != nullptr && l->league == League::WCVB && l->sport == Sport::VOLLEYBALL && l->logo_by_id && l->season_list);
  CHECK_EQ((int) League::WCVB, 13);  // saved on panels: never renumbered
  CHECK_EQ((int) Sport::VOLLEYBALL, 5);
  CHECK_EQ(std::string(league_path(League::WCVB)), std::string("womens-college-volleyball"));
  CHECK_EQ(league_live_name(League::WCVB), std::string("women's college volleyball"));
  const std::string base = "https://site.api.espn.com/apis/site/v2/sports/volleyball/womens-college-volleyball";
  CHECK_EQ(team_url(League::WCVB, kNebraska), base + "/teams/158");
  CHECK_EQ(event_url(League::WCVB, "401931745"), base + "/scoreboard/401931745");
  // The day's scoreboard lists all of Division I with no group (96 matches
  // on 2026-11-14, the same with group 90); a team's day reads its conference.
  CHECK_EQ(scan_url(League::WCVB, parse_iso8601_z("2026-11-26T02:00Z")), base + "/scoreboard?dates=20261125");
  CHECK_EQ(team_day_url(League::WCVB, 54, parse_iso8601_z("2026-11-26T01:00Z")),
           base + "/scoreboard?groups=54&dates=20261125");
  CHECK_EQ(team_logo_url(League::WCVB, kNebraska, "NEB"),
           std::string("https://a.espncdn.com/combiner/i?img=/i/teamlogos/ncaa/500-dark/158.png&w=64&h=64"));
  int n = 0;
  bool neb = false;
  for (size_t i = 0; i < kTeamCount; i++) {
    const Team &t = kTeams[i];
    if (t.league != League::WCVB)
      continue;
    n++;
    CHECK(t.abbr[0] != '\0' && t.name[0] != '\0' && t.espn_id < 1000000);  // none of ESPN's placeholder rows
    if (t.espn_id == kNebraska) {
      neb = true;
      CHECK_EQ(team_option(t), std::string("NCAAVB: Nebraska Cornhuskers"));
    }
  }
  CHECK(n >= 340 && neb);

  // The values 10 to 12 have no row: unknown leagues, and no place in the
  // arrays kept per league.
  CHECK_EQ((int) kLeagueCount, 14);
  for (uint8_t v = 10; v <= 12; v++) {
    CHECK(league_info((League) v) == nullptr && league_key((League) v)[0] == '\0');
    CHECK_EQ((int) game_league_slot(v), (int) kGameLeagueCount);
  }
  CHECK_EQ((int) game_league_slot((uint8_t) League::NFL), 0);
  CHECK_EQ((int) game_league_slot((uint8_t) League::WCBB), 9);
  CHECK_EQ((int) game_league_slot((uint8_t) League::WCVB), 10);
  CHECK_EQ((int) game_league_slot(200), (int) kGameLeagueCount);
  std::vector<bool> used(kGameLeagueCount, false);
  for (const auto &row : kLeagues) {
    const size_t slot = game_league_slot((uint8_t) row.league);
    CHECK(slot < kGameLeagueCount && !used[slot]);
    if (slot < kGameLeagueCount)
      used[slot] = true;
  }

  // A favorite and the league words, as /gameday/set reads them.
  SetOp op;
  CHECK(parse_set("favs", "wcvb:158,nfl:6", op, 16));
  CHECK_EQ((int) op.teams.size(), 2);
  CHECK(parse_set("live", "wcvb,nhl", op, 16));
  CHECK_EQ((int) op.mask, (1 << 13) | (1 << 3));
  CHECK_EQ(std::string(remote_sport_key((uint8_t) Sport::VOLLEYBALL)), std::string("volleyball"));
  CHECK_EQ(std::string(remote_sport_key(6)), std::string(""));
}

static void test_team() {
  Schedule s;
  CHECK(parse_team_str(slurp("team_wcvb_nd.json"), s));
  CHECK_EQ(s.event_id, std::string("401884518"));
  CHECK_EQ(s.kickoff_epoch, parse_iso8601_z("2026-11-25T21:00Z"));
  CHECK_EQ((int) s.group, 49);
  CHECK_EQ(s.opp_abbr, std::string("CAL"));
  // ESPN names no next match for a team in a conference final the same day
  // (Nebraska on 11/25): the conference's day finds it.
  CHECK(parse_team_str(slurp("team_wcvb_neb.json"), s));
  CHECK_EQ(s.event_id, std::string(""));
  CHECK_EQ((int) s.group, 54);

  SeasonRead r;
  CHECK(parse_season_str(slurp("schedule_wcvb_nd.json"), kNotreDame, r));
  CHECK_EQ((int) r.wins, 1);
  CHECK_EQ((int) r.losses, 2);
  CHECK_EQ((int) r.next.size(), 2);
  CHECK_EQ(r.next[0].opp, std::string("CAL"));
  CHECK_EQ(season_record(r, Sport::VOLLEYBALL, ""), std::string("1-2"));
  std::vector<Upcoming> up;
  CHECK(parse_upcoming_str(slurp("schedule_wcvb_nd.json"), kNotreDame, 4, up));
  CHECK(up.size() == 2 && up[0].opp_abbr == "CAL" && !up[0].home && up[1].home);
}

static void test_pre_post() {
  GameSnapshot p = event("event_wcvb_pre.json", kNebraska);
  CHECK(p.state == GameState::PRE);
  CHECK_EQ((int) p.team_rank, 1);
  CHECK_EQ((int) p.opp_rank, 8);
  CHECK_EQ(p.team_record, std::string("30-0"));
  TickerOptions o;
  CHECK_EQ(status_text(p, o, "Today 7:00 PM"),
           std::string("Big Ten Women's Volleyball Tournament - Championship | Today 7:00 PM | Fishers Event Center"));
  o.records = true;
  CHECK_EQ(board_team_score(p), 0);
  CHECK_EQ(volleyball_sets(p), std::string(""));

  // UGA 3, MSST 2 on 11/21: 23-25, 25-22, 19-25, 25-19, 15-9.
  GameSnapshot f = event("event_wcvb_final5.json", kGeorgia);
  CHECK(f.state == GameState::POST && f.completed && f.team_winner);
  CHECK_EQ(f.team_score, 3);
  CHECK_EQ(f.opp_score, 2);
  CHECK_EQ(board_team_score(f), 3);  // a final shows the sets
  CHECK_EQ(board_opp_score(f), 2);
  CHECK_EQ(volleyball_sets(f), std::string("23-25 25-22 19-25 25-19 15-9"));
  CHECK_EQ(status_text(f, TickerOptions(), ""), std::string("Final | UGA 17-10 | MSST 17-10"));
  CHECK_EQ(ha_status_text(f, ""), std::string("Final | UGA 3, MSST 2"));

  LineScoreText t = line_score_text(f, 128);
  CHECK_EQ(t.title, std::string("Final"));
  CHECK_EQ((int) t.font, 8);  // five sets don't fit in the large font
  CHECK_EQ(t.head, std::string(" 1  2  3  4  5  S"));
  CHECK_EQ(t.body, std::string("23 25 19 25 15  3\n25 22 25 19  9  2"));
  t = line_score_text(f, 64);
  CHECK(t.columns);
  CHECK_EQ(t.body, std::string("1    23   25\n2    25   22\n3    19   25\n4    25   19\n5    15    9"));
  // Four sets: the large font on a wide board, and the sets row on a single
  // panel. Three: no empty columns for sets nobody played.
  GameSnapshot four = live(3, 1, 4, {21, 25, 25, 25}, {25, 17, 18, 20});
  four.state = GameState::POST;
  four.completed = true;
  four.short_detail = "Final";
  t = line_score_text(four, 128);
  CHECK_EQ((int) t.font, 12);
  CHECK_EQ(t.head, std::string(" 1  2  3  4  S"));
  t = line_score_text(four, 64);
  CHECK_EQ(t.body, std::string("1    21   25\n2    25   17\n3    25   18\n4    25   20\nS     3    1"));
  GameSnapshot three = live(3, 0, 3, {25, 25, 29}, {19, 20, 27});
  three.state = GameState::POST;
  three.completed = true;
  CHECK_EQ(line_score_text(three, 128).head, std::string(" 1  2  3  S"));
  CHECK_EQ(line_score_text(three, 64).body, std::string("1    25   19\n2    25   20\n3    29   27\nS     3    0"));

  // Canceled: ESPN's word, no win.
  GameSnapshot c = f;
  c.completed = false;
  c.short_detail = "Canceled";
  CHECK_EQ(status_text(c, TickerOptions(), ""), std::string("Canceled"));
}

static void test_live_numbers() {
  GameSnapshot g = live(1, 1, 3, {25, 17, 12}, {19, 25, 9});
  CHECK_EQ(board_team_score(g), 12);
  CHECK_EQ(board_opp_score(g), 9);
  CHECK_EQ(clock_text(g), std::string("Set 3  1-1"));
  CHECK_EQ(volleyball_sets(g), std::string("25-19 17-25"));
  CHECK_EQ(volleyball_sets(g, ", "), std::string("25-19, 17-25"));
  CHECK(!volleyball_set_over(g) && !at_break(g) && !ha_at_break(g));

  // A set just named with no points sent for it yet.
  GameSnapshot fresh = live(1, 1, 3, {25, 17}, {19, 25});
  CHECK_EQ(board_team_score(fresh), 0);
  CHECK_EQ(board_opp_score(fresh), 0);
  CHECK_EQ(volleyball_sets(fresh), std::string("25-19 17-25"));
  // ESPN sends one side's points before the other's.
  GameSnapshot half = live(0, 0, 1, {1}, {});
  CHECK_EQ(board_team_score(half), 1);
  CHECK_EQ(board_opp_score(half), 0);
  CHECK_EQ(volleyball_sets(half), std::string(""));
  CHECK_EQ(clock_text(half), std::string("Set 1  0-0"));

  // Between sets the last one's points stay.
  GameSnapshot over = live(1, 1, 2, {25, 17}, {19, 25});
  CHECK(volleyball_set_over(over) && at_break(over) && pick_break(over) && ha_at_break(over));
  CHECK_EQ(board_team_score(over), 17);
  CHECK_EQ(board_opp_score(over), 25);
  CHECK_EQ(clock_text(over), std::string("End 2  1-1"));
  CHECK_EQ(volleyball_sets(over), std::string("25-19 17-25"));

  // Everything fits its row: 10 characters for the clock on a wide board
  // (64 px of a 6 px font), 25 for the sets (128 px of a 5 px one).
  GameSnapshot longest = live(2, 2, 5, {25, 25, 30, 28, 14}, {27, 23, 32, 26, 13});
  CHECK(clock_text(longest).size() <= 10);
  CHECK_EQ(volleyball_sets(longest), std::string("25-27 25-23 30-32 28-26"));
  CHECK(volleyball_sets(longest).size() <= 25);

  // No set named, or a period past the fifth, reads nothing it shouldn't.
  GameSnapshot none = live(0, 0, 0, {}, {});
  none.short_detail = "In Progress";
  CHECK_EQ(clock_text(none), std::string("In Progress"));
  CHECK(!volleyball_set_over(none));
  GameSnapshot odd = live(0, 0, 40, {1}, {2});
  CHECK_EQ(board_team_score(odd), 0);
  CHECK_EQ(volleyball_sets(odd), std::string("1-2"));
  odd.period = -3;
  CHECK_EQ(board_team_score(odd), 0);

  // Every other sport's board is its score.
  GameSnapshot nba = g;
  nba.sport = Sport::BASKETBALL;
  nba.team_score = 70;
  CHECK_EQ(board_team_score(nba), 70);
  CHECK_EQ(volleyball_sets(nba), std::string(""));
  CHECK(!volleyball_set_over(nba) && !volleyball_match_point(nba));
}

// Read live from ESPN on 11/25: TTU @ CIN and WIS @ NEB.
static void test_recorded_live() {
  // CIN up 2-0, 24-19 in the third: a match point.
  GameSnapshot g = event("event_wcvb_live.json", kCincinnati);
  CHECK(g.state == GameState::IN);
  CHECK_EQ(board_team_score(g), 24);
  CHECK_EQ(board_opp_score(g), 19);
  CHECK_EQ(clock_text(g), std::string("Set 3  2-0"));
  CHECK_EQ(volleyball_sets(g), std::string("25-17 25-11"));
  CHECK(volleyball_match_point(g) && !volleyball_set_over(g) && !at_break(g));
  GameSnapshot t = event("event_wcvb_live.json", kTexasTech);
  CHECK_EQ(board_team_score(t), 19);
  CHECK_EQ(clock_text(t), std::string("Set 3  0-2"));
  CHECK_EQ(volleyball_sets(t), std::string("17-25 11-25"));

  // "End of 2nd": the set is in the sets won and its points stay up.
  GameSnapshot e = event("event_wcvb_setend.json", kCincinnati);
  CHECK(e.state == GameState::IN);
  CHECK(volleyball_set_over(e) && at_break(e) && ha_at_break(e));
  CHECK_EQ(board_team_score(e), 25);
  CHECK_EQ(board_opp_score(e), 11);
  CHECK_EQ(clock_text(e), std::string("End 2  2-0"));
  CHECK_EQ(volleyball_sets(e), std::string("25-17 25-11"));

  // ESPN said "End of 3rd" for ten seconds before the third's first point,
  // NEB up 2-0: still the third under way at 0-0.
  GameSnapshot x = event("event_wcvb_endglitch.json", kNebraska);
  CHECK(x.state == GameState::IN);
  CHECK(!volleyball_set_over(x) && !ha_at_break(x));
  CHECK_EQ(board_team_score(x), 0);
  CHECK_EQ(board_opp_score(x), 0);
  CHECK_EQ(clock_text(x), std::string("Set 3  2-0"));
  CHECK_EQ(volleyball_sets(x), std::string("25-21 25-10"));
}

static void test_ticker_and_row() {
  GameSnapshot g = live(1, 1, 3, {25, 17, 12}, {19, 25, 9});
  TickerOptions o;
  o.clock = false;
  CHECK_EQ(status_text(g, o, ""), std::string("25-19, 17-25 | NEB 30-0 | WIS 25-4"));
  o.row_sets = true;  // a wide board has the sets on its row and the records under the logos
  CHECK_EQ(status_text(g, o, ""), std::string("NEB 30-0 | WIS 25-4"));  // nothing else to say
  g.game_name = "Big Ten Tournament - Final";
  g.tv = "BTN";
  CHECK_EQ(status_text(g, o, ""), std::string("Big Ten Tournament - Final | BTN"));
  o.odds = false;
  CHECK_EQ(status_text(g, o, ""), std::string("Big Ten Tournament - Final"));
  o.row_sets = false;
  CHECK_EQ(status_text(g, o, ""), std::string("25-19, 17-25 | NEB 30-0 | WIS 25-4 | Big Ten Tournament - Final"));
  GameSnapshot bare = live(0, 0, 1, {3}, {2});
  bare.team_record.clear();
  bare.opp_record.clear();
  CHECK_EQ(status_text(bare, o, ""), std::string("In Progress"));

  RowHold hold;
  RowLines r = row_lines(g, hold, RowOptions(), true);
  CHECK_EQ((int) r.n, 1);
  CHECK_EQ(r.line[0].form(0), std::string("25-19 17-25"));
  CHECK_EQ((int) row_lines(live(0, 0, 1, {3}, {2}), hold, RowOptions(), true).n, 0);

  // No scoring log and no leaders read for volleyball.
  ScoreLog log;
  log.update(g);
  GameSnapshot next = live(2, 1, 4, {25, 17, 25, 0}, {19, 25, 20, 0});
  log.update(next);
  CHECK(log.lines("1").empty());
  CHECK(leaders_stage(event("event_wcvb_final5.json", kGeorgia)) == LeadersStage::NONE);
}

static void test_splash() {
  GameSnapshot a = live(1, 1, 3, {25, 17, 24}, {19, 25, 20});
  GameSnapshot pt = live(1, 1, 3, {25, 17, 24}, {19, 25, 21});
  CHECK_EQ(decide_splash(a, pt, true).text, std::string(""));  // a point
  GameSnapshot set = live(2, 1, 3, {25, 17, 25}, {19, 25, 20});
  CHECK_EQ(decide_splash(a, set, true).text, std::string("SET!"));
  CHECK_EQ(decide_splash(a, set, true, true).text, std::string("NEB SET"));
  CelebPlan plan = decide_celebration(a, set, decide_splash(a, set, true), CelebLevel::FULL, false);
  CHECK(plan.kind == CelebKind::WAVE);
  plan = decide_celebration(a, set, decide_splash(a, set, true), CelebLevel::SUBTLE, false);
  CHECK(plan.kind == CelebKind::FLASH);
  GameSnapshot theirs = live(1, 2, 3, {25, 17, 20}, {19, 25, 25});
  CHECK_EQ(decide_splash(a, theirs, true).text, std::string("WIS SET"));
  CHECK_EQ(decide_splash(a, theirs, false).text, std::string(""));
  plan = decide_celebration(a, theirs, decide_splash(a, theirs, true), CelebLevel::FULL, false);
  CHECK(plan.kind == CelebKind::MUTED);

  GameSnapshot b = live(2, 2, 5, {25, 17, 25, 20, 14}, {19, 25, 20, 25, 9});
  GameSnapshot won = live(3, 2, 5, {25, 17, 25, 20, 15}, {19, 25, 20, 25, 9});
  won.state = GameState::POST;
  won.completed = true;
  CHECK_EQ(decide_splash(b, won, true).text, std::string("NEB WINS!"));
  plan = decide_celebration(b, won, decide_splash(b, won, true), CelebLevel::FULL, false);
  CHECK(plan.kind == CelebKind::WIN);
  CHECK_EQ(plan.line2, std::string("3-2"));
  GameSnapshot lost = live(2, 3, 5, {25, 17, 25, 20, 9}, {19, 25, 20, 25, 15});
  lost.state = GameState::POST;
  lost.completed = true;
  CHECK_EQ(decide_splash(b, lost, true).text, std::string(""));
  CHECK_EQ(decide_splash(b, lost, true, true).text, std::string("WIS WINS!"));
  won.completed = false;  // suspended, not won
  CHECK_EQ(decide_splash(b, won, true).text, std::string(""));
  GameSnapshot other = set;
  other.event_id = "2";
  CHECK_EQ(decide_splash(a, other, true).text, std::string(""));
  // Another favorite's set, as a banner.
  CHECK_EQ(fav_score_banner(a, set), std::string("NEB SET, 2-1"));
  CHECK_EQ(fav_score_banner(a, pt), std::string(""));
}

static void test_action_and_interest() {
  // Match point: two sets and one point short of 25, or of 15 in the fifth.
  CHECK(volleyball_match_point(live(2, 0, 3, {25, 25, 24}, {20, 20, 22})));
  CHECK(!volleyball_match_point(live(2, 0, 3, {25, 25, 24}, {20, 20, 24})));  // deuce
  CHECK(!volleyball_match_point(live(1, 1, 3, {25, 20, 24}, {20, 25, 22})));  // only set point
  CHECK(volleyball_match_point(live(0, 2, 3, {20, 20, 26}, {25, 25, 27})));
  CHECK(volleyball_match_point(live(2, 2, 5, {25, 25, 20, 20, 14}, {20, 20, 25, 25, 11})));
  CHECK(!volleyball_match_point(live(2, 2, 5, {25, 25, 20, 20, 13}, {20, 20, 25, 25, 11})));
  CHECK(in_action(live(2, 0, 3, {25, 25, 24}, {20, 20, 22}), League::WCVB));
  CHECK(in_action(live(2, 2, 5, {25, 25, 20, 20, 1}, {20, 20, 25, 25, 0}), League::WCVB));
  CHECK(!in_action(live(1, 1, 3, {25, 20, 10}, {20, 25, 10}), League::WCVB));
  CHECK(!in_action(live(2, 2, 4, {25, 25, 20, 20}, {20, 20, 25, 25}), League::WCVB));  // between sets

  auto g = [](int away, int home, int set, int away_rank = 99, int home_rank = 99) {
    LiveGame v;
    v.league = (uint8_t) League::WCVB;
    v.away_score = (int16_t) away;
    v.home_score = (int16_t) home;
    v.period = (uint8_t) set;
    v.clock_s = 0;  // ESPN's clock is 0:00 all match
    v.away_rank = (uint8_t) away_rank;
    v.home_rank = (uint8_t) home_rank;
    return v;
  };
  CHECK_EQ(game_progress(League::WCVB, 1, 0), 200);
  CHECK_EQ(game_progress(League::WCVB, 5, 0), 1000);
  CHECK(!in_overtime(League::WCVB, 5));
  CHECK(live_interest(g(2, 2, 5)) > live_interest(g(1, 1, 3)));
  CHECK(live_interest(g(1, 1, 3)) > live_interest(g(2, 0, 3)));
  CHECK(live_interest(g(1, 0, 2)) > live_interest(g(2, 0, 3)));
  CHECK(upset_alert(g(2, 0, 3, 99, 4)));   // #4 a set from going out, in the third
  CHECK(!upset_alert(g(1, 0, 2, 99, 4)));  // early
  CHECK(!upset_alert(g(0, 2, 3, 99, 4)));  // #4 leads
  CHECK(!upset_alert(g(2, 2, 5, 99, 4)));  // level
}

static void test_home_assistant() {
  CHECK_EQ(std::string(ha_sport(Sport::VOLLEYBALL)), std::string("volleyball"));
  GameSnapshot a = live(1, 1, 3, {25, 17, 24}, {19, 25, 20});
  CHECK_EQ(ha_period_text(a), std::string("3rd set"));
  a.display_clock = "0:00";
  CHECK_EQ(ha_clock_text(a), std::string(""));
  CHECK_EQ(std::string(ha_play(a, 1)), std::string("set"));
  GameSnapshot over = live(2, 1, 3, {25, 17, 25}, {19, 25, 20});
  CHECK_EQ(ha_period_text(over), std::string("End of the 3rd set"));
  CHECK_EQ(ha_periods_over(over), 3);
  CHECK_EQ(ha_status_text(a, ""), std::string("Live | NEB 1, WIS 1"));

  // A set is one score event and one period; points are neither.
  HaTracker t;
  HaContext ctx;
  ctx.league = "wcvb";
  CHECK(t.update(a, ctx).empty());
  GameSnapshot pt = live(1, 1, 3, {25, 17, 24}, {19, 25, 21});
  CHECK(t.update(pt, ctx).empty());
  std::vector<HaEvent> out = t.update(over, ctx);
  int scores = 0, periods = 0, leads = 0;
  for (const auto &e : out) {
    if (e.kind == HaKind::SCORE) {
      scores++;
      CHECK_EQ(e.play, std::string("set"));
      CHECK_EQ(e.points, 1);
      CHECK_EQ(e.team_score, 2);
    }
    if (e.kind == HaKind::PERIOD) {
      periods++;
      CHECK_EQ(e.period_name, std::string("3rd set"));
    }
    leads += e.kind == HaKind::LEAD;
  }
  CHECK_EQ(scores, 1);
  CHECK_EQ(periods, 1);
  CHECK_EQ(leads, 1);
  GameSnapshot next = live(2, 1, 4, {25, 17, 25, 1}, {19, 25, 20, 0});
  CHECK(t.update(next, ctx).empty());  // the fourth set starts: nothing new
}

// The finals rotation keeps a league whose value is past the row count.
static void test_recap() {
  const int64_t now = parse_iso8601_z("2026-11-26T03:00Z");
  FinalList finals;
  Final f;
  f.league = (uint8_t) League::WCVB;
  f.kickoff_epoch = parse_iso8601_z("2026-11-25T18:00Z");
  f.detail = "Final";
  f.away_id = kWisconsin;
  f.home_id = kNebraska;
  f.away_score = 1;
  f.home_score = 3;
  finals.push_back(f);
  std::vector<RecapDay> days;
  recap_absorb(days, finals, {}, now);
  CHECK_EQ((int) recap_count(days, (uint8_t) League::WCVB), 1);
  CHECK_EQ(days.empty() ? std::string() : recap_header(days[0], now), std::string("Today's NCAAVB scores"));
  std::vector<RecapCard> cards = recap_cards(days, recap_bit((uint8_t) League::WCVB), false, now);
  CHECK_EQ((int) cards.size(), 2);  // the header and the final
  GameSnapshot s = final_snapshot(f);
  CHECK(s.sport == Sport::VOLLEYBALL);
  CHECK_EQ(board_team_score(s), 1);
  CHECK_EQ(board_opp_score(s), 3);
  // A league byte with no row is dropped, not indexed.
  finals[0].league = 11;
  days.clear();
  recap_absorb(days, finals, {}, now);
  CHECK(recap_cards(days, recap_bit((uint8_t) League::WCVB), false, now).empty());
}

int main() {
  test_league();
  test_team();
  test_pre_post();
  test_live_numbers();
  test_recorded_live();
  test_ticker_and_row();
  test_splash();
  test_action_and_interest();
  test_home_assistant();
  test_recap();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
