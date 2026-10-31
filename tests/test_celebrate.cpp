// Host tests for the celebrations: which one a score gets per sport, side
// and setting (celebrate.h), and the animation engine's math (celebrate_fx.h):
// particles stay in the pool, fall, fade out, every kind draws something on
// every frame of its run, and a frame costs little. Build with `make` in tests/.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
}

static CelebPlan sample(CelebKind k) {
  CelebPlan p;
  p.kind = k;
  p.color = 0xE31837;
  p.alt = 0xFFFFFF;
  p.text = "TEST";
  p.duration_ms = k == CelebKind::WIN ? kCelebWinMs : k == CelebKind::TOUCHDOWN ? kCelebTouchdownMs : 4500;
  return p;
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
                             CelebKind::MUTED};
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
  test_text_place();
  test_frame();
  test_particles();
  test_every_kind_runs();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
