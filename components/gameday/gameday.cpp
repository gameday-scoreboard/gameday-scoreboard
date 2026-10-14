#include "gameday.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cstring>
#include <vector>

#include "timezones.h"
#include "timezones_parsed.h"

#ifdef GAMEDAY_TZ_PARSED
#include "esphome/components/time/posix_tz.h"
#endif
#ifdef USE_UPDATE
#include "esphome/components/update/update_entity.h"
#endif

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace esphome {
namespace gameday {

static const char *const TAG = "gameday";

// ESPN's edge returns 403 to most unfamiliar agents, ESPHome's own among
// them, but accepts curl.
static const char *const USER_AGENT = "curl/8.0";

static const uint32_t MINUTE = 60 * 1000;
static const uint32_t SCHEDULE_INTERVAL = 6 * 60 * MINUTE;
static const uint32_t PRE_FAR_INTERVAL = 15 * MINUTE;
static const uint32_t PRE_NEAR_INTERVAL = MINUTE;
// ESPN's scoreboard copy changes about every 10 s (its cache-control
// max-age counts down from 10), so reading it any faster mostly fetches
// the same 256 KB again.
static const uint32_t IN_INTERVAL = 10 * 1000;
static const uint32_t POST_INTERVAL = MINUTE;
static const uint32_t POST_LINGER = 30 * MINUTE;
// For the panel's own trouble (a fetch task that never finished or could not
// start). A failed ESPN read backs off instead (back_off_).
static const uint32_t RETRY_INTERVAL = MINUTE;
// Wi-Fi and the clock are re-checked this often at boot: every second waited
// here is a second of boot screen.
static const uint32_t NOT_READY_INTERVAL = 500;
static const int64_t PRE_NEAR_SECONDS = 60 * 60;
static const uint32_t NO_LIVE_RESCAN = 2 * MINUTE;
// Install updates automatically: how often loop() looks, and how long a
// manifest read gets before its answer is used (the HTTP timeout is 20 s).
static const uint32_t AUTO_UPDATE_TICK = 10 * 1000;
static const uint32_t AUTO_UPDATE_CHECK_WAIT = MINUTE;
// A college scan is ~750KB on a Saturday. With nothing live it waits for the
// next kickoff instead of re-reading every 2 minutes, but never longer than
// this, so a moved kickoff or the next day's slate is still picked up.
static const uint32_t NO_GAMES_RESCAN = 3 * 60 * MINUTE;
// How long the loop waits on a worker task before writing it off. A job chains
// at most two requests (schedule then game, or the NFL then the NCAA
// scoreboard). http_request in gameday-common.yaml allows each one 20s, with a
// 30s watchdog_timeout as the hard ceiling, so two requests can legitimately
// spend 60s on the network alone. Add the TLS handshakes and the streamed
// parse of a Saturday NCAA scoreboard and the old 60s deadline was under the
// worst honest case, not over it. 2 x watchdog_timeout for the network plus
// 60s for handshakes and parsing leaves only a genuinely wedged worker here.
static const uint32_t WORKER_DEADLINE = 120 * 1000;

static const char *const MODE_OPTIONS[] = {"My team", "Live NFL", "Live college", "Live anything", "Favorite teams"};
static const uint32_t FAV_TICK = 1000;  // playlist / lock decisions re-run this often

// Feeds ArduinoJson straight from the HTTP socket so a scoreboard document
// never has to sit in RAM as a whole.
class ContainerReader {
 public:
  explicit ContainerReader(std::shared_ptr<http_request::HttpContainer> container)
      : container_(std::move(container)) {}

  int read() {
    if (!this->fill_())
      return -1;
    return static_cast<unsigned char>(this->buf_[this->pos_++]);
  }

  size_t readBytes(char *dst, size_t n) {
    size_t copied = 0;
    while (copied < n) {
      if (!this->fill_())
        break;
      size_t take = std::min(n - copied, this->len_ - this->pos_);
      memcpy(dst + copied, this->buf_ + this->pos_, take);
      this->pos_ += take;
      copied += take;
    }
    return copied;
  }

  size_t total() const { return this->total_; }

 private:
  bool fill_() {
    if (this->pos_ < this->len_)
      return true;
    if (this->eof_)
      return false;
    int n = this->container_->read(this->buf_, sizeof(this->buf_));
    if (n <= 0) {
      this->eof_ = true;
      return false;
    }
    this->pos_ = 0;
    this->len_ = (size_t) n;
    this->total_ += (size_t) n;
    return true;
  }

  std::shared_ptr<http_request::HttpContainer> container_;
  uint8_t buf_[1024];
  size_t pos_{0};
  size_t len_{0};
  size_t total_{0};
  bool eof_{false};
};

// ---- GamedaySelect ---------------------------------------------------------

void GamedaySelect::control(const std::string &value) {
  this->publish_state(value);
  if (this->parent_ == nullptr)
    return;
  if (this->type_ == SelectType::TEAM)
    this->parent_->select_team(value);
  else if (this->type_ == SelectType::MODE)
    this->parent_->select_mode(value);
  else if (this->type_ == SelectType::FAVORITE)
    this->parent_->select_favorite(this->slot_, value);
  else
    this->parent_->select_timezone(value);
}

// ---- GamedayComponent ------------------------------------------------------

#ifdef USE_PSRAM
// Keep ESPN parsing and /gameday/state in PSRAM because internal RAM is the
// heap that runs out
static json::SpiRamAllocator json_psram;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
#endif

void GamedayComponent::setup() {
#ifdef USE_PSRAM
  ::espn::set_json_allocator(&json_psram);
#endif
  this->logos_.setup();
  this->pref_ = global_preferences->make_preference<Prefs>(fnv1_hash("gameday_prefs_v1"));
  bool loaded = this->pref_.load(&this->prefs_);
  if (!loaded) {
    // No team until the owner picks one: a fresh panel follows live games
    // (no_team_default), never a team nobody chose.
    this->prefs_.league = (uint8_t) League::NFL;
    this->prefs_.team_id = 0;
    this->prefs_.tz_index = ::espn::kDefaultTimezone;
    this->prefs_.flags = FLAGS_DEFAULT;
  } else if (this->prefs_.team_id != 0 && this->current_team_() == nullptr) {
    this->prefs_.team_id = 0;  // a team that is no longer in the list
  }
  if (this->prefs_.tz_index >= ::espn::kTimezoneCount)
    this->prefs_.tz_index = ::espn::kDefaultTimezone;
  this->pref2_ = global_preferences->make_preference<Prefs2>(fnv1_hash("gameday_prefs2_v1"));
  if (!this->pref2_.load(&this->prefs2_) || this->prefs2_.mode > (uint8_t) Mode::FAVORITES ||
      !::espn::rotate_minutes_valid(this->prefs2_.rotate_minutes)) {
    this->prefs2_.mode = (uint8_t) Mode::MY_TEAM;
    this->prefs2_.rotate_minutes = ::espn::kRotateDefaultMinutes;
  }
  this->pref5_ = global_preferences->make_preference<Prefs5>(fnv1_hash("gameday_prefs5_v1"));
  Prefs5 p5{};
  if (!this->pref5_.load(&p5) || p5.no_team_ok != 1) {
    // First boot on firmware that knows "no team". Older firmware saved
    // Dallas on first boot; drop it where nobody can have picked it.
    bool live = this->prefs2_.mode >= (uint8_t) Mode::LIVE_NFL && this->prefs2_.mode <= (uint8_t) Mode::LIVE_ANY;
    if (loaded && ::espn::legacy_team_unpicked(this->prefs_.league, this->prefs_.team_id, this->flag_(FLAG_SETUP),
                                               live)) {
      ESP_LOGI(TAG, "Dropping the Dallas default nobody picked");
      this->prefs_.team_id = 0;
      this->save_prefs_();
    }
    p5.no_team_ok = 1;
    this->pref5_.save(&p5);
  }
  this->pref3_ = global_preferences->make_preference<Prefs3>(fnv1_hash("gameday_prefs3_v1"));
  if (!this->pref3_.load(&this->prefs3_))
    this->prefs3_ = Prefs3{};
  this->pref4_ = global_preferences->make_preference<Prefs4>(fnv1_hash("gameday_prefs4_v1"));
  if (!this->pref4_.load(&this->prefs4_) || this->prefs4_.lockon_minutes < 5 || this->prefs4_.lockon_minutes > 120 ||
      this->prefs4_.release_seconds < 30 || this->prefs4_.release_seconds > 3600 || this->prefs4_.collide > 1) {
    this->prefs4_.lockon_minutes = 15;
    this->prefs4_.release_seconds = 30;
    this->prefs4_.collide = 0;
  }
  this->pref6_ = global_preferences->make_preference<Prefs6>(fnv1_hash("gameday_prefs6_v1"));
  if (!this->pref6_.load(&this->prefs6_) || this->prefs6_.auto_update > 1)
    this->prefs6_ = Prefs6{};
#ifdef GAMEDAY_BAR
  this->bar_pref_ = global_preferences->make_preference<uint32_t>(fnv1_hash("gameday_bar_now_v1"));
  if (!this->bar_pref_.load(&this->now_tried_))
    this->now_tried_ = 0;
#endif
  this->team_cache_pref_ =
      global_preferences->make_preference<::espn::TeamCache>(fnv1_hash("gameday_team_cache_v1"));
  this->rebuild_favorites_(false);
  this->apply_timezone_();
  this->publish_selects_();
#ifdef GAMEDAY_DEMO
  this->demo_setup_();
#endif
  this->rebuild_state_(nullptr);
  if (this->base_ != nullptr)
    this->base_->add_handler(this);
  this->schedule_next_(3000);
}

std::string GamedayComponent::hostname() const { return std::string(App.get_name().c_str()); }

bool GamedayComponent::no_team_default() const {
  if (this->current_team_() != nullptr)
    return false;
  return this->prefs2_.mode == (uint8_t) Mode::MY_TEAM ||
         (this->prefs2_.mode == (uint8_t) Mode::FAVORITES && this->fav_.empty());
}

std::string GamedayComponent::favorite_option_(uint8_t slot) const {
  if (slot < 1 || slot > 4 || this->prefs3_.fav_id[slot - 1] == 0)
    return "None";
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if ((uint8_t) t.league == this->prefs3_.fav_league[slot - 1] && t.espn_id == this->prefs3_.fav_id[slot - 1])
      return std::string(t.league == League::NFL ? "NFL: " : "NCAAF: ") + t.name;
  }
  return "None";
}

void GamedayComponent::select_favorite(uint8_t slot, const std::string &option) {
  if (slot < 1 || slot > 4)
    return;
  uint8_t league = 0;
  uint32_t id = 0;
  if (option != "None") {
    for (size_t i = 0; i < ::espn::kTeamCount; i++) {
      const auto &t = ::espn::kTeams[i];
      if (std::string(t.league == League::NFL ? "NFL: " : "NCAAF: ") + t.name == option) {
        league = (uint8_t) t.league;
        id = t.espn_id;
        break;
      }
    }
    if (id == 0) {
      ESP_LOGW(TAG, "Unknown favorite '%s'", option.c_str());
      return;
    }
  }
  this->set_favorite_(slot, league, id);
}

void GamedayComponent::set_favorite_(uint8_t slot, uint8_t league, uint32_t id) {
  if (slot < 1 || slot > 4)
    return;
  this->prefs3_.fav_league[slot - 1] = league;
  this->prefs3_.fav_id[slot - 1] = id;
  this->pref3_.save(&this->prefs3_);
  bool first_pick = id != 0 && this->mark_setup_done_();
  ESP_LOGI(TAG, "Favorite %u: %s", (unsigned) slot, this->favorite_option_(slot).c_str());
  if (this->favorite_selects_[slot - 1] != nullptr)
    this->favorite_selects_[slot - 1]->publish_state(this->favorite_option_(slot));
  bool was_fav_mode = this->favorites_mode_();
  this->rebuild_favorites_(true);
  if (this->prefs2_.mode == (uint8_t) Mode::FAVORITES && (was_fav_mode || this->favorites_mode_())) {
    // The list changed under the mode: drop the board and let the next
    // cycle refresh what is missing and re-pick.
    this->generation_++;
    this->game_ = GameSnapshot{};
    this->prev_ = GameSnapshot{};
    this->emit_({});
    this->end_backoff_wait_();
    this->schedule_next_(0);
    return;
  }
  if (first_pick)
    this->emit_({});  // redraw the ticker without the setup prompt
  else
    this->rebuild_state_(&this->last_fields_);
}

#ifndef GAMEDAY_DEMO
void GamedayComponent::remote_number(uint8_t n) { this->press_favorite(n); }
#endif

void GamedayComponent::press_favorite(uint8_t slot) {
  std::string option = this->favorite_option_(slot);
  if (option == "None") {
    ESP_LOGI(TAG, "Favorite %u is empty", (unsigned) slot);
    return;
  }
  ESP_LOGI(TAG, "Remote button %u: %s", (unsigned) slot, option.c_str());
  if (this->favorites_mode_()) {
    // Jump to that favorite and pin it: a live game holds, a card seeds the playlist.
    for (size_t i = 0; i < this->fav_.size(); i++) {
      if (this->fav_[i].slot != slot)
        continue;
      this->fav_pinned_ = (int) i;
      this->fav_shown_ = (int) i;
      this->fav_shown_since_ = (int64_t) this->time_->timestamp_now();
      this->fav_poll_ms_ = 0;
      this->prev_ = GameSnapshot{};
      this->game_ = this->fav_[i].game;
      this->post_since_ms_ = 0;
      this->emit_({});
      this->end_backoff_wait_();
      this->schedule_next_(0);
      return;
    }
    return;
  }
  if (this->prefs2_.mode != (uint8_t) Mode::MY_TEAM) {
    this->prefs2_.mode = (uint8_t) Mode::MY_TEAM;
    this->pref2_.save(&this->prefs2_);
    if (this->mode_select_ != nullptr)
      this->mode_select_->publish_state(MODE_OPTIONS[0]);
    this->reset_game_();
    this->generation_++;
    this->schedule_next_(0);
  }
  if (this->team_select_ != nullptr)
    this->team_select_->publish_state(option);
  this->select_team(option);
}

// Push the stored choices into the two selects. Runs at setup and once more
// after the network is up, so the web page and the API always see them.
void GamedayComponent::publish_selects_() {
  std::string team = this->team_option_();
  ESP_LOGI(TAG, "Publishing selects: team '%s', timezone '%s'", team.c_str(),
           ::espn::kTimezones[this->prefs_.tz_index].name);
  if (this->team_select_ != nullptr && !team.empty())
    this->team_select_->publish_state(team);
  if (this->timezone_select_ != nullptr)
    this->timezone_select_->publish_state(::espn::kTimezones[this->prefs_.tz_index].name);
  if (this->mode_select_ != nullptr)
    this->mode_select_->publish_state(MODE_OPTIONS[this->prefs2_.mode]);
  for (uint8_t slot = 1; slot <= 4; slot++)
    if (this->favorite_selects_[slot - 1] != nullptr)
      this->favorite_selects_[slot - 1]->publish_state(this->favorite_option_(slot));
}

void GamedayComponent::select_mode(const std::string &option) {
  for (uint8_t i = 0; i <= (uint8_t) Mode::FAVORITES; i++) {
    if (option != MODE_OPTIONS[i])
      continue;
    if (i == this->prefs2_.mode) {
      if (this->mark_setup_done_())
        this->emit_({});  // redraw the ticker without the setup prompt
      return;
    }
    this->prefs2_.mode = i;
    this->pref2_.save(&this->prefs2_);
    ESP_LOGI(TAG, "Mode: %s", option.c_str());
    // The new mode's first scoreboard, not the mode switch itself, ends the
    // boot screen hold (mirrors apply_team_).
    this->board_ready_ = false;
    this->board_pending_ms_ = millis() == 0 ? 1 : millis();
    this->mark_setup_done_();
    if (this->mode_select_ != nullptr)
      this->mode_select_->publish_state(option);
    this->reset_game_();
    this->generation_++;
    this->emit_({});  // clear the board while the next fetch runs
    this->fire_action_("board_pending");  // the boot screen holds (gameday-common.yaml)
    this->end_backoff_wait_();
    this->schedule_next_(0);
    // Somebody asked for a different mode, so they are looking at the panel.
    this->fire_action_("user_pick");
    return;
  }
  ESP_LOGW(TAG, "Unknown mode '%s'", option.c_str());
}

void GamedayComponent::set_rotate_minutes(int minutes) {
  minutes = ::espn::clamp_rotate_minutes(minutes);
  if (minutes == this->prefs2_.rotate_minutes)
    return;
  this->prefs2_.rotate_minutes = (uint8_t) minutes;
  this->pref2_.save(&this->prefs2_);
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_lockon_minutes(int minutes) {
  minutes = minutes < 5 ? 5 : minutes > 120 ? 120 : minutes;
  if (minutes == this->prefs4_.lockon_minutes)
    return;
  this->prefs4_.lockon_minutes = (uint8_t) minutes;
  this->pref4_.save(&this->prefs4_);
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_release_seconds(int seconds) {
  seconds = seconds < 30 ? 30 : seconds > 3600 ? 3600 : seconds;
  if (seconds == this->prefs4_.release_seconds)
    return;
  this->prefs4_.release_seconds = (uint16_t) seconds;
  this->pref4_.save(&this->prefs4_);
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_collision_alternate(bool alternate) {
  if ((alternate ? 1 : 0) == this->prefs4_.collide)
    return;
  this->prefs4_.collide = alternate ? 1 : 0;
  this->pref4_.save(&this->prefs4_);
  this->rebuild_state_(&this->last_fields_);
}

// ---- Favorite Teams mode ------------------------------------------------------

// Rebuilds the entry list from the four slots. keep_cards carries a slot's
// fetched game over when the same team is still in the list (reordering).
void GamedayComponent::rebuild_favorites_(bool keep_cards) {
  std::vector<FavEntry> old = std::move(this->fav_);
  this->fav_.clear();
  for (uint8_t i = 0; i < 4; i++) {
    if (this->prefs3_.fav_id[i] == 0)
      continue;
    const ::espn::Team *team = nullptr;
    for (size_t t = 0; t < ::espn::kTeamCount; t++) {
      if ((uint8_t) ::espn::kTeams[t].league == this->prefs3_.fav_league[i] &&
          ::espn::kTeams[t].espn_id == this->prefs3_.fav_id[i]) {
        team = &::espn::kTeams[t];
        break;
      }
    }
    if (team == nullptr)
      continue;
    FavEntry e;
    e.team = team;
    e.slot = i + 1;
    if (keep_cards) {
      for (auto &o : old) {
        if (o.team == team) {
          e = o;
          e.slot = i + 1;
          break;
        }
      }
    }
    this->fav_.push_back(e);
  }
  this->fav_shown_ = -1;
  this->fav_pinned_ = -1;
  this->fav_locked_ = false;
  this->fav_poll_ms_ = 0;
}

::espn::FavRules GamedayComponent::fav_rules_() const {
  ::espn::FavRules r;
  r.lockon_s = (int64_t) this->prefs4_.lockon_minutes * 60;
  r.release_s = this->prefs4_.release_seconds;
  r.alternate = this->prefs4_.collide == 1;
  r.rotate_s = (int64_t) this->prefs2_.rotate_minutes * 60;
  r.dwell_s = 10;
  return r;
}

std::vector<::espn::FavGame> GamedayComponent::fav_games_() const {
  std::vector<::espn::FavGame> out;
  for (const auto &e : this->fav_) {
    ::espn::FavGame g;
    g.valid = e.game.valid;
    g.state = e.game.state;
    g.kickoff_epoch = e.game.kickoff_epoch;
    g.final_epoch = e.final_epoch;
    out.push_back(g);
  }
  return out;
}

// Runs the pick and puts the chosen entry's card on the board when it differs
// from what is up. Returns true on a switch.
bool GamedayComponent::fav_choose_(int64_t now_epoch) {
  ::espn::FavChoice c = ::espn::pick_favorite(this->fav_games_(), now_epoch, this->fav_rules_(), this->fav_shown_,
                                              this->fav_shown_since_, this->fav_pinned_);
  bool was_locked = this->fav_locked_;
  this->fav_locked_ = c.locked;
  if (this->fav_pinned_ >= 0 && !c.locked && c.index != this->fav_pinned_)
    this->fav_pinned_ = -1;  // the playlist moved past the pinned card
  if (c.index == this->fav_shown_) {
    if (was_locked != c.locked)
      this->fav_poll_ms_ = 0;
    return false;
  }
  this->fav_shown_ = c.index;
  this->fav_shown_since_ = now_epoch;
  this->fav_poll_ms_ = 0;
  this->prev_ = GameSnapshot{};
  this->game_ = c.index >= 0 ? this->fav_[c.index].game : GameSnapshot{};
  this->post_since_ms_ = 0;
  this->misses_ = 0;
  if (c.index >= 0)
    ESP_LOGI(TAG, "Favorites: showing %s%s", this->fav_[c.index].team->abbr, c.locked ? " (locked)" : "");
  this->emit_({});
  if (this->fav_shown_ >= 0 && this->game_.valid)
    this->mark_board_ready_();  // a kept card counts as the first board too
  return true;
}

// The board's game, except in the Favorites idle playlist, where the card on
// the board changes every 10 s and Home Assistant would log each one.
const GameSnapshot &GamedayComponent::ha_game_() const {
  if (!this->favorites_mode_() || this->fav_locked_ || this->demo_playing_())
    return this->game_;
  int i = ::espn::followed_favorite(this->fav_games_());
  return i >= 0 ? this->fav_[i].game : this->game_;
}

// Main loop: refresh a stale entry, else poll the locked game when due, else tick.
void GamedayComponent::start_fav_job_(uint32_t now) {
  Job &j = this->job_;
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  ::espn::FavRules rules = this->fav_rules_();
  for (auto &e : this->fav_) {
    if (e.game.valid && e.game.state == GameState::POST && e.final_epoch != 0 && !e.stale &&
        tnow - e.final_epoch >= rules.release_s)
      e.stale = true;  // released: the team endpoint now points at the next game
  }
  int refresh = -1;
  for (size_t i = 0; i < this->fav_.size(); i++) {
    const FavEntry &e = this->fav_[i];
    bool missing = !e.sched.valid || e.stale;
    uint32_t age = now - e.fetched_ms;
    if ((missing && (e.fetched_ms == 0 || age >= RETRY_INTERVAL)) || (!missing && age >= SCHEDULE_INTERVAL)) {
      refresh = (int) i;
      break;
    }
  }
  if (this->force_schedule_) {
    // Refresh Now in Favorites mode. Re-read the team that is on the board,
    // since that is the one the owner is looking at, and it is one fetch
    // rather than four. The other three keep their own six hour cycle.
    // Spend the flag here, not on success: a failed favorites fetch
    // reschedules at once, so a flag left set would retry in a tight loop.
    this->force_schedule_ = false;
    if (this->fav_shown_ >= 0)
      refresh = this->fav_shown_;
    else if (!this->fav_.empty())
      refresh = 0;
  }
  this->fav_choose_(tnow);
  if (refresh < 0 && this->fav_locked_ && this->fav_shown_ >= 0) {
    uint32_t due = this->interval_for_phase_();
    if (this->fav_poll_ms_ == 0 || now - this->fav_poll_ms_ >= due)
      refresh = -2;  // poll the shown game
  }
  if (refresh == -1 || this->backing_off_()) {
    this->schedule_next_(FAV_TICK);
    return;
  }
  int index = refresh >= 0 ? refresh : this->fav_shown_;
  FavEntry &e = this->fav_[index];
  j = Job{};
  j.generation = this->generation_;
  j.team = e.team;
  j.our_id = e.team->espn_id;
  j.fav_index = index;
  j.need_schedule = refresh >= 0;
  j.schedule = e.sched;
  if (refresh >= 0)
    e.fetched_ms = now == 0 ? 1 : now;
  this->start_worker_();
}

void GamedayComponent::apply_fav_job_(uint32_t now) {
  Job &j = this->job_;
  if (j.fav_index < 0 || j.fav_index >= (int) this->fav_.size() || this->fav_[j.fav_index].team != j.team) {
    this->schedule_next_(0);  // the list changed while the worker ran
    return;
  }
  FavEntry &e = this->fav_[j.fav_index];
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  bool shown = j.fav_index == this->fav_shown_;
  if (shown)
    this->fav_poll_ms_ = now == 0 ? 1 : now;
  if (j.need_schedule) {
    if (!j.schedule_ok) {
      this->misses_++;
      this->back_off_();  // start_fav_job_ waits it out and keeps the playlist ticking
      this->schedule_next_(0);
      return;
    }
    this->clear_backoff_();
    e.sched = j.schedule;
    ::espn::adopt_league(e.sched, e.team->league);
    e.stale = false;
    if (j.no_event) {
      ESP_LOGI(TAG, "No upcoming game for %s", e.team->abbr);
      e.game = GameSnapshot{};
      e.final_epoch = 0;
      if (shown)
        this->game_ = GameSnapshot{};
      if (!this->fav_choose_(tnow))
        this->rebuild_state_(&this->last_fields_);
      this->schedule_next_(0);
      return;
    }
  }
  if (!j.game_ok) {
    this->misses_++;
    this->back_off_();
    this->schedule_next_(0);
    return;
  }
  GameSnapshot old = e.game;
  e.game = j.game;
  if (!old.valid || old.event_id != e.game.event_id)
    e.final_epoch = 0;
  if (e.game.state == GameState::POST && old.valid && old.event_id == e.game.event_id && old.state != GameState::POST)
    e.final_epoch = tnow;
  this->mark_good_poll_();
  bool switched = this->fav_choose_(tnow);
  if (!switched && shown) {
    // Same game on the board: render the new poll with its splash.
    this->prev_ = this->game_;
    this->game_ = e.game;
    ::espn::Splash splash = ::espn::decide_splash(this->prev_, this->game_, this->opponent_splashes(), false);
    this->emit_(splash);
  } else if (!switched) {
    this->rebuild_state_(&this->last_fields_);  // the page's list of favorites' games
  }
  if (this->fav_shown_ >= 0 && this->game_.valid)
    this->mark_board_ready_();
  this->schedule_next_(0);
}

uint32_t GamedayComponent::our_id_() const {
  if (this->job_.fav_index >= 0)
    return this->job_.our_id;
  // In the fallback the panel is showing the saved team's own game, so the
  // snapshot is oriented around that team, exactly as in My team mode.
  uint8_t m = this->job_.mode;
  if (m >= (uint8_t) Mode::LIVE_NFL && m <= (uint8_t) Mode::LIVE_ANY && !this->live_fallback_)
    return this->live_away_id_;
  const ::espn::Team *t = this->current_team_();
  return t ? t->espn_id : 0;
}

void GamedayComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Game Day Scoreboard:");
  const ::espn::Team *t = this->current_team_();
  ESP_LOGCONFIG(TAG, "  Team: %s (%s id %u)", t ? t->name : "none", t ? t->abbr : "-", (unsigned) this->prefs_.team_id);
  ESP_LOGCONFIG(TAG, "  Timezone: %s", ::espn::kTimezones[this->prefs_.tz_index].name);
  ESP_LOGCONFIG(TAG, "  Flags: 0x%02X", this->prefs_.flags);
#ifdef GAMEDAY_BAR
  ESP_LOGCONFIG(TAG, "  Bar beta: installs from the beta channel");
#endif
}

void GamedayComponent::loop() {
  if (this->logos_.loop())
    this->fire_action_("logo_ready");  // gameday-common.yaml redraws the logos
  if (millis() - this->auto_update_ms_ >= AUTO_UPDATE_TICK) {
    this->auto_update_ms_ = millis();
    this->auto_update_tick_();
  }
#ifdef GAMEDAY_DEMO
  // While the scripted game plays, nothing new is fetched. A fetch already
  // running still finishes below, and is discarded as belonging to before.
  if (this->demo_loop_() && !this->busy_)
    return;
#endif
  // The finals rotation turns its cards on its own clock, fetch or no fetch.
  if (this->recap_on_ && !this->demo_playing_())
    this->recap_tick_();
  if (this->busy_) {
    if (!this->job_done_) {
      // A worker that died without setting job_done_ would block the loop
      // forever and Refresh Now could never clear it. Give up eventually, but
      // not before a slow-but-honest job could have finished: abandoning a
      // live worker is what makes the accepted run_job_/job_ write race
      // reachable, and that must stay out of reach on a merely slow network.
      if (this->busy_since_ms_ != 0 && (millis() - this->busy_since_ms_) >= WORKER_DEADLINE) {
        ESP_LOGW(TAG, "Fetch task did not finish in %us, giving up on it", (unsigned) (WORKER_DEADLINE / 1000));
        this->busy_ = false;
        this->busy_since_ms_ = 0;
        this->worker_seq_++;  // a late finisher must not signal job_done_
        // Wait a full retry before starting another worker rather than
        // stacking a second one on top of a task that may still be alive.
        this->schedule_next_(RETRY_INTERVAL);
      }
      return;
    }
    this->busy_ = false;
    this->busy_since_ms_ = 0;
    this->job_done_ = false;
    this->apply_job_();
    return;
  }
  if ((int32_t) (millis() - this->next_fetch_ms_) < 0)
    return;
  if (!network::is_connected() || this->time_ == nullptr || !this->time_->now().is_valid()) {
    this->schedule_next_(NOT_READY_INTERVAL);
    return;
  }
  if (!this->selects_published_) {
    this->selects_published_ = true;
    ESP_LOGI(TAG, "Startup: Wi-Fi and clock ready at %u ms", (unsigned) millis());
    this->publish_selects_();
  }
  this->start_job_();
}

const ::espn::Team *GamedayComponent::current_team_() const {
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if ((uint8_t) t.league == this->prefs_.league && t.espn_id == this->prefs_.team_id)
      return &t;
  }
  return nullptr;
}

std::string GamedayComponent::team_option_() const {
  const ::espn::Team *t = this->current_team_();
  if (t == nullptr)
    return "";
  return std::string(t->league == League::NFL ? "NFL: " : "NCAAF: ") + t->name;
}

void GamedayComponent::select_team(const std::string &option) {
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    std::string name = std::string(t.league == League::NFL ? "NFL: " : "NCAAF: ") + t.name;
    if (name == option) {
      this->apply_team_(t);
      return;
    }
  }
  ESP_LOGW(TAG, "Unknown team option '%s'", option.c_str());
}

void GamedayComponent::select_team_id(League league, uint32_t id) {
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if (t.league == league && t.espn_id == id) {
      this->apply_team_(t);
      return;
    }
  }
  ESP_LOGW(TAG, "Unknown team %s %u", league == League::NFL ? "nfl" : "ncaa", (unsigned) id);
}

void GamedayComponent::fire_action_(const std::string &name) {
  for (auto &cb : this->action_callbacks_)
    cb(name);
}

// The first team, mode or favorite picked from the page, the app or Home
// Assistant ends setup: the setup ticker goes and the boot screen hides.
// Returns true when this call is the one that ended it.
bool GamedayComponent::mark_setup_done_() {
  if (this->flag_(FLAG_SETUP))
    return false;
  this->prefs_.flags |= FLAG_SETUP;
  this->save_prefs_();
  this->fire_action_("team_picked");  // the setup screen listens for this
  return true;
}

void GamedayComponent::apply_team_(const ::espn::Team &t) {
  if ((uint8_t) t.league == this->prefs_.league && t.espn_id == this->prefs_.team_id) {
    // Picking the team the panel already had is still a pick: setup ends.
    if (this->mark_setup_done_())
      this->emit_({});  // redraw the ticker without the setup prompt
    return;
  }
  this->prefs_.league = (uint8_t) t.league;
  this->prefs_.team_id = t.espn_id;
  this->save_prefs_();
  ESP_LOGI(TAG, "Team changed to %s", t.name);
  // The boot screen holds for this team's first scoreboard (board_pending
  // below). Cleared before mark_setup_done_, whose team_picked hides the
  // setup screen through that same hold.
  this->board_ready_ = false;
  this->board_pending_ms_ = millis() == 0 ? 1 : millis();
  if (this->team_select_ != nullptr)
    this->team_select_->publish_state(this->team_option_());
  this->mark_setup_done_();
  // The select, the page and the app all land here, and only on a real
  // change, so this is the one place that knows a person picked a team.
  this->fire_action_("user_pick");
  this->reset_game_();
  this->generation_++;  // a fetch already in flight belongs to the old team
  // Behind the boot screen: the new team's logo and name, with no "Loading"
  // text. The game data follows in a second or two.
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::PRE;
  g.team_abbr = t.abbr;
  g.team_id = t.espn_id;
  g.team_logo = ::espn::team_logo_url(t.league, t.espn_id, t.abbr);
  this->game_ = g;
  this->emit_({});
  this->fire_action_("board_pending");  // the boot screen holds (gameday-common.yaml)
  this->schedule_next_(0);
}

// Back to no team: My team then runs the live board and the finals rotation
// (no_team_default). Setup stays done.
void GamedayComponent::clear_team_() {
  if (this->prefs_.team_id == 0)
    return;
  this->prefs_.team_id = 0;
  this->save_prefs_();
  ESP_LOGI(TAG, "Team cleared");
  this->board_ready_ = false;
  this->board_pending_ms_ = millis() == 0 ? 1 : millis();
  this->reset_game_();
  this->generation_++;
  this->emit_({});
  this->fire_action_("board_pending");
  this->schedule_next_(0);
}

void GamedayComponent::select_timezone(const std::string &option) {
  for (size_t i = 0; i < ::espn::kTimezoneCount; i++) {
    if (option != ::espn::kTimezones[i].name)
      continue;
    this->prefs_.tz_index = (uint8_t) i;
    this->save_prefs_();
    this->apply_timezone_();
    if (this->timezone_select_ != nullptr)
      this->timezone_select_->publish_state(::espn::kTimezones[i].name);
    // Re-render so a PRE ticker picks up the new kickoff time.
    if (this->game_.valid)
      this->emit_({});
    else
      this->rebuild_state_(&this->last_fields_);
    return;
  }
  ESP_LOGW(TAG, "Unknown timezone option '%s'", option.c_str());
}

void GamedayComponent::refresh_now() {
  // Read the team endpoint again, do not just re-poll the game already on
  // the board: the owner presses this when the board looks wrong, and a
  // moved, postponed or finished game only shows up in that read.
  this->force_schedule_ = true;
  this->end_backoff_wait_();
  this->schedule_next_(0);
}

void GamedayComponent::set_flag_(uint8_t f, bool on) {
  uint8_t next = on ? (this->prefs_.flags | f) : (this->prefs_.flags & ~f);
  if (next == this->prefs_.flags)
    return;
  this->prefs_.flags = next;
  this->save_prefs_();
  if (this->game_.valid)
    this->emit_({});
  else
    this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::save_prefs_() { this->pref_.save(&this->prefs_); }

void GamedayComponent::set_auto_update(bool on) {
#ifdef GAMEDAY_BAR
  return;
#endif
  if (on == this->auto_update())
    return;
  this->prefs6_.auto_update = on ? 1 : 0;
  this->pref6_.save(&this->prefs6_);
  ESP_LOGI(TAG, "Install updates automatically: %s", on ? "on" : "off");
  this->rebuild_state_(&this->last_fields_);
}

// A game the panel follows is in progress: the one on the board, which
// covers My team and the live modes, or any favorite's in Favorite teams.
bool GamedayComponent::followed_game_live_() const {
  if (this->game_.valid && this->game_.state == GameState::IN)
    return true;
  if (this->favorites_mode_())
    for (const auto &e : this->fav_)
      if (e.game.valid && e.game.state == GameState::IN)
        return true;
  return false;
}

void GamedayComponent::auto_update_tick_() {
#ifdef USE_UPDATE
  if (!this->auto_update() || this->time_ == nullptr || App.get_updates().empty())
    return;
  update::UpdateEntity *u = App.get_updates()[0];
  ESPTime now = this->time_->now();
  ::espn::AutoUpdateInputs in;
  in.enabled = true;
  in.clock_valid = now.is_valid();
  in.hour = now.hour;
  in.minute = now.minute;
  in.today = ::espn::auto_update_day(now.year, now.day_of_year);
  in.tried_day = this->prefs6_.tried_day;
  in.checked_day = this->auto_update_checked_;
  uint8_t mac[6];
  get_mac_address_raw(mac);
  in.slot = ::espn::auto_update_slot(mac[4], mac[5]);
  in.check_done = millis() - this->auto_update_check_ms_ >= AUTO_UPDATE_CHECK_WAIT;
  in.available = u->state == update::UPDATE_STATE_AVAILABLE;
  in.newer = ::espn::version_newer(u->update_info.latest_version, u->update_info.current_version);
  in.live = this->followed_game_live_();
#ifdef GAMEDAY_BAR
  in.now = in.available && ::espn::install_now_listed(u->update_info.summary, this->hostname()) &&
           fnv1_hash(u->update_info.latest_version) != this->now_tried_;
#endif
  switch (::espn::auto_update_step(in)) {
    case ::espn::AutoUpdateStep::IDLE:
      // So the next wait for a game is logged too (an install-now one comes
      // at any hour, not once a night).
      this->auto_update_waiting_ = false;
      break;
    case ::espn::AutoUpdateStep::CHECK:
      ESP_LOGI(TAG, "Auto update: checking for a new release");
      this->auto_update_checked_ = in.today;
      this->auto_update_check_ms_ = millis();
      this->auto_update_waiting_ = false;
      u->check();
      break;
    case ::espn::AutoUpdateStep::WAIT_GAME:
      if (!this->auto_update_waiting_)
        ESP_LOGI(TAG, "Auto update: %s is out, waiting for the live game to end",
                 u->update_info.latest_version.c_str());
      this->auto_update_waiting_ = true;
      break;
    case ::espn::AutoUpdateStep::INSTALL:
      // Saved, and written to flash now, before the install starts: a power
      // cut partway through boots the old firmware, which must not try again
      // until tomorrow night.
      ESP_LOGI(TAG, "Auto update: installing %s (running %s)", u->update_info.latest_version.c_str(),
               u->update_info.current_version.c_str());
      this->prefs6_.tried_day = in.today;
      this->pref6_.save(&this->prefs6_);
      global_preferences->sync();
      u->perform();
      break;
    case ::espn::AutoUpdateStep::INSTALL_NOW:
#ifdef GAMEDAY_BAR
      // Same as above, but the try is for this version, not tonight: a
      // failed one waits for the night, which tries once more.
      ESP_LOGI(TAG, "Auto update: installing %s now, as the beta channel asks (running %s)",
               u->update_info.latest_version.c_str(), u->update_info.current_version.c_str());
      this->now_tried_ = fnv1_hash(u->update_info.latest_version);
      this->bar_pref_.save(&this->now_tried_);
      global_preferences->sync();
      u->perform();
#endif
      break;
  }
#endif
}

void GamedayComponent::apply_timezone_() {
#ifdef GAMEDAY_TZ_PARSED
  // ESPHome 2026.9+: no POSIX parser on the device, hand the clock the
  // pre-parsed rules (scripts/build_timezones.py keeps the table in step).
  const ::espn::TzParsed &p = ::espn::kTimezonesParsed[this->prefs_.tz_index];
  auto rule = [](const ::espn::TzRule &r) {
    time::DSTRule d{};
    d.time_seconds = r.time_seconds;
    d.day = r.day;
    d.type = (time::DSTRuleType) r.type;
    d.month = r.month;
    d.week = r.week;
    d.day_of_week = r.day_of_week;
    return d;
  };
  time::ParsedTimezone tz{};
  tz.std_offset_seconds = p.std_offset_seconds;
  tz.dst_offset_seconds = p.dst_offset_seconds;
  tz.dst_start = rule(p.dst_start);
  tz.dst_end = rule(p.dst_end);
  time::set_global_tz(tz);
#else
  if (this->time_ != nullptr)
    this->time_->set_timezone(::espn::kTimezones[this->prefs_.tz_index].posix);
#endif
}

void GamedayComponent::reset_game_() {
  this->rebuild_favorites_(true);
  this->live_away_id_ = 0;
  this->live_started_ms_ = 0;
  this->live_none_ = false;
  this->live_rescan_ms_ = NO_LIVE_RESCAN;
  this->live_fallback_ = false;
  // The finals and the walks stay: they are per league, not per mode.
  this->recap_on_ = false;
  this->recap_cards_.clear();
  this->recap_step_ = 0;
  this->recap_step_ms_ = 0;
  this->recap_scan_next_ = 0;
  this->schedule_ = Schedule{};
  this->schedule_fetched_ms_ = 0;
  this->force_schedule_ = false;
  this->upcoming_.clear();
  this->upcoming_due_ = false;
  this->upcoming_after_poll_ = false;
  this->cache_unconfirmed_ = false;
  this->game_ = GameSnapshot{};
  this->prev_ = GameSnapshot{};
  this->post_since_ms_ = 0;
  this->misses_ = 0;
}

std::shared_ptr<http_request::HttpContainer> GamedayComponent::open_(const std::string &url) {
  std::vector<http_request::Header> headers = {
      {"User-Agent", USER_AGENT},
      {"Accept", "application/json"},
  };
  auto container = this->http_->get(url, headers, {"retry-after"});
  if (container == nullptr) {
    ESP_LOGW(TAG, "Request failed: %s", url.c_str());
    return nullptr;
  }
  if (container->status_code != 200) {
    ESP_LOGW(TAG, "HTTP %d for %s", container->status_code, url.c_str());
    if (::espn::is_throttle_status(container->status_code)) {
      // Worker task: the job is this task's until it reports done.
      this->job_.throttled = true;
      uint32_t ra = ::espn::parse_retry_after_ms(container->get_response_header("retry-after"));
      if (ra > this->job_.retry_after_ms)
        this->job_.retry_after_ms = ra;
    }
    container->end();
    return nullptr;
  }
  return container;
}

uint32_t GamedayComponent::back_off_() {
  if (this->job_.throttled)
    this->throttled_ = true;
  if (this->fail_streak_ < 255)
    this->fail_streak_++;
  uint32_t ms = ::espn::backoff_ms(this->fail_streak_, this->throttled_, this->job_.retry_after_ms, random_uint32());
  ESP_LOGW(TAG, "%u failed read(s) in a row%s: next try in %us", (unsigned) this->fail_streak_,
           this->throttled_ ? ", ESPN is refusing" : "", (unsigned) (ms / 1000));
  this->backoff_since_ms_ = millis();
  this->backoff_ms_ = ms;
  return ms;
}

bool GamedayComponent::fetch_schedule_(const ::espn::Team *team, Schedule &out) {
  std::string url = ::espn::team_url(team->league, team->espn_id);
  ESP_LOGD(TAG, "Fetching schedule: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container);
  Schedule s;
  bool ok = ::espn::parse_team(reader, s);
  container->end();
  if (!ok) {
    ESP_LOGW(TAG, "Schedule parse failed after %u bytes", (unsigned) reader.total());
    return false;
  }
  ESP_LOGI(TAG, "Schedule: event %s kickoff %lld group %u (%u bytes)", s.event_id.c_str(),
           (long long) s.kickoff_epoch, (unsigned) s.group, (unsigned) reader.total());
  out = s;
  return true;
}

bool GamedayComponent::fetch_upcoming_(const ::espn::Team *team, std::vector<::espn::Upcoming> &out) {
  std::string url = ::espn::schedule_url(team->league, team->espn_id);
  ESP_LOGD(TAG, "Fetching upcoming games: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container);
  // Four: the game on the board plus the three after it.
  bool ok = ::espn::parse_upcoming(reader, team->espn_id, 4, out);
  container->end();
  ESP_LOGI(TAG, "Upcoming: %u game(s) (%u bytes)", (unsigned) out.size(), (unsigned) reader.total());
  return ok;
}

bool GamedayComponent::fetch_live_games_(League league, std::vector<::espn::LiveGame> &out, int64_t &next_kickoff,
                                         std::vector<::espn::Final> *finals, int64_t epoch) {
  std::string url = ::espn::scan_url(league, epoch != 0 ? epoch : (int64_t) this->time_->timestamp_now());
  ESP_LOGD(TAG, "Scanning for live games: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container);
  size_t before = out.size();
  size_t finals_before = finals != nullptr ? finals->size() : 0;
  int64_t next = 0;
  bool ok = ::espn::parse_live_games(reader, out, &next, finals);
  for (size_t i = before; i < out.size(); i++)
    out[i].league = (uint8_t) league;
  if (finals != nullptr)
    for (size_t i = finals_before; i < finals->size(); i++)
      (*finals)[i].league = (uint8_t) league;
  container->end();
  ESP_LOGI(TAG, "Scan: %u live game(s), %u final(s) (%u bytes)", (unsigned) out.size(),
           (unsigned) (finals != nullptr ? finals->size() - finals_before : 0), (unsigned) reader.total());
  if (next != 0 && (next_kickoff == 0 || next < next_kickoff))
    next_kickoff = next;
  return ok;
}

bool GamedayComponent::fetch_game_(const ::espn::Team *team, const Schedule &schedule, GameSnapshot &out) {
  bool live = this->job_.mode >= (uint8_t) Mode::LIVE_NFL && this->job_.mode <= (uint8_t) Mode::LIVE_ANY;
  League league = live || this->job_.fav_index >= 0 ? (League) schedule.league : team->league;
  std::string url = ::espn::scoreboard_url(league, schedule.group, schedule.kickoff_epoch);
  ESP_LOGD(TAG, "Fetching game: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container);
  GameSnapshot g;
  bool ok = ::espn::parse_scoreboard(reader, schedule.event_id, this->our_id_(), g);
  container->end();
  if (!ok) {
    ESP_LOGW(TAG, "Game %s not parsed from scoreboard (%u bytes)", schedule.event_id.c_str(),
             (unsigned) reader.total());
    return false;
  }
  ESP_LOGI(TAG, "Game: %s %s %d - %s %d [%s] (%u bytes)", ::espn::state_name(g.state), g.team_abbr.c_str(),
           g.team_score, g.opp_abbr.c_str(), g.opp_score, g.short_detail.c_str(), (unsigned) reader.total());
  out = g;
  return true;
}

uint32_t GamedayComponent::interval_for_phase_() const {
  uint32_t ms;
  switch (this->game_.state) {
    case GameState::IN:
      ms = IN_INTERVAL;
      break;
    case GameState::POST:
      ms = POST_INTERVAL;
      break;
    case GameState::PRE: {
      int64_t now = (int64_t) this->time_->timestamp_now();
      int64_t until = this->game_.kickoff_epoch - now;
      ms = until <= PRE_NEAR_SECONDS ? PRE_NEAR_INTERVAL : PRE_FAR_INTERVAL;
      break;
    }
    default:
      ms = SCHEDULE_INTERVAL;
      break;
  }
  // The fallback card is only borrowing the board: a 15 minute PRE cycle, or
  // the 6 hours an out of season team gets, must never outlast the rescan.
  if (this->live_fallback_) {
    uint32_t since = millis() - this->live_started_ms_;
    uint32_t left = since >= this->live_rescan_ms_ ? 0 : this->live_rescan_ms_ - since;
    if (ms > left)
      ms = left;
  }
  return ms;
}

// Main loop: decide what this cycle needs and hand it to the worker task.
void GamedayComponent::start_job_() {
  uint32_t now = millis();
  Job &j = this->job_;
  j = Job{};
  j.generation = this->generation_;
  j.team = this->current_team_();
  j.mode = this->effective_mode_();
  if (this->favorites_mode_()) {
    this->start_fav_job_(now);
    return;
  }
  if (j.team == nullptr && !this->live_mode_()) {
    this->schedule_next_(SCHEDULE_INTERVAL);
    return;
  }
  if (this->live_mode_() && this->recap_on_) {
    // The finals rotation is up: read the days it still needs one by one,
    // then rescan for a live game when the wait is over.
    if (this->force_schedule_) {
      this->force_schedule_ = false;  // Refresh Now: scan now
      this->live_started_ms_ = 0;
    }
    int32_t today = ::espn::espn_day((int64_t) this->time_->timestamp_now());
    if (this->recap_job_(j, today)) {
      this->start_worker_();
      return;
    }
    uint32_t since = now - this->live_started_ms_;
    if (this->live_started_ms_ != 0 && since < this->live_rescan_ms_) {
      this->schedule_next_(this->live_rescan_ms_ - since);
      return;
    }
    j.need_scan = true;
    j.want_finals = true;
    this->start_worker_();
    return;
  }
  if (this->live_mode_()) {
    // Inside the fallback live_started_ms_ times the rescan instead of the
    // rotate: the board is on the owner's own team until a game kicks off.
    bool rescan_due = this->live_started_ms_ == 0 || (now - this->live_started_ms_) >= this->live_rescan_ms_;
    if (!this->live_fallback_ || rescan_due) {
      if (this->live_fallback_) {
        j.need_scan = true;
        // Stamped here, not when the scan lands, so a failed scan does not
        // fire another one on the retry a minute later.
        this->live_started_ms_ = now == 0 ? 1 : now;
      } else {
        bool rotate = this->live_started_ms_ != 0 &&
                      (now - this->live_started_ms_) >= (uint32_t) this->prefs2_.rotate_minutes * MINUTE;
        bool ended = this->game_.valid && this->game_.state != GameState::IN;
        j.need_scan = !this->schedule_.valid || rotate || ended || this->live_none_;
      }
      // With no team, nothing live means the finals rotation: the scan keeps
      // today's finals for it.
      j.want_finals = j.team == nullptr;
      j.schedule = this->schedule_;
      this->start_worker_();
      return;
    }
    // In the fallback with the rescan not due: fall through to the My team
    // path below, so the card is fetched and polled by that code and not by
    // a copy of it.
  }
  if (!this->schedule_.valid && !this->team_cache_tried_) {
    this->team_cache_tried_ = true;
    if (this->load_team_cache_(j.team)) {
      this->schedule_fetched_ms_ = now == 0 ? 1 : now;
      this->cache_unconfirmed_ = true;
      // Not upcoming_due_ yet: that would put the season download in front
      // of the scoreboard, which is what the cache is here to skip.
      this->upcoming_after_poll_ = true;
    }
  }
  j.need_schedule = ::espn::schedule_due(this->schedule_.valid, this->force_schedule_, now,
                                         this->schedule_fetched_ms_, SCHEDULE_INTERVAL);
  // The season schedule is ~200KB; it is fetched on a cycle of its own right
  // after the board has its game, never in front of it.
  if (this->upcoming_due_ && !j.need_schedule && this->schedule_.valid) {
    j.need_upcoming = true;
    j.schedule = this->schedule_;
    this->start_worker_();
    return;
  }
  if (this->post_since_ms_ != 0 && (now - this->post_since_ms_) >= POST_LINGER) {
    // Game is over and lingered: look for the next one. Re-reading the team
    // endpoint is not enough on its own, because it can keep pointing at a
    // game that finished hours ago. The season schedule lists only games that
    // have not started, so prefer it when it has one.
    int64_t tnow = (int64_t) this->time_->timestamp_now();
    const ::espn::Upcoming *next = nullptr;
    for (const auto &u : this->upcoming_) {
      if (u.kickoff_epoch > tnow && u.event_id != this->game_.event_id) {
        next = &u;
        break;
      }
    }
    this->prev_ = GameSnapshot{};
    this->game_ = GameSnapshot{};
    this->post_since_ms_ = 0;
    if (next != nullptr) {
      ESP_LOGI(TAG, "Game is over: taking event %s from the season schedule", next->event_id.c_str());
      // group, colors and the record are team properties from the team
      // endpoint and stay valid; only the event moves.
      this->schedule_.event_id = next->event_id;
      this->schedule_.kickoff_epoch = next->kickoff_epoch;
      this->schedule_fetched_ms_ = now == 0 ? 1 : now;
      this->upcoming_due_ = true;  // the list just lost an entry: refresh it
    } else {
      j.need_schedule = true;  // no list yet: a fresh boot or the end of a season
    }
  }
  j.schedule = this->schedule_;
  this->start_worker_();
}

void GamedayComponent::start_worker_() {
  this->job_done_ = false;
  this->busy_ = true;
  this->busy_since_ms_ = millis() == 0 ? 1 : millis();
  // TLS plus a nested JSON parse needs a roomy stack; 16KB has headroom.
  if (xTaskCreate(&GamedayComponent::worker_, "gameday_fetch", 16384, this, 1, nullptr) != pdPASS) {
    ESP_LOGW(TAG, "Could not start the fetch task (largest free internal block %u bytes)",
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    this->busy_ = false;
    this->schedule_next_(RETRY_INTERVAL);
  }
}

void GamedayComponent::worker_(void *arg) {
  auto *self = static_cast<GamedayComponent *>(arg);
  uint32_t mine = self->worker_seq_;
  self->run_job_();
  // A worker the loop gave up on must not hand back a result: the Job it
  // was writing into belongs to a later cycle by now.
  if (self->worker_seq_ == mine)
    self->job_done_ = true;
  vTaskDelete(nullptr);
}

// Worker task: network and parsing only, no display or entity access.
void GamedayComponent::run_job_() {
  Job &j = this->job_;
  if (j.need_recap) {
    // One day's scoreboard: its finals going back, its first kickoff going
    // forward. The live list is always empty for a day that is not today.
    std::vector<::espn::LiveGame> live;
    j.recap_ok = this->fetch_live_games_((League) j.recap_league, live, j.next_kickoff,
                                         j.recap_forward ? nullptr : &j.finals, ::espn::espn_day_epoch(j.recap_day));
    return;
  }
  if (j.need_scan) {
    uint8_t mode = j.mode;
    std::vector<::espn::Final> *finals = j.want_finals ? &j.finals : nullptr;
    j.scan_ok = true;
    if (mode == (uint8_t) Mode::LIVE_NFL || mode == (uint8_t) Mode::LIVE_ANY)
      j.scan_ok = this->fetch_live_games_(League::NFL, j.live, j.next_kickoff, finals) && j.scan_ok;
    if (mode == (uint8_t) Mode::LIVE_NCAA || mode == (uint8_t) Mode::LIVE_ANY)
      j.scan_ok = this->fetch_live_games_(League::NCAA, j.live, j.next_kickoff, finals) && j.scan_ok;
    return;  // the main loop picks a game, then the next cycle polls it
  }
  if (j.need_upcoming) {
    j.upcoming_ok = this->fetch_upcoming_(j.team, j.upcoming);
    return;
  }
  if (j.need_schedule) {
    Schedule s;
    j.schedule_ok = this->fetch_schedule_(j.team, s);
    if (!j.schedule_ok)
      return;
    j.schedule = s;
    ::espn::adopt_league(j.schedule, j.team->league);
    if (s.event_id.empty()) {
      j.no_event = true;
      return;
    }
    // Both logos download while the scoreboard read below runs, so the
    // board usually has them the moment it shows.
    League league = j.team->league;
    this->logos_.prefetch(::espn::team_logo_url(league, j.team->espn_id, j.team->abbr));
    if (s.opp_id != 0)
      this->logos_.prefetch(::espn::team_logo_url(league, s.opp_id, s.opp_abbr.c_str()));
  }
  j.game_ok = this->fetch_game_(j.team, j.schedule, j.game);
}

// Main loop: fold the worker's result into the component state and render.
void GamedayComponent::apply_job_() {
  Job &j = this->job_;
  if (j.generation != this->generation_) {
    ESP_LOGD(TAG, "Discarding a fetch for the previous team");
    return;
  }
  uint32_t now = millis();
  if (j.fav_index >= 0) {
    this->apply_fav_job_(now);
    return;
  }
  if (j.need_recap) {
    this->apply_recap_job_();
    return;
  }
  if (j.need_scan) {
    if (!j.scan_ok && j.live.empty()) {
      this->misses_++;
      if (this->recap_on_)
        this->live_started_ms_ = 0;  // retry the scan, not the long wait
      this->emit_({});
      this->schedule_next_(this->back_off_());
      return;
    }
    if (j.want_finals)
      this->recap_absorb_(j, ::espn::espn_day((int64_t) this->time_->timestamp_now()));
    this->clear_backoff_();
    // Prefer a different game than the one we just showed.
    std::vector<size_t> pick;
    for (size_t i = 0; i < j.live.size(); i++)
      if (j.live[i].event_id != this->schedule_.event_id)
        pick.push_back(i);
    if (pick.empty() && !j.live.empty())
      pick.push_back(0);
    if (pick.empty()) {
      // A failed scan took the retry above, so this kickoff covers every
      // league the mode reads.
      this->live_rescan_ms_ = ::espn::no_live_rescan_ms((int64_t) this->time_->timestamp_now(), j.next_kickoff,
                                                        NO_LIVE_RESCAN, NO_GAMES_RESCAN);
      if (this->live_rescan_ms_ > NO_LIVE_RESCAN)
        ESP_LOGI(TAG, "Next scan in %u min", (unsigned) (this->live_rescan_ms_ / MINUTE));
      const ::espn::Team *saved = this->current_team_();
      if (saved != nullptr) {
        // Nothing live, but the owner has a team: show that team's card and
        // keep scanning. The mode they picked is untouched.
        bool already = this->live_fallback_;
        this->live_none_ = true;
        this->live_fallback_ = true;
        this->live_started_ms_ = now == 0 ? 1 : now;  // start the rescan clock
        // No freshness marking here on purpose. What is on the board in the
        // fallback is the saved team's card, and only a poll of that card can
        // say how old it is. Marking a scan good here reset misses_ every
        // rescan, so a failing game endpoint never reached the three misses
        // the ticker warns at, and stale_s stayed near zero under a frozen
        // score. The card's own poll below marks it good when it lands.
        if (already) {
          // The card is already on the board: leave it there, and let the My
          // team path keep polling it rather than reloading it every scan.
          ESP_LOGI(TAG, "Still no live games: staying on %s", saved->abbr);
          this->rebuild_state_(&this->last_fields_);
          this->schedule_next_(0);
          return;
        }
        ESP_LOGI(TAG, "No live games right now: showing %s instead", saved->abbr);
        this->live_away_id_ = 0;  // the card is oriented around the saved team
        // Clearing the schedule is what forces the re-read; zeroing the
        // timestamp alone would not, because the test is on schedule_.valid.
        this->schedule_ = Schedule{};
        this->schedule_fetched_ms_ = 0;
        this->game_ = GameSnapshot{};
        this->prev_ = GameSnapshot{};
        this->post_since_ms_ = 0;
        this->emit_({});
        this->schedule_next_(0);  // fetch the card now
        return;
      }
      // No team either: the finals rotation (recap.h) has the board until a
      // game kicks off. The scan clock times the next scan from here.
      bool already = this->recap_on_;
      if (!already)
        ESP_LOGI(TAG, "No live games right now: showing recent finals");
      this->live_none_ = true;
      this->live_fallback_ = false;
      this->live_started_ms_ = now == 0 ? 1 : now;
      this->schedule_ = Schedule{};
      this->prev_ = GameSnapshot{};
      // The scan itself came back clean, so there is no run of failures to
      // report: clear the miss count. last_good_ms_ stays where it was,
      // because no game was polled - finals are fetched once, not polled,
      // so they have no freshness to report. Nothing polls a game in this
      // state, so clearing the count here cannot hide a failing game poll.
      this->misses_ = 0;
      this->recap_on_ = true;
      if (!already) {
        this->recap_step_ = 0;
        this->recap_step_ms_ = 0;
      }
      this->recap_rebuild_();
      this->mark_board_ready_();
      this->schedule_next_(0);  // the days the rotation still needs, or the wait
      return;
    }
    const ::espn::LiveGame &g = j.live[pick[random_uint32() % pick.size()]];
    ESP_LOGI(TAG, "Following %s @ %s", g.away_abbr.c_str(), g.home_abbr.c_str());
    this->live_none_ = false;
    this->live_fallback_ = false;
    this->recap_on_ = false;
    this->recap_cards_.clear();
    this->recap_step_ms_ = 0;
    this->schedule_ = Schedule{};
    this->schedule_.valid = true;
    this->schedule_.event_id = g.event_id;
    this->schedule_.group = g.group;
    this->schedule_.league = g.league;
    this->schedule_.kickoff_epoch = (int64_t) this->time_->timestamp_now();
    this->schedule_fetched_ms_ = now == 0 ? 1 : now;  // 0 reads as "never fetched"
    this->live_away_id_ = g.away_id;
    this->live_started_ms_ = now == 0 ? 1 : now;
    this->game_ = GameSnapshot{};
    this->prev_ = GameSnapshot{};
    this->schedule_next_(0);  // poll the chosen game right away
    return;
  }
  if (j.need_upcoming) {
    this->upcoming_due_ = false;
    if (j.upcoming_ok)
      this->upcoming_ = j.upcoming;
    this->rebuild_state_(&this->last_fields_);
    // The list is optional and waits for the next schedule read, but a
    // refusal still holds back the game poll that would come next.
    this->schedule_next_(j.throttled ? this->back_off_() : this->interval_for_phase_());
    return;
  }
  if (j.need_schedule) {
    if (!j.schedule_ok) {
      this->misses_++;
      this->emit_({});
      this->schedule_next_(this->back_off_());
      return;
    }
    this->clear_backoff_();
    this->schedule_ = j.schedule;
    this->schedule_fetched_ms_ = now == 0 ? 1 : now;  // 0 reads as "never fetched"
    this->force_schedule_ = false;
    this->upcoming_due_ = true;  // refreshed on the next cycle, once the board is drawn
    this->cache_unconfirmed_ = false;
    this->save_team_cache_(j.team);
    if (j.no_event) {
      ESP_LOGI(TAG, "No upcoming game for this team");
      this->game_ = GameSnapshot{};
      // Same as the no-live-games case: the schedule fetch succeeded, so the
      // failure run is over, but no GameSnapshot was applied and the board
      // has no score whose age last_good_ms_ could describe.
      this->misses_ = 0;
      this->emit_({});
      this->mark_board_ready_();
      this->schedule_next_(0);
      return;
    }
  }
  if (!j.game_ok) {
    this->misses_++;
    if (this->cache_unconfirmed_ && !j.throttled) {
      // The cached game is not on that day's scoreboard (moved, or the cache
      // is simply wrong): read the team endpoint now, not in six hours.
      ESP_LOGI(TAG, "Cached game not found: reading the team again");
      this->cache_unconfirmed_ = false;
      this->force_schedule_ = true;
      this->schedule_next_(0);
      return;
    }
    this->emit_({});
    this->schedule_next_(this->back_off_());
    return;
  }
  this->cache_unconfirmed_ = false;
  this->prev_ = this->game_;
  this->game_ = j.game;
  this->mark_good_poll_();
  if (this->upcoming_after_poll_) {
    this->upcoming_after_poll_ = false;
    this->upcoming_due_ = true;
  }
  ::espn::Splash splash =
      ::espn::decide_splash(this->prev_, this->game_, this->opponent_splashes(),
                            this->live_mode_() && !this->live_fallback_);
  if (this->game_.state == GameState::POST) {
    if (this->post_since_ms_ == 0)
      this->post_since_ms_ = now == 0 ? 1 : now;
  } else {
    this->post_since_ms_ = 0;
  }
  this->emit_(splash);
  this->mark_board_ready_();
  this->schedule_next_(this->upcoming_due_ ? 0 : this->interval_for_phase_());
}

void GamedayComponent::emit_(const ::espn::Splash &splash) {
  const GameSnapshot &g = this->game_;
  UpdateFields f;
  f.game_state = g.valid ? ::espn::state_name(g.state) : "NOT_FOUND";
  f.team_abbr = g.team_abbr;
  f.team_score = g.team_score;
  f.opponent_abbr = g.opp_abbr;
  f.opponent_score = g.opp_score;
  f.team_logo = g.team_logo;
  f.opponent_logo = g.opp_logo;
  f.possession = g.possession;
  f.team_timeouts = g.team_timeouts;
  f.opponent_timeouts = g.opp_timeouts;
  f.team_record = g.team_record;
  f.opponent_record = g.opp_record;
  f.last_play = g.last_play;
  if (g.valid && g.state == GameState::IN) {
    f.clock_text = ::espn::clock_text(g);
    f.down_distance = g.short_down_distance;
    f.is_red_zone = g.is_red_zone;
  }
  f.team_color = ::espn::parse_color(g.team_color);
  f.opponent_color = ::espn::parse_color(g.opp_color);
  f.splash_text = splash.text;
  f.splash_color = splash.color;

  TickerOptions opts;
  opts.clock = false;  // the clock has its own row on the panel; never in the ticker
  opts.down_distance = this->ticker_down_distance();
  opts.last_play = this->ticker_last_play();
  opts.odds = this->ticker_odds();

  auto kickoff_of = [this](const GameSnapshot &s) -> std::string {
    if (!s.valid || s.state != GameState::PRE || s.kickoff_epoch <= 0 || this->time_ == nullptr)
      return "";
    struct tm kick = ESPTime::from_epoch_local((time_t) s.kickoff_epoch).to_c_tm();
    struct tm now = this->time_->now().to_c_tm();
    return ::espn::kickoff_label(kick, now);
  };
  std::string kickoff = kickoff_of(g);
  f.kickoff = kickoff;
  f.status_text = ::espn::status_text(g, opts, kickoff);
  const GameSnapshot &ha = this->ha_game_();
  f.ha_status_text = ::espn::ha_status_text(ha, kickoff_of(ha));
  f.ha_last_play = ha.last_play;
  if (this->live_mode_() && !this->demo_playing_()) {
    // "college", "NFL", or nothing at all in Live any, with its own space.
    std::string league_word;
    if (this->effective_mode_() == (uint8_t) Mode::LIVE_NCAA)
      league_word = "college ";
    else if (this->effective_mode_() == (uint8_t) Mode::LIVE_NFL)
      league_word = "NFL ";
    const ::espn::Team *saved = this->current_team_();
    if (this->recap_on_) {
      // The finals rotation follows no game, so Home Assistant gets one steady
      // line instead of a change on every card.
      f.status_text = this->recap_ticker_;
      f.ha_status_text = "No live " + league_word + "games right now";
      f.ha_last_play.clear();
      f.message = this->recap_message_;
    } else if (this->live_fallback_ && saved != nullptr) {
      std::string showing = "No live " + league_word + "games, showing " + saved->abbr + " | ";
      f.status_text = showing + f.status_text;
      f.ha_status_text = showing + f.ha_status_text;
    } else if (!g.valid || g.state == GameState::NOT_FOUND) {
      f.status_text = this->live_none_ ? "No live " + league_word + "games right now"
                                       : "Looking for a live " + league_word + "game";
      f.ha_status_text = f.status_text;
      f.message = this->live_none_ ? "No live " + league_word + "games" : "Looking for a live " + league_word + "game";
    }
  }
  if (this->prefs2_.mode == (uint8_t) Mode::FAVORITES && this->fav_.empty() && !this->demo_playing_()) {
    f.status_text = "Favorites: add teams on the page | " + f.status_text;
    f.ha_status_text = "Favorites: add teams on the page | " + f.ha_status_text;
  }
  if (this->misses_ >= 3 && !this->demo_playing_()) {
    if (!this->ever_polled_good_()) {
      // Nothing has ever come back, so there is no age to quote. Saying "no
      // update for 1 min" here was wrong however long the panel had been
      // trying, on a screen someone is reading because something is broken.
      f.status_text += " | no update yet";
      f.ha_status_text += " | no update yet";
    } else {
      uint32_t mins = this->stale_seconds_() / 60;
      if (mins < 1)
        mins = 1;
      f.status_text += " | no update for " + std::to_string(mins) + " min";
      // Without the minutes, which would log a change every minute.
      f.ha_status_text += " | not updating";
    }
  }
  // Until a team, mode or favorite has been picked once, the ticker asks for
  // one. The app comes first; the address is for setting up without an
  // iPhone, over the hotspot and the device page.
  if (!this->flag_(FLAG_SETUP) && network::is_connected() && !this->demo_playing_()) {
    std::string ip;
    for (auto &a : network::get_ip_addresses()) {
      if (a.is_set() && a.is_ip4()) {
        char buf[48];
        a.str_to(buf);
        ip = buf;
        break;
      }
    }
    if (!ip.empty()) {
      std::string pick = "Pick a team in the Game Day app or at " + ip + " | ";
      f.status_text = pick + f.status_text;
      f.ha_status_text = pick + f.ha_status_text;
    }
  }

  if (!f.splash_text.empty())
    ESP_LOGI(TAG, "Splash: %s", f.splash_text.c_str());
  this->last_fields_ = f;
  this->rebuild_state_(&f);
  for (auto &cb : this->callbacks_)
    cb(f);
}

// ---- finals rotation ------------------------------------------------------------

// Main loop: the next day the rotation needs read, if any. Going back, per
// league, until it has kRecapMinFinals or has looked a week back. Going
// forward, only when no game is left today, until a day has a kickoff.
bool GamedayComponent::recap_job_(Job &j, int32_t today) {
  const uint8_t done = ::espn::kRecapMaxDays + 1;
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  for (uint8_t i = 0; i < 2; i++) {
    League league = i == 0 ? League::NFL : League::NCAA;
    if (!this->mode_reads_(league))
      continue;
    uint8_t &w = this->recap_back_[i];
    if (w == done && this->recap_back_day_[i] != today && ::espn::recap_count(this->recap_days_, (uint8_t) league) == 0)
      w = 0;  // a new day and still no finals: look again
    if (w == 0) {
      if (::espn::recap_count(this->recap_days_, (uint8_t) league) >= ::espn::kRecapMinFinals) {
        w = done;  // today's scan already had enough
        this->recap_back_day_[i] = today;
        continue;
      }
      w = 1;
    }
    if (w <= ::espn::kRecapMaxDays) {
      j.need_recap = true;
      j.recap_forward = false;
      j.recap_league = (uint8_t) league;
      j.recap_day = today - w;
      return true;
    }
  }
  if (this->recap_scan_next_ != 0)
    return false;  // a game later today: that is the next kickoff
  for (uint8_t i = 0; i < 2; i++) {
    League league = i == 0 ? League::NFL : League::NCAA;
    if (!this->mode_reads_(league))
      continue;
    uint8_t &w = this->recap_fwd_[i];
    // Found one and it has kicked off, or found none and it is a new day.
    if (w == done && this->recap_fwd_next_[i] <= tnow &&
        (this->recap_fwd_next_[i] != 0 || this->recap_fwd_day_[i] != today))
      w = 0;
    if (w == 0)
      w = 1;
    if (w <= ::espn::kRecapMaxDays) {
      j.need_recap = true;
      j.recap_forward = true;
      j.recap_league = (uint8_t) league;
      j.recap_day = today + w;
      return true;
    }
  }
  return false;
}

void GamedayComponent::apply_recap_job_() {
  Job &j = this->job_;
  const uint8_t done = ::espn::kRecapMaxDays + 1;
  uint8_t i = j.recap_league == (uint8_t) League::NFL ? 0 : 1;
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  int32_t today = ::espn::espn_day(tnow);
  if (!j.recap_ok) {
    // The same day again after the backoff. The rotation keeps going with
    // what it has.
    this->schedule_next_(this->back_off_());
    return;
  }
  this->clear_backoff_();
  if (j.recap_forward) {
    if (j.next_kickoff > tnow) {
      this->recap_fwd_next_[i] = j.next_kickoff;
      this->recap_fwd_[i] = done;
      this->recap_fwd_day_[i] = today;
    } else if (++this->recap_fwd_[i] > ::espn::kRecapMaxDays) {
      ESP_LOGI(TAG, "No %s kickoff in the next week", ::espn::recap_league_word(j.recap_league));
      this->recap_fwd_next_[i] = 0;
      this->recap_fwd_[i] = done;
      this->recap_fwd_day_[i] = today;
    }
  } else {
    ::espn::RecapDay d;
    d.league = j.recap_league;
    d.day = j.recap_day;
    d.finals = std::move(j.finals);
    ::espn::recap_merge(this->recap_days_, std::move(d));
    if (::espn::recap_count(this->recap_days_, j.recap_league) >= ::espn::kRecapMinFinals ||
        ++this->recap_back_[i] > ::espn::kRecapMaxDays) {
      this->recap_back_[i] = done;
      this->recap_back_day_[i] = today;
      ESP_LOGI(TAG, "Recent %s finals: %u", ::espn::recap_league_word(j.recap_league),
               (unsigned) ::espn::recap_count(this->recap_days_, j.recap_league));
    }
  }
  ::espn::recap_trim(this->recap_days_, today);
  this->recap_rebuild_();
  this->schedule_next_(0);
}

// A scan's finals are today's: they replace what the list had for today, so
// the rotation fills up as games end. A league with none keeps its days, so
// a scan that failed for one league never empties it.
void GamedayComponent::recap_absorb_(const Job &j, int32_t today) {
  for (uint8_t league : {(uint8_t) League::NFL, (uint8_t) League::NCAA}) {
    ::espn::RecapDay d;
    d.league = league;
    d.day = today;
    for (const auto &f : j.finals)
      if (f.league == league)
        d.finals.push_back(f);
    if (!d.finals.empty())
      ::espn::recap_merge(this->recap_days_, std::move(d));
  }
  ::espn::recap_trim(this->recap_days_, today);
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  this->recap_scan_next_ = j.next_kickoff > tnow ? j.next_kickoff : 0;
  this->recap_rebuild_();
}

int64_t GamedayComponent::recap_next_kickoff_() const {
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  int64_t best = this->recap_scan_next_ > tnow ? this->recap_scan_next_ : 0;
  for (uint8_t i = 0; i < 2; i++) {
    if (!this->mode_reads_(i == 0 ? League::NFL : League::NCAA))
      continue;
    int64_t k = this->recap_fwd_next_[i];
    if (k > tnow && (best == 0 || k < best))
      best = k;
  }
  return best;
}

// The cards for the leagues this mode reads. The card on the board stays up
// until its time is over; a rotation with nothing up yet starts now.
void GamedayComponent::recap_rebuild_() {
  this->recap_cards_ = ::espn::recap_cards(this->recap_days_, this->recap_leagues_(), this->recap_next_kickoff_() != 0);
  if (!this->recap_on_)
    return;
  if (this->recap_step_ >= this->recap_cards_.size())
    this->recap_step_ = 0;
  if (this->recap_step_ms_ == 0 || this->recap_cards_.size() < 2)
    this->recap_show_();
}

void GamedayComponent::recap_tick_() {
  if (this->recap_cards_.size() < 2 || this->recap_step_ms_ == 0)
    return;
  const ::espn::RecapCard &c = this->recap_cards_[this->recap_step_];
  if (millis() - this->recap_step_ms_ < ::espn::recap_card_ms(c.kind))
    return;
  this->recap_step_ = (this->recap_step_ + 1) % this->recap_cards_.size();
  this->recap_show_();
}

// Puts the current card on the board: a header or the next kickoff on the
// no-game screen, a final on the scoreboard. The ticker on a final names the
// day it was played.
void GamedayComponent::recap_show_() {
  this->recap_step_ms_ = millis() == 0 ? 1 : millis();
  this->prev_ = GameSnapshot{};
  this->post_since_ms_ = 0;
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  int32_t today = ::espn::espn_day(tnow);
  std::string word;
  if (this->effective_mode_() == (uint8_t) Mode::LIVE_NCAA)
    word = "college ";
  else if (this->effective_mode_() == (uint8_t) Mode::LIVE_NFL)
    word = "NFL ";
  std::string none = "No live " + word + "games right now";
  if (this->recap_cards_.empty()) {
    // Nothing in the last week and no kickoff in the next: the off season.
    this->game_ = GameSnapshot{};
    this->recap_message_ = "No live " + word + "games";
    this->recap_ticker_ = none;
    this->emit_({});
    return;
  }
  const ::espn::RecapCard &c = this->recap_cards_[this->recap_step_];
  switch (c.kind) {
    case ::espn::RecapKind::HEADER:
      this->game_ = GameSnapshot{};
      this->recap_message_ = ::espn::recap_header(this->recap_days_[c.day], today);
      this->recap_ticker_ = none;
      break;
    case ::espn::RecapKind::FINAL: {
      this->game_ = ::espn::final_snapshot(this->recap_days_[c.day].finals[c.final]);
      this->recap_message_.clear();
      std::string detail = this->game_.short_detail.empty() ? std::string("Final") : this->game_.short_detail;
      this->recap_ticker_ = ::espn::recap_header(this->recap_days_[c.day], today) + " | " + detail;
      break;
    }
    case ::espn::RecapKind::NEXT: {
      this->game_ = GameSnapshot{};
      int64_t kick = this->recap_next_kickoff_();
      if (kick == 0) {
        // It kicked off since the cards were made; the scan due now finds it.
        this->recap_message_ = "No live " + word + "games";
        this->recap_ticker_ = none;
        break;
      }
      struct tm k = ESPTime::from_epoch_local((time_t) kick).to_c_tm();
      struct tm n = this->time_->now().to_c_tm();
      std::string label = "Next: " + ::espn::kickoff_label(k, n);
      this->recap_message_ = label;
      this->recap_ticker_ = none + " | " + label;
      break;
    }
  }
  this->emit_({});
  // The logos for the card after this one start downloading now.
  const ::espn::RecapCard &nx = this->recap_cards_[(this->recap_step_ + 1) % this->recap_cards_.size()];
  if (nx.kind == ::espn::RecapKind::FINAL) {
    const ::espn::Final &f = this->recap_days_[nx.day].finals[nx.final];
    this->logos_.prefetch(::espn::team_logo_url((League) f.league, f.away_id, f.away_abbr.c_str()));
    this->logos_.prefetch(::espn::team_logo_url((League) f.league, f.home_id, f.home_abbr.c_str()));
  }
}

// ---- startup ------------------------------------------------------------------

// The first real scoreboard since boot or since the team changed is on the
// board: a polled game, or the mode deciding there is none. The boot screen
// hold (boot.yaml) ends on the board_ready action.
void GamedayComponent::mark_board_ready_() {
  if (this->board_ready_)
    return;
  this->board_ready_ = true;
  ESP_LOGI(TAG, "Board ready after %u ms (%s)", (unsigned) (millis() - this->board_pending_ms_),
           this->board_pending_ms_ == 0 ? "since boot" : "since the team change");
  this->fire_action_("board_ready");
}

// Only while it can still be right (startup.h): this team, saved in the last
// day, and a game that has not been over for hours.
bool GamedayComponent::load_team_cache_(const ::espn::Team *team) {
  ::espn::TeamCache c{};
  if (team == nullptr || !this->team_cache_pref_.load(&c))
    return false;
  Schedule s;
  if (!::espn::team_cache_load(c, team->league, team->espn_id, (int64_t) this->time_->timestamp_now(), s))
    return false;
  this->schedule_ = s;
  ESP_LOGI(TAG, "Team info from cache: event %s kickoff %lld", s.event_id.c_str(), (long long) s.kickoff_epoch);
  return true;
}

void GamedayComponent::save_team_cache_(const ::espn::Team *team) {
  ::espn::TeamCache c{};
  if (team == nullptr || !::espn::team_cache_save(this->schedule_, team->league, team->espn_id,
                                                  (int64_t) this->time_->timestamp_now(), c))
    return;
  this->team_cache_pref_.save(&c);
}

// ---- device page routes -----------------------------------------------------

static const char *const LEAGUE_KEY[] = {"nfl", "ncaa"};

static void put_team(JsonObject o, uint8_t league, uint32_t id) {
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if ((uint8_t) t.league == league && t.espn_id == id) {
      o["l"] = LEAGUE_KEY[league == (uint8_t) League::NFL ? 0 : 1];
      o["id"] = id;
      o["abbr"] = t.abbr;
      o["name"] = t.name;
      return;
    }
  }
}

// Serialises everything the page shows into state_json_. Main loop only; the
// HTTP task copies the finished string under the mutex.
void GamedayComponent::rebuild_state_(const UpdateFields *f) {
  JsonDocument doc(::espn::detail::json_allocator());
  const GameSnapshot &g = this->game_;
  doc["name"] = this->hostname();
  doc["version"] = App.get_comment();
  doc["setup"] = this->flag_(FLAG_SETUP);
  doc["panels"] = this->panels_ != nullptr ? this->panels_->cols() : 0;
  JsonObject team = doc["team"].to<JsonObject>();
  put_team(team, this->prefs_.league, this->prefs_.team_id);
  doc["mode"] = this->prefs2_.mode;
  doc["rotate"] = this->prefs2_.rotate_minutes;
  doc["lockon"] = this->prefs4_.lockon_minutes;
  doc["release"] = this->prefs4_.release_seconds;
  doc["collide"] = this->prefs4_.collide;
  JsonArray favs = doc["favs"].to<JsonArray>();
  for (uint8_t i = 0; i < 4; i++) {
    JsonObject o = favs.add<JsonObject>();
    if (this->prefs3_.fav_id[i] != 0)
      put_team(o, this->prefs3_.fav_league[i], this->prefs3_.fav_id[i]);
  }
  doc["tz"] = this->prefs_.tz_index;
  doc["tz_name"] = ::espn::kTimezones[this->prefs_.tz_index].name;
  doc["tz_iana"] = ::espn::kTimezones[this->prefs_.tz_index].iana;
  doc["tz_auto"] = this->tz_auto();
  doc["down"] = this->ticker_down_distance();
  doc["play"] = this->ticker_last_play();
  doc["odds"] = this->ticker_odds();
  doc["opp"] = this->opponent_splashes();
  doc["bootaddr"] = this->show_boot_address();
#if defined(USE_UPDATE) && !defined(GAMEDAY_BAR)
  // Left out where nothing can be installed (the demo build) or there is no
  // choice (the bar build), so the page and the app hide the switch.
  doc["autoupd"] = this->auto_update();
#endif
#ifdef GAMEDAY_BAR
  // The device page shows the bar beta note and a "Something wrong?" link.
  doc["bar"]["report"] = this->report_email_;
#endif
  doc["misses"] = this->misses_;
  doc["stale_s"] = this->stale_seconds_();
  doc["fallback"] = this->live_fallback_;
  doc["recap"] = this->recap_on_;

  JsonObject game = doc["game"].to<JsonObject>();
  game["s"] = g.valid ? ::espn::state_name(g.state) : "NOT_FOUND";
  uint8_t league = this->live_mode_() && this->schedule_.valid ? this->schedule_.league : this->prefs_.league;
  if (this->favorites_mode_() && this->fav_shown_ >= 0)
    league = (uint8_t) this->fav_[this->fav_shown_].team->league;
  if (this->recap_on_ && this->recap_step_ < this->recap_cards_.size() &&
      this->recap_cards_[this->recap_step_].kind == ::espn::RecapKind::FINAL)
    league = this->recap_days_[this->recap_cards_[this->recap_step_].day].league;
#ifdef GAMEDAY_DEMO
  if (this->demo_playing_())
    league = this->demo_league_();
#endif
  game["l"] = LEAGUE_KEY[league == (uint8_t) League::NFL ? 0 : 1];
  game["ta"] = g.team_abbr;
  game["ti"] = g.team_id;
  game["ts"] = g.team_score;
  game["oa"] = g.opp_abbr;
  game["oi"] = g.opp_id;
  game["os"] = g.opp_score;
  game["tr"] = g.team_record;
  game["or"] = g.opp_record;
  game["p"] = g.possession;
  game["tt"] = g.team_timeouts;
  game["ot"] = g.opp_timeouts;
  game["rz"] = g.is_red_zone;
  game["tv"] = g.tv;
  game["venue"] = g.venue;
  game["gn"] = g.game_name;
  game["odds"] = g.odds;
  game["ou"] = g.over_under;
  game["detail"] = g.short_detail;
  game["done"] = g.completed;
  game["kick"] = g.kickoff_epoch;
  game["lp"] = g.last_play;
  game["dd"] = g.down_distance;
  if (f != nullptr) {
    game["c"] = f->clock_text;
    game["d"] = f->down_distance;
    game["k"] = f->kickoff;
    char color[8];
    snprintf(color, sizeof(color), "%06X", (unsigned) f->team_color);
    game["tc"] = color;
    snprintf(color, sizeof(color), "%06X", (unsigned) f->opponent_color);
    game["oc"] = color;
    doc["status"] = f->status_text;
    doc["splash"] = f->splash_text;
    snprintf(color, sizeof(color), "%06X", (unsigned) f->splash_color);
    doc["splash_color"] = color;
  }
  game["id"] = g.event_id;
  JsonArray next = doc["next"].to<JsonArray>();
  if (this->favorites_mode_()) {
    // One row per favorite: its next game, or the one in progress.
    doc["shown"] = this->fav_shown_ >= 0 ? (int) this->fav_[this->fav_shown_].slot : 0;
    doc["locked"] = this->fav_locked_;
    for (const auto &e : this->fav_) {
      JsonObject o = next.add<JsonObject>();
      o["slot"] = e.slot;
      JsonObject t = o["t"].to<JsonObject>();
      put_team(t, (uint8_t) e.team->league, e.team->espn_id);
      if (!e.game.valid)
        continue;
      o["id"] = e.game.event_id;
      o["s"] = ::espn::state_name(e.game.state);
      o["kick"] = e.game.kickoff_epoch;
      o["oi"] = e.game.opp_id;
      o["oa"] = e.game.opp_abbr;
      o["ts"] = e.game.team_score;
      o["os"] = e.game.opp_score;
      o["tv"] = e.game.tv;
      o["detail"] = e.game.short_detail;
      o["done"] = e.game.completed;
    }
  }
  for (const auto &u : this->upcoming_) {
    JsonObject o = next.add<JsonObject>();
    o["id"] = u.event_id;
    o["kick"] = u.kickoff_epoch;
    o["oi"] = u.opp_id;
    o["oa"] = u.opp_abbr;
    o["on"] = u.opp_name;
    o["home"] = u.home;
    o["neutral"] = u.neutral;
    o["tv"] = u.tv;
  }
  std::string out;
  serializeJson(doc, out);
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  this->state_json_ = std::move(out);
}

std::string GamedayComponent::current_state_json_() {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->state_json_;
}

bool GamedayComponent::canHandle(AsyncWebServerRequest *request) const {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  return request->url_to(buf).starts_with("/gameday/");
}

void GamedayComponent::handleRequest(AsyncWebServerRequest *request) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(buf);
  if (url == StringRef("/gameday/state") && request->method() == HTTP_GET) {
    std::string body = this->current_state_json_();
    request->send(200, "application/json", body.c_str());
    return;
  }
  // Free heap right now, in KiB, for bench and burn-in scripts. The Free Heap
  // sensors only read every 5 minutes so Home Assistant isn't logging them.
  if (url == StringRef("/gameday/heap") && request->method() == HTTP_GET) {
    char body[128];
    snprintf(body, sizeof(body), "{\"internal\":%.1f,\"block\":%.1f,\"psram\":%.1f}",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024.0f,
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024.0f,
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024.0f);
    request->send(200, "application/json", body);
    return;
  }
  if (url == StringRef("/gameday/set") && request->method() == HTTP_POST) {
    this->handle_set_(request);
    return;
  }
  if (url == StringRef("/gameday/action") && request->method() == HTTP_POST) {
    std::string name = request->arg("do");
    if (name.empty()) {
      request->send(400, "application/json", "{\"error\":\"do is required\"}");
      return;
    }
    bool known = name == "refresh" || name == "qc";
#ifdef GAMEDAY_DEMO
    known = known || name == "demo" || name == "demo_stop";
#endif
    if (!known) {
      request->send(404, "application/json", "{\"error\":\"unknown action\"}");
      return;
    }
    this->defer([this, name]() {
      if (name == "refresh")
        this->refresh_now();
#ifdef GAMEDAY_DEMO
      else if (name == "demo")
        this->start_demo((uint8_t) ::espn::DemoKind::PRO);
      else if (name == "demo_stop")
        this->stop_demo();
#endif
      for (auto &cb : this->action_callbacks_)
        cb(name);
    });
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
#ifdef GAMEDAY_DEMO
  if (url == StringRef("/gameday/demo")) {
    this->handle_demo_(request);
    return;
  }
#endif
  request->send(404, "application/json", "{\"error\":\"unknown route\"}");
}

static const char *const SET_KEYS[] = {"team",     "mode",   "rotate",  "fav1",    "fav2", "fav3",
                                       "fav4",     "tz",     "tzauto",  "down",    "play", "odds",
                                       "opp",      "panels", "bootaddr", "lockon", "release", "collide",
                                       "autoupd"};

void GamedayComponent::handle_set_(AsyncWebServerRequest *request) {
  std::vector<std::pair<std::string, std::string>> kv;
  for (const char *key : SET_KEYS)
    if (request->hasParam(key))
      kv.emplace_back(key, request->arg(key));
  if (kv.empty()) {
    request->send(400, "application/json", "{\"error\":\"nothing to set\"}");
    return;
  }
  this->defer([this, kv]() { this->apply_set_(kv); });
  request->send(200, "application/json", "{\"ok\":true}");
}

bool parse_team_ref(const std::string &v, uint8_t &league, uint32_t &id) {
  league = 0;
  id = 0;
  size_t colon = v.find(':');
  if (colon == std::string::npos)
    return false;
  std::string lg = v.substr(0, colon);
  id = (uint32_t) strtoul(v.c_str() + colon + 1, nullptr, 10);
  if (lg == "nfl")
    league = (uint8_t) League::NFL;
  else if (lg == "ncaa")
    league = (uint8_t) League::NCAA;
  else
    return false;
  return id != 0;
}

void GamedayComponent::apply_set_(const std::vector<std::pair<std::string, std::string>> &kv) {
  bool dirty = false;
  for (const auto &p : kv) {
    const std::string &k = p.first;
    const std::string &v = p.second;
    uint8_t league;
    uint32_t id;
    if (k == "team") {
      if (parse_team_ref(v, league, id))
        this->select_team_id((League) league, id);
      else if (v == "none")
        this->clear_team_();
    } else if (k == "mode") {
      int m = atoi(v.c_str());
      if (m >= 0 && m <= (int) Mode::FAVORITES)
        this->select_mode(MODE_OPTIONS[m]);
    } else if (k == "rotate") {
      this->set_rotate_minutes(atoi(v.c_str()));
    } else if (k == "lockon") {
      this->set_lockon_minutes(atoi(v.c_str()));
    } else if (k == "release") {
      this->set_release_seconds(atoi(v.c_str()));
    } else if (k == "collide") {
      this->set_collision_alternate(v == "1");
    } else if (k.size() == 4 && k.compare(0, 3, "fav") == 0) {
      uint8_t slot = (uint8_t) (k[3] - '0');
      if (!parse_team_ref(v, league, id) && v != "none" && !v.empty()) {
        ESP_LOGW(TAG, "Bad favorite '%s'", v.c_str());
        continue;
      }
      this->set_favorite_(slot, league, id);
    } else if (k == "tz") {
      int i = ::espn::timezone_index(v.c_str());
      if (i >= 0)
        this->select_timezone(::espn::kTimezones[i].name);
      else
        ESP_LOGW(TAG, "Unknown timezone '%s'", v.c_str());
    } else if (k == "tzauto") {
      this->set_tz_auto(v == "1");
      dirty = true;
    } else if (k == "down") {
      this->set_ticker_down_distance(v == "1");
    } else if (k == "play") {
      this->set_ticker_last_play(v == "1");
    } else if (k == "odds") {
      this->set_ticker_odds(v == "1");
    } else if (k == "opp") {
      this->set_opponent_splashes(v == "1");
    } else if (k == "bootaddr") {
      this->set_show_boot_address(v == "1");
    } else if (k == "autoupd") {
      this->set_auto_update(v == "1");
    } else if (k == "panels") {
      if (this->panels_ != nullptr)
        this->panels_->set_cols((uint8_t) atoi(v.c_str()));
    }
  }
  if (dirty)
    this->rebuild_state_(&this->last_fields_);
}

}  // namespace gameday
}  // namespace esphome
