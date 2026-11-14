// Host tests for hockey's big play (celeb_hockey.h) on recorded ESPN reads:
// the hat trick from the game's goal leaders, both sides, the settings that
// quiet it, a power play goal read live (no strength in the read, so the
// plain lamp), and the frames at both board widths. Build with `make` in
// tests/.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "ArduinoJson.h"
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

static std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// DAL @ WSH, 11/3: Robertson's hat trick in a 6-5 DAL win, the final read.
constexpr uint32_t kDal = 9, kWsh = 23, kRobertson = 4233875;

// The recorded final, put back in the 3rd period with Robertson on `goals`
// and DAL on `dal` (the read is otherwise ESPN's), so one recording gives
// the reads either side of his third goal.
static std::string hat_trick_read(int goals, int dal) {
  JsonDocument doc;
  if (deserializeJson(doc, slurp("fixtures/event_nhl_hattrick_final.json"), DeserializationOption::NestingLimit(40)))
    return "";
  doc["status"]["type"]["state"] = "in";
  doc["status"]["type"]["completed"] = false;
  doc["status"]["period"] = 3;
  JsonObject comp = doc["competitions"][0];
  comp["status"] = doc["status"];
  JsonArray sides = comp["competitors"];
  for (JsonObject c : sides) {
    if (c["id"] != std::to_string(kDal))
      continue;
    c["score"] = std::to_string(dal);
    JsonArray lists = c["leaders"];
    for (JsonObject l : lists) {
      if (l["name"] != "goals")
        continue;
      JsonArray tops = l["leaders"];
      for (JsonObject top : tops)
        if (top["athlete"]["id"] == std::to_string(kRobertson))
          top["displayValue"] = std::to_string(goals);
    }
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

static GameSnapshot read_as(const std::string &json, uint32_t team) {
  GameSnapshot s;
  CHECK(parse_event_str(json, Sport::HOCKEY, team, s, "401892640"));
  return s;
}

static CelebPlan plan(const GameSnapshot &prev, const GameSnapshot &cur, CelebLevel level, bool neutral = false,
                      bool opp_splashes = true, bool big_plays = true) {
  Splash s = decide_splash(prev, cur, opp_splashes, neutral);
  return decide_celebration(prev, cur, s, level, neutral, opp_splashes, big_plays);
}

static void test_scorers_parse() {
  GameSnapshot s = read_as(hat_trick_read(3, 6), kDal);
  CHECK(s.state == GameState::IN);
  CHECK(s.nhl.team_scorer_count == 1);
  CHECK(s.nhl.team_scorers[0].id == kRobertson);
  CHECK(s.nhl.team_scorers[0].goals == 3);
  CHECK(s.nhl.opp_scorer_count == 0);  // five WSH scorers, one goal each
  // The same read for a WSH panel: the sides swap.
  GameSnapshot w = read_as(hat_trick_read(3, 6), kWsh);
  CHECK(w.nhl.opp_scorer_count == 1 && w.nhl.opp_scorers[0].id == kRobertson);
  // At the final ESPN's leaders are the game's, but the big plays are over.
  GameSnapshot f;
  CHECK(parse_event_str(slurp("fixtures/event_nhl_hattrick_final.json"), Sport::HOCKEY, kDal, f));
  CHECK(f.nhl.team_scorer_count == 0);
}

static void test_hat_trick() {
  const GameSnapshot two = read_as(hat_trick_read(2, 5), kDal), three = read_as(hat_trick_read(3, 6), kDal);
  CelebPlan p = plan(two, three, CelebLevel::FULL);
  CHECK(p.kind == CelebKind::HAT_TRICK);
  CHECK(p.text == "HAT TRICK!");
  CHECK(p.duration_ms == kCelebHatTrickMs);
  CHECK(p.color == celeb_bright(parse_color(three.team_color)));
  // Subtle keeps the goal's flash, Off shows nothing, Big plays off keeps the lamp.
  CHECK(plan(two, three, CelebLevel::SUBTLE).kind == CelebKind::FLASH);
  CHECK(plan(two, three, CelebLevel::OFF).kind == CelebKind::NONE);
  CHECK(plan(two, three, CelebLevel::FULL, false, true, false).kind == CelebKind::GOAL_LAMP);
  // The same read again (a redraw) is no new hat trick.
  CHECK(plan(three, three, CelebLevel::FULL).kind == CelebKind::NONE);
  // ESPN's leaders a read behind the score: the lamp plays on the goal, the
  // hat trick when the count catches up.
  const GameSnapshot lag = read_as(hat_trick_read(2, 6), kDal);
  CHECK(plan(two, lag, CelebLevel::FULL).kind == CelebKind::GOAL_LAMP);
  CHECK(plan(lag, three, CelebLevel::FULL).kind == CelebKind::HAT_TRICK);
  // A panel that never saw him on two (it started after, or missed the
  // read): no hat trick, only the goal.
  const GameSnapshot one = read_as(hat_trick_read(1, 5), kDal);
  CHECK(plan(one, three, CelebLevel::FULL).kind == CelebKind::GOAL_LAMP);
  // A fourth goal is not a hat trick.
  const GameSnapshot four = read_as(hat_trick_read(4, 7), kDal);
  CHECK(plan(three, four, CelebLevel::FULL).kind == CelebKind::GOAL_LAMP);
  // Another game's read before this one: no comparison, no show.
  GameSnapshot other = two;
  other.event_id = "401892641";
  CHECK(plan(other, three, CelebLevel::FULL).kind != CelebKind::HAT_TRICK);
  // The shootout's goals are not goals.
  GameSnapshot so_two = two, so_three = three;
  so_two.period = so_three.period = 5;
  CHECK(plan(so_two, so_three, CelebLevel::FULL).kind != CelebKind::HAT_TRICK);
}

static void test_hat_trick_other_side() {
  // A WSH panel: Robertson is the other side.
  const GameSnapshot two = read_as(hat_trick_read(2, 5), kWsh), three = read_as(hat_trick_read(3, 6), kWsh);
  CHECK(plan(two, three, CelebLevel::FULL).kind == CelebKind::MUTED);
  // Neutral modes celebrate either side, with the side's abbreviation,
  // unless Opponent scores too is off.
  CelebPlan p = plan(two, three, CelebLevel::FULL, true);
  CHECK(p.kind == CelebKind::HAT_TRICK);
  CHECK(p.text == "DAL HAT TRICK!");
  CHECK(p.color == celeb_bright(parse_color(three.opp_color)));
  CHECK(plan(two, three, CelebLevel::FULL, true, false).kind != CelebKind::HAT_TRICK);
}

// FLA @ BOS, 11/8: Geekie's goal read live, the last play of the read. ESPN's
// play-by-play has it as a power play goal; the read the panel makes has no
// strength on the play, so it is a goal like any other.
static void test_power_play_goal_live() {
  const std::string cur_json = slurp("fixtures/event_nhl_goal.json");
  JsonDocument doc;
  CHECK(!deserializeJson(doc, cur_json, DeserializationOption::NestingLimit(40)));
  CHECK(doc["competitions"][0]["situation"]["lastPlay"]["type"]["text"] == "Goal");
  CHECK(doc["competitions"][0]["situation"]["lastPlay"]["strength"].isNull());
  // The read before: BOS a goal fewer.
  JsonArray sides = doc["competitions"][0]["competitors"];
  for (JsonObject c : sides)
    if (c["id"] == "1")
      c["score"] = std::to_string(atoi(c["score"].as<const char *>()) - 1);
  std::string prev_json;
  serializeJson(doc, prev_json);
  GameSnapshot prev, cur;
  CHECK(parse_event_str(prev_json, Sport::HOCKEY, 1, prev, "401892674"));
  CHECK(parse_event_str(cur_json, Sport::HOCKEY, 1, cur, "401892674"));
  CHECK(cur.play_type == "Goal");
  CHECK(plan(prev, cur, CelebLevel::FULL).kind == CelebKind::GOAL_LAMP);
  CHECK(plan(prev, cur, CelebLevel::FULL, true).kind == CelebKind::GOAL_LAMP);
}

// The hat trick's frames at both widths: something lit on every frame
// until the fade, and the show over when its time is up.
static void test_art_runs() {
  static uint16_t buf[128 * 64];
  {
    const CelebKind k = CelebKind::HAT_TRICK;
    for (int w : {128, 64}) {
      const CelebArt *art = celeb_art_for(k, w);
      CHECK(art != nullptr);
      if (art == nullptr)
        continue;
      CHECK(art->w == w && art->h == 64);
      CelebPlan p;
      p.kind = k;
      p.color = 0x006847;
      p.alt = 0xFFFFFF;
      p.duration_ms = kCelebHatTrickMs;
      // The duration is the art's own length.
      CHECK(p.duration_ms == (uint32_t) art->frames * 1000 / art->fps);
      CelebFrame f{buf, w, 64, w};
      Celebration c;
      c.start(p, w, 64, 7);
      int dark = 0, frames = (int) (p.duration_ms / 1000.0f * 30);
      for (int i = 0; i < frames; i++) {
        c.step(1.0f / 30);
        c.render(f);
        if (f.lit() == 0 && c.elapsed() < p.duration_ms / 1000.0f - 0.6f)
          dark++;
      }
      CHECK(dark == 0);
      c.step(0.1f);
      CHECK(!c.running());
    }
  }
}

int main() {
  test_scorers_parse();
  test_hat_trick();
  test_hat_trick_other_side();
  test_power_play_goal_live();
  test_art_runs();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
