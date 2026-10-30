// The settings backup (components/gameday/settings_doc.h): the document GET
// /gameday/settings writes from the state, and the checks POST
// /gameday/settings makes before it applies anything.
#include <ArduinoJson.h>

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "settings_doc.h"

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

// A release build on two panels wide, as gameday-common.yaml builds it.
// It is the panel the settings fixture was saved from.
SettingsTarget full_target() {
  SettingsTarget t;
  t.name = "gameday-a1b2c3";
  t.fav_max = 16;
  t.panels = 2;
  t.max_panels = 2;
  t.autoupd = true;
  t.beta = true;
  t.brightness = true;
  t.scroll = true;
  return t;
}

SettingsImport import(const std::string &json, const SettingsTarget &t = full_target()) {
  JsonDocument doc;
  SettingsImport in;
  if (deserializeJson(doc, json)) {
    in.error = "test: the JSON doesn't parse";
    return in;
  }
  parse_settings(doc.as<JsonVariantConst>(), t, in);
  return in;
}

const SetOp *find(const SettingsImport &in, SetKind kind) {
  for (const auto &op : in.ops)
    if (op.kind == kind)
      return &op;
  return nullptr;
}

bool skipped(const SettingsImport &in, const std::string &what) {
  for (const auto &s : in.skipped)
    if (s == what)
      return true;
  return false;
}

// Refused: an error, and nothing to apply.
void refused(const std::string &json, const char *why) {
  SettingsImport in = import(json);
  SCHECK(!in.error.empty() && in.ops.empty() && in.brightness < 0 && in.scroll < 0, "%s was accepted: %s", why,
         json.c_str());
}

// The export of the state fixture is the settings fixture, key for key, and
// every key in it is a set key or one of the backup's own.
void test_export() {
  JsonDocument state, fixture, out;
  SCHECK(!deserializeJson(state, slurp("fixtures/api/state.json")), "fixtures/api/state.json doesn't parse");
  SCHECK(!deserializeJson(fixture, slurp("fixtures/api/settings.json")), "fixtures/api/settings.json doesn't parse");
  settings_from_state(state.as<JsonObjectConst>(), 0, 128, 5, out);
  std::string got, want;
  serializeJson(out, got);
  serializeJson(fixture, want);
  SCHECK(got == want,
         "the export of fixtures/api/state.json isn't fixtures/api/settings.json (the companion apps read it):\n"
         "  got  %s\n  want %s",
         got.c_str(), want.c_str());

  std::set<std::string> set_keys(std::begin(kSetKeys), std::end(kSetKeys));
  for (JsonPairConst p : out.as<JsonObjectConst>()) {
    std::string k = p.key().c_str();
    bool own = k == "schema" || k == "version" || k == "name" || k == "brightness" || k == "scroll";
    SCHECK(own || set_keys.count(k), "the export writes %s, which POST /gameday/set doesn't take", k.c_str());
  }
  // Every set key but the remote's four slots (favs has them) is exported.
  for (const char *k : kSetKeys) {
    std::string key = k;
    bool slot = key.size() == 4 && key.compare(0, 3, "fav") == 0;
    SCHECK(slot || !out[k].isNull(), "the export leaves out %s; add it to settings_from_state", k);
  }

  // An empty slot between two favorites, a live list of its own, and a build
  // without the numbers or a panel layout. With no favorite picked the
  // state's live list is every league; the backup keeps the panel's own.
  JsonDocument s2, out2;
  deserializeJson(s2, R"({"favs":[{"l":"nfl","id":6},{},{"l":"mlb","id":15},{},{}],
    "live":["nfl","ncaa","mlb","mls","epl","nba","wnba","mcbb","nhl"],"live_auto":false,"panels":0,
    "tz_name":"US Eastern","tz_auto":false})");
  uint16_t mask = (uint16_t) ((1u << (uint8_t) League::MLB) | (1u << (uint8_t) League::NHL));
  settings_from_state(s2.as<JsonObjectConst>(), mask, -1, -1, out2);
  std::string got2;
  serializeJson(out2, got2);
  SCHECK(got2 ==
             R"({"schema":1,"favs":["nfl:6","","mlb:15"],"live":["mlb","nhl"],"tz":"US Eastern","tzauto":false})",
         "export with an empty slot: %s", got2.c_str());
}

void test_types() {
  for (const char *k : kSetKeys)
    SCHECK(setting_type(k) != SettingType::NONE, "set key %s has no type in settings_doc.h's setting_type", k);
  SCHECK(setting_type("brightness") == SettingType::NONE, "brightness is not a set key");
}

// The fixture comes back whole: nothing refused or left out, every set key
// in it an op, in kSetKeys order.
void test_round_trip() {
  SettingsImport in = import(slurp("fixtures/api/settings.json"));
  SCHECK(in.error.empty(), "the settings fixture was refused: %s", in.error.c_str());
  SCHECK(in.skipped.empty(), "the settings fixture left out %zu keys, the first %s", in.skipped.size(),
         in.skipped.empty() ? "" : in.skipped[0].c_str());
  // Every set key but fav1 to fav4, which favs covers
  size_t want = sizeof(kSetKeys) / sizeof(kSetKeys[0]) - 4;
  SCHECK(in.ops.size() == want, "the fixture gives %zu ops, not %zu", in.ops.size(), want);
  SCHECK(in.brightness == 128 && in.scroll == 5, "brightness %d scroll %d", in.brightness, in.scroll);
  SCHECK(!in.restart, "the same panel count restarts the panel");
  SCHECK(!in.ops.empty() && in.ops[0].kind == SetKind::MODE && in.ops[0].value == 4, "the first op isn't the mode");
  const SetOp *favs = find(in, SetKind::FAVS);
  SCHECK(favs != nullptr && favs->teams.size() == 3 && favs->teams[1].id == 251, "favs came back wrong");
  const SetOp *tz = find(in, SetKind::TZ);
  SCHECK(tz != nullptr && std::string(kTimezones[tz->value].name) == "US Central", "the time zone came back wrong");
  const SetOp *live = find(in, SetKind::LIVE);
  SCHECK(live != nullptr && live->mask == 0, "an empty live list isn't auto");
  const SetOp *start = find(in, SetKind::NIGHT_START);
  SCHECK(start != nullptr && start->value == 23 * 60, "nightstart came back wrong");
  const SetOp *url = find(in, SetKind::MSGURL);
  SCHECK(url != nullptr && url->text == "http://homeassistant.local:8123/local/gameday.txt", "msgurl came back wrong");

  // The document's order doesn't matter.
  SettingsImport b = import(R"({"schema":1,"name":"gameday-a1b2c3","panels":1,"rotate":5,"mode":4})");
  SCHECK(b.error.empty() && b.ops.size() == 3 && b.ops[0].kind == SetKind::MODE && b.ops[1].kind == SetKind::ROTATE &&
             b.ops[2].kind == SetKind::PANELS,
         "ops aren't in kSetKeys order");
  SCHECK(b.restart, "two panels to one doesn't restart the panel");

  SettingsImport empty = import(R"({"schema":1})");
  SCHECK(empty.error.empty() && empty.ops.empty() && empty.skipped.empty(), "a document with only its schema");
}

void test_favs() {
  // The remote's slots keep their teams: an empty slot stays empty.
  SettingsImport in = import(R"({"schema":1,"favs":["nfl:6","","mlb:15","none"]})");
  const SetOp *f = find(in, SetKind::FAVS);
  SCHECK(in.error.empty() && f != nullptr && f->teams.size() == 4 && f->teams[0].id == 6 && f->teams[1].id == 0 &&
             f->teams[2].id == 15 && f->teams[3].id == 0,
         "an empty favorite slot moved the teams after it");
  // A team this firmware doesn't have empties its slot and is listed.
  in = import(R"({"schema":1,"favs":["nfl:6","epl:999999","mlb:15"]})");
  f = find(in, SetKind::FAVS);
  SCHECK(in.error.empty() && f != nullptr && f->teams.size() == 3 && f->teams[1].id == 0 && f->teams[2].id == 15,
         "an unknown favorite wasn't left as an empty slot");
  SCHECK(skipped(in, "favs=epl:999999"), "the unknown favorite isn't listed");
  in = import(R"({"schema":1,"favs":["xfl:5"]})");
  SCHECK(in.error.empty() && skipped(in, "favs=xfl:5"), "a league this firmware doesn't know isn't listed");
  in = import(R"({"schema":1,"favs":[]})");
  f = find(in, SetKind::FAVS);
  SCHECK(in.error.empty() && f != nullptr && f->teams.empty(), "an empty list doesn't clear the favorites");
  refused(R"({"schema":1,"favs":["nfl:6","nfl:6"]})", "a favorite listed twice");
  refused(R"({"schema":1,"favs":["dallas"]})", "a favorite that isn't a team");
  refused(R"({"schema":1,"favs":"nfl:6"})", "favs as text");
  refused(R"({"schema":1,"favs":[6]})", "favs as numbers");
  // Past FAV_MAX: the first 16 come back, the rest are listed.
  std::string seventeen = R"({"schema":1,"favs":[)";
  for (int i = 0; i < 16; i++)
    seventeen += std::string(i ? "," : "") + "\"\"";
  in = import(seventeen + R"(,"nfl:6"]})");
  f = find(in, SetKind::FAVS);
  SCHECK(in.error.empty() && f != nullptr && f->teams.size() == 16 && skipped(in, "favs=nfl:6"),
         "a 17th favorite wasn't left out and listed");
}

void test_refused() {
  refused("[]", "a list");
  refused(R"("hello")", "text");
  refused(R"({"mode":4})", "no schema");
  refused(R"({"schema":"1","mode":4})", "schema as text");
  refused(R"({"schema":0})", "schema 0");
  SettingsImport newer = import(R"({"schema":2,"mode":4})");
  SCHECK(newer.error.find("newer firmware") != std::string::npos, "schema 2 says: %s", newer.error.c_str());

  // Wrong types
  refused(R"({"schema":1,"mode":"4"})", "mode as text");
  refused(R"({"schema":1,"down":1})", "a switch as a number");
  refused(R"({"schema":1,"down":"true"})", "a switch as text");
  refused(R"({"schema":1,"tz":1})", "tz as an index");
  refused(R"({"schema":1,"live":"nfl"})", "live as text");
  refused(R"({"schema":1,"rotate":2.5})", "a fraction");
  refused(R"({"schema":1,"brightness":"128"})", "brightness as text");

  // Out of range, where a single set would clamp: left out and listed, the
  // rest applied (newer firmware may take more).
  const char *const out_of_range[] = {
      R"({"schema":1,"mode":6,"down":false})",       R"({"schema":1,"mode":-1,"down":false})",
      R"({"schema":1,"rotate":0,"down":false})",     R"({"schema":1,"lockon":121,"down":false})",
      R"({"schema":1,"release":10,"down":false})",   R"({"schema":1,"msgevery":31,"down":false})",
      R"({"schema":1,"nightlevel":0,"down":false})", R"({"schema":1,"nightlevel":256,"down":false})",
      R"({"schema":1,"collide":2,"down":false})",    R"({"schema":1,"panels":3,"down":false})",
      R"({"schema":1,"panels":0,"down":false})",     R"({"schema":1,"brightness":0,"down":false})",
      R"({"schema":1,"brightness":256,"down":false})", R"({"schema":1,"scroll":11,"down":false})"};
  for (const char *doc : out_of_range) {
    SettingsImport in = import(doc);
    SCHECK(in.error.empty() && in.ops.size() == 1 && in.ops[0].kind == SetKind::DOWN && in.skipped.size() == 1 &&
               in.brightness < 0 && in.scroll < 0 && !in.restart,
           "out of range wasn't left out alone: %s (error '%s', %zu ops, %zu left out)", doc, in.error.c_str(),
           in.ops.size(), in.skipped.size());
  }
  SCHECK(skipped(import(R"({"schema":1,"mode":6})"), "mode=6"), "mode=6 isn't listed");
  SCHECK(skipped(import(R"({"schema":1,"scroll":11})"), "scroll=11"), "scroll=11 isn't listed");
  {
    // An After the game choice from newer firmware is left out, not a refusal;
    // a lead out of range is left out rather than clamped.
    SettingsImport in = import(R"({"schema":1,"after":"effects","wake":true,"wakelead":300})");
    SCHECK(in.error.empty() && skipped(in, "after=effects") && skipped(in, "wakelead=300") && in.ops.size() == 1 &&
               in.ops[0].kind == SetKind::WAKE && in.ops[0].on,
           "after=effects: error '%s', %zu ops", in.error.c_str(), in.ops.size());
    SettingsImport ok = import(R"({"schema":1,"after":"clock","wakelead":45})");
    const SetOp *after = find(ok, SetKind::AFTER);
    const SetOp *lead = find(ok, SetKind::WAKE_LEAD);
    SCHECK(after != nullptr && after->value == (int) After::CLOCK && lead != nullptr && lead->value == 45,
           "after=clock and wakelead=45 restore");
  }

  refused(R"({"schema":1,"brightness":40,"scroll":"5"})", "a good brightness with a bad scroll");

  // Values the panel doesn't take
  refused(R"({"schema":1,"fav2":"dallas"})", "a slot that isn't a team");
  refused(R"({"schema":1,"nightstart":"25:00"})", "nightstart 25:00");
  refused(R"({"schema":1,"nightend":"7am"})", "nightend 7am");

  // One bad key refuses the good ones with it.
  refused(R"({"schema":1,"mode":4,"favs":["nfl:6"],"down":"yes"})", "a document with one bad key");
}

// What newer firmware or another build has is left out and listed; the rest
// is applied.
void test_left_out() {
  SettingsImport in = import(R"({"schema":1,"team":"nfl:999999","mode":1,"sparkle":true,"tz":"Mars/Olympus",
    "live":["nfl","xfl"],"fav1":"nba:999999","created":"2027-01-01"})");
  SCHECK(in.error.empty(), "refused: %s", in.error.c_str());
  SCHECK(skipped(in, "team"), "My team's team isn't listed as left out");
  SCHECK(skipped(in, "fav1=nba:999999") && find(in, SetKind::FAV_SLOT) == nullptr, "an unknown slot was applied");
  SCHECK(skipped(in, "sparkle") && skipped(in, "created"), "unknown keys aren't listed");
  SCHECK(skipped(in, "tz=Mars/Olympus") && find(in, SetKind::TZ) == nullptr, "an unknown time zone was applied");
  const SetOp *live = find(in, SetKind::LIVE);
  SCHECK(skipped(in, "live=xfl") && live != nullptr && live->mask == (1u << (uint8_t) League::NFL),
         "live wasn't kept to the leagues this firmware has");
  SCHECK(find(in, SetKind::MODE) != nullptr && find(in, SetKind::MODE)->value == 1, "mode wasn't applied");
  // A backup from when My team was a mode comes back as Favorites.
  SettingsImport old = import(R"({"schema":1,"mode":0,"team":"nfl:6"})");
  const SetOp *old_mode = find(old, SetKind::MODE);
  SCHECK(old_mode != nullptr && old_mode->value == 4 && skipped(old, "team"),
         "My team's mode 0 didn't restore as Favorites");

  // A message list address this firmware can't read (newer firmware may take
  // another scheme or a longer one) is left out like a time zone it lacks.
  in = import(R"({"schema":1,"msgurl":"ftp://example.com/list.txt","down":false})");
  SCHECK(in.error.empty() && skipped(in, "msgurl=ftp://example.com/list.txt") && find(in, SetKind::MSGURL) == nullptr &&
             find(in, SetKind::DOWN) != nullptr,
         "an address this firmware can't read wasn't left out alone: '%s'", in.error.c_str());
  refused(R"({"schema":1,"msgurl":7})", "msgurl as a number");

  // The Between games list: a card from newer firmware is left out and
  // listed, the rest applied; a card the backup doesn't name stays as it is.
  in = import(R"({"schema":1,"cards":{"next":false,"standings":true,"news":true}})");
  const SetOp *cards = find(in, SetKind::CARDS);
  SCHECK(in.error.empty() && skipped(in, "cards=standings") && cards != nullptr &&
             cards->mask == (card_bit(Card::NEXT) | card_bit(Card::NEWS)) && cards->value == card_bit(Card::NEWS),
         "the cards list didn't come back as next off, news on: '%s'", in.error.c_str());
  in = import(R"({"schema":1,"cards":{"standings":true},"down":false})");
  SCHECK(in.error.empty() && skipped(in, "cards=standings") && find(in, SetKind::CARDS) == nullptr &&
             find(in, SetKind::DOWN) != nullptr,
         "a list naming only a card this firmware lacks wasn't left out alone");
  // A backup from before the list: the countdown's own key still restores it.
  in = import(R"({"schema":1,"countdown":false})");
  SCHECK(in.error.empty() && find(in, SetKind::COUNTDOWN) != nullptr && find(in, SetKind::CARDS) == nullptr,
         "a backup from before the list didn't restore the countdown");
  refused(R"({"schema":1,"cards":["next"]})", "cards as a list");
  refused(R"({"schema":1,"cards":{"next":1}})", "a card as a number");

  // A bar or demo build: no update switches, no panel layout, no numbers.
  SettingsTarget bare;
  in = import(R"({"schema":1,"autoupd":true,"beta":true,"panels":2,"brightness":40,"scroll":3,"down":false})", bare);
  SCHECK(in.error.empty() && in.ops.size() == 1 && find(in, SetKind::DOWN) != nullptr, "bare build applied %zu ops",
         in.ops.size());
  SCHECK(skipped(in, "autoupd") && skipped(in, "beta") && skipped(in, "panels") && skipped(in, "brightness=40") &&
             skipped(in, "scroll=3"),
         "a bare build didn't list what it can't take");
  SCHECK(in.brightness < 0 && in.scroll < 0 && !in.restart, "a bare build kept numbers it can't set");
  // Still checked when the build can't take them.
  refused(R"({"schema":1,"brightness":"900"})", "brightness as text on any build");
}

// The nickname and the panel layout are the panel's own: a file saved from
// another panel leaves them as they are and says so, one from this panel
// (after a factory reset) brings them back.
void test_other_panel() {
  std::string other = slurp("fixtures/api/settings.json");
  size_t at = other.find("gameday-a1b2c3");
  SCHECK(at != std::string::npos, "the settings fixture has no name");
  if (at != std::string::npos)
    other.replace(at, 14, "gameday-ffffff");
  SettingsTarget t = full_target();
  SettingsImport in = import(other, t);
  SCHECK(in.error.empty() && in.skipped.empty(), "another panel's file: '%s', %zu left out", in.error.c_str(),
         in.skipped.size());
  SCHECK(find(in, SetKind::NICK) == nullptr && find(in, SetKind::PANELS) == nullptr && !in.restart,
         "another panel's nickname or layout was applied");
  // The same count of panels has nothing to keep, so only the nickname is listed.
  SCHECK(in.kept.size() == 1 && in.kept[0] == "nick", "kept %zu, the first %s", in.kept.size(),
         in.kept.empty() ? "" : in.kept[0].c_str());
  SCHECK(find(in, SetKind::MODE) != nullptr && find(in, SetKind::FAVS) != nullptr, "the owner's settings didn't come");
  // A nickname the same as this panel's isn't listed.
  t.nick = "Living room";
  in = import(other, t);
  SCHECK(in.error.empty() && in.kept.empty(), "the same nickname was listed as kept");
  // A 64x64 panel's file onto a 128x64 one keeps its two panels.
  in = import(R"({"schema":1,"name":"gameday-ffffff","panels":1,"down":false})");
  SCHECK(in.error.empty() && in.kept.size() == 1 && in.kept[0] == "panels" && !in.restart &&
             find(in, SetKind::PANELS) == nullptr && in.ops.size() == 1,
         "another panel's layout: %zu kept, %zu ops", in.kept.size(), in.ops.size());
  // No name says nothing about where the file came from: kept too.
  in = import(R"({"schema":1,"panels":1,"nick":"Kitchen"})");
  SCHECK(in.error.empty() && in.kept.size() == 2 && in.ops.empty() && !in.restart, "a file with no name: %zu kept",
         in.kept.size());
  // This panel's own file, after a reset: both come back.
  in = import(R"({"schema":1,"name":"gameday-a1b2c3","panels":1,"nick":"Kitchen"})");
  SCHECK(in.error.empty() && in.kept.empty() && in.restart && find(in, SetKind::NICK) != nullptr &&
             find(in, SetKind::NICK)->text == "Kitchen" && find(in, SetKind::PANELS) != nullptr,
         "this panel's own file didn't restore its nickname and layout");
  // Kept or not, a value of the wrong type still refuses the file, and a
  // layout out of range is still left out.
  refused(R"({"schema":1,"name":"gameday-ffffff","panels":"2"})", "another panel's layout as text");
  refused(R"({"schema":1,"name":"gameday-ffffff","nick":5})", "another panel's nickname as a number");
  in = import(R"({"schema":1,"name":"gameday-ffffff","panels":3})");
  SCHECK(in.error.empty() && skipped(in, "panels=3") && in.kept.empty(), "a third panel wasn't left out");
}

// The same start and end would leave the schedule on and never running: the
// end is left out, and the panel keeps its own.
void test_night_hours() {
  SettingsImport in = import(R"({"schema":1,"night":true,"nightstart":"23:00","nightend":"23:00"})");
  SCHECK(in.error.empty() && skipped(in, "nightend=23:00") && find(in, SetKind::NIGHT_END) == nullptr &&
             find(in, SetKind::NIGHT_START) != nullptr && find(in, SetKind::NIGHT) != nullptr,
         "the same start and end wasn't left out: '%s'", in.error.c_str());
  in = import(R"({"schema":1,"nightstart":"22:00","nightend":"06:30"})");
  SCHECK(in.error.empty() && in.skipped.empty() && find(in, SetKind::NIGHT_END) != nullptr &&
             find(in, SetKind::NIGHT_END)->value == 6 * 60 + 30,
         "good hours didn't come back");
}

// Playlists come back by name and team, as lists= text in the order given.
void test_playlists() {
  SettingsImport in = import(R"({"schema":1,"lists":[{"n":"Pros","t":["nfl:6","mlb:999999"]},{"n":" ; ","t":["nfl:6"]},
    {"n":"Sun=day;s","t":[]}],"list":2})");
  SCHECK(in.error.empty(), "refused: %s", in.error.c_str());
  const SetOp *lists = find(in, SetKind::LISTS);
  SCHECK(lists != nullptr && lists->text == "Pros=nfl:6;Sundays=", "lists came back as %s",
         lists != nullptr ? lists->text.c_str() : "nothing");
  SCHECK(skipped(in, "lists=Pros:mlb:999999") && skipped(in, "lists="), "an unknown team or a nameless playlist isn't listed");
  SCHECK(find(in, SetKind::LIST) != nullptr && find(in, SetKind::LIST)->value == 2, "list wasn't applied");
  std::string many = R"({"schema":1,"lists":[)";
  for (int i = 0; i <= kListMax; i++)
    many += std::string(i ? "," : "") + R"({"n":"P)" + std::to_string(i) + R"(","t":[]})";
  in = import(many + "]}");
  SCHECK(in.error.empty() && skipped(in, "lists=P16"), "the playlist past kListMax isn't listed");
  in = import(R"({"schema":1,"list":17})");
  SCHECK(skipped(in, "list=17") && find(in, SetKind::LIST) == nullptr, "a list past 16 was applied");
  refused(R"({"schema":1,"lists":"Pros=nfl:6"})", "lists as text");
  refused(R"({"schema":1,"lists":[{"n":"Pros"}]})", "a playlist with no teams list");
  refused(R"({"schema":1,"lists":[{"n":"Pros","t":["cowboys"]}]})", "a playlist team that isn't a team");
  refused(R"({"schema":1,"lists":[{"n":"Pros","t":[6]}]})", "a playlist team as a number");

  // The sport row playing, by its word; one this firmware has no row for is listed.
  in = import(R"({"schema":1,"list":0,"sport":"basketball"})");
  SCHECK(in.error.empty() && find(in, SetKind::SPORT) != nullptr && find(in, SetKind::SPORT)->value == 3,
         "sport wasn't applied");
  in = import(R"({"schema":1,"sport":"racing"})");
  SCHECK(in.error.empty() && skipped(in, "sport=racing") && find(in, SetKind::SPORT) == nullptr,
         "a sport this firmware doesn't know wasn't listed");
  refused(R"({"schema":1,"sport":3})", "sport as a number");
  JsonDocument st, out;
  deserializeJson(st, R"({"sport":"hockey"})");
  settings_from_state(st.as<JsonObjectConst>(), 0, -1, -1, out);
  SCHECK(std::string(out["sport"] | "") == "hockey", "the export left the sport row out");
  // Each playlist's options come back as their tokens (playlists.h ListOpt).
  in = import(R"({"schema":1,"lists":[{"n":"Sundays","t":["nfl:6"],"days":"0","start":"11:00","end":"23:00","only":true,"today":false},
    {"n":"Late","t":[],"days":"60","only":false},{"n":"Plain","t":["nfl:6"]}]})");
  SCHECK(in.error.empty(), "refused: %s", in.error.c_str());
  lists = find(in, SetKind::LISTS);
  SCHECK(lists != nullptr && lists->text == "Sundays=nfl:6,@d:0,@t:11:00-23:00,@only,@today:0;Late=,@d:06;Plain=nfl:6",
         "options came back as %s", lists != nullptr ? lists->text.c_str() : "nothing");
  in = import(R"({"schema":1,"lists":[{"n":"A","t":[],"days":"1","start":"25:00","end":"23:00"}]})");
  SCHECK(in.error.empty() && skipped(in, "lists=A:25:00-23:00") && find(in, SetKind::LISTS)->text == "A=,@d:1",
         "hours the panel can't read are left out and listed");
  refused(R"({"schema":1,"lists":[{"n":"A","t":[],"days":1}]})", "days as a number");
  refused(R"({"schema":1,"lists":[{"n":"A","t":[],"start":"11:00"}]})", "a start with no end");
  refused(R"({"schema":1,"lists":[{"n":"A","t":[],"only":"yes"}]})", "only as text");
  refused(R"({"schema":1,"lists":[{"n":"A","t":[],"today":1}]})", "today as a number");
  // The export writes only what is set, so a backup from firmware without
  // the options reads the same (settings_from_state).
  JsonDocument state, exported;
  deserializeJson(state, R"({"favs":[{"l":"nfl","id":6}],"lists":[{"n":"A","f":[0],"days":"0","start":"11:00","end":"23:00","only":true,"today":true},
    {"n":"B","f":[0],"days":"","start":"00:00","end":"00:00","only":false},{"n":"C","f":[],"days":"5"}]})");
  settings_from_state(state.as<JsonObjectConst>(), 0, -1, -1, exported);
  std::string text;
  serializeJson(exported["lists"], text);
  SCHECK(text == R"([{"n":"A","t":["nfl:6"],"days":"0","start":"11:00","end":"23:00","only":true,"today":true},{"n":"B","t":["nfl:6"]},{"n":"C","t":[],"days":"5"}])",
         "the export's options: %s", text.c_str());
}

// The WizMote's buttons (remote1 to remote4): text the panel takes, and a
// team or sport it doesn't know left out and listed.
void test_remote() {
  SettingsImport in = import(R"({"schema":1,"remote1":"team:nfl:6","remote2":"list:Sundays","remote3":"sport:hockey","remote4":"none"})");
  SCHECK(in.error.empty() && in.ops.size() == 4 && in.ops[1].kind == SetKind::REMOTE && in.ops[1].slot == 2 &&
             in.ops[1].text == "list:Sundays",
         "remote keys came back wrong: %s", in.error.c_str());
  in = import(R"({"schema":1,"remote1":"team:xfl:5","remote2":"sport:curling","remote3":"fav"})");
  SCHECK(in.error.empty() && skipped(in, "remote1=team:xfl:5") && skipped(in, "remote2=sport:curling") &&
             in.ops.size() == 1,
         "an unknown team or sport isn't left out and listed");
  refused(R"({"schema":1,"remote1":1})", "a remote button as a number");
}

// The fullest export the panel can write comes back through its own import:
// 16 favorites with the longest league and team id, all of them in 16
// playlists, and every text at its cap in characters JSON escapes.
void test_fullest_fits() {
  JsonDocument state, out;
  SCHECK(!deserializeJson(state, slurp("fixtures/api/state.json")), "fixtures/api/state.json doesn't parse");
  JsonArray favs = state["favs"].to<JsonArray>();
  for (int i = 0; i < 16; i++) {
    JsonObject f = favs.add<JsonObject>();
    f["l"] = "wnba";
    f["id"] = 100000 + i;
  }
  JsonArray lists = state["lists"].to<JsonArray>();
  for (int i = 0; i < kListMax; i++) {
    JsonObject l = lists.add<JsonObject>();
    l["n"] = std::string(kListNameMax, '"');
    JsonArray f = l["f"].to<JsonArray>();
    for (int j = 0; j < 16; j++)
      f.add(j);
    l["days"] = "0123456";
    l["start"] = "11:00";
    l["end"] = "23:00";
    l["only"] = true;
    l["today"] = true;
  }
  state["list"] = kListMax;
  state["msg"] = std::string(kMsgTextMax, '"');
  state["msgurl"] = "http://" + std::string(kMsgUrlMax - 7, 'a');
  state["nick"] = std::string(kNickMax, '"');
  settings_from_state(state.as<JsonObjectConst>(), 0, 255, 10, out);
  size_t bytes = measureJson(out);
  SCHECK(bytes <= kSettingsMaxBytes, "the fullest backup is %zu bytes, past kSettingsMaxBytes (%zu)", bytes,
         kSettingsMaxBytes);
}

// Counts what the reply allocates through the hook the firmware points at PSRAM.
struct CountingAllocator : ArduinoJson::Allocator {
  int calls = 0;
  int live = 0;
  void *allocate(size_t size) override {
    calls++;
    live++;
    return malloc(size);
  }
  void deallocate(void *ptr) override {
    if (ptr != nullptr)
      live--;
    free(ptr);
  }
  void *reallocate(void *ptr, size_t size) override {
    if (ptr == nullptr) {
      calls++;
      live++;
    }
    return realloc(ptr, size);
  }
};

void test_reply() {
  SettingsImport in = import(R"({"schema":1,"name":"gameday-a1b2c3","panels":1,"sparkle":1})");
  SCHECK(settings_reply(in) == R"({"ok":true,"restart":true,"skipped":["sparkle"],"kept":[]})", "reply: %s",
         settings_reply(in).c_str());
  in = import(R"({"schema":1,"name":"gameday-ffffff","panels":1,"nick":"Kitchen"})");
  SCHECK(settings_reply(in) == R"({"ok":true,"restart":false,"skipped":[],"kept":["panels","nick"]})", "reply: %s",
         settings_reply(in).c_str());
  CountingAllocator counting;
  set_json_allocator(&counting);
  std::string reply = settings_reply(in);
  set_json_allocator(nullptr);
  SCHECK(!reply.empty() && counting.calls > 0 && counting.live == 0,
         "the reply didn't use the PSRAM hook (%d calls, %d live)", counting.calls, counting.live);
  in = import(R"({"schema":1,"mode":"x"})");
  SCHECK(settings_reply(in) == R"({"error":"mode should be a whole number."})", "reply: %s",
         settings_reply(in).c_str());
}

}  // namespace

int main() {
  test_types();
  test_export();
  test_round_trip();
  test_favs();
  test_refused();
  test_left_out();
  test_other_panel();
  test_night_hours();
  test_playlists();
  test_remote();
  test_fullest_fits();
  test_reply();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
