// Host tests for the celebrations: which one a score gets per sport, side
// and setting (celebrate.h), and the animation engine's math (celebrate_fx.h):
// particles stay in the pool, fall, fade out, every kind draws something on
// every frame of its run, and a frame costs little. Build with `make` in tests/.
#include <ArduinoJson.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "celeb_art_index.h"
#include "celebrate.h"
#include "celebrate_fx.h"

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

#define CHECK_EQ(a, b)                                                                                     \
  do {                                                                                                     \
    checks++;                                                                                              \
    if (!((a) == (b))) {                                                                                   \
      failures++;                                                                                          \
      printf("FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b);                                        \
    }                                                                                                      \
  } while (0)

static GameSnapshot live(Sport sport, int team, int opp) {
  GameSnapshot s;
  s.valid = true;
  s.event_id = "401";
  s.state = GameState::IN;
  s.sport = sport;
  s.team_abbr = "KC";
  s.opp_abbr = "BUF";
  s.team_id = 12;
  s.opp_id = 2;
  s.team_color = "E31837";
  s.opp_color = "00338D";
  s.team_score = team;
  s.opp_score = opp;
  return s;
}

static GameSnapshot final_(Sport sport, int team, int opp) {
  GameSnapshot s = live(sport, team, opp);
  s.state = GameState::POST;
  s.completed = true;
  return s;
}

// What the firmware does for one poll: decide_splash, then the celebration.
static CelebPlan plan(const GameSnapshot &prev, const GameSnapshot &cur, CelebLevel level, bool neutral = false,
                      bool opp_splashes = true) {
  Splash s = decide_splash(prev, cur, opp_splashes, neutral);
  return decide_celebration(prev, cur, s, level, neutral, opp_splashes);
}

static void test_levels() {
  // Stored byte: 0 is Full so an older panel's saved blob keeps celebrating.
  CHECK(celeb_level_from_stored(0) == CelebLevel::FULL);
  CHECK(celeb_level_from_stored(1) == CelebLevel::OFF);
  CHECK(celeb_level_from_stored(2) == CelebLevel::SUBTLE);
  CHECK(celeb_level_from_stored(200) == CelebLevel::FULL);
  for (CelebLevel l : {CelebLevel::OFF, CelebLevel::SUBTLE, CelebLevel::FULL})
    CHECK(celeb_level_from_stored(celeb_level_to_stored(l)) == l);
  CHECK_EQ(std::string(celeb_level_name(CelebLevel::SUBTLE)), "subtle");
}

static void test_colors() {
  CHECK_EQ(celeb_bright(0x000000), 0xFFFFFFu);
  CHECK_EQ(celeb_bright(0x700000), 0xE00000u);
  CHECK_EQ(celeb_bright(0xFF0000), 0xFF0000u);
  CHECK_EQ(celeb_dim(0xFF8040), 0x7F4020u);
  CHECK_EQ(celeb_alt_for(0xFFFFFF), 0xFFD000u);  // gold next to a white team
  CHECK_EQ(celeb_alt_for(0xE31837), 0xFFFFFFu);
  CHECK_EQ(rgb565(0xFFFFFF), 0xFFFFu);
  CHECK_EQ(rgb565(0xFF0000), 0xF800u);
  CHECK_EQ(rgb565(0x00FF00), 0x07E0u);
  CHECK_EQ(rgb565(0x0000FF), 0x001Fu);
  CHECK_EQ(rgb_scale(0x804020, 128), 0x402010u);
  CHECK_EQ(rgb_mix(0x000000, 0xFFFFFF, 128), 0x7F7F7Fu);
}

static void test_football() {
  GameSnapshot a = live(Sport::FOOTBALL, 7, 3);
  // Our touchdown.
  CelebPlan p = plan(a, live(Sport::FOOTBALL, 14, 3), CelebLevel::FULL);
  CHECK(p.kind == CelebKind::TOUCHDOWN);
  CHECK_EQ(p.text, "TOUCHDOWN!");
  CHECK_EQ(p.color, celeb_bright(0xE31837));
  CHECK_EQ(p.duration_ms, kCelebTouchdownMs);
  CHECK(p.line2.empty());
  // With the extra point in the same read, still a touchdown; a two-point
  // try alone, a field goal and a safety sweep.
  CHECK(plan(a, live(Sport::FOOTBALL, 15, 3), CelebLevel::FULL).kind == CelebKind::TOUCHDOWN);
  CHECK(plan(a, live(Sport::FOOTBALL, 9, 3), CelebLevel::FULL).kind == CelebKind::WAVE);
  CHECK(plan(a, live(Sport::FOOTBALL, 10, 3), CelebLevel::FULL).kind == CelebKind::WAVE);
  CHECK_EQ(plan(a, live(Sport::FOOTBALL, 10, 3), CelebLevel::FULL).text, "FIELD GOAL!");
  CHECK(plan(a, live(Sport::FOOTBALL, 8, 3), CelebLevel::FULL).kind == CelebKind::WAVE);
  // The other side's touchdown is muted, dim, short, whatever the level.
  p = plan(a, live(Sport::FOOTBALL, 7, 10), CelebLevel::FULL);
  CHECK(p.kind == CelebKind::MUTED);
  CHECK_EQ(p.text, "BUF TOUCHDOWN");
  CHECK_EQ(p.color, celeb_dim(celeb_bright(0x00338D)));
  CHECK_EQ(p.duration_ms, kCelebMutedMs);
  CHECK(plan(a, live(Sport::FOOTBALL, 7, 10), CelebLevel::SUBTLE).kind == CelebKind::MUTED);
  // Opponent scores too off: no splash, no celebration.
  CHECK(plan(a, live(Sport::FOOTBALL, 7, 10), CelebLevel::FULL, false, false).kind == CelebKind::NONE);
  // Subtle: our touchdown is the plain flash; Off: nothing at all.
  p = plan(a, live(Sport::FOOTBALL, 14, 3), CelebLevel::SUBTLE);
  CHECK(p.kind == CelebKind::FLASH);
  CHECK_EQ(p.duration_ms, kCelebFlashMs);
  CHECK(plan(a, live(Sport::FOOTBALL, 14, 3), CelebLevel::OFF).kind == CelebKind::NONE);
  // Neutral (Live games): the other side's touchdown gets the full show in
  // its own color, named.
  p = plan(a, live(Sport::FOOTBALL, 7, 10), CelebLevel::FULL, true);
  CHECK(p.kind == CelebKind::TOUCHDOWN);
  CHECK_EQ(p.text, "BUF TOUCHDOWN");
  CHECK_EQ(p.color, celeb_bright(0x00338D));
  // Both sides moving in one read (a safety read late with our touchdown):
  // the splash is ours, so is the celebration.
  CHECK(plan(a, live(Sport::FOOTBALL, 14, 5), CelebLevel::FULL).kind == CelebKind::TOUCHDOWN);
  // A different game or a pre-game read celebrates nothing.
  GameSnapshot other = live(Sport::FOOTBALL, 14, 3);
  other.event_id = "402";
  CHECK(plan(a, other, CelebLevel::FULL).kind == CelebKind::NONE);
}

static void test_win() {
  GameSnapshot a = live(Sport::FOOTBALL, 24, 10);
  CelebPlan p = plan(a, final_(Sport::FOOTBALL, 24, 10), CelebLevel::FULL);
  CHECK(p.kind == CelebKind::WIN);
  CHECK_EQ(p.text, "KC WINS!");
  CHECK_EQ(p.line2, "24-10");
  CHECK_EQ(p.duration_ms, kCelebWinMs);
  // Their win: muted (we lost), in Favorites; the winner's show when neutral.
  GameSnapshot b = live(Sport::FOOTBALL, 10, 24);
  CHECK(plan(b, final_(Sport::FOOTBALL, 10, 24), CelebLevel::FULL).kind == CelebKind::NONE);  // no splash for a loss
  p = plan(b, final_(Sport::FOOTBALL, 10, 24), CelebLevel::FULL, true);
  CHECK(p.kind == CelebKind::WIN);
  CHECK_EQ(p.text, "BUF WINS!");
  CHECK_EQ(p.line2, "24-10");  // the winner's points first
  CHECK_EQ(p.color, celeb_bright(0x00338D));
  // A level soccer final decided by the shootout: our win is the full show
  // with the shootout in the second line, not the other side's muted flash.
  GameSnapshot sl = live(Sport::SOCCER, 1, 1);
  GameSnapshot so = final_(Sport::SOCCER, 1, 1);
  so.soc.team_shootout = 4;
  so.soc.opp_shootout = 3;
  Splash sos = decide_splash(sl, so, true, false);
  CHECK_EQ(sos.text, "KC WINS!");
  p = decide_celebration(sl, so, sos, CelebLevel::FULL, false);
  CHECK(p.kind == CelebKind::WIN);
  CHECK_EQ(p.line2, "1-1 (4-3)");
  CHECK_EQ(p.color, celeb_bright(0xE31837));
  // Their shootout win: nothing in Favorites (no splash), their show when neutral.
  so.soc.team_shootout = 3;
  so.soc.opp_shootout = 4;
  CHECK(plan(sl, so, CelebLevel::FULL).kind == CelebKind::NONE);
  p = plan(sl, so, CelebLevel::FULL, true);
  CHECK(p.kind == CelebKind::WIN);
  CHECK_EQ(p.text, "BUF WINS!");
  CHECK_EQ(p.line2, "1-1 (4-3)");
  CHECK_EQ(p.color, celeb_bright(0x00338D));
  // The second leg of a two-legged tie: ESPN's score is this leg's (1-2),
  // level on aggregate, and the shootout decides it, not the leg.
  GameSnapshot leg = live(Sport::SOCCER, 1, 2);
  GameSnapshot legf = final_(Sport::SOCCER, 1, 2);
  legf.soc.team_shootout = 4;
  legf.soc.opp_shootout = 3;
  Splash legs = decide_splash(leg, legf, true, false);
  CHECK_EQ(legs.text, "KC WINS!");
  p = decide_celebration(leg, legf, legs, CelebLevel::FULL, false);
  CHECK(p.kind == CelebKind::WIN);
  CHECK_EQ(p.line2, "1-2 (4-3)");
  legf.soc.team_shootout = 3;
  legf.soc.opp_shootout = 4;
  CHECK(plan(leg, legf, CelebLevel::FULL).kind == CelebKind::NONE);  // their win: no splash for us
  // UPSET ALERT is not a score: the plain flash, even with a touchdown in
  // the same read for the silent side.
  GameSnapshot u = live(Sport::FOOTBALL, 7, 3);
  p = decide_celebration(u, live(Sport::FOOTBALL, 7, 10), Splash{"UPSET ALERT", 0xFFD000}, CelebLevel::FULL,
                         true);
  CHECK(p.kind == CelebKind::FLASH);
  CHECK_EQ(p.text, "UPSET ALERT");
  // Subtle keeps the win to a flash.
  CHECK(plan(a, final_(Sport::FOOTBALL, 24, 10), CelebLevel::SUBTLE).kind == CelebKind::FLASH);
  // A suspended game (not completed) has no win.
  GameSnapshot sus = final_(Sport::FOOTBALL, 24, 10);
  sus.completed = false;
  CHECK(plan(a, sus, CelebLevel::FULL).kind == CelebKind::NONE);
  // Every sport's win is the same screen.
  for (Sport s : {Sport::BASEBALL, Sport::BASKETBALL, Sport::HOCKEY, Sport::SOCCER}) {
    GameSnapshot l = live(s, 3, 1);
    CHECK(plan(l, final_(s, 3, 1), CelebLevel::FULL).kind == CelebKind::WIN);
  }
}

static void test_baseball() {
  GameSnapshot a = live(Sport::BASEBALL, 2, 1);
  GameSnapshot hr = live(Sport::BASEBALL, 4, 1);
  hr.mlb.play_type = "Home Run";
  CelebPlan p = plan(a, hr, CelebLevel::FULL);
  CHECK(p.kind == CelebKind::HOME_RUN);
  CHECK_EQ(p.text, "HOME RUN!");
  CHECK_EQ(p.duration_ms, kCelebHomeRunMs);
  // A run that is not a home run sweeps.
  p = plan(a, live(Sport::BASEBALL, 3, 1), CelebLevel::FULL);
  CHECK(p.kind == CelebKind::WAVE);
  CHECK_EQ(p.text, "RUN!");
  // Their home run is muted.
  GameSnapshot ohr = live(Sport::BASEBALL, 2, 3);
  ohr.mlb.play_type = "Home Run";
  p = plan(a, ohr, CelebLevel::FULL);
  CHECK(p.kind == CelebKind::MUTED);
  CHECK_EQ(p.text, "BUF HOME RUN");
}

static void test_hockey_soccer() {
  GameSnapshot h = live(Sport::HOCKEY, 1, 1);
  CelebPlan p = plan(h, live(Sport::HOCKEY, 2, 1), CelebLevel::FULL);
  CHECK(p.kind == CelebKind::GOAL_LAMP);
  CHECK_EQ(p.text, "GOAL!");
  CHECK_EQ(p.duration_ms, kCelebGoalMs);
  CHECK(plan(h, live(Sport::HOCKEY, 1, 2), CelebLevel::FULL).kind == CelebKind::MUTED);
  GameSnapshot s = live(Sport::SOCCER, 0, 0);
  p = plan(s, live(Sport::SOCCER, 1, 0), CelebLevel::FULL);
  CHECK(p.kind == CelebKind::GOAL_NET);
  CHECK_EQ(p.text, "GOAL!");
  p = plan(s, live(Sport::SOCCER, 0, 1), CelebLevel::FULL, true);
  CHECK(p.kind == CelebKind::GOAL_NET);
  CHECK_EQ(p.text, "BUF GOAL");
  // A soccer draw at full time: no win, nothing.
  CHECK(plan(live(Sport::SOCCER, 1, 1), final_(Sport::SOCCER, 1, 1), CelebLevel::FULL).kind == CelebKind::NONE);
}

// A basketball read whose last play is ESPN's three-point text, with the
// scoreValue and team ESPN gives a made three (value 0 is a document
// without a scoreValue, so the words decide).
static GameSnapshot three(int team, int opp, const char *play = "Stephen Curry makes 26-foot three point jumper",
                          int value = 3, uint32_t by = 12) {
  GameSnapshot s = live(Sport::BASKETBALL, team, opp);
  s.last_play = play;
  s.play_value = (int8_t) value;
  s.play_team = by;
  return s;
}

static void test_basketball() {
  GameSnapshot a = live(Sport::BASKETBALL, 50, 48);
  // decide_splash says nothing for a basket; Full celebrates a three-point
  // shot read on its own.
  CHECK(decide_splash(a, three(53, 48), true, false).text.empty());
  CelebPlan p = plan(a, three(53, 48), CelebLevel::FULL);
  CHECK(p.kind == CelebKind::THREE);
  CHECK_EQ(p.text, "THREE!");
  CHECK_EQ(p.duration_ms, kCelebThreeMs);
  CHECK_EQ(p.color, celeb_bright(0xE31837));
  CHECK(plan(a, three(53, 48, "KEVIN DURANT MAKES THREE-POINT JUMPER"), CelebLevel::FULL).kind == CelebKind::THREE);
  // No scoreValue in the document: the words decide, a make only.
  CHECK(plan(a, three(53, 48, "made Three Point Jumper", 0, 0), CelebLevel::FULL).kind == CelebKind::THREE);
  GameSnapshot pt = live(Sport::BASKETBALL, 53, 48);
  pt.play_type = "Three Point Jumper";
  pt.last_play = "Caitlin Clark makes 25-foot three point jumper";
  CHECK(plan(a, pt, CelebLevel::FULL).kind == CelebKind::THREE);
  // A read that ends on a missed three (ESPN uses the same words) after
  // three points came another way, by either team: no show. Nor a made
  // three by the other team with our points moving, nor a free throw
  // (scoreValue 1) as the last of an and-one.
  CHECK(plan(a, three(53, 48, "Jayson Tatum misses 26-foot three point jumper", 0, 12), CelebLevel::FULL).kind ==
        CelebKind::NONE);
  CHECK(plan(a, three(53, 48, "missed Three Point Jumper", 0, 2), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, three(53, 48, "misses three point jumper", 0, 0), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, three(53, 48, "makes 26-foot three point jumper", 3, 2), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, three(53, 48, "makes free throw 1 of 1", 1, 12), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, three(53, 48, "makes 26-foot three point jumper", 2, 12), CelebLevel::FULL).kind == CelebKind::NONE);
  // Their made three in Live games is theirs by team id.
  CHECK(plan(a, three(50, 51, "makes 26-foot three point jumper", 3, 2), CelebLevel::FULL, true).kind ==
        CelebKind::THREE);
  CHECK(plan(a, three(50, 51, "makes 26-foot three point jumper", 3, 12), CelebLevel::FULL, true).kind ==
        CelebKind::NONE);
  // Three points that were not a three: an and-one, three free throws, or a
  // read with no play text at all. Two points, a free throw, two baskets
  // read at once, or a three with the other side moving too: no show.
  CHECK(plan(a, three(53, 48, "LeBron James makes free throw 1 of 1", 1), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, three(53, 48, "makes driving layup", 2), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, live(Sport::BASKETBALL, 53, 48), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, three(52, 48), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, three(51, 48), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, three(55, 48), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, three(53, 50), CelebLevel::FULL).kind == CelebKind::NONE);
  // Their three: nothing in Favorites, their show when neutral, and nothing
  // when neutral with Opponent scores too off (as their other scores).
  CHECK(plan(a, three(50, 51), CelebLevel::FULL).kind == CelebKind::NONE);
  p = plan(a, three(50, 51, "makes 26-foot three point jumper", 3, 2), CelebLevel::FULL, true);
  CHECK(p.kind == CelebKind::THREE);
  CHECK_EQ(p.text, "BUF THREE!");
  CHECK(plan(a, three(50, 51, "makes 26-foot three point jumper", 3, 2), CelebLevel::FULL, true, false).kind ==
        CelebKind::NONE);
  // Subtle and Off leave baskets alone, as before.
  CHECK(plan(a, three(53, 48), CelebLevel::SUBTLE).kind == CelebKind::NONE);
  CHECK(plan(a, three(53, 48), CelebLevel::OFF).kind == CelebKind::NONE);
  // The latch: the component redraws the board between polls (a failed
  // poll, a setting change, a card) and asks again with the same read; the
  // three shows once. The next three is a new pair; a stale read that goes
  // back to an old pair stays quiet.
  CelebLatch latch;
  GameSnapshot t1 = three(53, 48);
  CHECK(plan(a, t1, CelebLevel::FULL).kind == CelebKind::THREE && latch.take(t1));
  CHECK(plan(a, t1, CelebLevel::FULL).kind == CelebKind::THREE && !latch.take(t1));
  CHECK(!latch.take(t1));
  GameSnapshot t2 = three(56, 48);
  CHECK(latch.take(t2));
  CHECK(!latch.take(t2));
  GameSnapshot other = three(53, 48);
  other.event_id = "402";
  CHECK(latch.take(other));
  // ESPN's play id: rewriting a play's words is the same play (one
  // interception played four times in 90 seconds on 11/12).
  {
    GameSnapshot p = three(53, 48);
    p.sport = Sport::FOOTBALL;
    p.event_id = "501";
    p.play_id = 4017727501234;
    p.last_play = "J.Daniels pass deep left intended for C.Okonkwo INTERCEPTED by J.Holland at NYG 0.";
    CelebLatch l;
    const uint32_t s = 1000;
    CHECK(l.take(p, CelebKind::INTERCEPTION, 0));
    p.last_play += " Touchback.";
    CHECK(!l.take(p, CelebKind::INTERCEPTION, 22 * s));
    p.last_play += " ** Injury Update";
    CHECK(!l.take(p, CelebKind::INTERCEPTION, 65 * s));
    // A new id for the same turnover inside 90 seconds stays quiet too.
    GameSnapshot q = p;
    q.play_id = 4017727501299;
    CHECK(!l.take(q, CelebKind::INTERCEPTION, 80 * s));
    // A different turnover, or a sack, is news.
    q.play_id = 4017727501300;
    CHECK(l.take(q, CelebKind::FUMBLE, 85 * s));
    q.play_id = 4017727501301;
    CHECK(l.take(q, CelebKind::SACK, 100 * s));
    q.play_id = 4017727501302;
    CHECK(l.take(q, CelebKind::SACK, 110 * s));  // two sacks in a row both show
    // The next interception, minutes later, shows.
    q.play_id = 4017727509999;
    CHECK(l.take(q, CelebKind::INTERCEPTION, 11 * 60 * s));
    // Another game is its own.
    GameSnapshot r = q;
    r.event_id = "502";
    CHECK(l.take(r, CelebKind::INTERCEPTION, 11 * 60 * s + 1));
  }
  // The win still shows.
  CHECK(plan(live(Sport::BASKETBALL, 99, 98), final_(Sport::BASKETBALL, 99, 98), CelebLevel::FULL).kind ==
        CelebKind::WIN);
}

static void test_text_place() {
  CelebTextPlace t = celeb_text_place(CelebKind::WIN, 128, 64);
  CHECK_EQ(t.font, 16);
  CHECK(t.y >= 0 && t.line2_y > t.y + 15 && t.line2_y + 16 <= 64);
  t = celeb_text_place(CelebKind::WIN, 64, 64);
  CHECK_EQ(t.font, 12);
  CHECK(t.line2_y > t.y + 11);
  for (CelebKind k : {CelebKind::TOUCHDOWN, CelebKind::WAVE, CelebKind::HOME_RUN, CelebKind::GOAL_NET,
                      CelebKind::GOAL_LAMP, CelebKind::THREE, CelebKind::FLASH}) {
    t = celeb_text_place(k, 128, 64);
    CHECK(t.y >= 0 && t.y + 16 <= 64);
    CHECK_EQ(t.line2_y, -1);
    t = celeb_text_place(k, 64, 64);
    CHECK(t.y >= 0 && t.y + 12 <= 64);
  }
  // The art draws its own title: the page's words sit off the board.
  for (CelebKind k : {CelebKind::DUNK, CelebKind::ALLEY_OOP, CelebKind::GRAND_SLAM, CelebKind::WALK_OFF})
    for (int w : {128, 64}) {
      t = celeb_text_place(k, w, 64);
      CHECK(t.y >= 64);
      CHECK_EQ(t.line2_y, -1);
    }
}

static CelebPlan sample(CelebKind k) {
  CelebPlan p;
  p.kind = k;
  p.color = 0xE31837;
  p.alt = 0xFFFFFF;
  p.text = "TEST";
  p.duration_ms = k == CelebKind::WIN         ? kCelebWinMs
                  : k == CelebKind::TOUCHDOWN ? kCelebTouchdownMs
                  : k == CelebKind::DUNK      ? kCelebDunkMs
                  : k == CelebKind::ALLEY_OOP ? kCelebAlleyOopMs
                  : k == CelebKind::GRAND_SLAM ? kCelebGrandSlamMs
                  : k == CelebKind::WALK_OFF  ? kCelebWalkOffMs
                                              : 4500;
  return p;
}

// A basketball read whose last play is a made two of ESPN's type, by team
// `by` (12 is ours, 2 theirs).
static GameSnapshot two(int team, int opp, const char *type, const char *text = "", int value = 2, uint32_t by = 12) {
  GameSnapshot s = live(Sport::BASKETBALL, team, opp);
  s.play_type = type;
  s.last_play = text;
  s.play_value = (int8_t) value;
  s.play_team = by;
  return s;
}

static CelebPlan plan_big(const GameSnapshot &prev, const GameSnapshot &cur, CelebLevel level, bool neutral,
                          bool opp_splashes, bool big_plays) {
  Splash s = decide_splash(prev, cur, opp_splashes, neutral);
  return decide_celebration(prev, cur, s, level, neutral, opp_splashes, big_plays);
}

static void test_basketball_big_plays() {
  GameSnapshot a = live(Sport::BASKETBALL, 50, 48);
  // Every dunk ESPN names, ours: DUNK!, on Full.
  for (const char *type : {"Dunk Shot", "Running Dunk Shot", "Cutting Dunk Shot", "Tip Dunk Shot", "Driving Dunk Shot",
                           "Putback Dunk Shot"}) {
    CelebPlan p = plan(a, two(52, 48, type), CelebLevel::FULL);
    CHECK(p.kind == CelebKind::DUNK);
    CHECK_EQ(p.text, "DUNK!");
    CHECK_EQ(p.duration_ms, kCelebDunkMs);
    CHECK_EQ(p.color, celeb_bright(0xE31837));
  }
  // An alley-oop is an alley-oop first, dunk or layup.
  for (const char *type : {"Alley Oop Dunk Shot", "Running Alley Oop Dunk Shot", "Alley Oop Layup Shot"}) {
    CelebPlan p = plan(a, two(52, 48, type), CelebLevel::FULL);
    CHECK(p.kind == CelebKind::ALLEY_OOP);
    CHECK_EQ(p.text, "ALLEY-OOP!");
    CHECK_EQ(p.duration_ms, kCelebAlleyOopMs);
  }
  // Any other two, a missed dunk (scoreValue 0 with the points from before),
  // an and-one (three points in the read), both sides moving, or their dunk
  // in Favorites: nothing.
  CHECK(plan(a, two(52, 48, "Driving Layup Shot"), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, two(52, 48, "Running Dunk Shot", "misses running dunk", 0), CelebLevel::FULL).kind ==
        CelebKind::NONE);
  CHECK(plan(a, two(53, 48, "Running Dunk Shot"), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, two(53, 48, "Free Throw - 1 of 1", "", 1), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, two(52, 50, "Running Dunk Shot"), CelebLevel::FULL).kind == CelebKind::NONE);
  CHECK(plan(a, two(50, 50, "Running Dunk Shot", "", 2, 2), CelebLevel::FULL).kind == CelebKind::NONE);
  // Our points with their team on the play: not ours.
  CHECK(plan(a, two(52, 48, "Running Dunk Shot", "", 2, 2), CelebLevel::FULL).kind == CelebKind::NONE);
  // A document with no scoreValue: the words decide, a make only.
  CHECK(plan(a, two(52, 48, "Dunk Shot", "Victor Wembanyama makes two point dunk", 0, 0), CelebLevel::FULL).kind ==
        CelebKind::DUNK);
  CHECK(plan(a, two(52, 48, "Dunk Shot", "Victor Wembanyama misses two point dunk", 0, 0), CelebLevel::FULL).kind ==
        CelebKind::NONE);
  CHECK(plan(a, two(52, 48, "", "Victor Wembanyama makes alley oop dunk shot", 0, 0), CelebLevel::FULL).kind ==
        CelebKind::ALLEY_OOP);
  // Neutral boards: either side, with its abbreviation; theirs needs
  // Opponent scores too.
  CelebPlan n = plan(a, two(50, 50, "Running Dunk Shot", "", 2, 2), CelebLevel::FULL, true);
  CHECK(n.kind == CelebKind::DUNK);
  CHECK_EQ(n.text, "BUF DUNK!");
  CHECK_EQ(n.color, celeb_bright(0x00338D));
  CHECK_EQ(plan(a, two(52, 48, "Alley Oop Dunk Shot"), CelebLevel::FULL, true).text, "KC ALLEY-OOP!");
  CHECK(plan(a, two(50, 50, "Running Dunk Shot", "", 2, 2), CelebLevel::FULL, true, false).kind == CelebKind::NONE);
  // Subtle and Off: a dunk shows nothing, as any basket.
  CHECK(plan(a, two(52, 48, "Running Dunk Shot"), CelebLevel::SUBTLE).kind == CelebKind::NONE);
  CHECK(plan(a, two(52, 48, "Running Dunk Shot"), CelebLevel::OFF).kind == CelebKind::NONE);
  // Big plays off: no dunk, and every score and win keeps its show.
  CHECK(plan_big(a, two(52, 48, "Running Dunk Shot"), CelebLevel::FULL, false, true, false).kind == CelebKind::NONE);
  CHECK(plan_big(a, three(53, 48), CelebLevel::FULL, false, true, false).kind == CelebKind::THREE);
  CHECK(plan_big(live(Sport::BASKETBALL, 99, 98), final_(Sport::BASKETBALL, 99, 98), CelebLevel::FULL, false, true,
                 false)
            .kind == CelebKind::WIN);
  CHECK(plan_big(live(Sport::FOOTBALL, 0, 0), live(Sport::FOOTBALL, 7, 0), CelebLevel::FULL, false, true, false).kind ==
        CelebKind::TOUCHDOWN);
  // Not in progress (a final read with a dunk last): the win, not a dunk.
  GameSnapshot fin = two(100, 98, "Running Dunk Shot");
  fin.state = GameState::POST;
  fin.completed = true;
  CHECK(plan(live(Sport::BASKETBALL, 98, 98), fin, CelebLevel::FULL).kind == CelebKind::WIN);
  // Another game's read: nothing.
  GameSnapshot other = two(52, 48, "Running Dunk Shot");
  other.event_id = "402";
  CHECK(plan(a, other, CelebLevel::FULL).kind == CelebKind::NONE);
  // The latch: a redraw of the same read shows it once; a stale read that
  // steps back and then forward to the same play stays quiet; the next dunk
  // shows. So a bench loop that serves the same play again shows nothing:
  // each repeat needs its own play.
  CelebLatch latch;
  GameSnapshot d1 = two(52, 48, "Running Dunk Shot", "Victor Wembanyama makes running dunk");
  CHECK(celeb_latched(plan(a, d1, CelebLevel::FULL).kind) && latch.take(d1));
  CHECK(!latch.take(d1));
  CHECK(plan(d1, a, CelebLevel::FULL).kind == CelebKind::NONE);  // the stale read
  CHECK(plan(a, d1, CelebLevel::FULL).kind == CelebKind::DUNK && !latch.take(d1));
  GameSnapshot d2 = two(54, 48, "Running Dunk Shot", "Victor Wembanyama makes running dunk");
  CHECK(plan(d1, d2, CelebLevel::FULL).kind == CelebKind::DUNK && latch.take(d2));
  // A play that scores nothing at the same score is a new play by its words.
  GameSnapshot d3 = d2;
  d3.last_play = "Stephon Castle steals the ball";
  CHECK(latch.take(d3));
}

static std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Recorded play-by-play from three NBA games (11/7), replayed the way the
// panel reads a game: every 10 seconds, the newest play as the read's last
// play with the score after it, through the firmware's own parser. Every
// show has to be a made dunk or alley-oop by that side; Subtle, Off and Big
// plays off show none; a redraw never repeats one.
static void test_basketball_recorded() {
  JsonDocument plays;
  CHECK(!deserializeJson(plays, slurp("fixtures/plays_nba_20261107.json")));
  JsonDocument tmpl;
  CHECK(!deserializeJson(tmpl, slurp("fixtures/event_nba_live.json"), DeserializationOption::NestingLimit(40)));
  int shown[3] = {0, 0, 0}, made[2] = {0, 0};  // dunk, alley-oop, three
  for (JsonObjectConst g : plays["games"].as<JsonArrayConst>()) {
    JsonArrayConst list = g["plays"];
    for (const char *side : {"home", "away"}) {
      const uint32_t our_id = g[side]["id"].as<uint32_t>();
      for (JsonArrayConst pl : list)
        if (pl[4].as<uint32_t>() == our_id && pl[3].as<int>() == 2) {
          const std::string t = pl[1].as<std::string>();
          if (celeb_has_word(t, "alley oop"))
            made[1]++;
          else if (celeb_has_word(t, "dunk"))
            made[0]++;
        }
      GameSnapshot prev;
      CelebLatch latch;
      const int64_t t0 = list[0][0].as<int64_t>(), t1 = list[list.size() - 1][0].as<int64_t>();
      size_t i = 0;
      for (int64_t tick = t0; tick <= t1; tick += 10) {
        while (i + 1 < list.size() && list[i + 1][0].as<int64_t>() <= tick)
          i++;
        JsonArrayConst pl = list[i];
        JsonDocument doc;
        doc.set(tmpl.as<JsonObjectConst>());
        JsonObject comp = doc["competitions"][0];
        JsonArray sides = comp["competitors"];
        for (JsonObject c : sides) {
          const bool home = c["homeAway"] == "home";
          JsonObjectConst t = g[home ? "home" : "away"];
          const std::string id = std::to_string(t["id"].as<uint32_t>());
          c["id"] = id;
          c["team"]["id"] = id;
          c["team"]["abbreviation"] = t["abbr"];
          c["team"]["color"] = t["color"];
          c["score"] = std::to_string(pl[home ? 5 : 6].as<int>());
        }
        doc["id"] = g["event"];
        JsonObject lp = comp["situation"]["lastPlay"];
        lp["id"] = std::to_string(1000 + i);  // each play its own id, as ESPN gives
        lp["type"]["text"] = pl[1];
        lp["text"] = pl[2];
        lp["scoreValue"] = pl[3];
        lp["team"]["id"] = std::to_string(pl[4].as<uint32_t>());
        std::string json;
        serializeJson(doc, json);
        GameSnapshot cur;
        if (!parse_event_str(json, Sport::BASKETBALL, our_id, cur)) {
          CHECK(false);
          break;
        }
        const Splash s = decide_splash(prev, cur, true, false);
        const CelebPlan p = decide_celebration(prev, cur, s, CelebLevel::FULL, false, true, true);
        if (celeb_latched(p.kind)) {
          CHECK(latch.take(cur));
          CHECK(!latch.take(cur));  // the redraw
        }
        if (p.kind == CelebKind::DUNK || p.kind == CelebKind::ALLEY_OOP) {
          // Ours, made, two points, and the kind ESPN named.
          CHECK_EQ(cur.play_team, our_id);
          CHECK_EQ((int) cur.play_value, 2);
          CHECK_EQ(cur.team_score - prev.team_score, 2);
          const bool oop = celeb_has_word(cur.play_type, "alley oop");
          CHECK(oop == (p.kind == CelebKind::ALLEY_OOP));
          CHECK(oop || celeb_has_word(cur.play_type, "dunk"));
          shown[p.kind == CelebKind::DUNK ? 0 : 1]++;
        }
        if (p.kind == CelebKind::THREE)
          shown[2]++;
        // Subtle, Off and Big plays off never show one; the three keeps its
        // show with Big plays off.
        CHECK(!celeb_is_art(decide_celebration(prev, cur, s, CelebLevel::SUBTLE, false, true, true).kind));
        CHECK(decide_celebration(prev, cur, s, CelebLevel::OFF, false, true, true).kind == CelebKind::NONE);
        const CelebPlan off = decide_celebration(prev, cur, s, CelebLevel::FULL, false, true, false);
        CHECK(!celeb_is_art(off.kind));
        CHECK((off.kind == CelebKind::THREE) == (p.kind == CelebKind::THREE));
        prev = cur;
      }
    }
  }
  printf("  recorded NBA: %d of %d dunks and %d of %d alley-oops shown at 10 s reads, %d threes\n", shown[0], made[0],
         shown[1], made[1], shown[2]);
  // Most show; a basket another play follows inside the same 10 s is one the
  // panel never reads.
  CHECK(shown[0] * 10 >= made[0] * 6 && shown[0] <= made[0]);
  CHECK(shown[1] * 10 >= made[1] * 6 && shown[1] <= made[1]);
}

// The packed art: every play at both widths, sized for its board, its runs
// covering exactly one frame each, the first frame whole, and the frames
// lasting as long as the show.
static void test_art() {
  for (CelebKind k : {CelebKind::DUNK, CelebKind::ALLEY_OOP, CelebKind::GRAND_SLAM, CelebKind::WALK_OFF}) {
    for (int w : {128, 64}) {
      const CelebArt *a = celeb_art_for(k, w);
      CHECK(a != nullptr);
      if (a == nullptr)
        continue;
      CHECK_EQ((int) a->w, w);
      CHECK_EQ((int) a->h, 64);
      CHECK(a->frames > 0 && a->fps > 0);
      const uint32_t ms = (uint32_t) a->frames * 1000 / a->fps, show = sample(k).duration_ms;
      CHECK(ms <= show && ms + 1000 / a->fps >= show);
      for (int n = 0; n < a->frames; n++) {
        const uint8_t *q = a->data + a->offsets[n], *end = a->data + a->offsets[n + 1];
        int px = 0;
        bool keeps = false;
        while (q < end) {
          keeps |= (*q >> 4) == kArtKeep;
          int len = (*q & 0x0F) + 1;
          q++;
          if (len == 16)
            len = 16 + *q++;
          px += len;
        }
        CHECK_EQ(px, (int) a->w * a->h);
        CHECK(q == end);
        if (n == 0)
          CHECK(!keeps);
      }
    }
  }
  CHECK(celeb_art_for(CelebKind::THREE, 128) == nullptr);
}

static void test_frame() {
  uint16_t buf[8 * 4];
  memset(buf, 0xAA, sizeof(buf));
  CelebFrame f{buf, 8, 4, 8};
  f.clear();
  CHECK_EQ(f.lit(), 0);
  // Out of bounds writes land nowhere.
  f.put(-1, 0, 0xFFFFFF);
  f.put(8, 0, 0xFFFFFF);
  f.put(0, 4, 0xFFFFFF);
  f.put(0, -1, 0xFFFFFF);
  CHECK_EQ(f.lit(), 0);
  f.put(7, 3, 0xFFFFFF);
  CHECK_EQ(f.lit(), 1);
  CHECK_EQ(buf[3 * 8 + 7], 0xFFFFu);
  // glow keeps the brighter channel.
  f.put(0, 0, 0xFF0000);
  f.glow(0, 0, 0x0000FF);
  CHECK_EQ(buf[0], rgb565(0xFF00FF));
  f.glow(0, 0, 0x400000);
  CHECK_EQ(buf[0], rgb565(0xFF00FF));
  f.clear();
  f.line(0, 0, 7, 3, 0x00FF00);
  CHECK(f.lit() >= 8);
  f.clear();
  f.disc(4, 2, 1, 0xFFFFFF);
  CHECK_EQ(f.lit(), 5);
  f.clear(0x102030);
  CHECK_EQ(f.lit(), 32);
  // A stride wider than the width leaves the padding alone.
  uint16_t wide[8 * 2];
  memset(wide, 0xAA, sizeof(wide));
  CelebFrame g{wide, 4, 2, 8};
  g.clear();
  CHECK_EQ(wide[4], 0xAAAAu);
  CHECK_EQ(wide[0], 0u);
}

static void test_particles() {
  // A burst: every spark falls, fades and dies within two seconds; the pool
  // never overflows however many bursts stack up.
  Celebration c;
  c.start(sample(CelebKind::TOUCHDOWN), 128, 64, 7);
  CHECK(c.particles() > 0);
  CHECK(c.particles() <= kCelebParticles);
  CHECK(c.running());
  int most = 0;
  for (int i = 0; i < 30 * 6; i++) {
    c.step(1.0f / 30);
    most = c.particles() > most ? c.particles() : most;
    CHECK(c.particles() <= kCelebParticles);
  }
  CHECK(most > 40);                           // rockets kept the sky busy
  CHECK(!c.running());                        // 6 s past a 5 s show
  CHECK_EQ(c.particles(), 0);                 // and everything burned out
  // Confetti never dies and never leaves the board for long.
  Celebration w;
  w.start(sample(CelebKind::WIN), 128, 64, 3);
  int n = w.particles();
  CHECK(n > 100);
  for (int i = 0; i < 30 * 10; i++)
    w.step(1.0f / 30);
  CHECK_EQ(w.particles(), n);
  // The same seed gives the same show on the panel and on the host.
  uint16_t a[128 * 64], b[128 * 64];
  CelebFrame fa{a, 128, 64, 128}, fb{b, 128, 64, 128};
  Celebration c1, c2;
  c1.start(sample(CelebKind::TOUCHDOWN), 128, 64, 11);
  c2.start(sample(CelebKind::TOUCHDOWN), 128, 64, 11);
  for (int i = 0; i < 20; i++) {
    c1.step(1.0f / 30);
    c2.step(1.0f / 30);
  }
  c1.render(fa);
  c2.render(fb);
  CHECK(memcmp(a, b, sizeof(a)) == 0);
  c2.start(sample(CelebKind::TOUCHDOWN), 128, 64, 12);
  for (int i = 0; i < 20; i++)
    c2.step(1.0f / 30);
  c2.render(fb);
  CHECK(memcmp(a, b, sizeof(a)) != 0);
}

// Every animated kind, on both board widths: something lit on every frame
// of its run, nothing lit after the fade, and a frame's step plus render
// well under a 33 ms budget even on a slow host.
static void test_every_kind_runs() {
  static uint16_t buf[128 * 64];
  const CelebKind kinds[] = {CelebKind::TOUCHDOWN, CelebKind::WAVE,  CelebKind::HOME_RUN, CelebKind::GOAL_NET,
                             CelebKind::GOAL_LAMP, CelebKind::THREE, CelebKind::WIN,      CelebKind::FLASH,
                             CelebKind::MUTED,     CelebKind::DUNK,  CelebKind::ALLEY_OOP};
  for (int w : {128, 64}) {
    for (CelebKind k : kinds) {
      CelebFrame f{buf, w, 64, w};
      Celebration c;
      CelebPlan p = sample(k);
      c.start(p, w, 64, 5);
      int frames = (int) (p.duration_ms / 1000.0f * 30) + 1;
      int dark = 0, worst_us = 0;
      double total_us = 0;
      for (int i = 0; i < frames; i++) {
        auto t0 = std::chrono::steady_clock::now();
        c.step(1.0f / 30);
        c.render(f);
        auto t1 = std::chrono::steady_clock::now();
        int us = (int) std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        total_us += us;
        worst_us = us > worst_us ? us : worst_us;
        // The last 0.6 s fades out; the final frame may be all black.
        if (f.lit() == 0 && c.running() && c.elapsed() < p.duration_ms / 1000.0f - 0.1f)
          dark++;
      }
      if (dark != 0)
        printf("kind %d at %d wide: %d dark frames\n", (int) k, w, dark);
      CHECK_EQ(dark, 0);
      CHECK(worst_us < 20000);
      printf("  kind %d %3dx64: %d frames, avg %.0f us, worst %d us\n", (int) k, w, frames, total_us / frames,
             worst_us);
      // Two frames past the end the show says it is over.
      c.step(1.0f / 30);
      c.step(1.0f / 30);
      CHECK(!c.running());
    }
  }
}

int main() {
  test_levels();
  test_colors();
  test_football();
  test_win();
  test_baseball();
  test_hockey_soccer();
  test_basketball();
  test_basketball_big_plays();
  test_basketball_recorded();
  test_art();
  test_text_place();
  test_frame();
  test_particles();
  test_every_kind_runs();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
