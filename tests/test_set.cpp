// The device page's set requests and Live mode's league pick
// (components/gameday/set_request.h). The table in fixtures/set_cases.json is
// the same one tests/web/mock_device.py checks itself against.
#include <ArduinoJson.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "set_request.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define SCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::string keys(const std::vector<League> &leagues) {
  std::string out;
  for (League l : leagues)
    out += (out.empty() ? "" : ",") + std::string(league_key(l));
  return out;
}

// What the firmware keeps for the settings a set request reaches, and what
// apply_set_'s setters store. PanelLayout's 1 to max_cols check (2 in
// gameday-common.yaml) is the one refusal outside set_request.h.
struct Settings {
  TeamRef team;
  uint8_t team_league{0};
  int mode{0}, rotate{1}, lockon{15}, release{30}, tz{1}, panels{1};
  bool collide{false}, today{false}, tz_auto{true}, down{true}, count{true}, match{true}, play{true}, odds{true}, opp{true};
  bool bootaddr{true}, autoupd{false}, beta{false};
  TeamRef favs[16];
  uint16_t live_mask{0};
  std::string msg, msgurl;
  int msgevery{kMsgEveryDefault};
};
constexpr int kFavMax = 16;
constexpr int kMaxCols = 2;

TeamRef team_ref(const char *s) {
  TeamRef t;
  parse_team_ref(s == nullptr ? "" : s, t.league, t.id);
  return t;
}

std::string team_str(const TeamRef &t) {
  return t.id == 0 ? "" : std::string(league_key((League) t.league)) + ":" + std::to_string(t.id);
}

void load(Settings &s, JsonObjectConst o) {
  if (o["team_league"].is<const char *>()) {
    const LeagueInfo *info = league_by_key(o["team_league"]);
    s.team_league = info != nullptr ? (uint8_t) info->league : 0;
  }
  if (o["team"].is<const char *>()) {
    s.team = team_ref(o["team"]);
    if (s.team.id != 0)
      s.team_league = s.team.league;
  }
  if (o["favs"].is<JsonArrayConst>()) {
    int i = 0;
    for (auto f : o["favs"].as<JsonArrayConst>())
      if (i < kFavMax)
        s.favs[i++] = team_ref(f.as<const char *>());
    for (; i < kFavMax; i++)
      s.favs[i] = TeamRef{};
  }
  if (o["live_auto"].is<bool>() || o["live"].is<JsonArrayConst>()) {
    std::string list;
    for (auto l : o["live"].as<JsonArrayConst>())
      list += std::string(l.as<const char *>()) + ",";
    s.live_mask = o["live_auto"].as<bool>() ? 0 : live_mask_from_keys(list);
  }
  auto num = [&](const char *k, int &v) {
    if (o[k].is<int>())
      v = o[k].as<int>();
  };
  auto flag = [&](const char *k, bool &v) {
    if (o[k].is<bool>())
      v = o[k].as<bool>();
  };
  num("mode", s.mode);
  num("rotate", s.rotate);
  num("lockon", s.lockon);
  num("release", s.release);
  num("tz", s.tz);
  num("panels", s.panels);
  num("msgevery", s.msgevery);
  if (o["msg"].is<const char *>())
    s.msg = o["msg"].as<const char *>();
  if (o["msgurl"].is<const char *>())
    s.msgurl = o["msgurl"].as<const char *>();
  int collide = s.collide ? 1 : 0;
  num("collide", collide);
  s.collide = collide != 0;
  flag("today", s.today);
  flag("tz_auto", s.tz_auto);
  flag("down", s.down);
  flag("count", s.count);
  flag("match", s.match);
  flag("play", s.play);
  flag("odds", s.odds);
  flag("opp", s.opp);
  flag("bootaddr", s.bootaddr);
  flag("autoupd", s.autoupd);
  flag("beta", s.beta);
}

void apply(Settings &s, const SetOp &op) {
  switch (op.kind) {
    case SetKind::TEAM:
      s.team = op.team;
      if (op.team.id != 0)
        s.team_league = op.team.league;
      break;
    case SetKind::MODE: s.mode = op.value; break;
    case SetKind::ROTATE: s.rotate = op.value; break;
    case SetKind::LOCKON: s.lockon = op.value; break;
    case SetKind::RELEASE: s.release = op.value; break;
    case SetKind::COLLIDE: s.collide = op.on; break;
    case SetKind::TODAY: s.today = op.on; break;
    case SetKind::LIVE: s.live_mask = op.mask; break;
    case SetKind::FAVS:
      for (int i = 0; i < kFavMax; i++)
        s.favs[i] = i < (int) op.teams.size() ? op.teams[i] : TeamRef{};
      break;
    case SetKind::FAV_SLOT: s.favs[op.slot - 1] = op.team; break;
    case SetKind::TZ: s.tz = op.value; break;
    case SetKind::TZ_AUTO: s.tz_auto = op.on; break;
    case SetKind::DOWN: s.down = op.on; break;
    case SetKind::COUNT: s.count = op.on; break;
    case SetKind::MATCH: s.match = op.on; break;
    case SetKind::PLAY: s.play = op.on; break;
    case SetKind::ODDS: s.odds = op.on; break;
    case SetKind::OPP: s.opp = op.on; break;
    case SetKind::BOOTADDR: s.bootaddr = op.on; break;
    case SetKind::AUTOUPD: s.autoupd = op.on; break;
    case SetKind::BETA: s.beta = op.on; break;
    case SetKind::MSG: s.msg = op.text; break;
    case SetKind::MSGURL: s.msgurl = op.text; break;
    case SetKind::MSGEVERY: s.msgevery = op.value; break;
    case SetKind::PANELS:
      if (op.value >= 1 && op.value <= kMaxCols)
        s.panels = op.value;
      break;
  }
}

// The settings keys of rebuild_state_'s document, teams as league:id.
void state_doc(const Settings &s, JsonDocument &doc) {
  doc["team"] = team_str(s.team);
  doc["mode"] = s.mode;
  doc["rotate"] = s.rotate;
  doc["lockon"] = s.lockon;
  doc["release"] = s.release;
  doc["collide"] = s.collide ? 1 : 0;
  int fav_n = 4;
  for (int i = 4; i < kFavMax; i++)
    if (s.favs[i].id != 0)
      fav_n = i + 1;
  JsonArray favs = doc["favs"].to<JsonArray>();
  for (int i = 0; i < fav_n; i++)
    favs.add(team_str(s.favs[i]));
  doc["today"] = s.today;
  std::vector<uint8_t> fav_leagues;
  for (const auto &f : s.favs)
    if (f.id != 0)
      fav_leagues.push_back(f.league);
  JsonArray live = doc["live"].to<JsonArray>();
  for (League l : live_leagues(s.live_mask, s.team_league, fav_leagues))
    live.add(league_key(l));
  doc["live_auto"] = s.live_mask == 0;
  doc["tz"] = s.tz;
  doc["tz_auto"] = s.tz_auto;
  doc["down"] = s.down;
  doc["count"] = s.count;
  doc["match"] = s.match;
  doc["play"] = s.play;
  doc["odds"] = s.odds;
  doc["opp"] = s.opp;
  doc["bootaddr"] = s.bootaddr;
  doc["autoupd"] = s.autoupd;
  doc["beta"] = s.beta;
  doc["panels"] = s.panels;
  doc["msg"] = s.msg;
  doc["msgurl"] = s.msgurl;
  doc["msgevery"] = s.msgevery;
}

void test_fixture_cases() {
  JsonDocument fx;
  DeserializationError err = deserializeJson(fx, slurp("fixtures/set_cases.json"));
  SCHECK(!err, "fixtures/set_cases.json: %s", err.c_str());
  int n = 0;
  for (JsonObjectConst c : fx["cases"].as<JsonArrayConst>()) {
    n++;
    const char *name = c["name"] | "?";
    Settings s;
    load(s, fx["base"].as<JsonObjectConst>());
    load(s, c["start"].as<JsonObjectConst>());
    // handle_set_: only kSetKeys, in that order; 400 when none are there.
    std::vector<std::pair<std::string, std::string>> kv;
    for (const char *key : kSetKeys)
      for (JsonArrayConst p : c["set"].as<JsonArrayConst>())
        if (std::string(p[0].as<const char *>()) == key)
          kv.emplace_back(key, p[1].as<const char *>());
    int status = kv.empty() ? 400 : 200;
    SCHECK(status == (c["status"] | 200), "%s: status %d", name, status);
    SetOp op;
    for (const auto &p : kv)
      if (parse_set(p.first, p.second, op, kFavMax))
        apply(s, op);
    JsonDocument got;
    state_doc(s, got);
    for (JsonPairConst e : c["expect"].as<JsonObjectConst>()) {
      std::string want, have;
      serializeJson(e.value(), want);
      serializeJson(got[e.key().c_str()], have);
      SCHECK(want == have, "%s: %s is %s, expected %s", name, e.key().c_str(), have.c_str(), want.c_str());
    }
  }
  SCHECK(n >= 50, "only %d cases read from fixtures/set_cases.json", n);
}

void test_parse_set() {
  SetOp op;
  SCHECK(!parse_set("bogus", "1", op, kFavMax), "unknown key");
  SCHECK(!parse_set("fav5", "nfl:6", op, kFavMax), "fav5 is not a remote slot");
  SCHECK(!parse_set("fav0", "nfl:6", op, kFavMax), "fav0 is not a remote slot");
  SCHECK(parse_set("fav4", "nfl:6", op, kFavMax) && op.kind == SetKind::FAV_SLOT && op.slot == 4 && op.team.id == 6,
         "fav4");
  SCHECK(parse_set("fav1", "none", op, kFavMax) && op.slot == 1 && op.team.id == 0, "fav1 none");
  SCHECK(!parse_set("fav1", "nfl:99999", op, kFavMax), "fav1 unknown team");
  SCHECK(parse_set("team", "none", op, kFavMax) && op.kind == SetKind::TEAM && op.team.id == 0, "team none");
  SCHECK(parse_set("team", "nfl:6x", op, kFavMax) && op.team.id == 6, "team id reads like strtoul");

  SCHECK(parse_set("favs", "nfl:6,bogus,nfl:6,mlb:15", op, kFavMax), "favs");
  SCHECK(op.teams.size() == 2 && op.refused.size() == 1 && op.refused[0] == "bogus", "favs drops repeats, logs bad");
  SCHECK(parse_set("favs", "nfl:1,nfl:2,nfl:3", op, 2) && op.teams.size() == 2, "favs stops at fav_max");

  SCHECK(parse_set("rotate", "-5", op, kFavMax) && op.value == kRotateMinMinutes, "rotate -5");
  SCHECK(parse_set("rotate", "999", op, kFavMax) && op.value == kRotateMaxMinutes, "rotate 999");
  SCHECK(parse_set("lockon", "", op, kFavMax) && op.value == kLockonMinMinutes, "lockon empty");
  SCHECK(parse_set("release", "100000", op, kFavMax) && op.value == kReleaseMaxSeconds, "release 100000");
  SCHECK(parse_set("mode", "0", op, kFavMax) && op.value == 0, "mode 0");
  SCHECK(parse_set("mode", "5", op, kFavMax) && op.value == 5, "mode 5");
  SCHECK(!parse_set("mode", "6", op, kFavMax), "mode 6");
  SCHECK(parse_set("panels", "7", op, kFavMax) && op.value == 7, "panels passes the count through");
  SCHECK(parse_set("tz", "America/Chicago", op, kFavMax) && op.value == 1, "tz IANA");
  SCHECK(!parse_set("tz", "-1", op, kFavMax), "tz -1");
  SCHECK(parse_set("down", "1", op, kFavMax) && op.on, "down 1");
  SCHECK(parse_set("down", "on", op, kFavMax) && !op.on, "down on is off");
  SCHECK(parse_set("msg", "Happy birthday", op, kFavMax) && op.kind == SetKind::MSG && op.text == "Happy birthday",
         "msg");
  SCHECK(parse_set("msg", std::string(300, 'x'), op, kFavMax) && op.text.size() == kMsgTextMax, "msg is capped");
  SCHECK(parse_set("msgurl", "https://example.com/names.txt", op, kFavMax) && op.kind == SetKind::MSGURL, "msgurl");
  SCHECK(!parse_set("msgurl", "ftp://example.com/names.txt", op, kFavMax), "msgurl ftp");
  SCHECK(!parse_set("msgurl", "https://exa mple.com", op, kFavMax), "msgurl with a space");
  SCHECK(!parse_set("msgurl", "https://" + std::string(kMsgUrlMax, 'a'), op, kFavMax), "msgurl too long");
  SCHECK(parse_set("msgevery", "0", op, kFavMax) && op.value == 0, "msgevery 0 holds the board");
  SCHECK(parse_set("msgevery", "99", op, kFavMax) && op.value == kMsgEveryMax, "msgevery 99");
  SCHECK(!parse_set("msgevery", "abc", op, kFavMax), "msgevery text is refused, not 0");
  SCHECK(!parse_set("msgevery", "-1", op, kFavMax), "msgevery -1");

  // Every key handle_set_ forwards decodes to something.
  for (const char *key : kSetKeys) {
    bool known = parse_set(key, "1", op, kFavMax) || parse_set(key, "nfl:6", op, kFavMax) ||
                 parse_set(key, "", op, kFavMax);
    SCHECK(known, "kSetKeys has %s but parse_set refuses every value", key);
  }
}

void test_live_leagues() {
  std::vector<uint8_t> none;
  SCHECK(keys(live_leagues(0, (uint8_t) League::NFL, none)) == "nfl", "auto, nothing saved");
  SCHECK(keys(live_leagues(0, (uint8_t) League::MLB, none)) == "mlb", "auto, team only");
  SCHECK(keys(live_leagues(0, (uint8_t) League::NHL, {(uint8_t) League::NCAA, (uint8_t) League::NHL})) == "ncaa,nhl",
         "auto, team plus favorites, league order");
  SCHECK(keys(live_leagues(0, 200, none)) == "nfl", "auto, unknown league byte falls back to NFL");
  SCHECK(keys(live_leagues(0, 200, {(uint8_t) League::EPL})) == "epl", "auto, unknown team league skipped");
  uint16_t mlb_nba = (uint16_t) ((1u << (uint8_t) League::MLB) | (1u << (uint8_t) League::NBA));
  SCHECK(keys(live_leagues(mlb_nba, (uint8_t) League::NFL, {(uint8_t) League::NHL})) == "mlb,nba",
         "an explicit mask ignores team and favorites");
  SCHECK(keys(live_leagues(1u << 15, (uint8_t) League::MLB, none)) == "nfl", "a mask of unknown leagues is NFL");

  SCHECK(live_mask_from_keys("") == 0, "empty keys");
  SCHECK(live_mask_from_keys("xfl,MLB") == 0, "unknown and upper-case keys");
  SCHECK(live_mask_from_keys("mlb,,mlb") == (1u << (uint8_t) League::MLB), "repeats and empty items");
  SCHECK(live_mask_from_keys("nhl,") == (1u << (uint8_t) League::NHL), "trailing comma");
}

// What each mode's scan reads is what its finals rotation walks.
void test_scan_leagues() {
  std::vector<League> live = {League::MLB, League::NBA, League::NHL};
  SCHECK(keys(scan_leagues((uint8_t) Mode::MY_TEAM, live)) == "", "My team scans none");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE_NFL, live)) == "nfl", "Live NFL");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE_NCAA, live)) == "ncaa", "Live college");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE_ANY, live)) == "nfl,ncaa", "Live anything and the no-team default");
  SCHECK(keys(scan_leagues((uint8_t) Mode::FAVORITES, live)) == "", "Favorites scans none");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE, live)) == "mlb,nba,nhl", "Live games, its own leagues");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE, live_leagues(0, (uint8_t) League::EPL, {}))) == "epl",
         "Live games, auto from the team's league");
  SCHECK(keys(scan_leagues(kModeCount, live)) == "", "an unknown mode scans none");
}

// A change to Live games' leagues restarts the scan only when it changes what
// the scan reads.
void test_live_scan_changed() {
  const uint8_t live = (uint8_t) Mode::LIVE;
  const uint8_t nfl = (uint8_t) League::NFL;
  std::vector<uint8_t> none;
  uint16_t ncaa = (uint16_t) (1u << (uint8_t) League::NCAA);
  uint16_t nfl_only = (uint16_t) (1u << nfl);
  // No team saved, list on auto (NFL), then set to college only: the
  // living room panel that kept cycling NFL finals.
  SCHECK(live_scan_changed(live, live_leagues(0, nfl, none), live_leagues(ncaa, nfl, none)), "auto NFL to college");
  SCHECK(!live_scan_changed(live, live_leagues(0, nfl, none), live_leagues(nfl_only, nfl, none)),
         "auto NFL to an explicit NFL reads the same league");
  SCHECK(!live_scan_changed(live, live_leagues(ncaa, nfl, none), live_leagues(ncaa, nfl, none)), "same list");
  // On auto a new favorite in another league adds that league.
  SCHECK(live_scan_changed(live, live_leagues(0, nfl, none), live_leagues(0, nfl, {(uint8_t) League::NHL})),
         "auto, a favorite added in a new league");
  SCHECK(!live_scan_changed(live, live_leagues(0, nfl, none), live_leagues(0, nfl, {nfl})),
         "auto, a favorite added in the team's league");
  SCHECK(!live_scan_changed(live, live_leagues(ncaa, nfl, none), live_leagues(ncaa, nfl, {(uint8_t) League::NHL})),
         "an explicit list ignores a new favorite");
  // Modes that don't read the list never restart for it.
  for (uint8_t m : {(uint8_t) Mode::MY_TEAM, (uint8_t) Mode::LIVE_NFL, (uint8_t) Mode::LIVE_NCAA,
                    (uint8_t) Mode::LIVE_ANY, (uint8_t) Mode::FAVORITES})
    SCHECK(!live_scan_changed(m, live_leagues(0, nfl, none), live_leagues(ncaa, nfl, none)),
           "mode %u ignores Live games' list", (unsigned) m);
}

}  // namespace

int run_set_tests(int &total_checks) {
  test_parse_set();
  test_live_leagues();
  test_scan_leagues();
  test_live_scan_changed();
  test_fixture_cases();
  total_checks += checks;
  return failures;
}
