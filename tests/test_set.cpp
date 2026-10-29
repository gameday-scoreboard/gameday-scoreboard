// The device page's set requests and Live mode's league pick
// (components/gameday/set_request.h). The table in fixtures/set_cases.json is
// the same one tests/web/mock_device.py checks itself against.
#include <ArduinoJson.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "playlists.h"
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
  int mode{4}, rotate{1}, lockon{15}, release{30}, tz{1}, panels{1};
  bool collide{false}, today{false}, tz_auto{true}, down{true}, count{true}, match{true}, play{true}, odds{true}, opp{true}, best{true};
  bool jump{false}, banner{true}, favtake{false};
  bool leaders{true};
  bool bootaddr{true}, autoupd{false}, beta{false}, countdown{true};
  std::string cdname;
  uint32_t cdymd{0};
  TeamRef favs[16];
  // Playlists as the firmware keeps them: a name, a mask over favs and its options.
  struct List {
    std::string first;
    uint16_t second;
    ListOpts opts;
  };
  std::vector<List> lists;
  int list{0};
  uint16_t live_mask{0};
  std::string msg, msgurl, nick, hook;
  int msgevery{kMsgEveryDefault};
  int pregame{kPregameDefault};
  NightPrefs night{night_defaults()};
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
  if (o["favs"].is<JsonArrayConst>()) {
    int i = 0;
    for (auto f : o["favs"].as<JsonArrayConst>())
      if (i < kFavMax)
        s.favs[i++] = team_ref(f.as<const char *>());
    for (; i < kFavMax; i++)
      s.favs[i] = TeamRef{};
  }
  if (o["lists"].is<JsonArrayConst>()) {
    s.lists.clear();
    for (JsonObjectConst l : o["lists"].as<JsonArrayConst>()) {
      uint16_t mask = 0;
      for (auto f : l["f"].as<JsonArrayConst>())
        mask |= (uint16_t) (1u << f.as<int>());
      ListOpts o;
      o.days = (uint8_t) (l["days"] | 0);
      o.from = (uint16_t) night_parse_time(l["from"] | "00:00");
      o.to = (uint16_t) night_parse_time(l["to"] | "00:00");
      o.only = l["only"] | false;
      o.today = (int8_t) (l["today"] | -1);
      s.lists.push_back({l["n"].as<const char *>(), mask, o});
    }
  }
  if (o["list"].is<int>())
    s.list = o["list"].as<int>();
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
  num("pregame", s.pregame);
  if (o["hook"].is<const char *>())
    s.hook = o["hook"].as<const char *>();
  if (o["msg"].is<const char *>())
    s.msg = o["msg"].as<const char *>();
  if (o["msgurl"].is<const char *>())
    s.msgurl = o["msgurl"].as<const char *>();
  if (o["nick"].is<const char *>())
    s.nick = o["nick"].as<const char *>();
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
  flag("best", s.best);
  flag("jump", s.jump);
  flag("banner", s.banner);
  flag("favtake", s.favtake);
  flag("leaders", s.leaders);
  flag("bootaddr", s.bootaddr);
  flag("autoupd", s.autoupd);
  flag("beta", s.beta);
  flag("countdown", s.countdown);
  if (o["cdname"].is<const char *>())
    s.cdname = o["cdname"].as<const char *>();
  if (o["cddate"].is<const char *>())
    parse_countdown_date(o["cddate"].as<const char *>(), s.cdymd);
  bool on = s.night.on != 0, screen = s.night.screen_off == 0;
  flag("night", on);
  flag("nightscreen", screen);
  s.night.on = on ? 1 : 0;
  s.night.screen_off = screen ? 0 : 1;
  int level = s.night.level;
  num("nightlevel", level);
  s.night.level = (uint8_t) level;
  if (o["nightstart"].is<const char *>())
    s.night.start = (uint16_t) night_parse_time(o["nightstart"].as<const char *>());
  if (o["nightend"].is<const char *>())
    s.night.end = (uint16_t) night_parse_time(o["nightend"].as<const char *>());
}

std::vector<TeamRef> fav_refs(const Settings &s) { return std::vector<TeamRef>(s.favs, s.favs + kFavMax); }

// remap_lists_: each playlist follows its teams through a favorites change.
void remap(Settings &s, const std::vector<TeamRef> &before) {
  for (auto &l : s.lists)
    l.second = remap_mask(l.second, before, fav_refs(s));
}

void apply(Settings &s, const SetOp &op, bool with_lists, int keep_list) {
  std::vector<TeamRef> before = fav_refs(s);
  switch (op.kind) {
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
      remap(s, before);
      break;
    case SetKind::FAV_SLOT:
      s.favs[op.slot - 1] = op.team;
      remap(s, before);
      break;
    case SetKind::LISTS: {
      // set_lists_: the one being played stays chosen by its name, or by
      // list= in the same request.
      // A playlist sent without options keeps its own by name.
      std::string active = s.list > 0 ? s.lists[s.list - 1].first : "";
      std::vector<Settings::List> old = s.lists;
      s.lists.clear();
      s.list = 0;
      for (const auto &d : parse_lists(op.text, kFavMax)) {
        ListOpts o = d.opts;
        for (const auto &l : old)
          if (!d.has_opts && l.first == d.name)
            o = l.opts;
        s.lists.push_back({d.name, list_mask(d.teams, before), o});
        if (!active.empty() && d.name == active)
          s.list = (int) s.lists.size();
      }
      if (keep_list >= 0 && keep_list <= (int) s.lists.size())
        s.list = keep_list;
      break;
    }
    case SetKind::LIST:
      // set_active_list_ switches to Favorite teams; a number past the last
      // playlist is refused. With lists= set_lists_ took it.
      if (!with_lists && op.value <= (int) s.lists.size()) {
        s.list = op.value;
        s.mode = (int) Mode::FAVORITES;
      }
      break;
    case SetKind::TZ: s.tz = op.value; break;
    case SetKind::TZ_AUTO: s.tz_auto = op.on; break;
    case SetKind::DOWN: s.down = op.on; break;
    case SetKind::COUNT: s.count = op.on; break;
    case SetKind::MATCH: s.match = op.on; break;
    case SetKind::PLAY: s.play = op.on; break;
    case SetKind::ODDS: s.odds = op.on; break;
    case SetKind::OPP: s.opp = op.on; break;
    case SetKind::BEST: s.best = op.on; break;
    case SetKind::JUMP: s.jump = op.on; break;
    case SetKind::BANNER: s.banner = op.on; break;
    case SetKind::FAVTAKE: s.favtake = op.on; break;
    case SetKind::LEADERS: s.leaders = op.on; break;
    case SetKind::BOOTADDR: s.bootaddr = op.on; break;
    case SetKind::AUTOUPD: s.autoupd = op.on; break;
    case SetKind::BETA: s.beta = op.on; break;
    case SetKind::COUNTDOWN: s.countdown = op.on; break;
    case SetKind::CD_NAME: s.cdname = op.text; break;
    case SetKind::CD_DATE: s.cdymd = op.ymd; break;
    case SetKind::MSG: s.msg = op.text; break;
    case SetKind::MSGURL: s.msgurl = op.text; break;
    case SetKind::MSGEVERY: s.msgevery = op.value; break;
    case SetKind::NICK: s.nick = op.text; break;
    case SetKind::HOOK: s.hook = op.text; break;
    case SetKind::PREGAME: s.pregame = op.value; break;
    case SetKind::NIGHT: s.night.on = op.on ? 1 : 0; break;
    case SetKind::NIGHT_START: s.night.start = (uint16_t) op.value; break;
    case SetKind::NIGHT_END: s.night.end = (uint16_t) op.value; break;
    case SetKind::NIGHT_SCREEN: s.night.screen_off = op.on ? 0 : 1; break;
    case SetKind::NIGHT_LEVEL: s.night.level = (uint8_t) op.value; break;
    case SetKind::PANELS:
      if (op.value >= 1 && op.value <= kMaxCols)
        s.panels = op.value;
      break;
  }
}

// The settings keys of rebuild_state_'s document, teams as league:id.
void state_doc(const Settings &s, JsonDocument &doc) {
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
  JsonArray lists = doc["lists"].to<JsonArray>();
  for (const auto &l : s.lists) {
    JsonObject o = lists.add<JsonObject>();
    o["n"] = l.first;
    JsonArray f = o["f"].to<JsonArray>();
    for (int i = 0; i < kFavMax; i++)
      if (s.favs[i].id != 0 && ((l.second >> i) & 1u))
        f.add(i);
    o["days"] = l.opts.days;
    o["from"] = night_time_text(l.opts.from);
    o["to"] = night_time_text(l.opts.to);
    o["only"] = l.opts.only;
    o["today"] = l.opts.today;
  }
  doc["list"] = s.list;
  doc["today"] = s.today;
  std::vector<uint8_t> fav_leagues;
  for (const auto &f : s.favs)
    if (f.id != 0)
      fav_leagues.push_back(f.league);
  JsonArray live = doc["live"].to<JsonArray>();
  bool no_team = no_team_default((uint8_t) s.mode, !fav_leagues.empty());
  for (League l : no_team ? no_team_leagues() : live_leagues(s.live_mask, fav_leagues))
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
  doc["best"] = s.best;
  doc["jump"] = s.jump;
  doc["banner"] = s.banner;
  doc["favtake"] = s.favtake;
  doc["leaders"] = s.leaders;
  doc["bootaddr"] = s.bootaddr;
  doc["autoupd"] = s.autoupd;
  doc["beta"] = s.beta;
  doc["countdown"] = s.countdown;
  doc["cdname"] = s.cdname;
  doc["cddate"] = countdown_date_text(s.cdymd);
  doc["panels"] = s.panels;
  doc["msg"] = s.msg;
  doc["msgurl"] = s.msgurl;
  doc["msgevery"] = s.msgevery;
  doc["nick"] = s.nick;
  doc["hook"] = s.hook;
  doc["pregame"] = s.pregame;
  doc["night"] = s.night.on != 0;
  doc["nightstart"] = night_time_text(s.night.start);
  doc["nightend"] = night_time_text(s.night.end);
  doc["nightscreen"] = s.night.screen_off == 0;
  doc["nightlevel"] = s.night.level;
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
    std::vector<SetOp> ops;
    for (const auto &p : kv) {
      SetOp op;
      if (parse_set(p.first, p.second, op, kFavMax))
        ops.push_back(op);
    }
    // apply_ops_: list= with lists= names the playlist playing after the change.
    bool with_lists = false;
    int keep_list = -1;
    for (const auto &op : ops) {
      with_lists = with_lists || op.kind == SetKind::LISTS;
      if (op.kind == SetKind::LIST)
        keep_list = op.value;
    }
    for (const auto &op : ops)
      apply(s, op, with_lists, keep_list);
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
  SCHECK(!parse_set("team", "nfl:6", op, kFavMax), "team is not a setting");

  SCHECK(parse_set("favs", "nfl:6,bogus,nfl:6,mlb:15", op, kFavMax), "favs");
  SCHECK(op.teams.size() == 2 && op.refused.size() == 1 && op.refused[0] == "bogus", "favs drops repeats, logs bad");
  SCHECK(parse_set("favs", "nfl:1,nfl:2,nfl:3", op, 2) && op.teams.size() == 2, "favs stops at fav_max");

  SCHECK(parse_set("rotate", "-5", op, kFavMax) && op.value == kRotateMinMinutes, "rotate -5");
  SCHECK(parse_set("rotate", "999", op, kFavMax) && op.value == kRotateMaxMinutes, "rotate 999");
  SCHECK(!parse_set("lockon", "", op, kFavMax), "lockon empty is refused");
  SCHECK(!parse_set("release", "60s", op, kFavMax), "release with trailing text is refused");
  SCHECK(!parse_set("mode", "abc", op, kFavMax), "mode text is refused, not Favorites");
  SCHECK(!parse_set("mode", "1.5", op, kFavMax), "mode 1.5 is refused");
  SCHECK(!parse_set("rotate", "9999999", op, kFavMax), "rotate past six digits is refused");
  SCHECK(parse_set("release", "100000", op, kFavMax) && op.value == kReleaseMaxSeconds, "release 100000");
  SCHECK(parse_set("mode", "0", op, kFavMax) && op.value == (int) Mode::FAVORITES, "mode 0, My team before, is Favorites");
  SCHECK(parse_set("mode", "2", op, kFavMax) && op.value == 2, "an old live mode stays");
  SCHECK(read_mode(0) == 4 && read_mode(4) == 4 && read_mode(5) == 5 && read_mode(6) == -1 && read_mode(-1) == -1,
         "read_mode");
  SCHECK(parse_set("mode", "5", op, kFavMax) && op.value == 5, "mode 5");
  SCHECK(!parse_set("mode", "6", op, kFavMax), "mode 6");
  SCHECK(parse_set("panels", "7", op, kFavMax) && op.value == 7, "panels passes the count through");
  SCHECK(parse_set("tz", "America/Chicago", op, kFavMax) && op.value == 1, "tz IANA");
  SCHECK(!parse_set("tz", "-1", op, kFavMax), "tz -1");
  SCHECK(parse_set("down", "1", op, kFavMax) && op.on, "down 1");
  SCHECK(parse_set("down", "on", op, kFavMax) && !op.on, "down on is off");
  SCHECK(parse_set("countdown", "0", op, kFavMax) && op.kind == SetKind::COUNTDOWN && !op.on, "countdown 0");
  SCHECK(parse_set("cdname", "  Christmas \n", op, kFavMax) && op.kind == SetKind::CD_NAME && op.text == "Christmas",
         "cdname trims");
  SCHECK(parse_set("cddate", "2026-12-25", op, kFavMax) && op.kind == SetKind::CD_DATE && op.ymd == 20261225,
         "cddate");
  SCHECK(parse_set("cddate", "none", op, kFavMax) && op.ymd == 0, "cddate none clears");
  SCHECK(!parse_set("cddate", "12/25/2026", op, kFavMax), "cddate in another format");
  SCHECK(!parse_set("cddate", "2027-02-29", op, kFavMax), "cddate on a day that doesn't exist");
  SCHECK(parse_set("msg", "Happy birthday", op, kFavMax) && op.kind == SetKind::MSG && op.text == "Happy birthday",
         "msg");
  SCHECK(parse_set("msg", std::string(300, 'x'), op, kFavMax) && op.text.size() == kMsgTextMax, "msg is capped");
  SCHECK(parse_set("nick", "Garage", op, kFavMax) && op.kind == SetKind::NICK && op.text == "Garage", "nick");
  SCHECK(parse_set("nick", std::string(100, 'x'), op, kFavMax) && op.text.size() == kNickMax, "nick is capped");
  SCHECK(parse_set("nick", "a\nb", op, kFavMax) && op.text == "a b", "nick is one line");
  SCHECK(parse_set("nick", "Caf\xE9 \xC3\xA9t\xC3", op, kFavMax) && op.text == "Caf \xC3\xA9t",
         "bytes that aren't UTF-8 are left out of a nickname");
  SCHECK(utf8_only("A\xE0\x81\x81" "B\xED\xA0\x80" "C\xF4\x90\x80\x80" "D\xC1\xBF" "E") == "ABCDE",
         "overlong forms, surrogates and code points past U+10FFFF are left out");
  SCHECK(utf8_only("\xC3\xA9\xE2\x80\x99\xF0\x9F\x8F\x88") == "\xC3\xA9\xE2\x80\x99\xF0\x9F\x8F\x88",
         "valid two, three and four byte characters stay");
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
                 parse_set(key, "", op, kFavMax) || parse_set(key, "07:00", op, kFavMax);
    SCHECK(known, "kSetKeys has %s but parse_set refuses every value", key);
  }
}

void test_live_leagues() {
  std::vector<uint8_t> none;
  SCHECK(keys(live_leagues(0, none)) == "nfl", "auto, no favorites is NFL");
  SCHECK(keys(live_leagues(0, {(uint8_t) League::NHL, (uint8_t) League::NCAA})) == "ncaa,nhl",
         "auto, the favorites' leagues in league order");
  SCHECK(keys(live_leagues(0, {200})) == "nfl", "auto, an unknown league falls back to NFL");
  SCHECK(keys(live_leagues(0, {200, (uint8_t) League::EPL})) == "epl", "auto, an unknown league skipped");
  uint16_t mlb_nba = (uint16_t) ((1u << (uint8_t) League::MLB) | (1u << (uint8_t) League::NBA));
  SCHECK(keys(live_leagues(mlb_nba, {(uint8_t) League::NHL})) == "mlb,nba", "an explicit mask ignores the favorites");
  SCHECK(keys(live_leagues(1u << 15, none)) == "nfl", "a mask of unknown leagues is NFL");

  SCHECK(live_mask_from_keys("") == 0, "empty keys");
  SCHECK(live_mask_from_keys("xfl,MLB") == 0, "unknown and upper-case keys");
  SCHECK(live_mask_from_keys("mlb,,mlb") == (1u << (uint8_t) League::MLB), "repeats and empty items");
  SCHECK(live_mask_from_keys("nhl,") == (1u << (uint8_t) League::NHL), "trailing comma");
}

// What each mode's scan reads is what its finals rotation walks.
void test_scan_leagues() {
  std::vector<League> live = {League::MLB, League::NBA, League::NHL};
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE_NFL, live)) == "nfl", "Live NFL");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE_NCAA, live)) == "ncaa", "Live college");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE_ANY, live)) == "nfl,ncaa", "Live anything");
  SCHECK(keys(scan_leagues((uint8_t) Mode::FAVORITES, live)) == "", "Favorites scans none");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE, live)) == "mlb,nba,nhl", "Live games, its own leagues");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE, live_leagues(0, {(uint8_t) League::EPL}))) == "epl",
         "Live games, auto from a favorite's league");
  SCHECK(keys(scan_leagues(kModeCount, live)) == "", "an unknown mode scans none");
}

// A panel in Favorites with none set runs Live games over every league.
void test_no_team() {
  SCHECK(keys(no_team_leagues()) == "nfl,ncaa,mlb,mls,epl,nba,wnba,mcbb,nhl", "every league, kLeagues order");
  SCHECK(no_team_leagues().size() == kLeagueCount, "one entry per league");
  SCHECK(keys(scan_leagues((uint8_t) Mode::LIVE, no_team_leagues())) == keys(no_team_leagues()),
         "the no-team default's scan reads every league");
  const uint8_t favs = (uint8_t) Mode::FAVORITES;
  SCHECK(no_team_default(favs, false), "Favorites with none set");
  SCHECK(!no_team_default(favs, true), "Favorites with one set");
  for (uint8_t m : {(uint8_t) Mode::LIVE_NFL, (uint8_t) Mode::LIVE_NCAA, (uint8_t) Mode::LIVE_ANY, (uint8_t) Mode::LIVE})
    SCHECK(!no_team_default(m, false), "mode %u is a choice", (unsigned) m);
}

// A change to Live games' leagues restarts the scan only when it changes what
// the scan reads.
void test_live_scan_changed() {
  const uint8_t live = (uint8_t) Mode::LIVE;
  const uint8_t nfl = (uint8_t) League::NFL;
  std::vector<uint8_t> none;
  uint16_t ncaa = (uint16_t) (1u << (uint8_t) League::NCAA);
  uint16_t nfl_only = (uint16_t) (1u << nfl);
  // No favorites, list on auto (NFL), then set to college only: the
  // living room panel that kept cycling NFL finals.
  SCHECK(live_scan_changed(live, live_leagues(0, none), live_leagues(ncaa, none)), "auto NFL to college");
  SCHECK(!live_scan_changed(live, live_leagues(0, none), live_leagues(nfl_only, none)),
         "auto NFL to an explicit NFL reads the same league");
  SCHECK(!live_scan_changed(live, live_leagues(ncaa, none), live_leagues(ncaa, none)), "same list");
  // On auto a new favorite in another league adds that league.
  SCHECK(live_scan_changed(live, live_leagues(0, {nfl}), live_leagues(0, {nfl, (uint8_t) League::NHL})),
         "auto, a favorite added in a new league");
  SCHECK(!live_scan_changed(live, live_leagues(0, {nfl}), live_leagues(0, {nfl, nfl})),
         "auto, a favorite added in a league already followed");
  SCHECK(!live_scan_changed(live, live_leagues(ncaa, none), live_leagues(ncaa, {(uint8_t) League::NHL})),
         "an explicit list ignores a new favorite");
  // Modes that don't read the list never restart for it.
  for (uint8_t m : {(uint8_t) Mode::LIVE_NFL, (uint8_t) Mode::LIVE_NCAA, (uint8_t) Mode::LIVE_ANY,
                    (uint8_t) Mode::FAVORITES})
    SCHECK(!live_scan_changed(m, live_leagues(0, none), live_leagues(ncaa, none)),
           "mode %u ignores Live games' list", (unsigned) m);
}

// Playlists (playlists.h): parsing a set request's lists, and the masks that
// keep them over the favorites list.
void test_playlists() {
  std::vector<ListDef> l = parse_lists("Football=nfl:6,ncaa:251;Hoops=nba:6", kFavMax);
  SCHECK(l.size() == 2 && l[0].name == "Football" && l[0].teams.size() == 2 && l[1].name == "Hoops" &&
             l[1].teams.size() == 1,
         "two playlists in order");
  SCHECK(parse_lists("", kFavMax).empty(), "\"\" clears them");
  l = parse_lists("=nfl:6;  ;Dallas=nfl:6,nfl:6,nfl:99999,bogus", kFavMax);
  SCHECK(l.size() == 1 && l[0].name == "Dallas" && l[0].teams.size() == 1, "no name drops it; repeats, unknown teams go");
  l = parse_lists("Empty", kFavMax);
  SCHECK(l.size() == 1 && l[0].teams.empty(), "a playlist with no teams is kept");
  std::string many;
  for (int i = 0; i < kListMax + 3; i++)
    many += "P" + std::to_string(i) + "=nfl:6;";
  SCHECK(parse_lists(many, kFavMax).size() == (size_t) kListMax, "at most kListMax playlists");
  SCHECK(parse_lists("A=nfl:1,nfl:2,nfl:3", 2)[0].teams.size() == 2, "no more teams than favorites");
  SCHECK(list_name_clean("  Sunday   Football Crew  ") == "Sunday Football", "cut to 16 bytes, spaces squeezed");
  SCHECK(list_name_clean("a\tb\x01" "c") == "a bc", "tabs to spaces, control characters out");
  SCHECK(list_name_clean("ÉÉÉÉÉÉÉÉÉ").size() == 16, "cut at a character boundary");

  std::vector<TeamRef> favs = {{(uint8_t) League::NFL, 6}, {(uint8_t) League::NBA, 6}, {(uint8_t) League::NHL, 25}};
  SCHECK(list_mask({{(uint8_t) League::NHL, 25}, {(uint8_t) League::NFL, 6}}, favs) == 0b101, "mask by favorite");
  SCHECK(list_mask({{(uint8_t) League::MLB, 15}}, favs) == 0, "a team that isn't a favorite has no bit");
  std::vector<TeamRef> moved = {{(uint8_t) League::NHL, 25}, {(uint8_t) League::NFL, 6}};
  SCHECK(remap_mask(0b011, favs, moved) == 0b10, "reordered: Dallas moves, the Mavericks left");
  SCHECK(remap_mask(0b001, favs, favs) == 0b001, "unchanged list, same mask");
  SCHECK(list_has(0, false, 7) && list_has(0, true, 7), "no playlist, or an empty one: every favorite");
  SCHECK(list_has(0b10, true, 1) && !list_has(0b10, true, 0), "a playlist: its own favorites");

  // Options after '@'
  l = parse_lists("Sundays=nfl:6@1,11:00,23:00,1,-1;Hoops=nba:6;Bad=nba:6@1,25:00,23:00,0,0", kFavMax);
  SCHECK(l.size() == 3 && l[0].teams.size() == 1 && l[0].has_opts && l[0].opts.days == 1 && l[0].opts.from == 660 &&
             l[0].opts.to == 1380 && l[0].opts.only && l[0].opts.today == -1,
         "options after the teams");
  SCHECK(!l[1].has_opts && l[1].opts == ListOpts{}, "no options: keeps its own");
  SCHECK(l[2].teams.size() == 1 && !l[2].has_opts, "options that don't read: keeps its own, teams still read");
  ListOpts o;
  SCHECK(parse_list_opts("127,00:00,00:00,0,1", o) && o.days == 127 && o.today == 1, "every day, today on");
  for (const char *bad : {"", "1", "128,00:00,00:00,0,0", "1,00:00,00:00,2,0", "1,00:00,00:00,0,2",
                          "1,00:00,00:00,0,-2", "1,0000,00:00,0,0", "1,00:00,00:00,0,0,0", "x,00:00,00:00,0,0"})
    SCHECK(!parse_list_opts(bad, o), "%s should be refused", bad);
  SCHECK(list_opts_text(l[0].opts) == "1,11:00,23:00,1,-1", "options back to text: %s",
         list_opts_text(l[0].opts).c_str());

  // Schedules. Weekdays count from 0, Sunday.
  ListOpts sun;
  sun.days = 1;
  sun.from = 11 * 60;
  sun.to = 23 * 60;
  SCHECK(list_sched_on(sun, 0, 11 * 60) && list_sched_on(sun, 0, 23 * 60 - 1), "inside Sunday's window");
  SCHECK(!list_sched_on(sun, 0, 11 * 60 - 1) && !list_sched_on(sun, 0, 23 * 60) && !list_sched_on(sun, 1, 12 * 60),
         "outside it, and on Monday");
  ListOpts sat;  // all of Saturday
  sat.days = 64;
  SCHECK(list_sched_on(sat, 6, 0) && list_sched_on(sat, 6, 1439) && !list_sched_on(sat, 0, 0) &&
             !list_sched_on(sat, 5, 1439),
         "the same time twice is the whole day");
  ListOpts late;  // Saturday 8 PM to 1 AM
  late.days = 64;
  late.from = 20 * 60;
  late.to = 60;
  SCHECK(list_sched_on(late, 6, 20 * 60) && list_sched_on(late, 0, 30) && !list_sched_on(late, 0, 60) &&
             !list_sched_on(late, 6, 30),
         "past midnight belongs to the day it started");
  SCHECK(!list_sched_on(ListOpts{}, 0, 600), "no days, no schedule");
  ListOpts both[2] = {sat, sun};
  both[0].days = 1;  // all of Sunday, overlapping sun
  SCHECK(list_sched_want(both, 2, 0, 12 * 60) == 1, "the higher in the order wins");
  SCHECK(list_sched_want(both + 1, 1, 0, 12 * 60) == 1 && list_sched_want(both, 2, 1, 12 * 60) == 0, "want");

  // Stepping: on to the scheduled list, back at the end
  ListSched st;
  SCHECK(list_sched_step(st, 0, 0, false) == -1, "no schedule, no change");
  SCHECK(list_sched_step(st, 2, 1, true) == -1 && st.on == 0, "a held game finishes first");
  SCHECK(list_sched_step(st, 2, 1, false) == 1 && st.on == 1 && st.before == 2, "the window starts");
  SCHECK(list_sched_step(st, 1, 1, false) == -1, "inside it, no change");
  SCHECK(list_sched_step(st, 1, 0, true) == -1 && st.on == 1, "the end waits for a held game too");
  SCHECK(list_sched_step(st, 1, 0, false) == 2 && st.on == 0, "back to what played before");
  // A pick by hand inside the window stays when it ends
  st = ListSched{};
  list_sched_step(st, 0, 1, false);
  SCHECK(list_sched_step(st, 3, 1, false) == -1, "a hand pick inside the window stands");
  SCHECK(list_sched_step(st, 3, 0, false) == -1 && st.on == 0, "and stays after it");
  // From one window straight into another, back to what played before both
  st = ListSched{};
  list_sched_step(st, 0, 2, false);
  SCHECK(list_sched_step(st, 2, 1, false) == 1 && st.before == 0, "the next window takes over");
  SCHECK(list_sched_step(st, 1, 0, false) == 0, "and the end goes back to All favorites");
  // Already playing the scheduled list: nothing to switch, nothing to undo
  st = ListSched{};
  SCHECK(list_sched_step(st, 1, 1, false) == -1 && st.on == 1, "already on it");
  SCHECK(list_sched_step(st, 1, 0, false) == -1, "stays on it after");
}

}  // namespace

int run_set_tests(int &total_checks) {
  test_playlists();
  test_parse_set();
  test_live_leagues();
  test_scan_leagues();
  test_no_team();
  test_live_scan_changed();
  test_fixture_cases();
  total_checks += checks;
  return failures;
}
