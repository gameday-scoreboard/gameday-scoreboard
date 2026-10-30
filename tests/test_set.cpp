// The device page's set requests and Live mode's league pick
// (components/gameday/set_request.h). The table in fixtures/set_cases.json is
// the same one tests/web/mock_device.py checks itself against.
#include <ArduinoJson.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
  bool bootaddr{true}, autoupd{false}, beta{false}, countdown{true}, top25{true}, news{false}, gameleaders{true};
  uint16_t cards_off{0};  // the Between games list's other cards (cards.h)
  bool field{true}, winbar{true}, spark{true};
  std::string cdname;
  uint32_t cdymd{0};
  TeamRef favs[16];
  // Playlists as the firmware keeps them: a name, a mask over favs and the
  // options (Prefs9 and Prefs10).
  struct List {
    std::string first;
    uint16_t second;
    ListOpt opt;
  };
  std::vector<List> lists;
  int list{0};
  int sport{0};  // the sport row playing, 1 + Sport; 0 = none
  RemoteTarget remote[4];  // the WizMote's buttons (Prefs11)
  uint16_t live_mask{0};
  std::string msg, msgurl, nick, hook;
  std::string town;
  bool wxon{false};
  int msgevery{kMsgEveryDefault};
  int pregame{kPregameDefault};
  NightPrefs night{night_defaults()};
  WakePrefs wake{wake_defaults()};
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

// GamedayComponent::card_on and set_card: a card that kept its own switch
// reads and writes that one.
bool *own_switch(Settings &s, Card c) {
  switch (c) {
    case Card::COUNTDOWN: return &s.countdown;
    case Card::TOP25: return &s.top25;
    case Card::NEWS: return &s.news;
    case Card::LEADERS: return &s.gameleaders;
    case Card::WEATHER: return &s.wxon;
    default: return nullptr;
  }
}

bool card_on(const Settings &s, Card c) {
  const bool *own = own_switch(const_cast<Settings &>(s), c);
  return own != nullptr ? *own : (s.cards_off & card_bit(c)) == 0;
}

void set_card(Settings &s, Card c, bool on) {
  if (bool *own = own_switch(s, c))
    *own = on;
  else if (on)
    s.cards_off &= (uint16_t) ~card_bit(c);
  else
    s.cards_off |= card_bit(c);
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
      ListOpt opt;
      if (l["days"].is<const char *>())
        list_opt_token("@d:" + std::string(l["days"].as<const char *>()), opt);
      if (l["start"].is<const char *>())
        list_opt_token("@t:" + std::string(l["start"].as<const char *>()) + "-" + (l["end"] | "00:00"), opt);
      if (l["only"] | false)
        opt.flags |= LISTOPT_ONLY;
      if (l["today"].is<bool>())
        list_opt_token(l["today"].as<bool>() ? "@today:1" : "@today:0", opt);
      s.lists.push_back({l["n"].as<const char *>(), mask, opt});
    }
  }
  if (o["list"].is<int>())
    s.list = o["list"].as<int>();
  if (o["sport"].is<const char *>())
    s.sport = sport_row_from_key(o["sport"].as<const char *>());
  if (o["remote"].is<JsonArrayConst>()) {
    int b = 0;
    for (JsonObjectConst r : o["remote"].as<JsonArrayConst>())
      if (b < 4)
        remote_parse(r["v"] | "fav", s.remote[b++]);
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
  flag("field", s.field);
  flag("winbar", s.winbar);
  flag("spark", s.spark);
  flag("bootaddr", s.bootaddr);
  flag("autoupd", s.autoupd);
  flag("beta", s.beta);
  flag("countdown", s.countdown);
  flag("top25", s.top25);
  flag("news", s.news);
  flag("gameleaders", s.gameleaders);
  flag("wxon", s.wxon);
  if (o["town"].is<const char *>())
    s.town = o["town"].as<const char *>();
  // As the backup lists them: {"next": true, ...}.
  for (JsonPairConst p : o["cards"].as<JsonObjectConst>())
    if (const CardRow *r = card_row(p.key().c_str()))
      set_card(s, r->card, p.value().as<bool>());
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
  bool wake = s.wake.on != 0;
  flag("wake", wake);
  s.wake.on = wake ? 1 : 0;
  int lead = s.wake.lead;
  num("wakelead", lead);
  s.wake.lead = (uint8_t) lead;
  if (o["after"].is<const char *>())
    s.wake.after = (uint8_t) after_parse(o["after"].as<const char *>());
}

std::vector<TeamRef> fav_refs(const Settings &s) { return std::vector<TeamRef>(s.favs, s.favs + kFavMax); }

// sport_rows_: the sport rows the favorites give Playing.
uint8_t rows(const Settings &s) {
  uint8_t sports = 0;
  for (const auto &f : s.favs)
    if (f.id != 0)
      sports |= sport_bit(f.league);
  return sport_rows(sports);
}

// remap_lists_: each playlist follows its teams through a favorites change,
// and a sport row the favorites have no row for stops.
void remap(Settings &s, const std::vector<TeamRef> &before) {
  for (auto &l : s.lists)
    l.second = remap_mask(l.second, before, fav_refs(s));
  if (!sport_row_has(rows(s), (uint8_t) s.sport))
    s.sport = 0;
}

void apply(Settings &s, const SetOp &op, bool with_lists, int keep_list, bool sport_pick) {
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
      std::string active = s.list > 0 ? s.lists[s.list - 1].first : "";
      s.lists.clear();
      s.list = 0;
      for (const auto &d : parse_lists(op.text, kFavMax)) {
        s.lists.push_back({d.name, list_mask(d.teams, before), d.opt});
        if (!active.empty() && d.name == active)
          s.list = (int) s.lists.size();
      }
      if (keep_list >= 0 && keep_list <= (int) s.lists.size()) {
        s.list = keep_list;
        s.sport = 0;
      }
      break;
    }
    case SetKind::REMOTE: {
      // set_remote_: a playlist's name as the playlists keep it.
      RemoteTarget t;
      remote_parse(op.text, t);
      if (t.kind == (uint8_t) RemoteKind::LIST) {
        std::string name = list_name_clean(remote_name(t));
        memset(t.name, 0, sizeof(t.name));
        memcpy(t.name, name.data(), name.size());
        if (name.empty())
          t = RemoteTarget{};
      }
      s.remote[op.slot - 1] = t;
      break;
    }
    case SetKind::LIST:
      // set_active_list_ switches to Favorite teams; a number past the last
      // playlist is refused. With lists= set_lists_ took it, and with a
      // sport row picked, sport= does.
      if (!with_lists && !sport_pick && op.value <= (int) s.lists.size()) {
        s.list = op.value;
        s.sport = 0;
        s.mode = (int) Mode::FAVORITES;
      }
      break;
    case SetKind::SPORT:
      // set_sport_row_ plays a row the favorites give; none stops the one
      // playing unless list= says what plays.
      if (op.value != 0 ? sport_row_has(rows(s), (uint8_t) op.value) : keep_list < 0 && s.sport != 0) {
        s.list = 0;
        s.sport = op.value;
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
    case SetKind::FIELD: s.field = op.on; break;
    case SetKind::WINBAR: s.winbar = op.on; break;
    case SetKind::SPARK: s.spark = op.on; break;
    case SetKind::BOOTADDR: s.bootaddr = op.on; break;
    case SetKind::AUTOUPD: s.autoupd = op.on; break;
    case SetKind::BETA: s.beta = op.on; break;
    case SetKind::COUNTDOWN: s.countdown = op.on; break;
    case SetKind::TOP25: s.top25 = op.on; break;
    case SetKind::NEWS: s.news = op.on; break;
    case SetKind::GAMELEADERS: s.gameleaders = op.on; break;
    case SetKind::CARDS:
      for (const auto &r : kCards)
        if (op.mask & card_bit(r.card))
          set_card(s, r.card, (op.value & card_bit(r.card)) != 0);
      break;
    case SetKind::CD_NAME: s.cdname = op.text; break;
    case SetKind::CD_DATE: s.cdymd = op.ymd; break;
    case SetKind::MSG: s.msg = op.text; break;
    case SetKind::MSGURL: s.msgurl = op.text; break;
    case SetKind::MSGEVERY: s.msgevery = op.value; break;
    case SetKind::NICK: s.nick = op.text; break;
    case SetKind::HOOK: s.hook = op.text; break;
    case SetKind::PREGAME: s.pregame = op.value; break;
    // set_town(): a new town turns the card on, "" turns it off, the same
    // town again leaves the switch alone.
    case SetKind::TOWN:
      if (op.text != s.town) {
        s.town = op.text;
        s.wxon = !op.text.empty();
      }
      break;
    case SetKind::WXON: s.wxon = op.on; break;
    case SetKind::NIGHT: s.night.on = op.on ? 1 : 0; break;
    case SetKind::NIGHT_START: s.night.start = (uint16_t) op.value; break;
    case SetKind::NIGHT_END: s.night.end = (uint16_t) op.value; break;
    case SetKind::NIGHT_SCREEN: s.night.screen_off = op.on ? 0 : 1; break;
    case SetKind::NIGHT_LEVEL: s.night.level = (uint8_t) op.value; break;
    case SetKind::WAKE: s.wake.on = op.on ? 1 : 0; break;
    case SetKind::WAKE_LEAD: s.wake.lead = (uint8_t) op.value; break;
    case SetKind::AFTER: s.wake.after = (uint8_t) op.value; break;
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
    std::string days;
    for (int d = 0; d < 7; d++)
      if ((l.opt.days >> d) & 1u)
        days += (char) ('0' + d);
    o["days"] = days;
    o["start"] = list_time_text(l.opt.start);
    o["end"] = list_time_text(l.opt.end);
    o["only"] = (l.opt.flags & LISTOPT_ONLY) != 0;
    if (l.opt.flags & LISTOPT_TODAY_SET)
      o["today"] = (l.opt.flags & LISTOPT_TODAY) != 0;
  }
  doc["list"] = s.list;
  std::vector<std::string> names;
  for (const auto &l : s.lists)
    names.push_back(l.first);
  JsonArray remote = doc["remote"].to<JsonArray>();
  for (int b = 1; b <= 4; b++) {
    JsonObject o = remote.add<JsonObject>();
    o["v"] = remote_text(s.remote[b - 1]);
    o["gone"] = remote_resolve((uint8_t) b, s.remote[b - 1], fav_refs(s), names).gone;
  }
  JsonArray sports = doc["sports"].to<JsonArray>();
  for (uint8_t row = 1; row <= kSportCount; row++)
    if (sport_row_has(rows(s), row))
      sports.add(kSportKeys[row - 1]);
  doc["sport"] = sport_row_has(rows(s), (uint8_t) s.sport) ? kSportKeys[s.sport - 1] : "";
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
  doc["field"] = s.field;
  doc["winbar"] = s.winbar;
  doc["spark"] = s.spark;
  doc["bootaddr"] = s.bootaddr;
  doc["autoupd"] = s.autoupd;
  doc["beta"] = s.beta;
  doc["countdown"] = s.countdown;
  doc["top25"] = s.top25;
  doc["news"] = s.news;
  doc["gameleaders"] = s.gameleaders;
  // The state's list has names and more; the cases compare the switches.
  JsonObject cards = doc["cards"].to<JsonObject>();
  for (const auto &r : kCards)
    cards[r.id] = card_on(s, r.card);
  doc["cdname"] = s.cdname;
  doc["cddate"] = countdown_date_text(s.cdymd);
  doc["panels"] = s.panels;
  doc["msg"] = s.msg;
  doc["msgurl"] = s.msgurl;
  doc["msgevery"] = s.msgevery;
  doc["nick"] = s.nick;
  doc["hook"] = s.hook;
  doc["pregame"] = s.pregame;
  doc["town"] = s.town;
  doc["wxon"] = s.wxon;
  doc["night"] = s.night.on != 0;
  doc["nightstart"] = night_time_text(s.night.start);
  doc["nightend"] = night_time_text(s.night.end);
  doc["nightscreen"] = s.night.screen_off == 0;
  doc["nightlevel"] = s.night.level;
  doc["wake"] = s.wake.on != 0;
  doc["wakelead"] = s.wake.lead;
  doc["after"] = after_word(s.wake.after);
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
    // A sport row picked takes over list= in the same request.
    bool with_lists = false, sport_pick = false;
    int keep_list = -1;
    for (const auto &op : ops) {
      with_lists = with_lists || op.kind == SetKind::LISTS;
      sport_pick = sport_pick || (op.kind == SetKind::SPORT && op.value != 0);
      if (op.kind == SetKind::LIST)
        keep_list = op.value;
    }
    for (const auto &op : ops)
      apply(s, op, with_lists, keep_list, sport_pick);
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

void test_cards() {
  // Every card has its own id and bit, and the bits fit the saved mask.
  uint16_t seen = 0;
  for (const auto &r : kCards) {
    SCHECK(card_row(r.id) == &r, "card %s: its id is its own", r.id);
    SCHECK((seen & card_bit(r.card)) == 0, "card %s: its bit is its own", r.id);
    seen |= card_bit(r.card);
    // A card saved in cards_off keeps its bit through a load; the countdown and
    // news keep their own bytes.
    bool own = r.card == Card::COUNTDOWN || r.card == Card::NEWS || r.card == Card::WEATHER;
    SCHECK(((kCardsOffBits & card_bit(r.card)) != 0) == !own, "card %s: cards_off keeps its bit", r.id);
    // A saved 0 is the default for a card in cards_off, so every one is on by default.
    SCHECK(own || r.on, "card %s: on by default", r.id);
  }
  // Bits the cards had before the list (game leaders 0, Top 25 5) keep their meaning.
  SCHECK(card_bit(Card::LEADERS) == 1u << 0 && card_bit(Card::TOP25) == 1u << 5, "saved bits kept");
  SCHECK(card_row("news") != nullptr && !card_row("news")->on, "news is off by default");

  // The board's base card. Next game on: the clock only between seasons, with Clock on.
  SCHECK(!clock_instead(true, true, true, false), "next game on, in season: the next game");
  SCHECK(clock_instead(true, true, true, true), "next game on, between seasons: the clock");
  SCHECK(!clock_instead(true, false, true, true), "clock off, between seasons: the next game stays");
  // Next game off: the clock between games whatever Clock says, never over a game or a final.
  SCHECK(clock_instead(false, true, true, false), "next game off: the clock");
  SCHECK(clock_instead(false, false, true, false), "next game and clock off: still the clock");
  SCHECK(!clock_instead(false, true, false, false), "next game off, a game on the board: the game");
  SCHECK(!clock_instead(false, false, false, false), "next game and clock off, a game on the board: the game");
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
  SCHECK(parse_set("sport", "basketball", op, kFavMax) && op.kind == SetKind::SPORT && op.value == 3, "sport row");
  SCHECK(parse_set("sport", "none", op, kFavMax) && op.kind == SetKind::SPORT && op.value == 0, "sport none");
  SCHECK(!parse_set("sport", "", op, kFavMax) && !parse_set("sport", "Football", op, kFavMax) &&
             !parse_set("sport", "racing", op, kFavMax),
         "a word that names no sport");

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
  SCHECK(parse_set("top25", "0", op, kFavMax) && op.kind == SetKind::TOP25 && !op.on, "top25 0");
  SCHECK(parse_set("gameleaders", "0", op, kFavMax) && op.kind == SetKind::GAMELEADERS && !op.on, "gameleaders 0");
  SCHECK(parse_set("cards", "next:0,clock:1,standings:1", op, kFavMax) && op.kind == SetKind::CARDS &&
             op.mask == (card_bit(Card::NEXT) | card_bit(Card::CLOCK)) && op.value == card_bit(Card::CLOCK) &&
             op.refused.size() == 1 && op.refused[0] == "standings:1",
         "cards: two known, one left out");
  SCHECK(!parse_set("cards", "standings:1", op, kFavMax), "cards naming none it has");
  SCHECK(!parse_set("cards", "", op, kFavMax), "cards empty");
  SCHECK(!parse_set("cards", "next", op, kFavMax), "cards with no value");
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
  SCHECK(parse_set("wake", "1", op, kFavMax) && op.kind == SetKind::WAKE && op.on, "wake 1");
  SCHECK(parse_set("wakelead", "30", op, kFavMax) && op.kind == SetKind::WAKE_LEAD && op.value == 30, "wakelead 30");
  SCHECK(parse_set("wakelead", "1", op, kFavMax) && op.value == kWakeLeadMin, "wakelead is clamped up");
  SCHECK(parse_set("wakelead", "900", op, kFavMax) && op.value == kWakeLeadMax, "wakelead is clamped down");
  SCHECK(!parse_set("wakelead", "15m", op, kFavMax), "wakelead takes a whole number");
  SCHECK(parse_set("after", "clock", op, kFavMax) && op.kind == SetKind::AFTER && op.value == (int) After::CLOCK,
         "after clock");
  SCHECK(parse_set("after", "off", op, kFavMax) && op.value == (int) After::OFF, "after off");
  SCHECK(parse_set("after", "scoreboard", op, kFavMax) && op.value == (int) After::SCOREBOARD, "after scoreboard");
  SCHECK(!parse_set("after", "effects", op, kFavMax), "after effects isn't in this firmware");

  // Every key handle_set_ forwards decodes to something.
  for (const char *key : kSetKeys) {
    bool known = parse_set(key, "1", op, kFavMax) || parse_set(key, "nfl:6", op, kFavMax) ||
                 parse_set(key, "", op, kFavMax) || parse_set(key, "07:00", op, kFavMax) ||
                 parse_set(key, "none", op, kFavMax) || parse_set(key, "next:1", op, kFavMax) ||
                 parse_set(key, "clock", op, kFavMax) ||
                 parse_set(key, "fav", op, kFavMax);
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

  // Sport rows.
  SCHECK(sport_row_from_key("football") == 1 && sport_row_from_key("soccer") == 5 && sport_row_from_key("") == 0 &&
             sport_row_from_key("Football") == 0 && sport_row_from_key("racing") == 0,
         "sport keys to rows");
  SCHECK(sport_bit((uint8_t) League::NCAA) == sport_bit((uint8_t) League::NFL) &&
             sport_bit((uint8_t) League::MCBB) == 1u << (uint8_t) Sport::BASKETBALL && sport_bit(200) == 0,
         "a bit per sport, none for an unknown league");
  uint8_t football = sport_bit((uint8_t) League::NFL), hoops = sport_bit((uint8_t) League::NBA);
  SCHECK(sport_rows(0) == 0 && sport_rows(football) == 0, "one sport or none: no rows");
  SCHECK(sport_rows(football | hoops) == (football | hoops), "two sports: a row each");
  SCHECK(sport_row_has(football | hoops, 1) && sport_row_has(football | hoops, 3) &&
             !sport_row_has(football | hoops, 2) && !sport_row_has(0xff, 0) && !sport_row_has(0xff, 6),
         "rows by number");
  for (int s = 0; s < kSportCount; s++)
    SCHECK(std::string(kSportRowNames[s]).size() <= kListNameMax, "%s is longer than a playlist name", kSportRowNames[s]);
}

// Local day of the week (0 Sunday) and minute after midnight of an epoch in
// a POSIX timezone, as the panel's clock gives them.
std::pair<int, int> local_dm(int64_t epoch, const char *tz) {
  setenv("TZ", tz, 1);
  tzset();
  time_t t = (time_t) epoch;
  struct tm lt;
  localtime_r(&t, &lt);
  return {lt.tm_wday, lt.tm_hour * 60 + lt.tm_min};
}

// Playlist options (playlists.h): the tokens in the lists= text, the
// schedule across midnight and through daylight saving, the tick's steps.
void test_list_options() {
  std::vector<ListDef> l = parse_lists("Sundays=nfl:6,@d:0,@t:11:00-23:00,@only,@today:1;Plain=nba:6", kFavMax);
  SCHECK(l.size() == 2 && l[0].teams.size() == 1 && l[1].teams.size() == 1, "tokens aren't teams");
  SCHECK(l[0].opt.days == 1 && l[0].opt.start == 11 * 60 && l[0].opt.end == 23 * 60, "days and hours read");
  SCHECK(l[0].opt.flags == (LISTOPT_ONLY | LISTOPT_TODAY_SET | LISTOPT_TODAY), "only and today read");
  SCHECK(l[1].opt == ListOpt{}, "no tokens, no options");
  SCHECK(list_opt_text(l[0].opt) == ",@d:0,@t:11:00-23:00,@only,@today:1", "options back as tokens: %s",
         list_opt_text(l[0].opt).c_str());
  SCHECK(list_opt_text(ListOpt{}).empty(), "no options, no tokens");
  l = parse_lists("A=@d:6530x,@t:7:05-6:30,@today:0", kFavMax);
  SCHECK(l[0].opt.days == 0b1101001 && l[0].opt.start == 7 * 60 + 5 && l[0].opt.end == 6 * 60 + 30,
         "days in any order, odd characters dropped, hours past midnight");
  SCHECK(l[0].opt.flags == LISTOPT_TODAY_SET, "today:0 is set and off");
  SCHECK(list_opt_text(l[0].opt) == ",@d:0356,@t:07:05-06:30,@today:0", "days sorted, hours zero padded: %s",
         list_opt_text(l[0].opt).c_str());
  l = parse_lists("A=@t:11:00-23:00;B=@d:1,@t:25:00-23:00;C=@d:1,@t:11:00;D=@t:9:00-9:00,@d:2", kFavMax);
  SCHECK(l[0].opt.days == 0 && list_opt_text(l[0].opt).empty(), "hours with no days are no schedule");
  SCHECK(l[1].opt.start == 0 && l[1].opt.end == 0 && l[2].opt.start == 0, "bad hours dropped");
  SCHECK(l[3].opt.days == 4 && list_opt_text(l[3].opt) == ",@d:2", "the same start and end is all day");
  SCHECK(list_parse_time("00:00") == 0 && list_parse_time("23:59") == 23 * 60 + 59 && list_parse_time("24:00") < 0 &&
             list_parse_time("12:60") < 0 && list_parse_time("noon") < 0 && list_parse_time("1:5") < 0,
         "time parsing");
  SCHECK(list_today(ListOpt{}, true) && !list_today(ListOpt{}, false), "no setting follows the switch");
  SCHECK(list_today(l[0].opt, true), "A: switch");
  ListOpt off;
  list_opt_token("@today:0", off);
  SCHECK(!list_today(off, true), "today off beats the switch");

  // Sundays 11:00 to 23:00
  ListOpt sun;
  list_opt_token("@d:0", sun);
  list_opt_token("@t:11:00-23:00", sun);
  SCHECK(list_scheduled(sun, 0, 11 * 60) && list_scheduled(sun, 0, 22 * 60 + 59), "inside the hours");
  SCHECK(!list_scheduled(sun, 0, 10 * 60 + 59) && !list_scheduled(sun, 0, 23 * 60), "outside the hours");
  SCHECK(!list_scheduled(sun, 1, 12 * 60) && !list_scheduled(sun, 6, 12 * 60), "other days");
  // Saturdays 20:00 to 02:00: into Sunday morning, not from Friday night
  ListOpt late;
  list_opt_token("@d:6", late);
  list_opt_token("@t:20:00-02:00", late);
  SCHECK(list_scheduled(late, 6, 20 * 60) && list_scheduled(late, 6, 23 * 60 + 59), "Saturday evening");
  SCHECK(list_scheduled(late, 0, 0) && list_scheduled(late, 0, 1 * 60 + 59), "Sunday small hours");
  SCHECK(!list_scheduled(late, 0, 2 * 60) && !list_scheduled(late, 0, 20 * 60), "Sunday 2 AM is over; Sunday night isn't a Saturday");
  SCHECK(!list_scheduled(late, 6, 1 * 60), "Saturday 1 AM is Friday's night, not scheduled");
  // Every day all day
  ListOpt all;
  list_opt_token("@d:0123456", all);
  SCHECK(list_scheduled(all, 3, 0) && list_scheduled(all, 3, 23 * 60 + 59), "all day");
  SCHECK(!list_scheduled(ListOpt{}, 0, 12 * 60), "no schedule");
  // Overlaps: the list higher in the order wins
  ListOpt opts[3] = {ListOpt{}, sun, all};
  SCHECK(scheduled_list(opts, 3, 0, 12 * 60) == 2, "Sunday noon: the Sundays list, above the every-day one");
  SCHECK(scheduled_list(opts, 3, 2, 12 * 60) == 3, "Tuesday: the every-day list");
  SCHECK(scheduled_list(opts, 2, 2, 12 * 60) == 0, "without it, nothing");
  SCHECK(scheduled_list(opts, 0, 0, 12 * 60) == 0, "no lists");

  // Through daylight saving in US Central: the hours follow the local
  // clock. Fall back 2026-11-01: 1:30 AM CDT (06:30Z) and 1:30 AM CST
  // (07:30Z) are both inside a Saturday-night window that ends at 2 AM, and
  // 2:30 AM CST (08:30Z) is past it. Spring forward 2026-03-08: 1:59 AM CST
  // (07:59Z) is inside, the next minute is 3:00 AM CDT and outside.
  const char *central = "CST6CDT,M3.2.0,M11.1.0";
  auto at = [&](int64_t epoch) { return local_dm(epoch, central); };
  std::pair<int, int> dm = at(1793514600);  // 2026-11-01 06:30Z, 1:30 AM CDT Sunday
  SCHECK(dm.first == 0 && dm.second == 90 && list_scheduled(late, dm.first, dm.second), "1:30 AM CDT inside");
  dm = at(1793518200);  // 07:30Z, 1:30 AM CST (the hour again)
  SCHECK(dm.first == 0 && dm.second == 90 && list_scheduled(late, dm.first, dm.second), "1:30 AM CST inside too");
  dm = at(1793521800);  // 08:30Z, 2:30 AM CST
  SCHECK(dm.first == 0 && dm.second == 150 && !list_scheduled(late, dm.first, dm.second), "2:30 AM CST over");
  dm = at(1772956740);  // 2026-03-08 07:59Z, 1:59 AM CST Sunday
  SCHECK(dm.first == 0 && dm.second == 119 && list_scheduled(late, dm.first, dm.second), "1:59 AM CST inside");
  dm = at(1772956800);  // 08:00Z, 3:00 AM CDT
  SCHECK(dm.first == 0 && dm.second == 180 && !list_scheduled(late, dm.first, dm.second), "3:00 AM CDT over");
  // A Sunday 11 AM start lands an hour later in UTC after the fall back
  dm = at(1793466000);  // 2026-10-31 17:00Z, Saturday noon CDT: not Sunday
  SCHECK(dm.first == 6 && !list_scheduled(sun, dm.first, dm.second), "Saturday noon");
  dm = at(1793552400);  // 2026-11-01 17:00Z, 11:00 AM CST Sunday
  SCHECK(dm.first == 0 && dm.second == 11 * 60 && list_scheduled(sun, dm.first, dm.second), "11 AM CST Sunday starts it");
  dm = at(1793548800);  // 16:00Z, 10:00 AM CST
  SCHECK(dm.second == 10 * 60 && !list_scheduled(sun, dm.first, dm.second), "10 AM CST, not yet");

  // The tick's steps (schedule_step)
  SchedStep st = schedule_step(2, 0, 0, false);
  SCHECK(st.start && st.list == 2 && !st.restore, "a schedule starts");
  st = schedule_step(2, 2, 2, false);
  SCHECK(!st.start && !st.restore, "under way: nothing");
  st = schedule_step(2, 0, 0, true);
  SCHECK(!st.start && !st.restore, "a locked game finishes first");
  st = schedule_step(0, 2, 2, false);
  SCHECK(st.restore && !st.start, "over: back to what played");
  st = schedule_step(0, 2, 1, false);
  SCHECK(!st.restore && !st.start, "over, but the owner picked another list: it stays");
  st = schedule_step(3, 2, 2, false);
  SCHECK(st.start && st.list == 3, "a higher list's schedule takes over");
  st = schedule_step(0, 2, 2, true);
  SCHECK(!st.restore, "over, but locked: wait");
}

// The WizMote's buttons (set_request.h RemoteTarget, playlists.h
// remote_resolve): the text both ways and what a button does now.
void test_remote() {
  RemoteTarget t;
  SCHECK(remote_parse("fav", t) && t.kind == (uint8_t) RemoteKind::FAVORITE && remote_text(t) == "fav", "fav");
  SCHECK(remote_parse("team:nfl:6", t) && t.kind == (uint8_t) RemoteKind::TEAM && t.league == (uint8_t) League::NFL &&
             t.id == 6 && remote_text(t) == "team:nfl:6",
         "a team");
  SCHECK(!remote_parse("team:nfl:99999", t) && !remote_parse("team:dallas", t) && !remote_parse("team:", t),
         "a team the panel doesn't know is refused");
  SCHECK(remote_parse("list:Sunday Football", t) && t.kind == (uint8_t) RemoteKind::LIST &&
             remote_name(t) == "Sunday Football" && remote_text(t) == "list:Sunday Football",
         "a playlist by name");
  SCHECK(remote_parse("list:ABCDEFGHIJKLMNOPQRST", t) && remote_name(t) == "ABCDEFGHIJKLMNOP", "a name is cut to 16 bytes");
  SCHECK(!remote_parse("list:", t), "a playlist with no name is refused");
  SCHECK(remote_parse("sport:hockey", t) && t.kind == (uint8_t) RemoteKind::SPORT && t.sport == (uint8_t) Sport::HOCKEY &&
             remote_text(t) == "sport:hockey",
         "a sport row");
  SCHECK(!remote_parse("sport:Hockey", t) && !remote_parse("sport:curling", t), "an unknown sport is refused");
  SCHECK(remote_parse("all", t) && remote_text(t) == "all" && remote_parse("none", t) && remote_text(t) == "none", "all, none");
  SCHECK(!remote_parse("", t) && !remote_parse("nfl:6", t) && !remote_parse("favorite", t), "anything else is refused");
  SetOp op;
  SCHECK(parse_set("remote2", "team:nfl:6", op, kFavMax) && op.kind == SetKind::REMOTE && op.slot == 2 &&
             op.text == "team:nfl:6",
         "remote2 set key");
  SCHECK(!parse_set("remote5", "fav", op, kFavMax) && !parse_set("remote0", "fav", op, kFavMax) &&
             !parse_set("remote1", "team:dallas", op, kFavMax),
         "remote keys are 1 to 4, values checked");

  const uint8_t NFL = (uint8_t) League::NFL, NBA = (uint8_t) League::NBA, NHL = (uint8_t) League::NHL;
  std::vector<TeamRef> favs = {{NFL, 6}, {NBA, 6}, {}, {NHL, 25}};
  std::vector<std::string> names = {"Sundays", "Hoops"};
  RemoteAction a = remote_resolve(2, RemoteTarget{}, favs, names);
  SCHECK(a.kind == RemoteKind::FAVORITE && a.fav_slot == 2 && !a.gone, "the default is favorite n");
  remote_parse("team:nhl:25", t);
  a = remote_resolve(1, t, favs, names);
  SCHECK(a.kind == RemoteKind::TEAM && a.fav_slot == 4 && !a.gone, "a team is followed to its slot");
  remote_parse("team:mlb:15", t);
  a = remote_resolve(3, t, favs, names);
  SCHECK(a.kind == RemoteKind::FAVORITE && a.fav_slot == 3 && a.gone, "a team that isn't a favorite: the default, gone");
  remote_parse("list:Hoops", t);
  a = remote_resolve(1, t, favs, names);
  SCHECK(a.kind == RemoteKind::LIST && a.list == 2 && !a.gone, "a playlist by name");
  remote_parse("list:Gone", t);
  a = remote_resolve(4, t, favs, names);
  SCHECK(a.kind == RemoteKind::FAVORITE && a.fav_slot == 4 && a.gone, "a playlist that is gone: the default");
  remote_parse("sport:basketball", t);
  a = remote_resolve(1, t, favs, names);
  SCHECK(a.kind == RemoteKind::SPORT && a.sport_row == 1 + (uint8_t) Sport::BASKETBALL && !a.gone, "a sport row");
  remote_parse("sport:baseball", t);
  a = remote_resolve(1, t, favs, names);
  SCHECK(a.kind == RemoteKind::FAVORITE && a.gone, "a sport with no favorites: the default");
  a = remote_resolve(1, t, {{NFL, 6}}, names);
  SCHECK(a.gone, "no rows with one sport");
  remote_parse("all", t);
  a = remote_resolve(3, t, favs, names);
  SCHECK(a.kind == RemoteKind::ALL && a.list == 0 && !a.gone, "all favorites");
  remote_parse("none", t);
  a = remote_resolve(3, t, favs, names);
  SCHECK(a.kind == RemoteKind::NONE && !a.gone, "nothing");
}

}  // namespace

int run_set_tests(int &total_checks) {
  test_remote();
  test_list_options();
  test_playlists();
  test_parse_set();
  test_cards();
  test_live_leagues();
  test_scan_leagues();
  test_no_team();
  test_live_scan_changed();
  test_fixture_cases();
  total_checks += checks;
  return failures;
}
