// Host tests for football's big plays (celeb_football.h) on recorded ESPN
// reads: each play read the way the panel reads it (scoreboard/<id>, the
// read before it and the read with it), from both sides, in the neutral
// modes, on Subtle and Off and with Big plays off, and that every play has
// frames at both widths that last as long as its show. Build with `make` in
// tests/.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "celeb_art_index.h"
#include "celebrate.h"

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

#define CHECK_EQ(a, b)                                              \
  do {                                                              \
    checks++;                                                       \
    if (!((a) == (b))) {                                            \
      failures++;                                                   \
      printf("FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
    }                                                               \
  } while (0)

static std::string slurp(const std::string &name) {
  std::ifstream f("fixtures/" + name);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// One recorded play: the read before it and the read with it, from one
// team's side.
struct Play {
  GameSnapshot before, with;
};

static Play load(const char *name, uint32_t side) {
  Play p;
  const std::string base = std::string("event_nfl_") + name;
  if (!parse_event_str(slurp(base + "_before.json"), Sport::FOOTBALL, side, p.before) ||
      !parse_event_str(slurp(base + ".json"), Sport::FOOTBALL, side, p.with)) {
    failures++;
    printf("FAIL: can't read the %s fixtures for team %u\n", name, (unsigned) side);
  }
  return p;
}

// What the firmware does for one poll: decide_splash, then the celebration.
static CelebPlan plan(const GameSnapshot &prev, const GameSnapshot &cur, CelebLevel level = CelebLevel::FULL,
                      bool neutral = false, bool opp_splashes = true, bool big_plays = true) {
  Splash s = decide_splash(prev, cur, opp_splashes, neutral);
  return decide_celebration(prev, cur, s, level, neutral, opp_splashes, big_plays);
}

// The play from the defense's side, the offense's, and neutral: the
// defense celebrates; the offense sees nothing (no score moved); neutral
// shows the defense's play in its colors with its abbreviation, unless
// Opponent scores too is off and the defense is the "other" side.
static void check_both_sides(const char *name, uint32_t defense, uint32_t offense, CelebKind kind,
                             const char *words, uint32_t ms) {
  printf("  %s\n", name);
  Play d = load(name, defense), o = load(name, offense);
  CelebPlan p = plan(d.before, d.with);
  CHECK(p.kind == kind);
  CHECK_EQ(p.text, std::string(words));
  CHECK_EQ(p.duration_ms, ms);
  CHECK_EQ(p.color, celeb_bright(parse_color(d.with.team_color)));
  CHECK(plan(o.before, o.with).kind == CelebKind::NONE);
  // Neutral, either side's view: the defense's play.
  for (Play *v : {&d, &o}) {
    CelebPlan n = plan(v->before, v->with, CelebLevel::FULL, true);
    CHECK(n.kind == kind);
    CHECK_EQ(n.text, d.with.team_abbr + " " + words);
    CHECK_EQ(n.color, celeb_bright(parse_color(d.with.team_color)));
  }
  // Neutral with Opponent scores too off: only the side the panel calls
  // ours (the one it was given) celebrates.
  CHECK(plan(d.before, d.with, CelebLevel::FULL, true, false).kind == kind);
  CHECK(plan(o.before, o.with, CelebLevel::FULL, true, false).kind == CelebKind::NONE);
  // Subtle, Off and Big plays off: a play that scores nothing shows nothing.
  CHECK(plan(d.before, d.with, CelebLevel::SUBTLE).kind == CelebKind::NONE);
  CHECK(plan(d.before, d.with, CelebLevel::OFF).kind == CelebKind::NONE);
  CHECK(plan(d.before, d.with, CelebLevel::FULL, false, true, false).kind == CelebKind::NONE);
  // A redraw of the same read is not a new play.
  CHECK(plan(d.with, d.with).kind == CelebKind::NONE);
  // The read before it is no play at all.
  CHECK(football_big_play(d.before.play_type) == FootballBigPlay::NONE ||
        plan(d.before, d.before).kind == CelebKind::NONE);
}

static void test_types() {
  // Every turnover and sack type in 14 NFL games (11/1 and 11/8) and 20 FBS
  // games (11/7).
  CHECK(football_big_play("Sack") == FootballBigPlay::SACK);
  CHECK(football_big_play("Pass Interception Return") == FootballBigPlay::INTERCEPTION);
  CHECK(football_big_play("Interception") == FootballBigPlay::INTERCEPTION);  // college, downed in the end zone
  CHECK(football_big_play("Interception Return Touchdown") == FootballBigPlay::PICK_SIX);
  CHECK(football_big_play("Fumble Recovery (Opponent)") == FootballBigPlay::FUMBLE);
  CHECK(football_big_play("Sack Opp Fumble Recovery") == FootballBigPlay::FUMBLE);
  CHECK(football_big_play("Fumble Recovery (Own)") == FootballBigPlay::NONE);
  CHECK(football_big_play("Fumble") == FootballBigPlay::NONE);  // college: the offense kept it
  CHECK(football_big_play("Fumble Return Touchdown") == FootballBigPlay::NONE);
  CHECK(football_big_play("Safety") == FootballBigPlay::NONE);
  CHECK(football_big_play("Rush") == FootballBigPlay::NONE);
  CHECK(football_big_play("Pass Reception") == FootballBigPlay::NONE);
  CHECK(football_big_play("") == FootballBigPlay::NONE);
}

static void test_recorded_plays() {
  // NYJ at KC, 11/8, 4th quarter: KC's C.Jones sacks G.Smith at the NYJ 14.
  check_both_sides("sack", 12, 20, CelebKind::SACK, "SACK!", kCelebSackMs);
  // DAL at IND, 11/8, 3rd quarter: Q.Williams strips J.Taylor and DAL's
  // S.James recovers. The read gives the ball (and the play) to DAL.
  check_both_sides("fumble", 6, 11, CelebKind::FUMBLE, "FUMBLE!", kCelebFumbleMs);
}

static void test_other_sports_quiet() {
  // The same play types in another sport's read are not football's.
  Play d = load("sack", 12);
  d.before.sport = d.with.sport = Sport::BASKETBALL;
  CHECK(plan(d.before, d.with).kind != CelebKind::SACK);
}

// Every football play has frames at both widths, sized for the board, and
// its show lasts exactly as long as its frames.
static void test_art() {
  const struct {
    CelebKind kind;
    uint32_t ms;
  } plays[] = {{CelebKind::INTERCEPTION, kCelebInterceptionMs},
               {CelebKind::PICK_SIX, kCelebPickSixMs},
               {CelebKind::FUMBLE, kCelebFumbleMs},
               {CelebKind::SACK, kCelebSackMs}};
  for (const auto &pl : plays) {
    CHECK(celeb_is_art(pl.kind));
    for (int w : {128, 64}) {
      const CelebArt *a = celeb_art_for(pl.kind, w);
      CHECK(a != nullptr);
      if (!a)
        continue;
      CHECK_EQ((int) a->w, w);
      CHECK_EQ((int) a->h, 64);
      CHECK_EQ((uint32_t) a->frames * 1000u / a->fps, pl.ms);
      // Every frame decodes to exactly the board's pixels: no run runs past
      // the end, none stops short.
      for (int n = 0; n < a->frames; n++) {
        const uint8_t *p = a->data + a->offsets[n], *end = a->data + a->offsets[n + 1];
        int px = 0;
        while (p < end) {
          uint8_t b = *p++;
          int len = (b & 0x0F) + 1;
          if (len == 16 && p < end)
            len = 16 + *p++;
          px += len;
        }
        if (px != w * 64) {
          failures++;
          printf("FAIL: kind %d at %d wide, frame %d decodes %d pixels\n", (int) pl.kind, w, n, px);
        }
        checks++;
      }
    }
  }
}

int main() {
  test_types();
  test_recorded_plays();
  test_other_sports_quiet();
  test_art();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
