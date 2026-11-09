// Host tests for football's big plays (celeb_football.h) on recorded ESPN
// reads: each play read the way the panel reads it (scoreboard/<id>, the
// read before it and the read with it), from both sides, in the neutral
// modes, on Subtle and Off and with Big plays off, and that every play has
// frames at both widths that last as long as its show. Build with `make` in
// tests/.
#include <cstdio>
#include <cstring>
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
  // GB at NE, 11/8: GB's X.McKinney intercepts D.Maye at midfield and is
  // pushed out at the NE 14. The read gives the ball (and the play) to GB.
  check_both_sides("interception", 9, 17, CelebKind::INTERCEPTION, "INTERCEPTION!", kCelebInterceptionMs);
}

// No live pick-six came up (1 in 25 games): the recorded interception, made
// one. ESPN types it "Interception Return Touchdown" and the score goes up 6
// in the same read, which decide_splash calls a touchdown.
static void test_pick_six() {
  Play gb = load("interception", 9), ne = load("interception", 17);
  gb.with.play_type = ne.with.play_type = "Interception Return Touchdown";
  gb.with.team_score += 6;
  ne.with.opp_score += 6;
  CelebPlan p = plan(gb.before, gb.with);
  CHECK(p.kind == CelebKind::PICK_SIX);
  CHECK_EQ(p.text, std::string("PICK SIX!"));
  CHECK_EQ(p.duration_ms, kCelebPickSixMs);
  // It takes over the touchdown; Subtle and Big plays off keep the touchdown.
  CHECK(plan(gb.before, gb.with, CelebLevel::SUBTLE).kind == CelebKind::FLASH);
  CHECK(plan(gb.before, gb.with, CelebLevel::FULL, false, true, false).kind == CelebKind::TOUCHDOWN);
  // NE's panel: the other side's touchdown, dim as ever.
  CHECK(plan(ne.before, ne.with).kind == CelebKind::MUTED);
  // Neutral: GB's pick-six from either side.
  CHECK(plan(ne.before, ne.with, CelebLevel::FULL, true).kind == CelebKind::PICK_SIX);
  CHECK_EQ(plan(ne.before, ne.with, CelebLevel::FULL, true).text, std::string("GB PICK SIX!"));
  // An interception with points but not six (a touchdown then the extra
  // point in one read) is the touchdown's, not a pick-six's.
  gb.with.team_score += 1;
  CHECK(plan(gb.before, gb.with).kind == CelebKind::TOUCHDOWN);
  // A plain interception read where a score moved anyway is no big play.
  Play odd = load("interception", 9);
  odd.with.opp_score += 3;
  CHECK(plan(odd.before, odd.with).kind != CelebKind::INTERCEPTION);
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
      CHECK_EQ((uint32_t) a->total_frames() * 1000u / a->fps, pl.ms);
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

// The pick six stores only its end and opens with the interception's own
// frames: the same size, fps and palette, its own frames starting whole, and
// the player drawing exactly the interception's pixels for its first 3 s,
// then its own, and starting over when played again.
static void test_pick_six_lead() {
  static CelebArtPlayer six, pick;
  static uint16_t a[128 * 64], b[128 * 64];
  for (int w : {128, 64}) {
    const CelebArt *p = celeb_art_for(CelebKind::PICK_SIX, w), *i = celeb_art_for(CelebKind::INTERCEPTION, w);
    CHECK(p != nullptr && i != nullptr && p->lead != nullptr);
    if (!p || !i || !p->lead)
      continue;
    CHECK(&p->lead() == i);
    CHECK_EQ((int) p->lead_frames, 36);
    CHECK(p->lead_frames <= i->frames);
    CHECK_EQ((int) p->fps, (int) i->fps);
    CHECK(p->w == i->w && p->h == i->h);
    bool same_palette = true;
    for (int k = 0; k < 16; k++)
      same_palette &= p->palette[k] == i->palette[k];
    CHECK(same_palette);
    // Its own first frame has no "unchanged" runs: it can't lean on the lead.
    bool keeps = false;
    for (const uint8_t *q = p->data; q < p->data + p->offsets[1]; q++) {
      keeps |= (*q >> 4) == kArtKeep;
      if ((*q & 0x0F) == 15)
        q++;
    }
    CHECK(!keeps);
    six.reset();
    pick.reset();
    int lead_same = 0, own_differs = 0;
    for (int n = 0; n < p->total_frames(); n++) {
      const float t = (n + 0.5f) / p->fps;
      six.render(a, w, 64, w, *p, t, 0x00338D, 0xFFFFFF, 1.0f);
      if (n < p->lead_frames) {
        pick.render(b, w, 64, w, *i, t, 0x00338D, 0xFFFFFF, 1.0f);
        lead_same += memcmp(a, b, sizeof(uint16_t) * w * 64) == 0;
      } else if (memcmp(a, b, sizeof(uint16_t) * w * 64) != 0) {
        own_differs++;  // b still holds the interception's frame 35
      }
    }
    CHECK_EQ(lead_same, (int) p->lead_frames);
    CHECK(own_differs >= p->frames - 1);
    // Played again from the start: the interception's first frame again.
    six.render(a, w, 64, w, *p, 0.01f, 0x00338D, 0xFFFFFF, 1.0f);
    pick.render(b, w, 64, w, *i, 0.01f, 0x00338D, 0xFFFFFF, 1.0f);
    CHECK(memcmp(a, b, sizeof(uint16_t) * w * 64) == 0);
  }
}

int main() {
  test_types();
  test_recorded_plays();
  test_pick_six();
  test_other_sports_quiet();
  test_art();
  test_pick_six_lead();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
