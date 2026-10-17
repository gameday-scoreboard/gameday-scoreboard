// The demo build's scripted game (firmware/gameday-demo.yaml). Release
// firmware compiles none of this: GAMEDAY_DEMO is only defined when the
// gameday block in YAML says demo: true.
#include "esphome/core/defines.h"
#ifdef GAMEDAY_DEMO

#include "gameday.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "demo.h"

namespace esphome {
namespace gameday {

static const char *const TAG = "gameday.demo";

using ::espn::DemoKind;

static const char *kind_name(uint8_t kind) {
  return kind == (uint8_t) DemoKind::PRO ? "pro" : kind == (uint8_t) DemoKind::COLLEGE ? "college" : "off";
}

void GamedayComponent::demo_setup_() {
  this->demo_pref_ = global_preferences->make_preference<DemoPrefs>(fnv1_hash("gameday_demo_v1"));
  if (!this->demo_pref_.load(&this->demo_prefs_) || this->demo_prefs_.running > (uint8_t) DemoKind::COLLEGE)
    this->demo_prefs_ = DemoPrefs{};
  // A show panel that loses power comes back playing. It waits for the
  // first real board (or a minute with no network) so Wi-Fi, the clock
  // and the logos have had their chance.
  this->demo_resume_ = this->demo_prefs_.running;
  if (this->demo_resume_ != 0)
    ESP_LOGI(TAG, "Demo: was playing %s at power off, resuming once the board is up", kind_name(this->demo_resume_));
  this->rebuild_demo_json_();
}

bool GamedayComponent::demo_loop_() {
  if (this->demo_resume_ != 0 && (this->board_ready_ || millis() > 60000)) {
    uint8_t kind = this->demo_resume_;
    this->demo_resume_ = 0;
    this->start_demo(kind);
  }
  if (this->demo_kind_ == 0)
    return false;
  this->demo_tick_();
  return this->demo_playing_();
}

void GamedayComponent::start_demo(uint8_t kind) {
  if (kind != (uint8_t) DemoKind::PRO && kind != (uint8_t) DemoKind::COLLEGE)
    return;
  ESP_LOGI(TAG, "Demo: starting %s, looping until stopped", kind_name(kind));
  // From the live board, keep it to come back to. Restarting from inside a
  // run keeps the board saved when the run began.
  if (this->demo_kind_ == 0 || this->demo_gap_) {
    this->demo_saved_game_ = this->game_;
    this->demo_saved_prev_ = this->prev_;
  }
  this->demo_resume_ = 0;
  this->generation_++;  // any fetch in flight belongs to before the demo
  this->demo_kind_ = kind;
  this->demo_gap_ = false;
  this->demo_step_ = 0;
  this->demo_next_ms_ = millis();
  // The boot screen holds for the first real scoreboard; the demo is one.
  this->mark_board_ready_();
  if (this->demo_prefs_.running != kind) {
    this->demo_prefs_.running = kind;
    this->demo_pref_.save(&this->demo_prefs_);
  }
  this->rebuild_demo_json_();
}

void GamedayComponent::stop_demo() {
  this->demo_resume_ = 0;
  if (this->demo_prefs_.running != 0) {
    this->demo_prefs_.running = 0;
    this->demo_pref_.save(&this->demo_prefs_);
  }
  if (this->demo_kind_ == 0) {
    this->rebuild_demo_json_();
    return;
  }
  ESP_LOGI(TAG, "Demo: stopped, back to live scores");
  bool was_playing = this->demo_playing_();
  this->demo_kind_ = 0;
  this->demo_gap_ = false;
  if (was_playing) {
    this->game_ = this->demo_saved_game_;
    this->prev_ = this->demo_saved_prev_;
    this->emit_({});
  }
  this->schedule_next_(0);
  this->rebuild_demo_json_();
}

void GamedayComponent::remote_number(uint8_t n) {
  switch (n) {
    case 1:
      this->start_demo((uint8_t) DemoKind::PRO);
      break;
    case 2:
      this->start_demo((uint8_t) DemoKind::COLLEGE);
      break;
    case 3:
      // The effects tour, once effects mode (backlog #39) is in this build.
      ESP_LOGI(TAG, "Remote button 3: the effects tour is not in this build yet");
      break;
    case 4:
      this->stop_demo();
      break;
    default:
      break;
  }
}

void GamedayComponent::demo_tick_() {
  uint32_t now = millis();
  if ((int32_t) (now - this->demo_next_ms_) < 0)
    return;
  if (this->demo_gap_) {
    // The normal board had its turn; take what it shows now as the board to
    // come back to and start the next run.
    this->demo_gap_ = false;
    this->demo_saved_game_ = this->game_;
    this->demo_saved_prev_ = this->prev_;
    this->generation_++;
    this->demo_step_ = 0;
    this->rebuild_demo_json_();
  } else if (this->demo_step_ >= ::espn::kDemoStepCount && this->demo_prefs_.madeup) {
    // Made-up teams are for filming: the live board between runs would put a
    // real team and its logo in the middle of the take, so loop straight on.
    ESP_LOGI(TAG, "Demo: run done, made-up teams on, starting the next run");
    this->demo_step_ = 0;
  } else if (this->demo_step_ >= ::espn::kDemoStepCount) {
    ESP_LOGI(TAG, "Demo: run done, live board for %us", (unsigned) (::espn::kDemoGapMs / 1000));
    this->demo_gap_ = true;
    this->game_ = this->demo_saved_game_;
    this->prev_ = this->demo_saved_prev_;
    this->emit_({});
    this->demo_next_ms_ = now + ::espn::kDemoGapMs;
    this->rebuild_demo_json_();
    return;
  }
  this->demo_show_step_(this->demo_step_ > 0);
  this->demo_next_ms_ = now + ::espn::kDemoSteps[this->demo_step_].hold_ms;
  this->demo_step_++;
}

// Puts script step demo_step_ on the board with the current matchup.
void GamedayComponent::demo_show_step_(bool splash) {
  const ::espn::DemoStep &d = ::espn::kDemoSteps[this->demo_step_];
  DemoKind kind = (DemoKind) this->demo_kind_;
  GameSnapshot g{};
  char color[8];
  if (this->demo_prefs_.madeup) {
    const auto &home = ::espn::demo_made_up(kind, true);
    const auto &away = ::espn::demo_made_up(kind, false);
    g.team_abbr = home.abbr;
    g.opp_abbr = away.abbr;
    snprintf(color, sizeof(color), "%06X", (unsigned) home.rgb);
    g.team_color = color;
    snprintf(color, sizeof(color), "%06X", (unsigned) away.rgb);
    g.opp_color = color;
  } else {
    ::espn::DemoMatchup m = this->demo_matchup_(this->demo_kind_);
    g.team_abbr = m.home->abbr;
    g.team_id = m.home->espn_id;
    g.team_logo = ::espn::team_logo_url(m.home->league, m.home->espn_id, m.home->abbr);
    g.opp_abbr = m.away->abbr;
    g.opp_id = m.away->espn_id;
    g.opp_logo = ::espn::team_logo_url(m.away->league, m.away->espn_id, m.away->abbr);
    snprintf(color, sizeof(color), "%06X", (unsigned) ::espn::team_rgb(m.home->league, m.home->espn_id));
    g.team_color = color;
    snprintf(color, sizeof(color), "%06X", (unsigned) ::espn::team_rgb(m.away->league, m.away->espn_id));
    g.opp_color = color;
  }
  g.valid = true;
  g.state = d.state;
  g.completed = d.state == GameState::POST;
  g.team_score = d.us;
  g.opp_score = d.them;
  g.display_clock = d.clock;
  g.period = d.period;
  g.possession = d.possession;
  g.short_down_distance = d.down;
  g.down_distance = d.down;
  g.is_red_zone = d.red_zone;
  g.last_play = d.play;
  g.team_winner = d.state == GameState::POST && d.us > d.them;
  g.team_timeouts = d.period <= 2 ? 3 : 2;
  g.opp_timeouts = d.period <= 2 ? 3 : 1;
  if (d.state == GameState::PRE) {
    g.short_detail = "Tonight";
    g.kickoff_epoch = this->time_ != nullptr ? (int64_t) this->time_->timestamp_now() + 3600 : 0;
  } else if (d.state == GameState::POST) {
    g.short_detail = "Final";
  } else {
    g.short_detail = std::string(d.clock) + " - " +
                     (d.period == 1 ? "1st" : d.period == 2 ? "2nd" : d.period == 3 ? "3rd" : "4th");
  }
  this->prev_ = this->game_;
  this->game_ = g;
  ::espn::Splash s = splash ? ::espn::decide_splash(this->prev_, g, true, false) : ::espn::Splash{};
  this->emit_(s);
}

// With made-up teams there is no logo to fetch, so each logo slot shows the
// side's abbreviation on its colors instead, the way the app shows every team.
void GamedayComponent::demo_badges_(UpdateFields &f) const {
  if (!this->demo_playing_() || !this->demo_prefs_.madeup)
    return;
  DemoKind kind = (DemoKind) this->demo_kind_;
  f.team_badge = true;
  f.opponent_badge = true;
  f.team_ring = ::espn::demo_made_up(kind, true).alt;
  f.opponent_ring = ::espn::demo_made_up(kind, false).alt;
}

::espn::DemoMatchup GamedayComponent::demo_matchup_(uint8_t kind) const {
  bool college = kind == (uint8_t) DemoKind::COLLEGE;
  return ::espn::demo_matchup((DemoKind) kind, college ? this->demo_prefs_.college_home : this->demo_prefs_.pro_home,
                              college ? this->demo_prefs_.college_away : this->demo_prefs_.pro_away,
                              (League) this->prefs_.league, this->prefs_.team_id);
}

uint8_t GamedayComponent::demo_league_() const {
  return (uint8_t) ::espn::demo_league((DemoKind) this->demo_kind_);
}

// ---- /gameday/demo ------------------------------------------------------------
//   GET  /gameday/demo   {"kind":"off|pro|college","playing":bool,"madeup":bool,
//                         "pro":{"home":team,"away":team},"college":{...}}
//   POST /gameday/demo   do=start&kind=pro|college, do=stop,
//                        pro_home|pro_away=nfl:6, college_home|college_away=ncaa:194, madeup=0/1

static void put_demo_team(JsonObject o, const ::espn::Team *t) {
  o["l"] = t->league == League::NFL ? "nfl" : "ncaa";
  o["id"] = t->espn_id;
  o["abbr"] = t->abbr;
  o["name"] = t->name;
}

static void put_made_up(JsonObject o, const ::espn::MadeUpTeam &t) {
  o["abbr"] = t.abbr;
  o["name"] = t.name;
}

void GamedayComponent::rebuild_demo_json_() {
  JsonDocument doc(::espn::detail::json_allocator());
  doc["kind"] = kind_name(this->demo_kind_);
  doc["playing"] = this->demo_playing_();
  doc["madeup"] = this->demo_prefs_.madeup != 0;
  for (uint8_t kind : {(uint8_t) DemoKind::PRO, (uint8_t) DemoKind::COLLEGE}) {
    JsonObject side = doc[kind_name(kind)].to<JsonObject>();
    ::espn::DemoMatchup m = this->demo_matchup_(kind);
    put_demo_team(side["home"].to<JsonObject>(), m.home);
    put_demo_team(side["away"].to<JsonObject>(), m.away);
    put_made_up(side["madeup_home"].to<JsonObject>(), ::espn::demo_made_up((DemoKind) kind, true));
    put_made_up(side["madeup_away"].to<JsonObject>(), ::espn::demo_made_up((DemoKind) kind, false));
  }
  std::string out;
  serializeJson(doc, out);
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  this->demo_json_ = std::move(out);
}

static const char *const DEMO_KEYS[] = {"do", "kind", "pro_home", "pro_away", "college_home", "college_away", "madeup"};

void GamedayComponent::handle_demo_(AsyncWebServerRequest *request) {
  if (request->method() == HTTP_GET) {
    std::string body;
    {
      std::lock_guard<std::mutex> lock(this->state_mutex_);
      body = this->demo_json_;
    }
    request->send(200, "application/json", body.c_str());
    return;
  }
  if (request->method() != HTTP_POST) {
    request->send(405, "application/json", "{\"error\":\"GET or POST\"}");
    return;
  }
  std::vector<std::pair<std::string, std::string>> kv;
  for (const char *key : DEMO_KEYS)
    if (request->hasParam(key))
      kv.emplace_back(key, request->arg(key));
  if (kv.empty()) {
    request->send(400, "application/json", "{\"error\":\"nothing to do\"}");
    return;
  }
  this->defer([this, kv]() { this->apply_demo_(kv); });
  request->send(200, "application/json", "{\"ok\":true}");
}

void GamedayComponent::apply_demo_(const std::vector<std::pair<std::string, std::string>> &kv) {
  std::string action, kind;
  bool teams_changed = false;
  for (const auto &p : kv) {
    const std::string &k = p.first;
    const std::string &v = p.second;
    if (k == "do") {
      action = v;
    } else if (k == "kind") {
      kind = v;
    } else if (k == "madeup") {
      this->demo_prefs_.madeup = v == "1" ? 1 : 0;
      teams_changed = true;
    } else {
      // pro_home=nfl:6. "none" goes back to the default pick.
      bool college = k.compare(0, 8, "college_") == 0;
      bool home = k.size() > 5 && k.compare(k.size() - 5, 5, "_home") == 0;
      uint8_t league;
      uint32_t id;
      if (!parse_team_ref(v, league, id)) {
        id = 0;
        league = college ? (uint8_t) League::NCAA : (uint8_t) League::NFL;
      }
      if (league != (uint8_t) (college ? League::NCAA : League::NFL)) {
        ESP_LOGW(TAG, "Demo: %s=%s is the wrong league", k.c_str(), v.c_str());
        continue;
      }
      // DemoPrefs is packed, so no references into it.
      if (college && home)
        this->demo_prefs_.college_home = id;
      else if (college)
        this->demo_prefs_.college_away = id;
      else if (home)
        this->demo_prefs_.pro_home = id;
      else
        this->demo_prefs_.pro_away = id;
      teams_changed = true;
    }
  }
  if (teams_changed) {
    this->demo_pref_.save(&this->demo_prefs_);
    if (this->demo_prefs_.madeup)
      ESP_LOGI(TAG, "Demo: teams now pro %s vs %s, college %s vs %s (made-up teams, no logos)",
               ::espn::demo_made_up(DemoKind::PRO, true).abbr, ::espn::demo_made_up(DemoKind::PRO, false).abbr,
               ::espn::demo_made_up(DemoKind::COLLEGE, true).abbr, ::espn::demo_made_up(DemoKind::COLLEGE, false).abbr);
    else
      ESP_LOGI(TAG, "Demo: teams now pro %s, college %s", this->demo_matchup_(1).home->abbr,
               this->demo_matchup_(2).home->abbr);
    // Mid-game, put the new teams on the board now rather than at the next play.
    if (this->demo_playing_() && this->demo_step_ > 0) {
      this->demo_step_--;
      this->demo_show_step_(false);
      this->demo_step_++;
    }
    // Turned on between runs: cut the live board short, the same as above.
    if (this->demo_gap_ && this->demo_prefs_.madeup)
      this->demo_next_ms_ = millis();
  }
  if (action == "start")
    this->start_demo(kind == "college" ? (uint8_t) DemoKind::COLLEGE : (uint8_t) DemoKind::PRO);
  else if (action == "stop")
    this->stop_demo();
  this->rebuild_demo_json_();
}

}  // namespace gameday
}  // namespace esphome

#endif  // GAMEDAY_DEMO
