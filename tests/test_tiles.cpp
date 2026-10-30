// The multi-game board (components/gameday/multi_board.h): which games get
// a tile, how tiles keep their places between polls, how many tiles the
// setting gives, and each tile's status line.
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "multi_board.h"

using namespace espn;

namespace {

int checks_ = 0, failures_ = 0;

#define TCHECK(cond)                                                     \
  do {                                                                   \
    checks_++;                                                           \
    if (!(cond)) {                                                       \
      failures_++;                                                       \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
    }                                                                    \
  } while (0)

#define TCHECK_EQ(a, b)                                                  \
  do {                                                                   \
    checks_++;                                                           \
    auto va_ = (a);                                                      \
    auto vb_ = (b);                                                      \
    if (!(va_ == vb_)) {                                                 \
      failures_++;                                                       \
      printf("FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b);     \
    }                                                                    \
  } while (0)

TileGame game(const char *id, GameState st, int away = 0, int home = 0, int period = 0, int clock_s = -1) {
  TileGame g;
  g.event_id = id;
  g.league = (uint8_t) League::NFL;
  g.state = st;
  g.away_score = away;
  g.home_score = home;
  g.period = (uint8_t) period;
  g.clock_s = clock_s;
  g.away_abbr = "AWY";
  g.home_abbr = "HOM";
  return g;
}

std::vector<std::string> ids(const std::vector<TileGame> &c, const std::vector<int> &plan) {
  std::vector<std::string> out;
  for (int i : plan)
    out.push_back(i < 0 ? "" : c[(size_t) i].event_id);
  return out;
}

void test_setting() {
  uint8_t v = 9;
  TCHECK(parse_tiles("1", v) && v == (uint8_t) Tiles::ONE);
  TCHECK(parse_tiles("2", v) && v == (uint8_t) Tiles::TWO);
  TCHECK(parse_tiles("4", v) && v == (uint8_t) Tiles::FOUR);
  TCHECK(parse_tiles("auto", v) && v == (uint8_t) Tiles::AUTO);
  TCHECK(!parse_tiles("3", v));
  TCHECK(!parse_tiles("", v));
  TCHECK(!parse_tiles("Auto", v));
  TCHECK_EQ(std::string(tiles_name(0)), std::string("1"));
  TCHECK_EQ(std::string(tiles_name(3)), std::string("auto"));
  TCHECK_EQ(std::string(tiles_name(7)), std::string("1"));  // a byte from the future reads as one game
}

void test_count() {
  // A 64-wide panel and the default never tile.
  TCHECK_EQ(tile_count((uint8_t) Tiles::FOUR, 64, 4, 8), 1);
  TCHECK_EQ(tile_count((uint8_t) Tiles::ONE, 128, 4, 8), 1);
  // Fixed counts, as long as there is more than one game on hand.
  TCHECK_EQ(tile_count((uint8_t) Tiles::TWO, 128, 3, 8), 2);
  TCHECK_EQ(tile_count((uint8_t) Tiles::FOUR, 128, 0, 5), 4);
  TCHECK_EQ(tile_count((uint8_t) Tiles::FOUR, 128, 0, 1), 1);
  TCHECK_EQ(tile_count((uint8_t) Tiles::FOUR, 128, 0, 0), 1);
  // Auto follows the live games.
  TCHECK_EQ(tile_count((uint8_t) Tiles::AUTO, 128, 0, 8), 1);
  TCHECK_EQ(tile_count((uint8_t) Tiles::AUTO, 128, 1, 8), 1);
  TCHECK_EQ(tile_count((uint8_t) Tiles::AUTO, 128, 2, 8), 2);
  TCHECK_EQ(tile_count((uint8_t) Tiles::AUTO, 128, 3, 8), 4);
  TCHECK_EQ(tile_count((uint8_t) Tiles::AUTO, 256, 6, 8), 4);
  TCHECK_EQ(tile_count(7, 128, 6, 8), 1);
}

void test_grid() {
  TileGrid g = tile_grid(2, 128);
  TCHECK(g.cols == 2 && g.rows == 1 && g.tile_w == 64 && g.tile_h == 64);
  g = tile_grid(4, 128);
  TCHECK(g.cols == 2 && g.rows == 2 && g.tile_w == 64 && g.tile_h == 32);
  g = tile_grid(4, 256);
  TCHECK(g.cols == 4 && g.rows == 1 && g.tile_w == 64 && g.tile_h == 64);
  g = tile_grid(2, 256);
  TCHECK(g.cols == 2 && g.rows == 1 && g.tile_w == 128 && g.tile_h == 64);
  g = tile_grid(1, 128);
  TCHECK(g.cols == 1 && g.rows == 1 && g.tile_w == 128 && g.tile_h == 64);
}

void test_fill_order() {
  TileRules r;
  int64_t now = 1'000'000;
  std::vector<TileGame> c;
  c.push_back(game("pre", GameState::PRE));
  c.back().kickoff_epoch = now + 3600;
  c.push_back(game("blowout", GameState::IN, 35, 3, 4, 300));
  c.push_back(game("close", GameState::IN, 21, 20, 4, 120));
  c.push_back(game("final", GameState::POST, 10, 7));
  // Empty board, four tiles: live games first, the closer one first, then
  // the final, then the game to come.
  auto plan = plan_tiles(c, {}, 4, now, r);
  auto got = ids(c, plan);
  TCHECK_EQ(got[0], std::string("close"));
  TCHECK_EQ(got[1], std::string("blowout"));
  TCHECK_EQ(got[2], std::string("final"));
  TCHECK_EQ(got[3], std::string("pre"));
  // Two tiles: only the live games.
  plan = plan_tiles(c, {}, 2, now, r);
  got = ids(c, plan);
  TCHECK(got.size() == 2 && got[0] == "close" && got[1] == "blowout");
}

void test_favorites_first() {
  TileRules r;
  int64_t now = 1'000'000;
  std::vector<TileGame> c;
  c.push_back(game("close", GameState::IN, 21, 20, 4, 120));
  c.push_back(game("fav2", GameState::IN, 35, 3, 2, 600));
  c.back().fav_rank = 1;
  c.push_back(game("fav1", GameState::IN, 0, 28, 3, 600));
  c.back().fav_rank = 0;
  auto got = ids(c, plan_tiles(c, {}, 4, now, r));
  TCHECK_EQ(got[0], std::string("fav1"));
  TCHECK_EQ(got[1], std::string("fav2"));
  TCHECK_EQ(got[2], std::string("close"));
  TCHECK_EQ(got[3], std::string(""));
}

void test_stable_slots() {
  TileRules r;
  int64_t now = 1'000'000;
  std::vector<TileGame> c;
  c.push_back(game("a", GameState::IN, 7, 7, 2, 500));
  c.push_back(game("b", GameState::IN, 0, 0, 1, 800));
  // The board has b in slot 0 and a in slot 1. A poll that makes a the more
  // interesting game leaves both where they are.
  auto got = ids(c, plan_tiles(c, {"b", "a"}, 2, now, r));
  TCHECK(got[0] == "b" && got[1] == "a");
  // A new live game takes the free slot, not another game's.
  c.push_back(game("c", GameState::IN, 50, 49, 4, 10));
  got = ids(c, plan_tiles(c, {"b", "a", "", ""}, 4, now, r));
  TCHECK(got[0] == "b" && got[1] == "a" && got[2] == "c" && got[3] == "");
  // A game that left the list frees its slot; the others stay put.
  c.erase(c.begin());  // a is gone
  got = ids(c, plan_tiles(c, {"b", "a", "c", ""}, 4, now, r));
  TCHECK(got[0] == "b" && got[1] == "" && got[2] == "c" && got[3] == "");
  // Fewer tiles than before: the first slot keeps its game, and a live
  // game that lost its tile takes the free one.
  got = ids(c, plan_tiles(c, {"b", "", "c", ""}, 2, now, r));
  TCHECK(got.size() == 2 && got[0] == "b" && got[1] == "c");  // the live game past slot 2 moves in
}

void test_live_bumps_filler() {
  TileRules r;
  int64_t now = 1'000'000;
  std::vector<TileGame> c;
  c.push_back(game("live1", GameState::IN, 7, 7, 2, 500));
  c.push_back(game("final", GameState::POST, 20, 17));
  c.push_back(game("pre", GameState::PRE));
  c.back().kickoff_epoch = now + 7200;
  c.push_back(game("final2", GameState::POST, 3, 0));
  // Board: live1, final, pre, final2. A new live game bumps the weakest
  // filler: the game to come ranks below finals.
  c.push_back(game("live2", GameState::IN, 14, 10, 3, 100));
  auto got = ids(c, plan_tiles(c, {"live1", "final", "pre", "final2"}, 4, now, r));
  TCHECK(got[0] == "live1" && got[1] == "final" && got[2] == "live2" && got[3] == "final2");
  // Another live game bumps a final; the live tiles never move.
  c.push_back(game("live3", GameState::IN, 0, 0, 1, 900));
  got = ids(c, plan_tiles(c, {"live1", "final", "live2", "final2"}, 4, now, r));
  TCHECK(got[0] == "live1" && got[2] == "live2");
  TCHECK((got[1] == "live3") != (got[3] == "live3"));
  // A fourth live game has nothing to bump once all four tiles are live.
  c.push_back(game("live4", GameState::IN, 0, 0, 1, 900));
  c.push_back(game("live5", GameState::IN, 0, 0, 1, 900));
  got = ids(c, plan_tiles(c, {"live1", "live3", "live2", "live4"}, 4, now, r));
  TCHECK(got[0] == "live1" && got[1] == "live3" && got[2] == "live2" && got[3] == "live4");
}

void test_final_hold() {
  TileRules r;
  r.final_hold_s = 600;
  int64_t now = 1'000'000;
  std::vector<TileGame> c;
  c.push_back(game("done", GameState::POST, 20, 17));
  c.back().final_epoch = now - 60;
  c.push_back(game("live", GameState::IN, 7, 7, 2, 500));
  // A final the panel saw end a minute ago keeps its tile.
  auto got = ids(c, plan_tiles(c, {"done", "live"}, 2, now, r));
  TCHECK(got[0] == "done" && got[1] == "live");
  // Past the hold it leaves, and the slot stays empty rather than pulling it back.
  got = ids(c, plan_tiles(c, {"done", "live"}, 2, now + 700, r));
  TCHECK(got[0] == "" && got[1] == "live");
  // A final found that way (no final_epoch) is fill material and stays.
  c[0].final_epoch = 0;
  got = ids(c, plan_tiles(c, {"done", "live"}, 2, now + 700, r));
  TCHECK(got[0] == "done" && got[1] == "live");
  // Seen finals rank ahead of found finals, newest first.
  std::vector<TileGame> f;
  f.push_back(game("found", GameState::POST));
  f.push_back(game("old", GameState::POST));
  f.back().final_epoch = now - 300;
  f.push_back(game("new", GameState::POST));
  f.back().final_epoch = now - 30;
  got = ids(f, plan_tiles(f, {}, 4, now, r));
  TCHECK(got[0] == "new" && got[1] == "old" && got[2] == "found" && got[3] == "");
}

void test_record() {
  TCHECK_EQ(tile_record("4-5-0"), std::string("4-5"));
  TCHECK_EQ(tile_record("10-4"), std::string("10-4"));
  TCHECK_EQ(tile_record("3-2-1-0"), std::string("3-2"));
  TCHECK_EQ(tile_record(""), std::string(""));
  TCHECK_EQ(tile_record("7"), std::string("7"));
}

void test_status() {
  TileGame g = game("x", GameState::IN, 0, 0, 3, 252);
  g.detail = "4:12 - 3rd";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("Q3 4:12"));
  g.period = 5;
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("OT 4:12"));
  g.period = 6;
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("2OT 4:12"));
  g.period = 2;
  g.detail = "Halftime";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("HALF"));
  g.detail = "End of 2nd";
  g.clock_s = 0;
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("END Q2"));
  g.detail = "Delayed";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("DELAY"));
  // Hockey periods, college basketball halves.
  g = game("h", GameState::IN, 0, 0, 2, 65);
  g.league = (uint8_t) League::NHL;
  g.detail = "1:05 - 2nd";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("P2 1:05"));
  g.league = (uint8_t) League::MCBB;
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("H2 1:05"));
  g.period = 3;
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("OT 1:05"));
  // Baseball halves from ESPN's words.
  g = game("b", GameState::IN, 0, 0, 5, -1);
  g.league = (uint8_t) League::MLB;
  g.detail = "Top 5th";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("TOP 5"));
  g.detail = "Bottom 5th";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("BOT 5"));
  g.detail = "Middle 5th";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("MID 5"));
  g.detail = "";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("INN 5"));
  // Soccer's elapsed minute.
  g = game("s", GameState::IN, 0, 0, 2, 65 * 60 + 10);
  g.league = (uint8_t) League::EPL;
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("65'"));
  g.detail = "Halftime";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("HT"));
  // Finals.
  g = game("f", GameState::POST, 20, 17);
  g.detail = "Final";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("F"));
  g.detail = "Final/OT";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("F/OT"));
  g.detail = "Final/SO";
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string("F/SO"));
  // Games to come: the time today, the day and time otherwise.
  g = game("p", GameState::PRE);
  struct tm now_local{};
  now_local.tm_year = 126;
  now_local.tm_yday = 300;
  now_local.tm_wday = 1;
  now_local.tm_hour = 12;
  struct tm kick = now_local;
  kick.tm_hour = 19;
  kick.tm_min = 30;
  TCHECK_EQ(tile_status(g, &kick, &now_local), std::string("7:30 PM"));
  kick.tm_yday = 303;
  kick.tm_wday = 4;
  kick.tm_hour = 0;
  kick.tm_min = 5;
  TCHECK_EQ(tile_status(g, &kick, &now_local), std::string("Thu 12:05 AM"));
  g.time_tbd = true;
  TCHECK_EQ(tile_status(g, &kick, &now_local), std::string("Thu TBD"));
  TCHECK_EQ(tile_status(g, nullptr, nullptr), std::string(""));
}

}  // namespace

int run_tile_tests(int &checks) {
  test_setting();
  test_count();
  test_grid();
  test_fill_order();
  test_favorites_first();
  test_stable_slots();
  test_live_bumps_filler();
  test_final_hold();
  test_record();
  test_status();
  checks += checks_;
  return failures_;
}
