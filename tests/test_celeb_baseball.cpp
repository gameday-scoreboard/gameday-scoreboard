// Host tests for baseball's big plays (celeb_baseball.h): the grand slam and
// the walk-off, read from recorded ESPN plays. Each fixture is ESPN's
// scoreboard/<id> read of a real game set back to the moment of the play,
// with ESPN's own lastPlay object from that game's play-by-play:
//   event_mlb_slam*          Soto's grand slam, NYM at DET, 9/1/2025, top 4th
//   event_mlb_walkoff_slam*  Freeman's walk-off grand slam, 2024 World Series
//                            game 1, NYY at LAD, bottom 10th
//   event_mlb_walkoff*       Marte's two-run walk-off single, TOR at CIN,
//                            9/1/2025, bottom 9th
//   event_mlb_triple_error*  Reynolds' bases-loaded triple and an error,
//                            four runs and no home run, CHW at PIT,
//                            4/7/2023, bottom 5th
// The *_final fixtures are the recorded final reads as they are.
// Build with `make` in tests/.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "celeb_baseball.h"
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

static std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static GameSnapshot read(const char *name, uint32_t team) {
  GameSnapshot s;
  const std::string path = std::string("fixtures/") + name + ".json";
  if (!parse_event_str(slurp(path.c_str()), Sport::BASEBALL, team, s)) {
    failures++;
    printf("FAIL: could not read %s\n", path.c_str());
  }
  return s;
}

// The celebration for a read, as the component decides it.
static CelebPlan plan(const GameSnapshot &a, const GameSnapshot &b, CelebLevel level = CelebLevel::FULL,
                      bool neutral = false, bool opp = true, bool big = true) {
  return decide_celebration(a, b, decide_splash(a, b, opp, neutral), level, neutral, opp, big);
}

// The show a read plays first: the walk-off ahead of a win posted with it.
static CelebPlan first_show(const GameSnapshot &a, const GameSnapshot &b, bool neutral = false) {
  CelebPlan p = plan(a, b, CelebLevel::FULL, neutral);
  CelebPlan walk = walk_off_before_win(a, b, p);
  return walk.kind == CelebKind::NONE ? p : walk;
}

constexpr uint32_t kMets = 21, kTigers = 6, kDodgers = 19, kYankees = 10, kReds = 17, kJays = 14, kRays = 30, kPirates = 23,
                   kWhiteSox = 4;

static void test_runners() {
  CHECK_EQ(baseball_runners_scored("J. Soto homered to right center (419 feet), McNeil scored, Mullins scored and "
                                   "Torrens scored."),
           3);
  CHECK_EQ(baseball_runners_scored("Mesa Jr. homered to right center (401 feet), Caminero scored."), 1);
  CHECK_EQ(baseball_runners_scored("Basallo homered to right center (433 feet)."), 0);
  CHECK_EQ(baseball_runners_scored(""), 0);
}

static void test_grand_slam() {
  GameSnapshot a = read("event_mlb_slam_before", kMets), b = read("event_mlb_slam", kMets);
  CHECK(a.state == GameState::IN && b.state == GameState::IN);
  CHECK_EQ(b.play_value, 4);
  CelebPlan p = plan(a, b);
  CHECK(p.kind == CelebKind::GRAND_SLAM);
  CHECK_EQ(p.text, "GRAND SLAM!");
  CHECK_EQ(p.duration_ms, kCelebGrandSlamMs);
  CHECK_EQ(p.color, celeb_bright(parse_color(b.team_color)));
  // Subtle keeps what a home run gets today, Off nothing, and so does Full
  // with Big plays off (the home run's own show).
  CelebPlan sub = plan(a, b, CelebLevel::SUBTLE);
  CHECK(sub.kind == CelebKind::FLASH);
  CHECK_EQ(sub.text, "HOME RUN!");
  CHECK(plan(a, b, CelebLevel::OFF).kind == CelebKind::NONE);
  CelebPlan off = plan(a, b, CelebLevel::FULL, false, true, false);
  CHECK(off.kind == CelebKind::HOME_RUN);
  CHECK_EQ(off.text, "HOME RUN!");
  // A redraw of the same read: nothing moved, nothing plays.
  CHECK(plan(b, b).kind == CelebKind::NONE);
  // The panel's latch stops the same read twice.
  CelebLatch latch;
  CHECK(latch.take(b));
  CHECK(!latch.take(b));

  // The other side's slam: muted on our board, the slam on a neutral one
  // with Opponent scores too on, never with it off.
  GameSnapshot ta = read("event_mlb_slam_before", kTigers), tb = read("event_mlb_slam", kTigers);
  CelebPlan theirs = plan(ta, tb);
  CHECK(theirs.kind == CelebKind::MUTED);
  CHECK_EQ(theirs.text, "NYM HOME RUN");
  CelebPlan neutral = plan(ta, tb, CelebLevel::FULL, true);
  CHECK(neutral.kind == CelebKind::GRAND_SLAM);
  CHECK_EQ(neutral.color, celeb_bright(parse_color(tb.opp_color)));
  CHECK(plan(ta, tb, CelebLevel::FULL, true, false).kind != CelebKind::GRAND_SLAM);

  // ESPN's batter's closing row, no words and no value in this game: four
  // runs, but nothing says a home run, so it stays the runs' sweep.
  GameSnapshot e = read("event_mlb_slam_endbatter", kMets);
  CHECK_EQ(e.play_value, 0);
  CelebPlan runs = plan(a, e);
  CHECK(runs.kind == CelebKind::WAVE);
  CHECK_EQ(runs.text, "4 RUNS!");

  // The words alone, with no value: three runners and the batter.
  GameSnapshot words = b;
  words.play_value = 0;
  CHECK(plan(a, words).kind == CelebKind::GRAND_SLAM);
  words.last_play = "J. Soto homered to right center (419 feet), McNeil scored and Mullins scored.";
  CHECK(plan(a, words).kind != CelebKind::GRAND_SLAM);

  // A two-run home run (a recorded one, TB at NYY): the home run, not a slam.
  GameSnapshot h0 = read("event_mlb_hr_before", kRays), h1 = read("event_mlb_hr", kRays);
  CHECK(plan(h0, h1).kind == CelebKind::HOME_RUN);
}

static void test_walk_off_slam() {
  GameSnapshot a = read("event_mlb_walkoff_slam_before", kDodgers);
  GameSnapshot b = read("event_mlb_walkoff_slam", kDodgers);
  GameSnapshot fin = read("event_mlb_walkoff_slam_final", kDodgers);
  CHECK(a.team_home && a.period == 10 && a.mlb.half == Half::BOTTOM);
  CHECK(fin.state == GameState::POST && fin.completed);
  // The run read before ESPN posts the final: the walk-off, over the slam.
  CelebPlan p = plan(a, b);
  CHECK(p.kind == CelebKind::WALK_OFF);
  CHECK_EQ(p.text, "WALK-OFF!");
  CHECK_EQ(p.duration_ms, kCelebWalkOffMs);
  CHECK_EQ(p.color, celeb_bright(parse_color(b.team_color)));
  // The batter's closing row (value 4, no words) reads the same.
  CHECK(plan(a, read("event_mlb_walkoff_slam_endbatter", kDodgers)).kind == CelebKind::WALK_OFF);
  // Then the final: the win screen on its own, no second walk-off.
  CelebPlan win = first_show(b, fin);
  CHECK(win.kind == CelebKind::WIN);
  CHECK_EQ(win.text, "LAD WINS!");
  CHECK_EQ(win.line2, "6-3");
  // The run and the final in one read: the walk-off first, the win's
  // colors, and the win screen it holds back is the usual one.
  CelebPlan whole = plan(a, fin);
  CHECK(whole.kind == CelebKind::WIN);
  CelebPlan walk = walk_off_before_win(a, fin, whole);
  CHECK(walk.kind == CelebKind::WALK_OFF);
  CHECK_EQ(walk.color, whole.color);
  CHECK_EQ(walk.duration_ms, kCelebWalkOffMs);
  CHECK_EQ(walk.text, "WALK-OFF!");
  // Subtle and Off: what they show today.
  CHECK(plan(a, b, CelebLevel::SUBTLE).kind == CelebKind::FLASH);
  CHECK(plan(a, b, CelebLevel::OFF).kind == CelebKind::NONE);
  CHECK(plan(a, b, CelebLevel::FULL, false, true, false).kind == CelebKind::HOME_RUN);

  // The Yankees' board: their loss stays quiet or muted, never a walk-off.
  GameSnapshot ya = read("event_mlb_walkoff_slam_before", kYankees);
  GameSnapshot yb = read("event_mlb_walkoff_slam", kYankees);
  GameSnapshot yf = read("event_mlb_walkoff_slam_final", kYankees);
  CHECK(!ya.team_home);
  CHECK(plan(ya, yb).kind == CelebKind::MUTED);
  CHECK(first_show(ya, yf).kind == CelebKind::NONE);
  // A neutral board with the Yankees as the first side: the Dodgers' walk-off.
  CelebPlan n = plan(ya, yb, CelebLevel::FULL, true);
  CHECK(n.kind == CelebKind::WALK_OFF);
  CHECK_EQ(n.color, celeb_bright(parse_color(yb.opp_color)));
  CHECK(first_show(ya, yf, true).kind == CelebKind::WALK_OFF);
}

static void test_walk_off_single() {
  GameSnapshot a = read("event_mlb_walkoff_before", kReds);
  GameSnapshot b = read("event_mlb_walkoff", kReds);
  GameSnapshot fin = read("event_mlb_walkoff_final", kReds);
  CHECK(plan(a, b).kind == CelebKind::WALK_OFF);
  CHECK(first_show(a, fin).kind == CelebKind::WALK_OFF);
  CHECK(first_show(b, fin).kind == CelebKind::WIN);
  CHECK(first_show(read("event_mlb_walkoff_before", kJays), read("event_mlb_walkoff_final", kJays)).kind ==
        CelebKind::NONE);

  // Not walk-offs. Taking the lead in the bottom of the 8th:
  GameSnapshot e8a = a, e8b = b;
  e8a.period = e8b.period = 8;
  CHECK(plan(e8a, e8b).kind != CelebKind::WALK_OFF);
  // Tying it in the bottom of the 9th (the game goes on):
  GameSnapshot tie = b;
  tie.team_score = 4;
  CHECK(plan(a, tie).kind != CelebKind::WALK_OFF);
  // Already ahead going into the 9th, final with no bottom half played:
  GameSnapshot ahead = a;
  ahead.team_score = 6;
  ahead.mlb.half = Half::TOP;
  GameSnapshot fin6 = fin;
  fin6.team_score = 6;
  CHECK(first_show(ahead, fin6).kind == CelebKind::WIN);
  // The away side going ahead in the top of the 10th:
  GameSnapshot t10a = read("event_mlb_walkoff_before", kJays), t10b = t10a;
  t10a.team_score = t10a.opp_score = 4;
  t10a.period = t10b.period = 10;
  t10a.mlb.half = t10b.mlb.half = Half::TOP;
  t10b.team_score = 5;
  t10b.opp_score = 4;
  CHECK(plan(t10a, t10b).kind != CelebKind::WALK_OFF);
  // A different game in the next read (the board switched games):
  GameSnapshot other = fin;
  other.event_id = "1";
  CHECK(first_show(a, other).kind != CelebKind::WALK_OFF);
}

// Four runs on one play that isn't a home run: Reynolds' bases-loaded triple
// and a throwing error, CHW at PIT, 4/7/2023, bottom 5th, PIT 8-7 to 12-7.
// ESPN gives the play 4, on its Play Result and on the Error row it posts
// just before; both are the runs' sweep, not the slam.
static void test_four_runs_not_a_slam() {
  GameSnapshot a = read("event_mlb_triple_error_before", kPirates);
  GameSnapshot b = read("event_mlb_triple_error", kPirates);
  GameSnapshot row = read("event_mlb_triple_error_row", kPirates);
  CHECK(a.state == GameState::IN && b.state == GameState::IN);
  CHECK(a.team_home && a.period == 5);
  CHECK_EQ(b.play_value, 4);
  CHECK_EQ(row.play_value, 4);
  CHECK(!baseball_home_run(b) && !baseball_home_run(row));
  CelebPlan p = plan(a, b);
  CHECK(p.kind == CelebKind::WAVE);
  CHECK_EQ(p.text, "4 RUNS!");
  CelebPlan r = plan(a, row);
  CHECK(r.kind == CelebKind::WAVE);
  CHECK_EQ(r.text, "4 RUNS!");
  // The White Sox side on a neutral board with Opponent scores too on.
  GameSnapshot wa = read("event_mlb_triple_error_before", kWhiteSox);
  GameSnapshot wb = read("event_mlb_triple_error", kWhiteSox);
  CHECK(plan(wa, wb, CelebLevel::FULL, true).kind != CelebKind::GRAND_SLAM);

  // ESPN's older rows (2023 and 2024 seasons) put the slam on the pitch:
  // type Home Run, words "Pitch 2 : Ball In Play", value 4. Still the slam.
  GameSnapshot s0 = read("event_mlb_slam_before", kMets), s1 = read("event_mlb_slam", kMets);
  s1.mlb.play_type = "Home Run";
  s1.last_play = "Pitch 2 : Ball In Play";
  CHECK(plan(s0, s1).kind == CelebKind::GRAND_SLAM);
}

// A slam in the bottom of the 9th that only ties: the slam, the game goes on.
static void test_slam_that_ties() {
  GameSnapshot a = read("event_mlb_walkoff_slam_before", kDodgers);
  GameSnapshot b = read("event_mlb_walkoff_slam", kDodgers);
  a.opp_score = b.opp_score = 6;
  CHECK(plan(a, b).kind == CelebKind::GRAND_SLAM);
}

int main() {
  test_runners();
  test_grand_slam();
  test_walk_off_slam();
  test_walk_off_single();
  test_slam_that_ties();
  test_four_runs_not_a_slam();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
