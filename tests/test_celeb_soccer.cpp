// Host tests for soccer's big plays (celeb_soccer.h): a brace, a hat trick,
// a penalty and a late goal, read from recorded ESPN games one goal at a
// time, the way the panel's reads see them. Build with `make` in tests/.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "celeb_soccer.h"

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

#define CHECK_EQ(a, b)                                                                         \
  do {                                                                                         \
    checks++;                                                                                  \
    if (!((a) == (b))) {                                                                       \
      failures++;                                                                              \
      printf("FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b);                            \
    }                                                                                          \
  } while (0)

static std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static std::string root_id(const std::string &json) {
  size_t k = json.find("\"id\"");
  size_t a = json.find('"', json.find(':', k) + 1), b = json.find('"', a + 1);
  return json.substr(a + 1, b - a - 1);
}

static GameSnapshot recorded(const char *file, uint32_t team) {
  GameSnapshot s;
  std::string json = slurp(file);
  CHECK(parse_event_str(json, Sport::SOCCER, team, s, root_id(json)));
  return s;
}

// The live read just after the game's first n goals: the goal list cut
// there and the score counted from it, in the given period.
static GameSnapshot after_goals(const GameSnapshot &full, size_t n, int period) {
  GameSnapshot s = full;
  s.state = GameState::IN;
  s.completed = false;
  s.period = period;
  s.soc.goals.resize(n);
  s.team_score = soccer_side_goals(s.soc, true);
  s.opp_score = soccer_side_goals(s.soc, false);
  s.soc.team_shootout = s.soc.opp_shootout = -1;
  return s;
}

// What goal n (1 is the first) brings, read by the panel in that period.
static SoccerBigPlay goal_play(const GameSnapshot &full, size_t n, int period, bool neutral = false,
                               bool opp = true) {
  GameSnapshot prev = after_goals(full, n - 1, period), cur = after_goals(full, n, period);
  return soccer_big_play(prev, cur, neutral, opp);
}

static void test_minutes() {
  CHECK_EQ(soccer_goal_minute("89'"), 89);
  CHECK_EQ(soccer_goal_minute("90'+7'"), 97);
  CHECK_EQ(soccer_goal_minute("45'+3'"), 48);
  CHECK_EQ(soccer_goal_minute("120'+1'"), 121);
  CHECK_EQ(soccer_goal_minute(""), 0);
  CHECK(soccer_late_minute(85, 2));
  CHECK(soccer_late_minute(97, 2));
  CHECK(!soccer_late_minute(84, 2));
  CHECK(!soccer_late_minute(48, 1));  // first-half stoppage
  CHECK(!soccer_late_minute(103, 3));
  CHECK(!soccer_late_minute(111, 4));
  CHECK(soccer_late_minute(118, 4));
  CHECK_EQ(soccer_caption_name("Dreyer", 12), std::string("DREYER"));
  CHECK_EQ(soccer_caption_name("Lopes Cabral", 8), std::string("CABRAL"));
  CHECK_EQ(soccer_caption_name("Alan Patrick", 12), std::string("ALAN PATRICK"));
}

// MLS, San Diego at Miami: Dreyer scores at 12' and again at 82'.
static void test_brace() {
  const uint32_t sd = 22529, mia = 20232;
  GameSnapshot g = recorded("fixtures/event_mls_final.json", sd);
  CHECK_EQ(g.soc.goals.size(), (size_t) 4);
  CHECK(goal_play(g, 1, 1).play == SoccerPlay::NONE);  // his first
  CHECK(goal_play(g, 2, 1).play == SoccerPlay::NONE);  // Miami's
  CHECK(goal_play(g, 3, 2).play == SoccerPlay::NONE);
  SoccerBigPlay b = goal_play(g, 4, 2);
  CHECK(b.play == SoccerPlay::BRACE);
  CHECK(b.ours);
  CHECK_EQ(b.words, std::string("DREYER"));
  // Miami's panel: the other side's brace is theirs, so nothing extra (the
  // muted goal flash, as before); in a neutral mode it is shown.
  GameSnapshot m = recorded("fixtures/event_mls_final.json", mia);
  CHECK(goal_play(m, 4, 2).play == SoccerPlay::NONE);
  CHECK(goal_play(m, 4, 2, true).play == SoccerPlay::BRACE);
  CHECK(!goal_play(m, 4, 2, true).ours);
  CHECK(goal_play(m, 4, 2, true, false).play == SoccerPlay::NONE);  // Opponent scores too off
}

// Premier League, Arsenal at Sunderland: a 90'+7' penalty to make it 2-0.
// Late, but not a go-ahead goal, so it is the penalty.
static void test_penalty() {
  GameSnapshot a = recorded("fixtures/event_epl_final.json", 359);
  CHECK(goal_play(a, 1, 2).play == SoccerPlay::NONE);
  SoccerBigPlay p = goal_play(a, 2, 2);
  CHECK(p.play == SoccerPlay::PENALTY);
  CHECK(p.words.empty());
  // PSG v Arsenal: Dembele's 65' penalty; the shootout kicks are not goals.
  GameSnapshot psg = recorded("fixtures/event_soccer_pens.json", 160);
  CHECK_EQ(psg.soc.goals.size(), (size_t) 2);
  CHECK(goal_play(psg, 2, 2).play == SoccerPlay::PENALTY);
  GameSnapshot ars = recorded("fixtures/event_soccer_pens.json", 359);
  CHECK(goal_play(ars, 2, 2).play == SoccerPlay::NONE);
  CHECK(goal_play(ars, 2, 2, true).play == SoccerPlay::PENALTY);
  // A read in the shootout never shows one, whatever it holds.
  GameSnapshot prev = after_goals(psg, 1, 5), cur = after_goals(psg, 2, 5);
  CHECK(soccer_big_play(prev, cur, false, true).play == SoccerPlay::NONE);
}

// Argentina v Cape Verde: Martinez's 92' winner at 1-1 is a late goal;
// Cape Verde's 103' equalizer and the 111' own goal in extra time are not.
static void test_late_goal() {
  GameSnapshot arg = recorded("fixtures/event_soccer_aet.json", 202);
  CHECK_EQ(arg.soc.goals.size(), (size_t) 5);
  CHECK(goal_play(arg, 1, 1).play == SoccerPlay::NONE);
  CHECK(goal_play(arg, 2, 2).play == SoccerPlay::NONE);
  SoccerBigPlay l = goal_play(arg, 3, 2);
  CHECK(l.play == SoccerPlay::LATE_GOAL);
  CHECK_EQ(l.words, std::string("92'"));
  CHECK(goal_play(arg, 4, 3, true).play == SoccerPlay::NONE);  // a level 2-2, not ahead
  CHECK(goal_play(arg, 5, 4).play == SoccerPlay::NONE);        // 111' is not the last five of extra time
  // The same goal in extra time's last minutes would be.
  GameSnapshot late = arg;
  late.soc.goals[4].clock = "118'";
  CHECK(goal_play(late, 5, 4).play == SoccerPlay::LATE_GOAL);
}

static GameSnapshot made_up(std::vector<SoccerGoal> goals, int period) {
  GameSnapshot s;
  s.valid = true;
  s.state = GameState::IN;
  s.sport = Sport::SOCCER;
  s.event_id = "7";
  s.team_abbr = "ARS";
  s.opp_abbr = "CHE";
  s.team_color = "EF0107";
  s.opp_color = "034694";
  s.period = period;
  s.soc.goals = std::move(goals);
  s.team_score = soccer_side_goals(s.soc, true);
  s.opp_score = soccer_side_goals(s.soc, false);
  return s;
}

static SoccerGoal goal(const char *clock, const char *name, bool ours, bool pen = false, bool og = false) {
  SoccerGoal g;
  g.clock = clock;
  g.name = name;
  g.ours = ours;
  g.penalty = pen;
  g.own_goal = og;
  return g;
}

static void test_made_up() {
  // A hat trick, its third a penalty: the hat trick wins.
  std::vector<SoccerGoal> gs = {goal("10'", "Saka", true), goal("30'", "Saka", true),
                                goal("50'", "Saka", true, true)};
  GameSnapshot two = made_up({gs[0], gs[1]}, 2), three = made_up(gs, 2);
  SoccerBigPlay h = soccer_big_play(two, three, false, true);
  CHECK(h.play == SoccerPlay::HAT_TRICK);
  CHECK_EQ(h.words, std::string("SAKA"));
  // A fourth goal is not a second hat trick.
  std::vector<SoccerGoal> g4 = gs;
  g4.push_back(goal("60'", "Saka", true));
  CHECK(soccer_big_play(three, made_up(g4, 2), false, true).play == SoccerPlay::NONE);
  // A late penalty that wins it at 1-1: the late goal wins.
  GameSnapshot level = made_up({goal("20'", "Saka", true), goal("40'", "Palmer", false)}, 2);
  GameSnapshot pen = made_up({goal("20'", "Saka", true), goal("40'", "Palmer", false),
                              goal("88'", "Odegaard", true, true)},
                             2);
  SoccerBigPlay lp = soccer_big_play(level, pen, false, true);
  CHECK(lp.play == SoccerPlay::LATE_GOAL);
  CHECK_EQ(lp.words, std::string("88'"));
  // An own goal never makes a brace for the player who put it in, and is
  // never a penalty; a late own goal that wins it still is a late goal.
  GameSnapshot og1 = made_up({goal("20'", "Saka", true)}, 2);
  GameSnapshot og2 = made_up({goal("20'", "Saka", true), goal("70'", "Saka", true, true, true)}, 2);
  CHECK(soccer_big_play(og1, og2, false, true).play == SoccerPlay::NONE);
  GameSnapshot ogl = made_up({goal("20'", "Saka", true), goal("40'", "Palmer", false),
                              goal("90'+2'", "James", true, false, true)},
                             2);
  CHECK(soccer_big_play(level, ogl, false, true).play == SoccerPlay::LATE_GOAL);
  // A goal at 45'+3' in the first half is not late.
  GameSnapshot fh0 = made_up({goal("20'", "Palmer", false)}, 1);
  GameSnapshot fh1 = made_up({goal("20'", "Palmer", false), goal("45'+3'", "Saka", true)}, 1);
  CHECK(soccer_big_play(fh0, fh1, false, true).play == SoccerPlay::NONE);
  // ESPN's goal list a read behind the score: a plain goal now, and no
  // big play later when the list catches up with the score unchanged.
  GameSnapshot lag = two;
  lag.team_score = 3;
  CHECK(soccer_big_play(two, lag, false, true).play == SoccerPlay::NONE);
  CHECK(soccer_big_play(lag, three, false, true).play == SoccerPlay::NONE);
  // Another game, a break or the final: nothing.
  GameSnapshot other = three;
  other.event_id = "8";
  CHECK(soccer_big_play(two, other, false, true).play == SoccerPlay::NONE);
  GameSnapshot ft = three;
  ft.state = GameState::POST;
  CHECK(soccer_big_play(two, ft, false, true).play == SoccerPlay::NONE);
  // The plan: the play's kind, the team's color and the caption in line2.
  CelebPlan p = big_play_soccer(two, three, Splash{}, false, true);
  CHECK(p.kind == CelebKind::HAT_TRICK);
  CHECK_EQ(p.text, std::string("HAT TRICK!"));
  CHECK_EQ(p.line2, std::string("SAKA"));
  CHECK_EQ(p.color, celeb_bright(parse_color("EF0107")));
  CHECK(p.duration_ms > 0);
  CHECK(big_play_soccer(og1, og2, Splash{}, false, true).kind == CelebKind::NONE);
}

int main() {
  test_minutes();
  test_brace();
  test_penalty();
  test_late_goal();
  test_made_up();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
