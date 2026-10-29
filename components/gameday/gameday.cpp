#include "gameday.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cstring>
#include <strings.h>
#include <vector>

#include "logo_lru.h"
#include "panel_text.h"
#include "timezones.h"
#include "timezones_parsed.h"

#ifdef GAMEDAY_TZ_PARSED
#include "esphome/components/time/posix_tz.h"
#endif
#ifdef USE_UPDATE
#include "esphome/components/update/update_entity.h"
#include <esp_ota_ops.h>
#include <esp_system.h>
#endif
#ifdef USE_GAMEDAY_SAVED_NETWORKS
#include "esphome/components/saved_networks/saved_networks.h"
#endif
#ifdef USE_GAMEDAY_CRASH_REPORT
#include "esphome/components/crash_report/crash_report.h"
#endif
#if defined(USE_API) && defined(USE_API_HOMEASSISTANT_SERVICES)
#include "esphome/components/api/api_server.h"
#define GAMEDAY_HA_EVENTS
#endif
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#include <esp_event.h>
#include <esp_wifi.h>
#endif

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#ifdef USE_MDNS
#include <mdns.h>  // the webhook looks .local names up itself
#endif
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>

namespace esphome {
namespace gameday {

static const char *const TAG = "gameday";

static_assert(::espn::kResetPowerOn == ESP_RST_POWERON && ::espn::kResetSoftware == ESP_RST_SW &&
                  ::espn::kResetPanic == ESP_RST_PANIC && ::espn::kResetWdt == ESP_RST_WDT &&
                  ::espn::kResetBrownout == ESP_RST_BROWNOUT && ::espn::kResetUsb == ESP_RST_USB &&
                  ::espn::kResetPowerGlitch == ESP_RST_PWR_GLITCH && ::espn::kResetCpuLockup == ESP_RST_CPU_LOCKUP,
              "health.h reset reasons no longer match esp_reset_reason_t");

// Survives a restart, a crash and a watchdog (health.h)
RTC_NOINIT_ATTR static ::espn::BootLog boot_log;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// A skipped night (night.h) outlives a restart inside it, the nightly update's
// among them, but not a power cut, which leaves this memory random: hence
// the mark.
struct NightSkipRtc {
  uint32_t mark;
  int64_t until;
};
static const uint32_t NIGHT_SKIP_MARK = 0x4E534B31;
RTC_NOINIT_ATTR static NightSkipRtc night_skip_rtc;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// ESPN's edge returns 403 to most unfamiliar agents, ESPHome's own among
// them, but accepts curl.
static const char *const USER_AGENT = "curl/8.0";

static const uint32_t MINUTE = 60 * 1000;
static const uint32_t SCHEDULE_INTERVAL = 6 * 60 * MINUTE;
static const uint32_t PRE_FAR_INTERVAL = 15 * MINUTE;
static const uint32_t PRE_NEAR_INTERVAL = MINUTE;
// ESPN's day scoreboard changes about every 10 s (its cache-control max-age
// counts down from 10) and its clock once a play, so the single-game read
// (max-age 1) gains little from going faster.
static const uint32_t IN_INTERVAL = 10 * 1000;
static const uint32_t POST_INTERVAL = MINUTE;
static const uint32_t POST_LINGER = 30 * MINUTE;
// For the panel's own trouble (a fetch task that never finished or could not
// start). A failed ESPN read backs off instead (back_off_).
static const uint32_t RETRY_INTERVAL = MINUTE;
// The longest one message list read keeps reading (run_msg_job_).
static const uint32_t MSG_READ_LIMIT = 30 * 1000;
// Wi-Fi and the clock are re-checked this often at boot: every second waited
// here is a second of boot screen.
static const uint32_t NOT_READY_INTERVAL = 500;
// With Wi-Fi up and the clock still unset, the time server is asked again this
// often. lwIP's own wait for a reply is 15 s, so a slow answer is never cut off.
static const uint32_t SNTP_ASK_INTERVAL = 15 * 1000;
// A time server usually answers within about a second of Wi-Fi. With none
// this long after (a network that blocks NTP, or a slow one), the clock is set
// from the Date header of an ESPN reply instead, and a failed read is tried
// again this often. Any ESPN URL will do: only the headers are read.
static const uint32_t HTTP_CLOCK_WAIT = 5 * 1000;
static const uint32_t HTTP_CLOCK_RETRY = 30 * 1000;
static const char *const HTTP_CLOCK_URL = "https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard";
static const int64_t PRE_NEAR_SECONDS = 60 * 60;
static const uint32_t NO_LIVE_RESCAN = 2 * MINUTE;
// Install updates automatically: how often loop() looks, and how long a
// manifest read gets before its answer is used (the HTTP timeout is 20 s).
static const uint32_t AUTO_UPDATE_TICK = 10 * 1000;
static const uint32_t AUTO_UPDATE_CHECK_WAIT = MINUTE;
// After an install, how long the new firmware runs before the update counts
// as taken: past safe_mode's one minute, when the bootloader can no longer
// roll it back.
static const uint32_t UPDATE_GOOD_BOOT = 2 * MINUTE;
// The Wi-Fi signal is read this often; each minute keeps the mean.
static const uint32_t WIFI_SAMPLE_INTERVAL = 10 * 1000;
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
// Days of league scoreboards read to find a team's next game when ESPN's team
// endpoint still names a finished one (leagues without a season list).
static const int LOOKAHEAD_DAYS = 3;

// Home Assistant's Mode select (select.py). The older live modes 1 to 3
// show as Live games.
static const char *const MODE_FAVORITES = "Favorite teams";
static const char *const MODE_LIVE = "Live games";
static const char *mode_option(uint8_t m) { return m == (uint8_t) Mode::FAVORITES ? MODE_FAVORITES : MODE_LIVE; }
static const uint32_t FAV_TICK = 1000;  // playlist / lock decisions re-run this often
// A live favorite that is not on the board is polled this often, for Jump to
// the action and the score banner. One game's read is 8 to 20 KB.
static const uint32_t FAV_SIDE_INTERVAL = 30 * 1000;

// Feeds ArduinoJson straight from the HTTP socket so a scoreboard document
// never has to sit in RAM as a whole. Adds what it read to `counter` when done.
class ContainerReader {
 public:
  ContainerReader(std::shared_ptr<http_request::HttpContainer> container, std::atomic<uint64_t> &counter)
      : container_(std::move(container)), counter_(counter) {}
  ~ContainerReader() { this->counter_ += this->total_; }
  ContainerReader(const ContainerReader &) = delete;
  ContainerReader &operator=(const ContainerReader &) = delete;

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
  std::atomic<uint64_t> &counter_;
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
  if (this->type_ == SelectType::MODE)
    this->parent_->select_mode(value);
  else if (this->type_ == SelectType::PLAYLIST)
    this->parent_->select_playlist(value);
  else
    this->parent_->select_timezone(value);
}

// ---- GamedayComponent ------------------------------------------------------

#ifdef USE_PSRAM
// Keep ESPN parsing and /gameday/state in PSRAM because internal RAM is the
// heap that runs out
static json::SpiRamAllocator json_psram;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
#endif

#ifdef USE_WIFI
// The reason the connection ended, set on the event task. ESPHome's own
// handler logs it but keeps nothing; wifi_tick_() reads it when the link goes
// down, and the sequence says whether one came since the link was up.
static std::atomic<int> wifi_reason{0};           // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static std::atomic<uint32_t> wifi_reason_seq{0};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static void on_wifi_disconnected(void *, esp_event_base_t, int32_t, void *data) {
  // ESPHome marks the link down when its main loop handles this same event,
  // so the one that ended a connection still finds it up. The retries after
  // it (no AP found, a failed sign-in) don't, and keep the first reason.
  auto *w = wifi::global_wifi_component;
  if (w != nullptr && !w->is_connected())
    return;
  wifi_reason.store(((wifi_event_sta_disconnected_t *) data)->reason);
  wifi_reason_seq.fetch_add(1);
}
#endif

void GamedayComponent::setup() {
#ifdef USE_PSRAM
  ::espn::set_json_allocator(&json_psram);
#endif
  // A restart keeps the wall clock, so a crash a moment ago has its time now.
  this->start_reason_ = ::espn::boot_log_start(boot_log, (int) esp_reset_reason(), (uint32_t) App.get_build_time(),
                                               (int64_t) ::time(nullptr));
  ESP_LOGI(TAG, "Started by %s, start %u since the power came on", ::espn::restart_key(this->start_reason_),
           (unsigned) boot_log.boots);
  if (boot_log.fault != 0 && boot_log.fault_boot != boot_log.boots)
    ESP_LOGW(TAG, "Last trouble: %s, %u start(s) ago", ::espn::restart_key((::espn::Restart) boot_log.fault),
             (unsigned) (boot_log.boots - boot_log.fault_boot));
  this->load_power_log_();  // on_boot has normally done it already
#ifdef USE_WIFI
  // About 2 KB: a day of minutes and the last drops. Without PSRAM (or with
  // it full) there is no history and the "wifi" object only has the total.
  auto *rssi = (int8_t *) heap_caps_calloc(::espn::kWifiMinutes, 1, MALLOC_CAP_SPIRAM);
  auto *drops = (::espn::WifiDrop *) heap_caps_calloc(::espn::kWifiDrops, sizeof(::espn::WifiDrop), MALLOC_CAP_SPIRAM);
  if (rssi != nullptr && drops != nullptr) {
    this->wifi_hist_.attach(rssi, drops);
  } else {
    heap_caps_free(rssi);
    heap_caps_free(drops);
  }
  if (esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnected, nullptr,
                                          nullptr) != ESP_OK)
    ESP_LOGW(TAG, "Can't follow Wi-Fi disconnects; drops will show no cause");
#endif
  this->logos_.setup();
  this->pictures_.setup();
  this->pref_ = global_preferences->make_preference<Prefs>(fnv1_hash("gameday_prefs_v1"));
  if (!this->pref_.load(&this->prefs_)) {
    this->prefs_ = Prefs{};
    this->prefs_.tz_index = ::espn::kDefaultTimezone;
    this->prefs_.flags = FLAGS_DEFAULT;
  }
  if (this->prefs_.tz_index >= ::espn::kTimezoneCount)
    this->prefs_.tz_index = ::espn::kDefaultTimezone;
  this->pref2_ = global_preferences->make_preference<Prefs2>(fnv1_hash("gameday_prefs2_v1"));
  // A fresh panel is in Favorites with none set, so it follows live games
  // (no_team_default) until a team is picked. My team's saved 0 loads as
  // Favorites (read_mode).
  if (!this->pref2_.load(&this->prefs2_) || ::espn::read_mode(this->prefs2_.mode) < 0 ||
      !::espn::rotate_minutes_valid(this->prefs2_.rotate_minutes)) {
    this->prefs2_.mode = (uint8_t) Mode::FAVORITES;
    this->prefs2_.rotate_minutes = ::espn::kRotateDefaultMinutes;
  }
  this->prefs2_.mode = (uint8_t) ::espn::read_mode(this->prefs2_.mode);
  this->pref3_ = global_preferences->make_preference<Prefs3>(fnv1_hash("gameday_prefs3_v1"));
  if (!this->pref3_.load(&this->prefs3_))
    this->prefs3_ = Prefs3{};
  this->pref4_ = global_preferences->make_preference<Prefs4>(fnv1_hash("gameday_prefs4_v1"));
  if (!this->pref4_.load(&this->prefs4_) || this->prefs4_.lockon_minutes < 5 || this->prefs4_.lockon_minutes > 120 ||
      this->prefs4_.release_seconds < 30 || this->prefs4_.release_seconds > 3600 || this->prefs4_.collide > 1) {
    this->prefs4_.lockon_minutes = 15;
    this->prefs4_.release_seconds = 30;
    // With several sports, two favorites playing at once is common: a new
    // panel alternates. One that was set up before keeps sticking.
    this->prefs4_.collide = this->flag_(FLAG_SETUP) ? 0 : 1;
    // Saved now: the choice depends on FLAG_SETUP, which the first team pick
    // sets, so recomputing it at the next boot would flip it.
    this->pref4_.save(&this->prefs4_);
  }
  this->pref7_ = global_preferences->make_preference<Prefs7>(fnv1_hash("gameday_prefs7_v1"));
  if (!this->pref7_.load(&this->prefs7_)) {
    this->prefs7_ = Prefs7{};
    // Only today's games in the playlist, for new panels; an existing one
    // keeps cycling every favorite until its owner turns this on.
    if (!this->flag_(FLAG_SETUP))
      this->prefs7_.flags |= FAV7_TODAY;
    this->pref7_.save(&this->prefs7_);  // same reason as Prefs4 above
  }
  this->pref8_ = global_preferences->make_preference<Prefs8>(fnv1_hash("gameday_prefs8_v1"));
  if (!this->pref8_.load(&this->prefs8_)) {
    // Firmware before this had one switch for every sport: start baseball
    // and soccer where football's is, so an update changes nothing.
    this->prefs8_ = Prefs8{};
    if (this->flag_(FLAG_DOWN))
      this->prefs8_.ticker = TICK8_COUNT | TICK8_MATCH;
    this->pref8_.save(&this->prefs8_);
  }
  this->countdown_pref_ = global_preferences->make_preference<CountdownPrefs>(fnv1_hash("gameday_countdown_v1"));
  uint32_t cd_ymd = 0;
  if (!this->countdown_pref_.load(&this->countdown_) || this->countdown_.off > 1 ||
      !::espn::parse_countdown_date(::espn::countdown_date_text(this->countdown_.ymd), cd_ymd) ||
      cd_ymd != this->countdown_.ymd)
    this->countdown_ = CountdownPrefs{};
  this->countdown_.name[::espn::kCountdownNameMax] = '\0';
  this->countdown_.cards_off &= ::espn::kCardsOffBits;
  if (this->countdown_.news > 1)
    this->countdown_.news = 0;
  this->pref9_ = global_preferences->make_preference<Prefs9>(fnv1_hash("gameday_prefs9_v1"));
  if (!this->pref9_.load(&this->prefs9_) || this->prefs9_.count > ::espn::kListMax ||
      this->prefs9_.active > this->prefs9_.count)
    this->prefs9_ = Prefs9{};  // no playlists: Favorite teams plays every favorite
  if (this->prefs9_.sport > ::espn::kSportCount || this->prefs9_.active != 0)
    this->prefs9_.sport = 0;
  this->load_msg_();
  this->load_nick_();
  this->load_hook_();
  this->load_wx_();
  this->pref6_ = global_preferences->make_preference<Prefs6>(fnv1_hash("gameday_prefs6_v1"));
  if (!this->pref6_.load(&this->prefs6_) || this->prefs6_.auto_update > 1)
    this->prefs6_ = Prefs6{};
#ifdef GAMEDAY_BETA_CHANNEL
  // Before the update entity's first check, which waits for the main loop.
  this->beta_pref_ = global_preferences->make_preference<uint8_t>(fnv1_hash("gameday_beta_v1"));
  if (!this->beta_pref_.load(&this->follow_beta_) || this->follow_beta_ > 1)
    this->follow_beta_ = 0;
  this->apply_channel_();
#endif
  this->settle_update_mark_();
#ifdef GAMEDAY_BAR
  this->bar_pref_ = global_preferences->make_preference<uint32_t>(fnv1_hash("gameday_bar_now_v1"));
  if (!this->bar_pref_.load(&this->now_tried_))
    this->now_tried_ = 0;
#endif
  this->team_cache_pref_ =
      global_preferences->make_preference<::espn::TeamCache>(fnv1_hash("gameday_team_cache_v1"));
  this->rebuild_favorites_(false);
  this->apply_timezone_();
  if (this->time_ != nullptr)
    this->time_->add_on_time_sync_callback([this]() {
      // After a power cut the clock starts here: the night schedule
      // applies now, not at the next tick.
      this->night_tick_();
      if (!this->clock_approx_)
        return;
      ESP_LOGI(TAG, "Time server answered: clock no longer approximate");
      this->clock_approx_ = false;
      this->rebuild_state_(&this->last_fields_);  // drops clock_approx
    });
  this->publish_selects_();
#ifdef GAMEDAY_DEMO
  this->demo_setup_();
#endif
  this->rebuild_state_(nullptr);
  if (this->base_ != nullptr)
    this->base_->add_handler(this);
  this->schedule_next_(3000);
  // A restart keeps the clock, and the nightly update restarts inside the
  // default hours: the boot screen goes to the night's brightness on the
  // first loop rather than at full brightness until the first tick, 10 s on.
  if (night_skip_rtc.mark == NIGHT_SKIP_MARK)
    this->night_skip_until_ = night_skip_rtc.until;
  this->defer([this]() { this->night_tick_(); });
}

std::string GamedayComponent::hostname() const { return std::string(App.get_name().c_str()); }

uint8_t GamedayComponent::fav_league_(uint8_t i) const {
  return i < 4 ? this->prefs3_.fav_league[i] : i < FAV_MAX ? this->prefs7_.fav_league[i - 4] : 0;
}

uint32_t GamedayComponent::fav_id_(uint8_t i) const {
  return i < 4 ? this->prefs3_.fav_id[i] : i < FAV_MAX ? this->prefs7_.fav_id[i - 4] : 0;
}

void GamedayComponent::put_fav_(uint8_t i, uint8_t league, uint32_t id) {
  if (i < 4) {
    this->prefs3_.fav_league[i] = league;
    this->prefs3_.fav_id[i] = id;
  } else if (i < FAV_MAX) {
    this->prefs7_.fav_league[i - 4] = league;
    this->prefs7_.fav_id[i - 4] = id;
  }
}

bool GamedayComponent::no_team_default() const {
  return ::espn::no_team_default(this->prefs2_.mode, !this->fav_.empty());
}

std::string GamedayComponent::favorite_option_(uint8_t slot) const {
  if (slot < 1 || slot > FAV_MAX || this->fav_id_(slot - 1) == 0)
    return "None";
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if ((uint8_t) t.league == this->fav_league_(slot - 1) && t.espn_id == this->fav_id_(slot - 1))
      return ::espn::team_option(t);
  }
  return "None";
}

void GamedayComponent::set_favorite_(uint8_t slot, uint8_t league, uint32_t id) {
  if (slot < 1 || slot > FAV_MAX)
    return;
  std::vector<League> live_before = this->live_leagues_();
  std::vector<::espn::TeamRef> before = this->fav_refs_all_();
  this->put_fav_(slot - 1, league, id);
  if (slot <= 4)
    this->pref3_.save(&this->prefs3_);
  else
    this->pref7_.save(&this->prefs7_);
  this->remap_lists_(before);
  bool first_pick = id != 0 && this->mark_setup_done_();
  this->sync_prefs_();
  ESP_LOGI(TAG, "Favorite %u: %s", (unsigned) slot, this->favorite_option_(slot).c_str());
  this->favorites_changed_(first_pick, live_before);
}

void GamedayComponent::set_favorites_(const std::vector<std::pair<uint8_t, uint32_t>> &list) {
  std::vector<League> live_before = this->live_leagues_();
  std::vector<::espn::TeamRef> before = this->fav_refs_all_();
  bool any = false;
  for (uint8_t i = 0; i < FAV_MAX; i++) {
    bool set = i < list.size();
    this->put_fav_(i, set ? list[i].first : 0, set ? list[i].second : 0);
    any = any || (set && list[i].second != 0);
  }
  this->pref3_.save(&this->prefs3_);
  this->pref7_.save(&this->prefs7_);
  this->remap_lists_(before);
  bool first_pick = any && this->mark_setup_done_();
  this->sync_prefs_();
  ESP_LOGI(TAG, "Favorites: %u team(s)", (unsigned) list.size());
  this->favorites_changed_(first_pick, live_before);
}

void GamedayComponent::favorites_changed_(bool first_pick, const std::vector<League> &live_before) {
  bool was_fav_mode = this->favorites_mode_();
  const ::espn::Team *fallback_before = this->fallback_team_();
  this->rebuild_favorites_(true);
  if (this->prefs2_.mode == (uint8_t) Mode::FAVORITES && (was_fav_mode || this->favorites_mode_())) {
    // The list changed under the mode: drop the board and let the next
    // cycle refresh what is missing and re-pick.
    this->generation_++;
    this->game_ = GameSnapshot{};
    this->prev_ = GameSnapshot{};
    this->no_game_confirmed_ = false;  // ESPN has not answered for the new list
    this->emit_({});
    this->end_backoff_wait_();
    this->schedule_next_(0);
    return;
  }
  // Favorites take over looks for the new list on the next cycle.
  this->live_fav_ = -1;
  this->take_scan_at_ = 1;
  // Live games on auto reads the favorites' leagues too.
  if (this->live_leagues_changed_(live_before))
    return;
  if (this->live_mode_() && this->live_fallback_ && this->fallback_team_() != fallback_before) {
    // Nothing is live and the board shows favorite #1, which is another
    // team now (or none): show the new one, or say nothing is live.
    ESP_LOGI(TAG, "Favorite #1 changed: scanning again");
    this->restart_board_();
    return;
  }
  if (first_pick)
    this->emit_({});  // redraw the ticker without the setup prompt
  else
    this->rebuild_state_(&this->last_fields_);
}

// ---- Playlists (playlists.h) -------------------------------------------------

std::vector<::espn::TeamRef> GamedayComponent::fav_refs_all_() const {
  std::vector<::espn::TeamRef> out;
  for (uint8_t i = 0; i < FAV_MAX; i++)
    out.push_back({this->fav_league_(i), this->fav_id_(i)});
  return out;
}

void GamedayComponent::remap_lists_(const std::vector<::espn::TeamRef> &before) {
  // A sport row whose sport left the favorites, or the only sport left in
  // them, has no row now: Favorite teams plays every favorite.
  bool sport_gone = this->prefs9_.sport != 0 && this->sport_playing_() == 0;
  if (sport_gone) {
    ESP_LOGI(TAG, "Playlist: every favorite (no %s row now)", ::espn::kSportKeys[this->prefs9_.sport - 1]);
    this->prefs9_.sport = 0;
  }
  // Sport rows come and go with the favorites.
  this->publish_playlist_select_();
  if (this->prefs9_.count == 0 && !sport_gone)
    return;
  std::vector<::espn::TeamRef> after = this->fav_refs_all_();
  for (uint8_t n = 0; n < this->prefs9_.count; n++)
    this->prefs9_.mask[n] = ::espn::remap_mask(this->prefs9_.mask[n], before, after);
  this->pref9_.save(&this->prefs9_);
  this->sync_prefs_();
}

uint8_t GamedayComponent::sport_rows_() const {
  uint8_t sports = 0;
  for (uint8_t i = 0; i < FAV_MAX; i++)
    if (this->fav_id_(i) != 0)
      sports |= ::espn::sport_bit(this->fav_league_(i));
  return ::espn::sport_rows(sports);
}

uint8_t GamedayComponent::sport_playing_() const {
  uint8_t row = this->prefs9_.sport;
  return row != 0 && ::espn::sport_row_has(this->sport_rows_(), row) ? row : 0;
}

std::string GamedayComponent::list_name_(uint8_t n) const {
  if (n < 1 || n > this->prefs9_.count)
    return "";
  const char *name = this->prefs9_.name[n - 1];
  return std::string(name, strnlen(name, ::espn::kListNameMax));
}

bool GamedayComponent::fav_in_list_(size_t i) const {
  uint8_t row = this->sport_playing_();
  if (row != 0)
    return i < FAV_MAX && this->fav_id_(i) != 0 && ::espn::sport_row_has(::espn::sport_bit(this->fav_league_(i)), row);
  uint8_t a = this->prefs9_.active;
  return ::espn::list_has(a != 0 ? this->prefs9_.mask[a - 1] : 0, a != 0, i);
}

void GamedayComponent::set_lists_(const std::vector<::espn::ListDef> &lists, int keep) {
  std::string active_name = this->list_name_(this->prefs9_.active);
  bool was_active = this->prefs9_.active != 0;
  uint16_t active_mask = was_active ? this->prefs9_.mask[this->prefs9_.active - 1] : 0;
  std::vector<::espn::TeamRef> favs = this->fav_refs_all_();
  Prefs9 p{};
  p.count = (uint8_t) std::min(lists.size(), (size_t) ::espn::kListMax);
  for (uint8_t n = 0; n < p.count; n++) {
    strncpy(p.name[n], lists[n].name.c_str(), ::espn::kListNameMax);
    p.mask[n] = ::espn::list_mask(lists[n].teams, favs);
  }
  // The playlist being played stays chosen by its name; with that name gone
  // it is every favorite. keep (list= in the same request, as a rename sends
  // it) names it instead.
  // A sport row playing goes on playing, unless list= names what plays.
  uint8_t sport_before = this->prefs9_.sport;
  p.sport = keep >= 0 && keep <= p.count ? 0 : sport_before;
  this->prefs9_ = p;
  uint8_t active = 0;
  if (keep >= 0 && keep <= p.count) {
    active = (uint8_t) keep;
  } else {
    for (uint8_t n = 1; n <= p.count && !active_name.empty(); n++)
      if (this->list_name_(n) == active_name)
        active = n;
  }
  this->prefs9_.active = active;
  this->pref9_.save(&this->prefs9_);
  this->sync_prefs_();
  ESP_LOGI(TAG, "Playlists: %u", (unsigned) p.count);
  this->publish_playlist_select_();
  uint16_t mask = active != 0 ? this->prefs9_.mask[active - 1] : 0;
  // A rename alone plays the same teams, so the board stays as it is.
  if (this->favorites_mode_() && ((active != 0) != was_active || mask != active_mask || p.sport != sport_before))
    this->set_active_list_(active, false);  // other teams play now: pick again
  else
    this->rebuild_state_(&this->last_fields_);
}

// From the page, Home Assistant or the remote (announce: the name goes up on
// the board, so a press in the dark says what it picked). Favorite teams
// starts playing it, from its first card.
void GamedayComponent::play_list_(uint8_t n, uint8_t row, bool announce) {
  if (n > this->prefs9_.count)
    n = 0;
  if (n != 0)
    row = 0;
  if (n != this->prefs9_.active || row != this->prefs9_.sport) {
    this->prefs9_.active = n;
    this->prefs9_.sport = row;
    this->pref9_.save(&this->prefs9_);
    this->sync_prefs_();
    ESP_LOGI(TAG, "Playlist: %s",
             row != 0 ? ::espn::kSportRowNames[row - 1] : n == 0 ? "every favorite" : this->list_name_(n).c_str());
  }
  this->publish_playlist_select_();
  if (announce)
    this->say(row != 0 ? ::espn::kSportRowNames[row - 1] : n == 0 ? "All favorites" : this->list_name_(n), 3);
  if (this->prefs2_.mode != (uint8_t) Mode::FAVORITES) {
    this->set_mode_((uint8_t) Mode::FAVORITES);
    return;
  }
  // As a change to the favorites list: drop the board and pick again from
  // the cards the panel already has.
  this->rebuild_favorites_(true);
  this->generation_++;
  this->game_ = GameSnapshot{};
  this->prev_ = GameSnapshot{};
  this->emit_({});
  this->end_backoff_wait_();
  this->schedule_next_(0);
}

void GamedayComponent::set_sport_row_(uint8_t row, bool announce) {
  if (row != 0 && !::espn::sport_row_has(this->sport_rows_(), row)) {
    ESP_LOGW(TAG, "No %s row: the favorites need it and another sport", ::espn::kSportKeys[row - 1]);
    this->publish_playlist_select_();
    return;
  }
  this->play_list_(0, row, announce);
}

void GamedayComponent::select_playlist(const std::string &option) {
  if (option == "All favorites") {
    this->set_active_list_(0, false);
    return;
  }
  uint8_t rows = this->sport_rows_();
  for (uint8_t row = 1; row <= ::espn::kSportCount; row++)
    if (::espn::sport_row_has(rows, row) && option == ::espn::kSportRowNames[row - 1]) {
      this->set_sport_row_(row, false);
      return;
    }
  for (uint8_t n = 1; n <= this->prefs9_.count; n++)
    if (this->list_name_(n) == option) {
      this->set_active_list_(n, false);
      return;
    }
  ESP_LOGW(TAG, "Unknown playlist '%s'", option.c_str());
  this->publish_playlist_select_();
}

// Home Assistant reads the options when it connects, so a playlist added or
// renamed shows there after its next reconnect.
void GamedayComponent::publish_playlist_select_() {
  if (this->playlist_select_ == nullptr)
    return;
  // All favorites, the sport rows, then the owner's playlists, as Playing on
  // the device page lists them.
  this->playlist_options_.clear();
  this->playlist_options_.push_back("All favorites");
  uint8_t rows = this->sport_rows_(), playing = this->sport_playing_();
  size_t current = 0;
  for (uint8_t row = 1; row <= ::espn::kSportCount; row++)
    if (::espn::sport_row_has(rows, row)) {
      if (row == playing)
        current = this->playlist_options_.size();
      this->playlist_options_.push_back(::espn::kSportRowNames[row - 1]);
    }
  size_t first_list = this->playlist_options_.size();
  for (uint8_t n = 1; n <= this->prefs9_.count; n++)
    this->playlist_options_.push_back(this->list_name_(n));
  if (playing == 0 && this->prefs9_.active != 0)
    current = first_list + this->prefs9_.active - 1;
  FixedVector<const char *> options;
  options.init(this->playlist_options_.size());
  for (const auto &o : this->playlist_options_)
    options.push_back(o.c_str());
  this->playlist_select_->traits.set_options(options);
  this->playlist_select_->publish_state(this->playlist_options_[current]);
}

#ifndef GAMEDAY_DEMO
// With playlists, buttons 1-4 play the first four; without, they jump to the
// first four favorites.
void GamedayComponent::remote_number(uint8_t n) {
  if (this->prefs9_.count == 0) {
    this->press_favorite(n);
    return;
  }
  if (n > this->prefs9_.count) {
    ESP_LOGI(TAG, "Remote button %u: no playlist %u", (unsigned) n, (unsigned) n);
    return;
  }
  ESP_LOGI(TAG, "Remote button %u: playlist %s", (unsigned) n, this->list_name_(n).c_str());
  this->set_active_list_(n, true);
}
#endif

void GamedayComponent::press_favorite(uint8_t slot) {
  std::string option = this->favorite_option_(slot);
  if (option == "None") {
    ESP_LOGI(TAG, "Favorite %u is empty", (unsigned) slot);
    return;
  }
  ESP_LOGI(TAG, "Remote button %u: %s", (unsigned) slot, option.c_str());
  // In Live games the button switches to Favorites, on that team.
  if (this->prefs2_.mode != (uint8_t) Mode::FAVORITES)
    this->set_mode_((uint8_t) Mode::FAVORITES);
  if (this->favorites_mode_()) {
    // Jump to that favorite and pin it: a live game holds, a card seeds the playlist.
    for (size_t i = 0; i < this->fav_.size(); i++) {
      if (this->fav_[i].slot != slot)
        continue;
      this->fav_pinned_ = (int) i;
      this->fav_shown_ = (int) i;
      this->fav_shown_since_ = (int64_t) this->time_->timestamp_now();
      this->fav_base_ = (int) i;
      this->fav_base_since_ = this->fav_shown_since_;
      this->fav_poll_ms_ = 0;
      this->prev_ = GameSnapshot{};
      this->game_ = this->fav_[i].game;
      this->post_since_ms_ = 0;
      this->emit_({});
      this->end_backoff_wait_();
      this->schedule_next_(0);
      return;
    }
  }
}

// Push the stored choices into the selects. Runs at setup and once more
// after the network is up, so the web page and the API always see them.
void GamedayComponent::publish_selects_() {
  ESP_LOGI(TAG, "Publishing selects: mode %u, timezone '%s'", (unsigned) this->prefs2_.mode,
           ::espn::kTimezones[this->prefs_.tz_index].name);
  if (this->timezone_select_ != nullptr)
    this->timezone_select_->publish_state(::espn::kTimezones[this->prefs_.tz_index].name);
  if (this->mode_select_ != nullptr)
    this->mode_select_->publish_state(mode_option(this->prefs2_.mode));
  this->publish_playlist_select_();
}

void GamedayComponent::select_mode(const std::string &option) {
  if (option == MODE_FAVORITES)
    this->set_mode_((uint8_t) Mode::FAVORITES);
  else if (option == MODE_LIVE)
    this->set_mode_((uint8_t) Mode::LIVE);
  else
    ESP_LOGW(TAG, "Unknown mode '%s'", option.c_str());
}

void GamedayComponent::set_mode_(uint8_t m) {
  if (m == this->prefs2_.mode) {
    if (this->mark_setup_done_())
      this->emit_({});  // redraw the ticker without the setup prompt
    return;
  }
  this->prefs2_.mode = m;
  this->pref2_.save(&this->prefs2_);
  ESP_LOGI(TAG, "Mode: %u", (unsigned) m);
  // The new mode's first scoreboard, not the mode switch itself, ends the
  // boot screen hold.
  this->board_ready_ = false;
  this->board_pending_ms_ = millis() == 0 ? 1 : millis();
  this->mark_setup_done_();
  this->sync_prefs_();
  if (this->mode_select_ != nullptr)
    this->mode_select_->publish_state(mode_option(m));
  this->restart_board_();
}

void GamedayComponent::set_rotate_minutes(int minutes) {
  minutes = ::espn::clamp_rotate_minutes(minutes);
  if (minutes == this->prefs2_.rotate_minutes)
    return;
  this->prefs2_.rotate_minutes = (uint8_t) minutes;
  this->pref2_.save(&this->prefs2_);
  this->sync_prefs_();
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_lockon_minutes(int minutes) {
  minutes = ::espn::clamp_lockon_minutes(minutes);
  if (minutes == this->prefs4_.lockon_minutes)
    return;
  this->prefs4_.lockon_minutes = (uint8_t) minutes;
  this->pref4_.save(&this->prefs4_);
  this->sync_prefs_();
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_release_seconds(int seconds) {
  seconds = ::espn::clamp_release_seconds(seconds);
  if (seconds == this->prefs4_.release_seconds)
    return;
  this->prefs4_.release_seconds = (uint16_t) seconds;
  this->pref4_.save(&this->prefs4_);
  this->sync_prefs_();
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_today_only(bool on) { this->set_fav7_(FAV7_TODAY, on); }

void GamedayComponent::set_favorites_take_over(bool on) {
  if (on == this->favorites_take_over())
    return;
  // Scan now rather than at the next Switch every, so a favorite live right
  // now takes the board. Off, the game on the board stays until the rotation
  // moves on, as any other game would.
  this->take_scan_at_ = 1;
  if (!on)
    this->live_fav_ = -1;
  this->set_fav7_(FAV7_TAKEOVER, on);
}

// In fav_ order (every entry has a team), so a rank is an index into fav_.
std::vector<::espn::FavRef> GamedayComponent::fav_refs_() const {
  std::vector<::espn::FavRef> out;
  for (const auto &e : this->fav_)
    out.push_back({(uint8_t) e.team->league, e.team->espn_id});
  return out;
}

void GamedayComponent::set_fav7_(uint8_t f, bool set) {
  uint8_t next = set ? (this->prefs7_.flags | f) : (this->prefs7_.flags & ~f);
  if (next == this->prefs7_.flags)
    return;
  this->prefs7_.flags = next;
  this->pref7_.save(&this->prefs7_);
  this->sync_prefs_();
  // The next pick follows the new rule; nothing on the board changes now.
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_live_leagues(const std::string &keys) { this->set_live_mask_(::espn::live_mask_from_keys(keys)); }

void GamedayComponent::set_live_mask_(uint16_t mask) {
  if (mask == this->prefs7_.live_mask)
    return;
  std::vector<League> before = this->live_leagues_();
  this->prefs7_.live_mask = mask;
  this->pref7_.save(&this->prefs7_);
  this->sync_prefs_();
  if (!this->live_leagues_changed_(before))
    this->rebuild_state_(&this->last_fields_);
}

std::vector<League> GamedayComponent::live_leagues_() const {
  if (this->no_team_default())
    return ::espn::no_team_leagues();
  std::vector<uint8_t> fav_leagues;
  for (uint8_t i = 0; i < FAV_MAX; i++)
    if (this->fav_id_(i) != 0)
      fav_leagues.push_back(this->fav_league_(i));
  return ::espn::live_leagues(this->prefs7_.live_mask, fav_leagues);
}

void GamedayComponent::set_collision_alternate(bool alternate) {
  if ((alternate ? 1 : 0) == this->prefs4_.collide)
    return;
  this->prefs4_.collide = alternate ? 1 : 0;
  this->pref4_.save(&this->prefs4_);
  this->sync_prefs_();
  this->rebuild_state_(&this->last_fields_);
}

// ---- Favorite Teams mode ------------------------------------------------------

// Rebuilds the entry list from the four slots. keep_cards carries a slot's
// fetched game over when the same team is still in the list (reordering).
void GamedayComponent::rebuild_favorites_(bool keep_cards) {
  FavList old = std::move(this->fav_);
  this->fav_.clear();
  for (uint8_t i = 0; i < FAV_MAX; i++) {
    if (this->fav_id_(i) == 0)
      continue;
    const ::espn::Team *team = nullptr;
    for (size_t t = 0; t < ::espn::kTeamCount; t++) {
      if ((uint8_t) ::espn::kTeams[t].league == this->fav_league_(i) &&
          ::espn::kTeams[t].espn_id == this->fav_id_(i)) {
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
  this->fav_base_ = -1;
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
  r.dwell_s = ::espn::kCountdownTurnS;
  r.countdown_s = ::espn::kCountdownTurnS;
  r.today_only = this->today_only();
  if (this->time_ != nullptr) {
    ESPTime t = this->time_->now();
    if (t.is_valid()) {
      r.day_start = (int64_t) t.timestamp - (t.hour * 3600 + t.minute * 60 + t.second);
      r.day_end = r.day_start + 24 * 3600;
    }
  }
  return r;
}

std::vector<::espn::FavGame> GamedayComponent::fav_games_() const {
  std::vector<::espn::FavGame> out;
  int64_t now = (int64_t) this->time_->timestamp_now();
  for (const auto &e : this->fav_) {
    ::espn::FavGame g;
    g.valid = e.game.valid;
    g.state = e.game.state;
    g.kickoff_epoch = e.game.kickoff_epoch;
    g.time_tbd = e.game.time_tbd;
    g.final_epoch = e.final_epoch;
    // Out of season, a daily sport's next game is weeks away. Football has no
    // horizon: a bye week must not take a team out of the playlist.
    g.horizon_s = ::espn::season_horizon(::espn::league_sport(e.team->league));
    g.countdown = this->countdown_on() && e.game.valid && e.game.state == GameState::PRE && !e.game.time_tbd &&
                  e.game.kickoff_epoch > 0;
    g.action = e.action_epoch != 0 && now - e.action_epoch < ::espn::kActionHoldS;
    g.in_list = this->fav_in_list_(e.slot - 1);
    out.push_back(g);
  }
  return out;
}

// Runs the pick and puts the chosen entry's card on the board when it differs
// from what is up. Returns true on a switch.
bool GamedayComponent::fav_choose_(int64_t now_epoch) {
  std::vector<::espn::FavGame> games = this->fav_games_();
  ::espn::FavChoice c = ::espn::pick_favorite(games, now_epoch, this->fav_rules_(), this->fav_base_,
                                              this->fav_base_since_, this->fav_pinned_);
  bool was_locked = this->fav_locked_;
  this->fav_locked_ = c.locked;
  if (this->fav_pinned_ >= 0 && !c.locked && c.index != this->fav_pinned_)
    this->fav_pinned_ = -1;  // the playlist moved past the pinned card
  if (c.index != this->fav_base_ || c.again) {
    this->fav_base_ = c.index;
    this->fav_base_since_ = now_epoch;
  }
  // Jump to the action, except over a game picked with the remote.
  int board = c.index;
  if (this->jump_to_action() && c.locked && !(this->fav_pinned_ >= 0 && c.index == this->fav_pinned_)) {
    int action = ::espn::action_favorite(games, c.index, this->fav_shown_);
    if (action >= 0)
      board = action;
  }
  if (board == this->fav_shown_) {
    if (was_locked != c.locked)
      this->fav_poll_ms_ = 0;
    if (c.again)
      this->fav_shown_since_ = now_epoch;  // a new turn, so its countdown comes after the card again
    return false;
  }
  this->fav_shown_ = board;
  this->fav_shown_since_ = now_epoch;
  this->fav_poll_ms_ = 0;
  this->prev_ = GameSnapshot{};
  this->game_ = board >= 0 ? this->fav_[board].game : GameSnapshot{};
  this->post_since_ms_ = 0;
  this->misses_ = 0;
  if (board >= 0)
    ESP_LOGI(TAG, "Favorites: showing %s%s", this->fav_[board].team->abbr,
             board != c.index ? " (for the action)" : c.locked ? " (locked)" : "");
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
        tnow - e.final_epoch >= rules.release_s &&
        !(e.game.event_id == e.stuck_event && now - e.stuck_ms < e.stuck_for_ms))
      e.stale = true;  // released: the team endpoint now points at the next game
  }
  // The playlist's own teams first, so its cards come up before the rest.
  int refresh = -1;
  for (int pass = 0; pass < 2 && refresh < 0; pass++) {
    for (size_t i = 0; i < this->fav_.size(); i++) {
      const FavEntry &e = this->fav_[i];
      if (this->fav_in_list_(e.slot - 1) != (pass == 0))
        continue;
      bool missing = !e.sched.valid || e.stale;
      uint32_t age = now - e.fetched_ms;
      if ((missing && (e.fetched_ms == 0 || age >= RETRY_INTERVAL)) || (!missing && age >= SCHEDULE_INTERVAL)) {
        refresh = (int) i;
        break;
      }
    }
  }
  bool forced = false;
  if (this->force_schedule_) {
    // Refresh Now in Favorites mode. Re-read the team that is on the board,
    // since that is the one the owner is looking at, and it is one fetch
    // rather than the whole list. The others keep their own six hour cycle.
    // Spend the flag here, not on success: a failed favorites fetch
    // reschedules at once, so a flag left set would retry in a tight loop.
    this->force_schedule_ = false;
    forced = true;
    if (this->fav_shown_ >= 0)
      refresh = this->fav_shown_;
    else if (!this->fav_.empty())
      refresh = 0;
  }
  this->fav_choose_(tnow);
  if (this->fav_locked_ && this->fav_shown_ >= 0 && !forced) {
    uint32_t due = this->interval_for_phase_();
    // A live game's poll goes first; a waiting refresh runs in the gap before
    // the next one, so a long list of favorites never stalls the score.
    if (this->fav_poll_ms_ == 0 || now - this->fav_poll_ms_ >= due)
      refresh = -2;  // poll the shown game
  }
  // In the gaps, the other live favorites' own polls, then (soccer) one
  // cup's team read per cycle.
  int side = refresh == -1 && !forced ? this->fav_side_due_(now, tnow) : -1;
  int cup = -1;
  for (size_t i = 0; refresh == -1 && side < 0 && cup < 0 && i < this->fav_.size(); i++) {
    const FavEntry &c = this->fav_[i];
    if (c.comp_next > 0 && c.comp_next < (int) c.comps.size())
      cup = (int) i;
  }
  if ((refresh == -1 && side < 0 && cup < 0) || this->backing_off_()) {
    this->schedule_next_(FAV_TICK);
    return;
  }
  int index = cup >= 0 ? cup : refresh >= 0 ? refresh : side >= 0 ? side : this->fav_shown_;
  FavEntry &e = this->fav_[index];
  j = Job{};
  j.generation = this->generation_;
  j.team = e.team;
  j.our_id = e.team->espn_id;
  j.fav_index = index;
  j.need_schedule = refresh >= 0;
  j.schedule = e.sched;
  size_t cups = ::espn::league_cup_count(e.team->league);
  if ((refresh >= 0 || cup >= 0) && cups > 0) {
    j.comps = e.comps;
    j.comps.resize(cups + 1);
  }
  if ((refresh >= 0 || cup >= 0) && e.game.valid && e.game.state == GameState::POST)
    j.done_event = e.game.event_id;
  if (cup >= 0) {
    j.comp = e.comp_next;
    j.need_game = e.game.event_id.empty() || e.game.event_id != e.sched.event_id;
  }
  if (refresh >= 0)
    e.fetched_ms = now == 0 ? 1 : now;
  if (side >= 0) {
    e.side_ms = now == 0 ? 1 : now;
    j.fav_side = index == side;
  }
  this->start_worker_();
}

int GamedayComponent::fav_side_due_(uint32_t now, int64_t now_epoch) const {
  if ((!this->jump_to_action() && !this->score_banner()) || !this->fav_locked_ || this->fav_shown_ < 0)
    return -1;
  int due = -1;
  uint32_t oldest = 0;
  for (size_t i = 0; i < this->fav_.size(); i++) {
    const FavEntry &e = this->fav_[i];
    if ((int) i == this->fav_shown_ || !e.game.valid || !e.sched.valid || e.stale)
      continue;
    // Under way, or due to be: ESPN can say PRE a poll or two past kickoff.
    bool live = e.game.state == GameState::IN ||
                (e.game.state == GameState::PRE && !e.game.time_tbd && e.game.kickoff_epoch <= now_epoch);
    // Two favorites playing each other: the board's own poll covers the game.
    if (!live || e.game.event_id == this->game_.event_id)
      continue;
    uint32_t age = e.side_ms == 0 ? UINT32_MAX : now - e.side_ms;
    if (age < ::espn::side_poll_wait_ms(e.side_fails, FAV_SIDE_INTERVAL) || (due >= 0 && age <= oldest))
      continue;
    due = (int) i;
    oldest = age;
  }
  return due;
}

// A team read (the league's or a cup's) that named no game: forget the one
// the card had and let another favorite take the board.
void GamedayComponent::fav_no_event_(FavEntry &e, bool shown, int64_t now_epoch) {
  ESP_LOGI(TAG, "No upcoming game for %s", e.team->abbr);
  this->no_game_confirmed_ = true;
  // The board's read answered, so its run of failures is over. Nothing is
  // polled with no game, so nothing else would clear the count, and the
  // ticker's "no update" and the hidden message card would stay.
  if (shown || this->fav_shown_ < 0)
    this->misses_ = 0;
  e.game = GameSnapshot{};
  e.final_epoch = 0;
  if (shown)
    this->game_ = GameSnapshot{};
  bool switched = this->fav_choose_(now_epoch);
  if (!switched && this->fav_shown_ < 0) {
    // No favorite has a card: once the last one is read, the off-season
    // clock's turn, and the boot screen can stop waiting for a game.
    this->emit_({});
    if (this->last_fields_.clock)
      this->mark_board_ready_();
  } else if (!switched) {
    this->rebuild_state_(&this->last_fields_);
  }
  this->schedule_next_(0);
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
  bool board = shown || this->fav_shown_ < 0;
  // A cup read that did not poll the game leaves the poll timer alone.
  if (shown && (j.comp < 0 || j.polled))
    this->fav_poll_ms_ = now == 0 ? 1 : now;
  if (j.need_schedule) {
    if (!j.schedule_ok) {
      // Another favorite's failed team read retries on its own clock
      // (RETRY_INTERVAL), not the board's.
      ::espn::FavPollEffect fx = ::espn::fav_poll_effect(false, false, j.throttled, board);
      if (fx.count_miss) {
        this->count_miss_();
        this->emit_({});  // the miss shows on the ticker and the page
      }
      if (fx.back_off)
        this->back_off_();  // start_fav_job_ waits it out and keeps the playlist ticking
      this->schedule_next_(0);
      return;
    }
    this->clear_backoff_();
    e.sched = j.schedule;
    ::espn::adopt_league(e.sched, e.team->league);
    e.stale = false;
    e.side_fails = 0;  // a fresh team read: the side polls start over
    if (!j.comps.empty()) {
      e.comps = j.comps;
      e.comp_next = 1;
    }
    if (j.no_event) {
      this->fav_no_event_(e, shown, tnow);
      return;
    }
  }
  if (j.comp > 0) {
    e.comps = j.comps;
    e.comp_next = j.comp + 1 < (int) j.comps.size() ? j.comp + 1 : -1;
    e.sched = j.schedule;
    // A failed poll backs off below with the other failed polls; another
    // favorite's failed cup read backs off only on a refusal.
    if (!j.polled && ::espn::cup_read_backs_off(j.cup_ok, false, false, j.throttled) && (board || j.throttled))
      this->back_off_();  // start_fav_job_ waits it out
    if (j.no_event) {
      this->fav_no_event_(e, shown, tnow);
      return;
    }
    if (!j.polled) {
      this->schedule_next_(0);
      return;
    }
  }
  ::espn::FavPollEffect fx = ::espn::fav_poll_effect(j.game_ok, j.fav_side, j.throttled, board);
  if (j.fav_side)
    e.side_fails = j.game_ok ? 0 : e.side_fails < 255 ? e.side_fails + 1 : 255;
  if (!j.game_ok) {
    if (fx.count_miss) {
      this->count_miss_();
      this->emit_({});
    }
    if (fx.back_off)
      this->back_off_();
    this->schedule_next_(0);
    return;
  }
  GameSnapshot old = e.game;
  e.game = j.game;
  // A refresh that came back with the same final backs off instead of
  // retrying every minute: six hours when the look-ahead found no game in the
  // coming days, 15 minutes when a read failed or the league does not look
  // ahead (soccer favorites). Football keeps its old behavior.
  if (j.need_schedule && old.valid && old.event_id == e.game.event_id && e.game.state == GameState::POST &&
      ::espn::league_sport(e.team->league) != ::espn::Sport::FOOTBALL) {
    e.stuck_event = e.game.event_id;
    e.stuck_ms = now == 0 ? 1 : now;
    e.stuck_for_ms = j.lookahead == 2 ? SCHEDULE_INTERVAL : 15 * MINUTE;
  }
  if (!old.valid || old.event_id != e.game.event_id)
    e.final_epoch = 0;
  if (e.game.state == GameState::POST && old.valid && old.event_id == e.game.event_id && old.state != GameState::POST)
    e.final_epoch = tnow;
  if (::espn::in_action(e.game, e.team->league))
    e.action_epoch = tnow;
  if (fx.mark_good)
    this->mark_good_poll_();
  else if (fx.clear_backoff)
    this->clear_backoff_();
  std::string banner = shown || !this->score_banner() ? "" : ::espn::fav_score_banner(old, e.game);
  bool switched = this->fav_choose_(tnow);
  if (switched && j.fav_index == this->fav_shown_) {
    // This read's game just took the board (Jump to the action): it was the
    // board's poll, so the board is fresh and its next poll waits the interval.
    this->mark_good_poll_();
    this->fav_poll_ms_ = now == 0 ? 1 : now;
  }
  // Not when that game just took the board, or is the one on it already.
  if (!banner.empty() && j.fav_index != this->fav_shown_ && e.game.event_id != this->game_.event_id) {
    this->fav_banner_text_ = banner;
    this->fav_banner_color_ = ::espn::parse_color(e.game.team_color);
    ESP_LOGI(TAG, "Score banner: %s", banner.c_str());
    this->fire_action_("fav_banner");  // gd_fav_banner in firmware/pages/gameday-live.yaml
  }
  if (!switched && shown) {
    // Same game on the board: render the new poll with its splash.
    this->prev_ = this->game_;
    this->game_ = e.game;
    ::espn::Splash splash = ::espn::decide_splash(this->prev_, this->game_, this->opponent_splashes(), false);
    this->emit_(splash);
  } else if (!switched && this->fav_shown_ < 0) {
    // No favorite has a card (every game past the season horizon): the
    // off-season clock's turn once the last one is read, as fav_no_event_.
    this->emit_({});
    if (this->last_fields_.clock)
      this->mark_board_ready_();
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
  // In the fallback the panel is showing favorite #1's own game, so the
  // snapshot is oriented around that team.
  uint8_t m = this->job_.mode;
  if (is_live_mode_(m) && !this->live_fallback_)
    return this->live_away_id_;
  const ::espn::Team *t = this->fallback_team_();
  return t ? t->espn_id : 0;
}

void GamedayComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Game Day Scoreboard:");
  const ::espn::Team *t = this->fallback_team_();
  ESP_LOGCONFIG(TAG, "  Mode: %u, %u favorite(s), #1 %s", (unsigned) this->prefs2_.mode, (unsigned) this->fav_.size(),
                t ? t->abbr : "none");
  ESP_LOGCONFIG(TAG, "  Timezone: %s", ::espn::kTimezones[this->prefs_.tz_index].name);
  ESP_LOGCONFIG(TAG, "  Flags: 0x%02X", this->prefs_.flags);
  ESP_LOGCONFIG(TAG, "  Weather: %s, town %s, place %s", this->weather_on() ? "on" : "off",
                this->wx_.town[0] ? this->wx_.town : "(none)", this->wx_.place[0] ? this->wx_.place : "(none)");
#ifdef GAMEDAY_BAR
  ESP_LOGCONFIG(TAG, "  Bar beta: installs from the beta channel");
#endif
#ifdef GAMEDAY_BETA_CHANNEL
  ESP_LOGCONFIG(TAG, "  Follow beta: %s", this->follow_beta() ? "on" : "off");
#endif
}

void GamedayComponent::on_shutdown() { ::espn::boot_log_planned(boot_log); }

void GamedayComponent::loop() {
  if (this->power_dip_.tick(millis()))
    this->fire_action_("guard");  // the dim start is over: pages/guard.yaml applies the owner's brightness
  if (this->logos_.loop())
    this->fire_action_("logo_ready");  // gameday-common.yaml redraws the logos
  if (this->logo_held_ && this->logo_hold_done_())
    this->draw_(this->last_fields_);
  if (millis() - this->auto_update_ms_ >= AUTO_UPDATE_TICK) {
    this->auto_update_ms_ = millis();
    this->auto_update_tick_();
    this->night_tick_();
    this->update_nudge_tick_();
    if (!this->power_settled_ && millis() >= ::espn::kPowerSettledMs) {
      // A settled run: the plug from this start's brownout comes down
      this->power_settled_ = true;
      if (this->power_dipped_) {
        this->power_dipped_ = false;
        this->fire_action_("guard");
      }
    }
    if (!this->boot_clock_noted_ && this->time_ != nullptr && this->time_->now().is_valid()) {
      // A fault at a start with no clock yet: when it happened
      this->boot_clock_noted_ = true;
      ::espn::boot_log_clock(boot_log, (int64_t) this->time_->now().timestamp - esp_timer_get_time() / 1000000);
    }
  }
  this->captive_tick_();
  this->wifi_tick_();
  // Before the demo's early return, so the scripted game takes the cards down.
  this->msg_tick_();
  this->top25_tick_();
  this->news_tick_();
  this->picture_tick_();
  // The game events' clock-driven part (the reminder before the start), and
  // the webhook's posts. Before the demo's early return too.
  if (millis() - this->ha_tick_ms_ >= 15000) {
    this->ha_tick_ms_ = millis();
    this->ha_track_();
  }
  this->hook_tick_();
#ifdef GAMEDAY_DEMO
  // While the scripted game plays, nothing new is fetched. A fetch already
  // running still finishes below, and is discarded as belonging to before.
  if (this->demo_loop_() && !this->busy_)
    return;
#endif
  // The finals rotation turns its cards on its own clock, fetch or no fetch.
  if (this->recap_on_ && !this->demo_playing_())
    this->recap_tick_();
  this->countdown_tick_();
  if (this->busy_) {
    if (!this->job_done_) {
      // A worker that died without setting job_done_ would block the loop
      // forever and Refresh Now could never clear it. Give up eventually, but
      // not before a slow-but-honest job could have finished: abandoning a
      // live worker is what makes the accepted run_job_/job_ write race
      // reachable, and that must stay out of reach on a merely slow network.
      // A Live games scan reads one scoreboard per league, so past two
      // leagues it gets the same allowance per request (60 s) as the rest.
      uint32_t deadline = WORKER_DEADLINE;
      if (this->job_.need_scan && this->job_.scan_leagues.size() > 2)
        deadline = WORKER_DEADLINE / 2 * (uint32_t) this->job_.scan_leagues.size();
      // A team read can add the look-ahead's day reads, and any other team
      // or cup read today's scoreboard (run_job_).
      const ::espn::Team *team = this->job_.team;
      if (this->job_.need_schedule && team != nullptr && this->needs_lookahead_(team->league))
        deadline = WORKER_DEADLINE / 2 * (2 + LOOKAHEAD_DAYS);
      else if ((this->job_.need_schedule || this->job_.comp > 0) && team != nullptr)
        deadline = WORKER_DEADLINE / 2 * 3;
      if (this->busy_since_ms_ != 0 && (millis() - this->busy_since_ms_) >= deadline) {
        ESP_LOGW(TAG, "Fetch task did not finish in %us, giving up on it", (unsigned) (deadline / 1000));
        this->busy_ = false;
        this->busy_since_ms_ = 0;
        this->worker_seq_++;  // a late finisher must not signal job_done_
        this->gave_up_ms_ = millis() == 0 ? 1 : millis();
        if (this->job_.need_msg)
          this->msg_fail_ms_ = this->gave_up_ms_;
        if (this->job_.need_top25)
          this->top25_fail_ms_[this->job_.top25_slot] = this->gave_up_ms_;
        if (this->job_.need_news)
          this->news_fail_ms_ = this->gave_up_ms_;
        if (this->job_.need_wx != 0) {
          this->wx_fail_ms_ = this->gave_up_ms_;
          this->wx_backoff_ms_ = this->wx_fail_.fail(false, 0, random_uint32());
          // start_wx_job_ took the lookup; it is due again after the wait,
          // like a lookup that failed (apply_wx_job_).
          if (this->job_.need_wx == 1 && this->job_.wx_town == this->wx_.town) {
            this->wx_lookup_status_ = 0;
            this->wx_lookup_due_ = true;
          }
        }
        // need_scan, like need_msg, was set before the task started, so the
        // abandoned task can't have changed it.
        if (this->job_.need_scan)
          this->live_started_ms_ =
              ::espn::live_clock_after_failed_scan(this->live_started_ms_, this->live_fallback_, this->recap_on_);
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
  // The message card's list, between ESPN reads and outside their timing,
  // but never sooner after a written-off worker than the next ESPN read is.
  if (this->msg_read_due_ && network::is_connected() &&
      (this->gave_up_ms_ == 0 || millis() - this->gave_up_ms_ >= RETRY_INTERVAL)) {
    this->start_msg_job_();
    return;
  }
  // The Top 25 card's poll, the same way.
  if (this->top25_read_due_ && network::is_connected() && this->clock_set_() &&
      (this->gave_up_ms_ == 0 || millis() - this->gave_up_ms_ >= RETRY_INTERVAL)) {
    this->start_top25_job_();
    return;
  }
  // The news card's headlines, the same way.
  if (this->news_read_due_ && network::is_connected() &&
      (this->gave_up_ms_ == 0 || millis() - this->gave_up_ms_ >= RETRY_INTERVAL)) {
    this->start_news_job_();
    return;
  }
  // The weather's lookup and forecast reads, the same way: between ESPN
  // reads, never alongside one, and not while a game the panel follows is live.
  if (this->wx_due_() && network::is_connected() &&
      (this->gave_up_ms_ == 0 || millis() - this->gave_up_ms_ >= RETRY_INTERVAL)) {
    this->start_wx_job_();
    return;
  }
  if ((int32_t) (millis() - this->next_fetch_ms_) < 0)
    return;
  if (!network::is_connected() || !this->clock_set_()) {
    this->wait_for_clock_();
    const uint32_t now = millis();
    if (network::is_connected() && !this->clock_set_() && now - this->wifi_up_ms_ >= HTTP_CLOCK_WAIT &&
        (this->http_clock_ms_ == 0 || now - this->http_clock_ms_ >= HTTP_CLOCK_RETRY)) {
      this->start_clock_job_();
      return;
    }
    this->schedule_next_(NOT_READY_INTERVAL);
    return;
  }
  if (!this->selects_published_) {
    this->selects_published_ = true;
    const uint32_t now = millis();
    ESP_LOGI(TAG, "Startup: Wi-Fi and clock ready at %u ms, clock set %u ms after Wi-Fi%s", (unsigned) now,
             (unsigned) (this->wifi_up_ms_ != 0 ? now - this->wifi_up_ms_ : 0),
             this->clock_approx_ ? " (from ESPN)" : "");
    this->publish_selects_();
  }
  this->start_job_();
}

// lwIP's SNTP doubles its wait after each failed try, up to 150 s, and every
// try fails while Wi-Fi is down. A panel that spent minutes in Bluetooth
// setup joined Wi-Fi and then waited over two minutes for the next try, with
// no clock to ask ESPN for today's games. Ask as soon as Wi-Fi is up, and
// again until the clock is set; a restart sends at once and resets the wait.
void GamedayComponent::wait_for_clock_() {
  if (this->clock_set_())
    return;
  if (!network::is_connected()) {
    this->wifi_up_ms_ = 0;
    this->sntp_asked_ms_ = 0;
    this->http_clock_ms_ = 0;
    // Back on Wi-Fi, the board says "Getting ready" again before "No internet".
    this->clock_wait_shown_ = false;
    this->clock_late_shown_ = false;
    return;
  }
  const uint32_t now = millis();
  if (this->wifi_up_ms_ == 0)
    this->wifi_up_ms_ = now == 0 ? 1 : now;
  if (!this->clock_wait_shown_) {
    // The board would say "no game" until the first fetch; say why instead.
    this->clock_wait_shown_ = true;
    this->emit_({});
  }
  if (!this->clock_late_shown_ && ::espn::clock_late(false, now - this->wifi_up_ms_)) {
    // Neither the time server nor ESPN's Date header has answered: say so
    // rather than "Getting ready" for as long as it lasts.
    this->clock_late_shown_ = true;
    ESP_LOGW(TAG, "No clock %us after Wi-Fi: no time server or ESPN answer, the board says No internet",
             (unsigned) ((now - this->wifi_up_ms_) / 1000));
    this->emit_({});
    this->fire_action_("no_internet");  // an effect gives the board back (pages/effects.yaml)
  }
  if (this->sntp_asked_ms_ != 0 && now - this->sntp_asked_ms_ < SNTP_ASK_INTERVAL)
    return;
  this->sntp_asked_ms_ = now == 0 ? 1 : now;
  if (esp_sntp_restart())
    ESP_LOGI(TAG, "Clock not set yet: asking the time server now");
}

uint32_t GamedayComponent::since_join_ms_() const {
  return this->wifi_up_ms_ != 0 && network::is_connected() ? millis() - this->wifi_up_ms_ : 0;
}

// UDP 123 is blocked on some networks (guest Wi-Fi, some offices and
// schools), and then no time server ever answers: no clock meant no game all
// day. Every ESPN reply carries the time in its Date header, and the TLS
// certificate check means it is ESPN's and not a captive portal's.
void GamedayComponent::start_clock_job_() {
  const uint32_t now = millis();
  this->http_clock_ms_ = now == 0 ? 1 : now;
  this->job_ = Job{};
  this->job_.generation = this->generation_;
  this->job_.need_clock = true;
  this->start_worker_();
}

void GamedayComponent::apply_clock_job_() {
  const Job &j = this->job_;
  this->schedule_next_(0);
  if (this->clock_set_())
    return;  // a time server answered meanwhile
  if (j.clock_epoch == 0) {
    ESP_LOGW(TAG, "Clock not set yet and no date from ESPN; trying again in %us",
             (unsigned) (HTTP_CLOCK_RETRY / 1000));
    return;
  }
  // The header is in whole seconds, so on average half a second behind when
  // it was sent. Add that and the time since it arrived.
  int64_t ms = j.clock_epoch * 1000 + 500 + (int64_t) (millis() - j.clock_got_ms);
  struct timeval tv;
  tv.tv_sec = (time_t) (ms / 1000);
  tv.tv_usec = (suseconds_t) ((ms % 1000) * 1000);
  settimeofday(&tv, nullptr);
  this->clock_approx_ = true;
  ESP_LOGI(TAG, "No time server answer %us after Wi-Fi: clock set from ESPN's Date header until one answers",
           (unsigned) ((millis() - this->wifi_up_ms_) / 1000));
  this->fire_action_("clock_approx");  // gameday-common.yaml checks for an update
}

void GamedayComponent::fire_action_(const std::string &name) {
  for (auto &cb : this->action_callbacks_)
    cb(name);
}

// The first mode or favorite picked from the page, the app or Home
// Assistant ends setup: the setup ticker goes and the boot screen hides.
// Returns true when this call is the one that ended it.
bool GamedayComponent::mark_setup_done_() {
  if (this->flag_(FLAG_SETUP))
    return false;
  this->prefs_.flags |= FLAG_SETUP;
  this->save_prefs_();
  this->sync_prefs_();
  this->fire_action_("team_picked");  // the setup screen listens for this
  return true;
}

void GamedayComponent::select_timezone(const std::string &option) {
  for (size_t i = 0; i < ::espn::kTimezoneCount; i++) {
    if (option != ::espn::kTimezones[i].name)
      continue;
    this->prefs_.tz_index = (uint8_t) i;
    this->save_prefs_();
    this->sync_prefs_();
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
  this->sync_prefs_();
  if (this->game_.valid)
    this->emit_({});
  else
    this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_best_game(bool on) {
  uint8_t next = on ? (this->prefs8_.live & ~LIVE8_RANDOM) : (this->prefs8_.live | LIVE8_RANDOM);
  if (next == this->prefs8_.live)
    return;
  this->prefs8_.live = next;
  this->pref8_.save(&this->prefs8_);
  this->sync_prefs_();
  // Nothing on the board changes: the next pick follows the new rule.
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_ticker8_(uint8_t f, bool on) {
  uint8_t next = on ? (this->prefs8_.ticker | f) : (this->prefs8_.ticker & ~f);
  if (next == this->prefs8_.ticker)
    return;
  this->prefs8_.ticker = next;
  this->pref8_.save(&this->prefs8_);
  this->sync_prefs_();
  if (this->game_.valid)
    this->emit_({});
  else
    this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::save_prefs_() { this->pref_.save(&this->prefs_); }

void GamedayComponent::save_countdown_() {
  this->countdown_pref_.save(&this->countdown_);
  this->sync_prefs_();
  this->countdown_tick_ms_ = 0;  // the card turns at the next loop, not up to a second later
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_countdown(bool on) {
  if (on == this->countdown_on())
    return;
  this->countdown_.off = on ? 0 : 1;
  this->save_countdown_();
}

bool GamedayComponent::card_on(::espn::Card c) const {
  switch (c) {
    case ::espn::Card::COUNTDOWN:
      return this->countdown_on();
    case ::espn::Card::NEWS:
      return this->news_on();
    case ::espn::Card::WEATHER:
      return this->weather_on();
    default:
      return (this->countdown_.cards_off & ::espn::card_bit(c)) == 0;
  }
}

void GamedayComponent::set_card(::espn::Card c, bool on) {
  if (c == ::espn::Card::COUNTDOWN)
    return this->set_countdown(on);
  if (c == ::espn::Card::NEWS)
    return this->set_news(on);
  if (c == ::espn::Card::WEATHER)
    return this->set_weather(on);
  if (on == this->card_on(c))
    return;
  if (on)
    this->countdown_.cards_off &= (uint16_t) ~::espn::card_bit(c);
  else
    this->countdown_.cards_off |= ::espn::card_bit(c);
  this->save_countdown_();
  for (const auto &r : ::espn::kCards)
    if (r.card == c)
      ESP_LOGI(TAG, "Between games: %s %s", r.name, on ? "on" : "off");
  // The cards that take turns see their switch at their next tick; the
  // board's own card, the next game or the clock, is drawn again now.
  if (c == ::espn::Card::NEXT || c == ::espn::Card::CLOCK)
    this->redraw_board_();
}

bool GamedayComponent::card_ok_(::espn::Card c) const {
  switch (c) {
    case ::espn::Card::CUSTOM: {
      if (this->countdown_.ymd == 0)
        return false;
      // Its day is over: nothing until the owner picks another.
      return !this->clock_set_() || (int64_t) this->time_->timestamp_now() < this->custom_start_() + ::espn::kDayS;
    }
    case ::espn::Card::MESSAGE:
      return !this->msg_url_.empty() || !this->msg_text_.empty();
    case ::espn::Card::TOP25:
      return this->top25_slots_() != 0;
    case ::espn::Card::PICTURE: {
      ::espn::PicInfo slots[::espn::kPicSlots];
      this->pictures_.slots(slots);
      return ::espn::pic_next_slot(slots, -1) >= 0;
    }
    case ::espn::Card::WEATHER:
      return this->wx_place_known_();  // the town was found
    default:
      return true;
  }
}

void GamedayComponent::redraw_board_() {
  if (this->demo_playing_() || !this->board_ready_)
    return;
  // The finals rotation keeps its card's ticker, so the card is put up again.
  if (this->recap_on_ && this->live_mode_())
    this->recap_show_();
  else
    this->emit_({});
}

void GamedayComponent::set_game_leaders(bool on) { this->set_card(::espn::Card::LEADERS, on); }

// ---- game leaders (game_leaders.h) ------------------------------------------------

bool GamedayComponent::leaders_due_() const {
  if (!this->game_leaders_on() || this->demo_playing_() || this->recap_on_ || this->game_.event_id.empty())
    return false;
  ::espn::LeadersStage st = ::espn::leaders_stage(this->game_);
  if (st == ::espn::LeadersStage::NONE)
    return false;
  if (this->leaders_.valid && this->leaders_.event_id == this->game_.event_id && this->leaders_.stage == st)
    return false;  // read already (with or without leaders in it)
  if (this->leaders_try_event_ == this->game_.event_id && this->leaders_try_stage_ == st) {
    if (this->leaders_tries_ >= ::espn::kLeadersTries)
      return false;
    if (this->leaders_tries_ > 0 && millis() - this->leaders_tried_ms_ < ::espn::kLeadersRetryMs)
      return false;
  }
  return true;
}

League GamedayComponent::board_league_() const {
  const ::espn::Team *first = this->fallback_team_();
  League league = this->live_mode_() && this->schedule_.valid ? (League) this->schedule_.league
                  : first != nullptr                         ? first->league
                                                             : League::NFL;
  if (this->favorites_mode_() && this->fav_shown_ >= 0 && this->fav_shown_ < (int) this->fav_.size())
    league = this->fav_[this->fav_shown_].team->league;
  return league;
}

bool GamedayComponent::leaders_card_due() const {
  if (!this->game_leaders_on() || !this->leaders_.valid || !this->leaders_.any() ||
      this->leaders_.event_id != this->game_.event_id)
    return false;
  ::espn::LeadersStage st = ::espn::leaders_stage(this->game_);
  if (st != this->leaders_.stage)
    return false;
  return st == ::espn::LeadersStage::FINAL || !this->leaders_half_shown_;
}

::espn::LeadersCardText GamedayComponent::leaders_card(int width_px) const {
  if (!this->leaders_card_due())
    return ::espn::LeadersCardText{};
  return ::espn::leaders_card_text(this->leaders_, width_px);
}

void GamedayComponent::leaders_turn_taken() {
  if (this->leaders_.stage == ::espn::LeadersStage::HALF)
    this->leaders_half_shown_ = true;
}

// Worker: the summary document, 400 KB to 1.2 MB, streamed through the
// filter in game_leaders.h. The log line carries the internal heap before and
// after, for the memory ledger.
bool GamedayComponent::fetch_leaders_(Job &j) {
  std::string url = ::espn::summary_url((League) j.leaders_league, j.leaders_event);
  ESP_LOGD(TAG, "Fetching game leaders: %s", url.c_str());
  const uint32_t start = millis();
  const unsigned free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
  const unsigned block_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024;
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
  ::espn::GameLeaders l;
  bool ok = ::espn::parse_summary(reader, j.leaders_sport, j.leaders_our_id, j.leaders_event, j.leaders_stage, l);
  container->end();
  ESP_LOGI(TAG, "Game leaders at the %s: %s, %u + %u lines (%u bytes, %u ms, internal heap %u/%u KB before, %u/%u after)",
           ::espn::leaders_stage_name(j.leaders_stage), ok ? "read" : "not parsed", (unsigned) l.team.count,
           (unsigned) l.opp.count, (unsigned) reader.total(), (unsigned) (millis() - start), free_before,
           block_before, (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
  if (!ok)
    return false;
  j.leaders = l;
  return true;
}

void GamedayComponent::set_top25(bool on) { this->set_card(::espn::Card::TOP25, on); }

void GamedayComponent::set_news(bool on) {
  if (on == this->news_on())
    return;
  this->countdown_.news = on ? 1 : 0;
  ESP_LOGI(TAG, "News card %s", on ? "on" : "off");
  this->news_reset_();
  this->save_countdown_();
}

void GamedayComponent::set_custom_countdown_name(const std::string &name) {
  std::string n = ::espn::countdown_name(name);
  if (n == this->countdown_.name)
    return;
  memset(this->countdown_.name, 0, sizeof(this->countdown_.name));
  memcpy(this->countdown_.name, n.data(), n.size());
  this->save_countdown_();
}

void GamedayComponent::set_custom_countdown_day(uint32_t ymd) {
  if (ymd == this->countdown_.ymd)
    return;
  this->countdown_.ymd = ymd;
  this->save_countdown_();
}

// ESPHome only writes saved preferences to flash from a 60 s timer or a
// clean shutdown, so a panel unplugged within the minute of a change came
// back without it. An owner's change is written now. sync() reads each
// pending blob back and writes only the ones that changed, and returns at
// once when nothing is pending, so the setters that run together inside
// apply_ops_ share one write at its end.
void GamedayComponent::sync_prefs_() {
  if (this->sync_gate_.request())
    global_preferences->sync();
}

void GamedayComponent::end_sync_batch_() {
  if (this->sync_gate_.end())
    global_preferences->sync();
}

void GamedayComponent::set_auto_update(bool on) {
#ifdef GAMEDAY_BAR
  (void) on;  // the bar build always installs; there is no setting
#else
  if (on == this->auto_update())
    return;
  this->prefs6_.auto_update = on ? 1 : 0;
  this->pref6_.save(&this->prefs6_);
  this->sync_prefs_();
  ESP_LOGI(TAG, "Install updates automatically: %s", on ? "on" : "off");
  this->rebuild_state_(&this->last_fields_);
#endif
}

void GamedayComponent::load_power_log_() {
  if (this->power_loaded_)
    return;
  this->power_loaded_ = true;
  this->power_pref_ = global_preferences->make_preference<::espn::PowerLog>(fnv1_hash("gameday_power_v1"));
  if (!this->power_pref_.load(&this->power_log_))
    this->power_log_ = ::espn::PowerLog{};
  const bool brownout =
      ::espn::classify_restart((int) esp_reset_reason(), false, false) == ::espn::Restart::BROWNOUT;
  if (::espn::power_log_start(this->power_log_, brownout)) {
    // Now, not at the next flash write: a weak brick can brown out again
    // within the minute.
    this->save_power_log_();
    this->power_dipped_ = true;
    this->power_dip_.pending = true;
    ESP_LOGW(TAG, "Brownout restart, %u in all: half brightness for %u s, the plug for %u min",
             (unsigned) this->power_log_.brownouts, (unsigned) (::espn::kDipMs / 1000),
             (unsigned) (::espn::kPowerSettledMs / 60000));
  }
}

void GamedayComponent::save_power_log_() {
  this->power_pref_.save(&this->power_log_);
  global_preferences->sync();
}

uint8_t GamedayComponent::guard_brightness(uint8_t want, bool night) {
  this->load_power_log_();
  if (night) {
    this->load_night_();
    want = ::espn::night_brightness(want, this->night_now_, this->night_);
    if (want == 0)
      return 0;  // screen off for the night
  }
  if (want == 0)
    return 0;
  return ::espn::dipped_brightness(want, this->power_dip_.light(millis()));
}

void GamedayComponent::load_night_() {
  if (this->night_loaded_)
    return;
  this->night_loaded_ = true;
  this->night_pref_ = global_preferences->make_preference<::espn::NightPrefs>(fnv1_hash("gameday_night_v1"));
  if (!this->night_pref_.load(&this->night_))
    this->night_ = ::espn::night_defaults();
  ::espn::night_sanitize(this->night_);
}

void GamedayComponent::set_night_(const ::espn::NightPrefs &p) {
  this->load_night_();
  if (memcmp(&p, &this->night_, sizeof(p)) == 0)
    return;
  this->night_ = p;
  this->night_pref_.save(&this->night_);
  this->sync_prefs_();
  ESP_LOGI(TAG, "Night schedule: %s, %s to %s, %s", p.on ? "on" : "off", ::espn::night_clock12(p.start).c_str(),
           ::espn::night_clock12(p.end).c_str(), p.screen_off ? "screen off" : "dimmed");
  // A skipped night runs to the new end time. Hours that leave now outside
  // the night end the skip at the tick below.
  if (this->night_skip_until_ != 0 && this->time_ != nullptr) {
    ESPTime now = this->time_->now();
    if (now.is_valid()) {
      this->night_skip_until_ =
          ::espn::night_skip_until(p, (int64_t) now.timestamp, now.hour * 60 + now.minute, now.second);
      this->save_night_skip_();
    }
  }
  this->night_tick_();
  // New hours or a new level with the state unchanged still need the
  // brightness applied again.
  this->fire_action_("guard");
  this->rebuild_state_(&this->last_fields_);
}

bool GamedayComponent::owner_team_(uint8_t league, uint32_t id) const {
  if (id == 0)
    return false;
  for (uint8_t i = 0; i < FAV_MAX; i++)
    if (this->fav_id_(i) == id && this->fav_league_(i) == league)
      return true;
  return false;
}

// A game of a favorite is in progress. Favorites only follows those, and
// Live games' fallback shows favorite #1, so a game on the board there is
// theirs. Live games follows anyone's: theirs if one side is the owner's, on
// the board or in the last scan.
bool GamedayComponent::owner_game_live_() const {
  if (!this->live_mode_() || this->live_fallback_)
    return this->followed_game_live_();
  for (const auto &side : this->scan_live_sides_)
    if (this->owner_team_(side.league, side.id))
      return true;
  const GameSnapshot &g = this->game_;
  const uint8_t league = this->schedule_.league;
  return g.valid && g.state == GameState::IN &&
         (this->owner_team_(league, g.team_id) || this->owner_team_(league, g.opp_id));
}

void GamedayComponent::night_tick_() {
  this->load_night_();
  ESPTime now = this->time_ != nullptr ? this->time_->now() : ESPTime{};
  const bool valid = now.is_valid();
  const int minute = valid ? now.hour * 60 + now.minute : 0;
  // Only asked inside the hours, where it matters
  const bool owner_live = this->night_.on && valid &&
                          ::espn::night_window(this->night_.start, this->night_.end, minute) &&
                          this->owner_game_live_();
  // A skip lasts to the end of the night it was made in; outside the hours,
  // or with the schedule off, it is over.
  if (this->night_skip_until_ != 0 && valid &&
      (!this->night_.on || !::espn::night_window(this->night_.start, this->night_.end, minute) ||
       (int64_t) now.timestamp >= this->night_skip_until_)) {
    this->night_skip_until_ = 0;
    this->save_night_skip_();
  }
  const bool skipped = this->night_skip_until_ != 0;
  const ::espn::Night next = ::espn::night_state(this->night_, valid, minute, owner_live, skipped);
  if (next == this->night_now_)
    return;
  const std::string until = ::espn::night_clock12(this->night_.end);
  switch (next) {
    case ::espn::Night::DIM:
      ESP_LOGI(TAG, "Night schedule: dimming to %u until %s", (unsigned) this->night_.level, until.c_str());
      break;
    case ::espn::Night::DARK:
      ESP_LOGI(TAG, "Night schedule: screen off until %s", until.c_str());
      break;
    case ::espn::Night::GAME:
      ESP_LOGI(TAG, "Night schedule: a game of yours is live, normal brightness until it ends");
      break;
    case ::espn::Night::SKIPPED:
      ESP_LOGI(TAG, "Night schedule: off for the rest of the night, back at %s",
               ::espn::night_clock12(this->night_.start).c_str());
      break;
    case ::espn::Night::DAY:
      ESP_LOGI(TAG, "Night schedule: over, normal brightness");
      break;
  }
  this->night_now_ = next;
  this->fire_action_("guard");  // pages/guard.yaml applies the brightness
  this->rebuild_state_(&this->last_fields_);
}

bool GamedayComponent::night_skip(const char *why) {
  if (!this->night_holding() || this->time_ == nullptr)
    return false;
  ESPTime now = this->time_->now();
  if (!now.is_valid())
    return false;  // holding needs a clock, so this can't happen
  ESP_LOGI(TAG, "Night schedule: %s skips the rest of the night", why);
  this->night_skip_until_ =
      ::espn::night_skip_until(this->night_, (int64_t) now.timestamp, now.hour * 60 + now.minute, now.second);
  this->save_night_skip_();
  this->night_tick_();
  return true;
}

void GamedayComponent::night_resume() {
  if (this->night_skip_until_ == 0)
    return;
  ESP_LOGI(TAG, "Night schedule: back on for the rest of the night");
  this->night_skip_until_ = 0;
  this->save_night_skip_();
  this->night_tick_();
}

void GamedayComponent::save_night_skip_() {
  night_skip_rtc.until = this->night_skip_until_;
  night_skip_rtc.mark = this->night_skip_until_ != 0 ? NIGHT_SKIP_MARK : 0;
}

void GamedayComponent::set_chip_temp(float c) {
  if (!this->heat_.feed(c))
    return;
  if (this->heat_.hot)
    ESP_LOGW(TAG, "Chip at %.1f C: running warm", c);
  else
    ESP_LOGI(TAG, "Chip at %.1f C: cooled down", c);
  this->fire_action_("guard");  // pages/guard.yaml shows or clears the corner mark
}

void GamedayComponent::set_follow_beta(bool on) {
#ifdef GAMEDAY_BETA_CHANNEL
  if (on == this->follow_beta())
    return;
  this->follow_beta_ = on ? 1 : 0;
  this->beta_pref_.save(&this->follow_beta_);
  this->sync_prefs_();
  ESP_LOGI(TAG, "Follow beta: %s", on ? "on" : "off");
  this->apply_channel_();
  // Read the new manifest now, so the page shows what this channel offers.
  if (this->beta_update_ != nullptr)
    this->beta_update_->update();
  this->rebuild_state_(&this->last_fields_);
#else
  (void) on;  // no beta channel in this build
#endif
}

#ifdef GAMEDAY_BETA_CHANNEL
void GamedayComponent::apply_channel_() {
  if (this->beta_update_ != nullptr)
    this->beta_update_->set_source_url(this->follow_beta() ? this->beta_source_ : this->stable_source_);
}
#endif

// At boot, what became of the last install (auto_update.h, UpdateMark), and
// from now on, note every install as it starts.
void GamedayComponent::settle_update_mark_() {
#if defined(USE_UPDATE) && defined(ESPHOME_PROJECT_VERSION)
  if (App.get_updates().empty())
    return;
  this->update_mark_pref_ = global_preferences->make_preference<::espn::UpdateMark>(fnv1_hash("gameday_update_mark_v1"));
  if (!this->update_mark_pref_.load(&this->update_mark_))
    this->update_mark_ = ::espn::UpdateMark{};
  const ::espn::UpdateMark before = this->update_mark_;
  const std::string running = ESPHOME_PROJECT_VERSION;
  // A power cut in the new firmware's first minute rolls it back too, but
  // says nothing against the release, so that one is tried again.
  const bool rolled_back = esp_ota_get_last_invalid_partition() != nullptr && esp_reset_reason() != ESP_RST_POWERON;
  switch (::espn::update_mark_boot(this->update_mark_, running, rolled_back)) {
    case ::espn::UpdateOutcome::NONE:
      break;
    case ::espn::UpdateOutcome::PENDING:
      // Cleared only once this boot is past the point of rolling back; a
      // crash before then and the old firmware sees the mark instead.
      this->set_timeout("update_mark", UPDATE_GOOD_BOOT, [this]() {
        if (strncmp(this->update_mark_.installing, ESPHOME_PROJECT_VERSION, ::espn::kUpdateVersionLen) != 0)
          return;
        ESP_LOGI(TAG, "Update to %s took", ESPHOME_PROJECT_VERSION);
        this->update_mark_.installing[0] = '\0';
        this->update_mark_pref_.save(&this->update_mark_);
      });
      break;
    case ::espn::UpdateOutcome::ROLLED_BACK:
      ESP_LOGW(TAG, "Update to %s rolled back to %s; automatic installs will skip it", before.installing,
               running.c_str());
      break;
    case ::espn::UpdateOutcome::NOT_INSTALLED:
      ESP_LOGI(TAG, "Update to %s didn't finish; still on %s", before.installing, running.c_str());
      break;
  }
  if (memcmp(&before, &this->update_mark_, sizeof(before)) != 0)
    this->update_mark_pref_.save(&this->update_mark_);
  if (this->update_mark_.failed[0] != '\0')
    ESP_LOGW(TAG, "Update %s rolled back on this panel before", this->update_mark_.failed);
  App.get_updates()[0]->add_on_state_callback([this]() { this->update_state_changed_(); });
#endif
}

void GamedayComponent::update_state_changed_() {
#if defined(USE_UPDATE) && defined(ESPHOME_PROJECT_VERSION)
  update::UpdateEntity *u = App.get_updates()[0];
  if (u->state == update::UPDATE_STATE_INSTALLING) {
    if (this->update_installing_)
      return;  // a progress update
    // Written to flash before the download starts, since the next thing
    // this firmware may do is restart into the new one.
    this->update_installing_ = true;
    ::espn::update_mark_set(this->update_mark_.installing, u->update_info.latest_version);
    this->update_mark_pref_.save(&this->update_mark_);
    global_preferences->sync();
  } else if (this->update_installing_) {
    // Back from installing with no restart: it failed before the new image
    // was in place, and this firmware runs on. Not a rollback.
    ESP_LOGI(TAG, "Update to %s didn't install; still on %s", this->update_mark_.installing, ESPHOME_PROJECT_VERSION);
    this->update_installing_ = false;
    this->update_mark_.installing[0] = '\0';
    this->update_mark_pref_.save(&this->update_mark_);
  }
#endif
}

// A game the panel follows is in progress: the one on the board, which
// covers the live modes and their fallback, or any favorite's in Favorite
// teams.
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
  in.failed = ::espn::update_failed_here(this->update_mark_, u->update_info.latest_version);
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
    case ::espn::AutoUpdateStep::SKIP_FAILED:
      if (this->auto_update_skipped_ != in.today)
        ESP_LOGI(TAG, "Auto update: %s rolled back on this panel, not installing it on its own",
                 u->update_info.latest_version.c_str());
      this->auto_update_skipped_ = in.today;
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
      // failed one is left to the nightly rule, which tries once in the
      // next 3 to 5 AM window (the same night, if it failed inside one).
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

// "Update ready" on the ticker (update_nudge.h). The board is redrawn when
// the line comes or goes, or the release waiting turns important or stops
// being, since a quiet board can go hours between redraws. Not in the bar
// build, which always installs on its own.
void GamedayComponent::update_nudge_tick_() {
#if defined(USE_UPDATE) && !defined(GAMEDAY_BAR)
  if (App.get_updates().empty() || this->time_ == nullptr)
    return;
  update::UpdateEntity *u = App.get_updates()[0];
  ESPTime now = this->time_->now();
  bool out = u->state == update::UPDATE_STATE_AVAILABLE &&
             ::espn::version_newer(u->update_info.latest_version, u->update_info.current_version);
  ::espn::UpdateNudgeInputs in;
  in.waiting = out && !::espn::update_failed_here(this->update_mark_, u->update_info.latest_version);
  in.auto_on = this->auto_update();
  in.clock_valid = now.is_valid();
  in.hour = now.hour;
  in.today = ::espn::auto_update_day(now.year, now.day_of_year);
  in.now = (int64_t) now.timestamp;
  in.live = this->followed_game_live_();
  in.dark = this->night_now_ == ::espn::Night::DARK;
  bool on = ::espn::update_nudge_on(in, this->nudge_day_) && !this->demo_playing_();
  bool important = out && ::espn::update_important(u->update_info.summary);
  if (on == this->nudge_on_ && important == this->update_important_)
    return;
  if (on != this->nudge_on_)
    ESP_LOGI(TAG, "Update ready on the ticker: %s", on ? u->update_info.latest_version.c_str() : "off");
  this->nudge_on_ = on;
  this->update_important_ = important;
  if (this->board_ready_)
    this->emit_({});
  else
    this->rebuild_state_(&this->last_fields_);
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
  this->lingered_event_.clear();
  this->linger_backoff_ms_ = 0;
  this->live_away_id_ = 0;
  this->live_away_rank_ = 99;
  this->live_home_rank_ = 99;
  this->live_started_ms_ = 0;
  this->live_none_ = false;
  this->live_rescan_ms_ = NO_LIVE_RESCAN;
  this->live_fallback_ = false;
  this->live_fav_ = -1;
  this->take_scan_at_ = 0;
  // The finals and the walks stay: they are per league, not per mode.
  this->recap_on_ = false;
  this->recap_cards_.clear();
  this->recap_step_ = 0;
  this->recap_step_ms_ = 0;
  this->recap_scan_next_ = 0;
  this->schedule_ = Schedule{};
  this->schedule_fetched_ms_ = 0;
  this->comps_.clear();
  this->comp_next_ = -1;
  this->done_event_.clear();
  this->force_schedule_ = false;
  this->upcoming_.clear();
  this->upcoming_due_ = false;
  this->upcoming_after_poll_ = false;
  this->cache_unconfirmed_ = false;
  this->game_ = GameSnapshot{};
  this->prev_ = GameSnapshot{};
  this->post_since_ms_ = 0;
  this->misses_ = 0;
  this->no_game_confirmed_ = false;
}

void GamedayComponent::restart_board_() {
  this->reset_game_();
  this->scan_live_sides_.clear();  // until the next scan
  this->generation_++;  // a scan in flight read what the board followed before
  this->emit_({});      // clear the board while the next fetch runs
  this->fire_action_("board_pending");  // the boot screen holds (gameday-common.yaml)
  this->end_backoff_wait_();
  this->schedule_next_(0);
  // Somebody changed what the panel follows, so they are looking at it.
  this->fire_action_("user_pick");
}

// Without this the finals and the "No live ... games" wait from the old
// leagues stayed up until the next scheduled scan, up to three hours.
bool GamedayComponent::live_leagues_changed_(const std::vector<League> &before) {
  if (!::espn::live_scan_changed(this->effective_mode_(), before, this->live_leagues_()))
    return false;
  ESP_LOGI(TAG, "Live games leagues changed: scanning again");
  this->board_ready_ = false;
  this->board_pending_ms_ = millis() == 0 ? 1 : millis();
  this->restart_board_();
  return true;
}

std::shared_ptr<http_request::HttpContainer> GamedayComponent::open_(const std::string &url) {
  std::vector<http_request::Header> headers = {
      {"User-Agent", USER_AGENT},
      {"Accept", "application/json"},
  };
  this->espn_requests_++;
  auto container = this->http_->get(url, headers, {"retry-after"});
  this->espn_status_ = container == nullptr ? 0 : container->status_code;
  this->espn_answer_ms_ = millis() == 0 ? 1 : millis();
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
  this->espn_ok_reads_++;
  return container;
}

// ---- Sign-in Wi-Fi (captive.h) ---------------------------------------------

struct ProbeHeaders {
  std::string location;
  std::string content_type;
};

static esp_err_t probe_event(esp_http_client_event_t *evt) {
  if (evt->event_id != HTTP_EVENT_ON_HEADER || evt->user_data == nullptr || evt->header_key == nullptr ||
      evt->header_value == nullptr)
    return ESP_OK;
  auto *h = static_cast<ProbeHeaders *>(evt->user_data);
  // Only the host matters, and a portal's link can carry a long query.
  if (strcasecmp(evt->header_key, "Location") == 0)
    h->location.assign(evt->header_value, strnlen(evt->header_value, 256));
  else if (strcasecmp(evt->header_key, "Content-Type") == 0)
    h->content_type.assign(evt->header_value, strnlen(evt->header_value, 64));
  return ESP_OK;
}

// Probe task. Its own esp_http_client rather than http_request, which follows
// redirects: a portal's redirect is the answer, and following it to an HTTPS
// sign-in page with its own certificate would only fail.
void GamedayComponent::probe_task_(void *arg) {
  auto *self = static_cast<GamedayComponent *>(arg);
  ProbeHeaders h;
  esp_http_client_config_t cfg = {};
  cfg.url = self->probe_url_.load();
  cfg.timeout_ms = 10000;
  cfg.disable_auto_redirect = true;
  cfg.event_handler = &probe_event;
  cfg.user_data = &h;
  cfg.user_agent = USER_AGENT;
  int status = 0;
  std::string body;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client != nullptr) {
    esp_http_client_set_header(client, "Accept", "application/json");
    if (esp_http_client_open(client, 0) == ESP_OK && esp_http_client_fetch_headers(client) >= 0) {
      status = esp_http_client_get_status_code(client);
      char buf[256];
      int n = esp_http_client_read(client, buf, sizeof(buf));
      if (n > 0)
        body.assign(buf, n);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
  }
  ::espn::Probe r = ::espn::classify_probe(status, h.location, h.content_type, body);
  const bool redirect = status >= 300 && status < 400 && !h.location.empty();
  ESP_LOGI(TAG, "Wi-Fi check: %s (HTTP %d%s%s)", ::espn::probe_name(r), status, redirect ? ", to " : "",
           redirect ? ::espn::detail::url_host(h.location).c_str() : "");
  self->probe_result_ = (uint8_t) r;
  self->probe_done_ = true;
  vTaskDelete(nullptr);
}

void GamedayComponent::start_probe_() {
  this->probe_join_done_ = true;
  this->probe_ms_ = millis();
  this->probe_ok_mark_ = this->espn_ok_reads_.load();
  this->probe_done_ = false;
  this->probe_running_ = true;
  // Plain HTTP and a 256 byte read, no TLS: a small stack is plenty.
  if (xTaskCreate(&GamedayComponent::probe_task_, "gameday_probe", 6144, this, 1, nullptr) != pdPASS) {
    ESP_LOGW(TAG, "Could not start the Wi-Fi check (largest free internal block %u bytes)",
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    this->probe_running_ = false;
  }
}

void GamedayComponent::set_captive_(bool on) {
  if (on == this->captive_)
    return;
  this->captive_ = on;
  if (on)
    ESP_LOGW(TAG, "This Wi-Fi needs a sign-in: a sign-in page answered in ESPN's place");
  else
    ESP_LOGI(TAG, "Wi-Fi sign-in state cleared");
  this->emit_({});
  this->fire_action_(on ? "captive" : "captive_clear");  // boot.yaml shows or hides the screen
}

// Once per join, then again while on a sign-in network or while ESPN reads
// fail (captive.h, probe_due). Any ESPN answer over HTTPS ends the state, so
// a network that only looks like a portal to the probe never covers a board
// that is working.
void GamedayComponent::captive_tick_() {
  const uint32_t now = millis();
  if (this->probe_running_) {
    if (!this->probe_done_.load())
      return;
    this->probe_running_ = false;
    const auto r = (::espn::Probe) this->probe_result_.load();
    if (r == ::espn::Probe::OPEN || this->espn_ok_reads_.load() != this->probe_ok_mark_) {
      this->captive_suspect_ = false;
      this->set_captive_(false);
    } else if (r == ::espn::Probe::CAPTIVE && network::is_connected() && !this->demo_playing_()) {
      if (this->clock_set_() && this->fail_run_.streak == 0 && !this->captive_) {
        // Nothing says ESPN is out of reach yet (a DNS block page can look
        // like a portal to the probe): ask ESPN now and say so only if that
        // read fails too. With no clock nothing can be read to check.
        ESP_LOGI(TAG, "Wi-Fi check saw a sign-in page: reading ESPN now to check");
        this->captive_suspect_ = true;
        this->suspect_fails_mark_ = this->read_fails_;
        this->end_backoff_wait_();
        if (!this->busy_)
          this->schedule_next_(0);
      } else {
        this->set_captive_(true);
      }
    }
  }
  if (!network::is_connected() || this->demo_playing_()) {
    this->probe_join_ms_ = 0;
    this->probe_join_done_ = false;
    this->captive_suspect_ = false;
    this->set_captive_(false);
    return;
  }
  if (this->probe_join_ms_ == 0)
    this->probe_join_ms_ = now == 0 ? 1 : now;
  if ((this->captive_ || this->captive_suspect_) && this->espn_ok_reads_.load() != this->probe_ok_mark_) {
    ESP_LOGI(TAG, "ESPN answered: this Wi-Fi %s", this->captive_ ? "no longer needs a sign-in" : "is open");
    this->captive_suspect_ = false;
    this->set_captive_(false);
  } else if (this->captive_suspect_ && this->read_fails_ != this->suspect_fails_mark_) {
    this->captive_suspect_ = false;
    this->set_captive_(true);
  }
  // Nothing is due inside the shortest recheck wait.
  if (this->probe_join_done_ && now - this->probe_ms_ < ::espn::kProbeCaptiveRecheckMs)
    return;
  // A sign-in network usually blocks the time server too, and with no clock
  // nothing is read from ESPN, so a clock still unset 30 s after joining
  // counts as reads failing.
  const bool failing =
      this->fail_run_.streak > 0 || (!this->clock_set_() && now - this->probe_join_ms_ >= 30000);
  if (::espn::probe_due(this->probe_join_done_, this->captive_, failing, now - this->probe_ms_))
    this->start_probe_();
}

uint32_t GamedayComponent::back_off_() {
  this->read_fails_++;
  uint32_t ms = this->fail_run_.fail(this->job_.throttled, this->job_.retry_after_ms, random_uint32());
  ESP_LOGW(TAG, "%u failed read(s) in a row%s: next try in %us", (unsigned) this->fail_run_.streak,
           this->fail_run_.throttled ? ", ESPN is refusing" : "", (unsigned) (ms / 1000));
  this->backoff_since_ms_ = millis();
  this->backoff_ms_ = ms;
  return ms;
}

bool GamedayComponent::fetch_schedule_(const ::espn::Team *team, Schedule &out, const char *comp_slug) {
  std::string url = comp_slug != nullptr ? ::espn::team_url(team->league, team->espn_id, comp_slug)
                                         : ::espn::team_url(team->league, team->espn_id);
  ESP_LOGD(TAG, "Fetching schedule: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
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

bool GamedayComponent::needs_lookahead_(League league) const {
  const ::espn::LeagueInfo *info = ::espn::league_info(league);
  return info != nullptr && info->sport != ::espn::Sport::FOOTBALL && !info->season_list;
}

int GamedayComponent::fetch_team_day_(const ::espn::Team *team, uint32_t group, int64_t day_epoch,
                                      const std::string &skip_event, Schedule &out, const char *comp_slug) {
  std::string url = ::espn::team_day_url(team->league, group, day_epoch, comp_slug);
  ESP_LOGD(TAG, "Looking ahead: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return -1;
  ContainerReader reader(container, this->espn_bytes_);
  int found = ::espn::parse_team_game(reader, team->espn_id, skip_event, out);
  container->end();
  return found;
}

// ESPN's team endpoint names a team's next game as soon as the current one
// starts (MIL and LAD on 2026-10-12; soccer the same, CRU and BRA in Brazil's
// Serie A that night, and a cup's team endpoint is the same endpoint; college
// football too, FLA @ TEX on 2026-10-17). So a
// read during a game (a boot, a team pick, the six-hour read, a cup read)
// checks today's scoreboard for one on now. Its date is taken 9 hours back
// (team_day_url's 5 plus 4 here), so a late West Coast game still finds its
// own day after midnight Eastern. The NFL's endpoint keeps the game
// (team_names_game_on_now), so it skips the read.
void GamedayComponent::take_game_on_now_(const ::espn::Team *team, Schedule &s, const std::string &done_event,
                                         const char *comp_slug) {
  if (::espn::team_names_game_on_now(team->league))
    return;
  int64_t now = (int64_t) this->time_->timestamp_now();
  if (!s.event_id.empty() && s.kickoff_epoch <= now)
    return;
  Schedule today;
  if (this->fetch_team_day_(team, s.group, now - 4 * 3600, done_event, today, comp_slug) > 0 &&
      ::espn::today_game_first(s, today)) {
    ESP_LOGI(TAG, "%s is playing now: event %s", team->abbr, today.event_id.c_str());
    s.event_id = today.event_id;
    s.kickoff_epoch = today.kickoff_epoch;
    s.time_tbd = today.time_tbd;
    // The team endpoint's opponent belongs to the next game.
    s.opp_id = 0;
    s.opp_abbr.clear();
  }
}

bool GamedayComponent::fetch_upcoming_(const ::espn::Team *team, std::vector<::espn::Upcoming> &out) {
  const ::espn::LeagueInfo *info = ::espn::league_info(team->league);
  if (info != nullptr && !info->season_list) {
    out.clear();  // the season schedule is megabytes in this league: no Up next list
    return true;
  }
  std::string url = ::espn::schedule_url(team->league, team->espn_id);
  ESP_LOGD(TAG, "Fetching upcoming games: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
  // Four: the game on the board plus the three after it.
  bool ok = ::espn::parse_upcoming(reader, team->espn_id, 4, out);
  container->end();
  ESP_LOGI(TAG, "Upcoming: %u game(s) (%u bytes)", (unsigned) out.size(), (unsigned) reader.total());
  return ok;
}

bool GamedayComponent::fetch_live_games_(League league, ::espn::LiveList &out, int64_t &next_kickoff,
                                         ::espn::FinalList *finals, ::espn::PendingList *pending,
                                         int64_t epoch, bool top25, const std::vector<uint32_t> *watch,
                                         int64_t *watch_kickoff) {
  std::string url = ::espn::scan_url(league, epoch != 0 ? epoch : (int64_t) this->time_->timestamp_now(), top25);
  ESP_LOGD(TAG, "Scanning for live games: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr)
    return false;
  ContainerReader reader(container, this->espn_bytes_);
  size_t before = out.size();
  size_t finals_before = finals != nullptr ? finals->size() : 0;
  size_t pending_before = pending != nullptr ? pending->size() : 0;
  int64_t next = 0, watched = 0;
  bool ok = ::espn::parse_live_games(reader, out, &next, finals, pending, watch, &watched);
  for (size_t i = before; i < out.size(); i++)
    out[i].league = (uint8_t) league;
  if (finals != nullptr)
    for (size_t i = finals_before; i < finals->size(); i++)
      (*finals)[i].league = (uint8_t) league;
  if (pending != nullptr)
    for (size_t i = pending_before; i < pending->size(); i++)
      (*pending)[i].league = (uint8_t) league;
  container->end();
  ESP_LOGI(TAG, "Scan: %u live game(s), %u final(s), %u to come (%u bytes)", (unsigned) out.size(),
           (unsigned) (finals != nullptr ? finals->size() - finals_before : 0),
           (unsigned) (pending != nullptr ? pending->size() - pending_before : 0), (unsigned) reader.total());
  if (next != 0 && (next_kickoff == 0 || next < next_kickoff))
    next_kickoff = next;
  if (watch_kickoff != nullptr && watched != 0 && (*watch_kickoff == 0 || watched < *watch_kickoff))
    *watch_kickoff = watched;
  return ok;
}

bool GamedayComponent::fetch_game_(const ::espn::Team *team, const Schedule &schedule, GameSnapshot &out) {
  bool live = is_live_mode_(this->job_.mode);
  League league = live || this->job_.fav_index >= 0 ? (League) schedule.league : team->league;
  // The one game, 8 to 20 KB a poll, where the day's scoreboard it sits in
  // is up to 750 KB for college football.
  const ::espn::LeagueInfo *info = ::espn::league_info(league);
  if (info == nullptr)
    return false;
  std::string url = ::espn::event_url(league, schedule.event_id, schedule.comp_slug);
  ESP_LOGD(TAG, "Fetching game: %s", url.c_str());
  auto container = this->open_(url);
  if (container == nullptr) {
    // Worker task: the job is this task's until it reports done.
    this->job_.game_gone = this->espn_status_ == 404;
    return false;
  }
  ContainerReader reader(container, this->espn_bytes_);
  GameSnapshot g;
  bool ok = ::espn::parse_event(reader, info->sport, this->our_id_(), g, schedule.event_id);
  container->end();
  if (!ok) {
    ESP_LOGW(TAG, "Game %s not parsed (%u bytes)", schedule.event_id.c_str(),
             (unsigned) reader.total());
    this->job_.game_gone = true;
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
      // A TBD game's kickoff is a midnight placeholder: nothing to close in on.
      int64_t now = (int64_t) this->time_->timestamp_now();
      int64_t until = this->game_.kickoff_epoch - now;
      ms = until <= PRE_NEAR_SECONDS && !this->game_.time_tbd ? PRE_NEAR_INTERVAL : PRE_FAR_INTERVAL;
      // Past the season horizon the board is the off-season clock, which a
      // poll can't change: the schedule's own six hours catch a new date.
      if (::espn::off_season(GameState::PRE, this->game_.kickoff_epoch, now,
                             ::espn::season_horizon(this->game_.sport)))
        ms = SCHEDULE_INTERVAL;
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

uint32_t GamedayComponent::linger_ms_() const {
  const ::espn::Team *t = this->fallback_team_();
  League league = t != nullptr ? t->league : League::NFL;
  if (this->live_mode_() && this->schedule_.valid)
    league = (League) this->schedule_.league;
  const ::espn::LeagueInfo *info = ::espn::league_info(league);
  // The team endpoint named this final again after the last linger and the
  // look-ahead found nothing or failed: back off (run_job_, apply_job_).
  if (this->linger_backoff_ms_ != 0 && !this->lingered_event_.empty() &&
      this->game_.event_id == this->lingered_event_ && this->needs_lookahead_(league))
    return this->linger_backoff_ms_;
  return (uint32_t) (info != nullptr ? info->linger_min : 30) * MINUTE;
}

// Main loop: decide what this cycle needs and hand it to the worker task.
void GamedayComponent::start_job_() {
  uint32_t now = millis();
  Job &j = this->job_;
  j = Job{};
  j.generation = this->generation_;
  j.team = this->fallback_team_();
  j.mode = this->effective_mode_();
  j.scan_leagues = this->scan_leagues_();
  j.top25 = this->no_team_default();
  // This game's leaders at halftime and the final (game_leaders.h): one
  // summary read for the board's game, ahead of whatever the mode reads next.
  if (this->leaders_due_()) {
    j.need_leaders = true;
    j.leaders_league = (uint8_t) this->board_league_();
    j.leaders_sport = this->game_.sport;
    j.leaders_event = this->game_.event_id;
    j.leaders_our_id = this->game_.team_id;
    j.leaders_stage = ::espn::leaders_stage(this->game_);
    if (j.leaders_event != this->leaders_try_event_ || j.leaders_stage != this->leaders_try_stage_) {
      this->leaders_try_event_ = j.leaders_event;
      this->leaders_try_stage_ = j.leaders_stage;
      this->leaders_tries_ = 0;
    }
    this->leaders_tries_++;
    this->leaders_tried_ms_ = now == 0 ? 1 : now;
    this->start_worker_();
    return;
  }
  if (this->favorites_mode_()) {
    this->start_fav_job_(now);
    return;
  }
  // Favorites take over: a scan when a favorite's game is due to start.
  bool take_due = false;
  if (this->take_over_()) {
    j.fav_watch = this->fav_refs_();
    take_due = this->take_scan_at_ != 0 && (int64_t) this->time_->timestamp_now() >= this->take_scan_at_;
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
    if (this->recap_job_(j, (int64_t) this->time_->timestamp_now())) {
      this->start_worker_();
      return;
    }
    if (!::espn::live_rescan_due(now, this->live_started_ms_, this->live_rescan_ms_, take_due)) {
      this->schedule_next_(this->live_rescan_ms_ - (now - this->live_started_ms_));
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
    bool rescan_due = ::espn::live_rescan_due(now, this->live_started_ms_, this->live_rescan_ms_, take_due);
    if (!this->live_fallback_ || rescan_due) {
      if (this->live_fallback_) {
        j.need_scan = true;
        // Stamped here, so a scan that finds nothing live again starts the
        // next wait. A failed scan clears it (apply_job_) and retries on the
        // back-off.
        this->live_started_ms_ = now == 0 ? 1 : now;
      } else {
        // Picking the best game: only at a break in this one, never in the
        // middle of a drive, and one look per break.
        // A favorite's game that took the board holds it with Stick: only a
        // slow look, for a higher favorite with no kickoff time to scan at.
        // A favorite higher in the list starting later comes in on its
        // kickoff scan (take_due). Alternate keeps Favorite teams' plain
        // timer, so it doesn't wait for a break (rotate_due, best_game.h).
        bool on_fav = this->take_over_() && this->live_fav_ >= 0;
        bool held = on_fav && this->prefs4_.collide == 0;
        bool brk = ::espn::pick_break(this->game_);
        if (!brk)
          this->break_looked_ = false;  // play is on again: the next break gets its look
        bool rotate = this->live_started_ms_ != 0 &&
                      ::espn::rotate_due(now - this->live_started_ms_,
                                         (uint32_t) this->prefs2_.rotate_minutes * MINUTE, held, on_fav,
                                         this->best_game(), brk, this->break_looked_);
        bool ended = this->game_.valid && this->game_.state != GameState::IN;
        j.need_scan = !this->schedule_.valid || rotate || ended || this->live_none_ || take_due;
        j.rotate = rotate;
      }
      // With no team, nothing live means the finals rotation: the scan keeps
      // today's finals for it.
      j.want_finals = j.team == nullptr;
      j.schedule = this->schedule_;
      this->start_worker_();
      return;
    }
    // In the fallback with the rescan not due: fall through to the one-team
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
  // Soccer: after each league read, one cup's team read per cycle.
  if (this->comp_next_ > 0 && this->comp_next_ < (int) this->comps_.size() && !j.need_schedule) {
    j.comp = this->comp_next_;
    j.comps = this->comps_;
    j.schedule = this->schedule_;
    j.need_game = this->game_.event_id.empty() || this->game_.event_id != this->schedule_.event_id;
    j.done_event = this->done_event_;
    this->start_worker_();
    return;
  }
  // The season schedule is ~200KB; it is fetched on a cycle of its own right
  // after the board has its game, never in front of it.
  if (this->upcoming_due_ && !j.need_schedule && this->schedule_.valid) {
    j.need_upcoming = true;
    j.schedule = this->schedule_;
    this->start_worker_();
    return;
  }
  if (this->post_since_ms_ != 0 && (now - this->post_since_ms_) >= this->linger_ms_()) {
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
    this->done_event_ = this->game_.event_id;
    this->lingered_event_ = this->game_.event_id;
    this->game_ = GameSnapshot{};
    this->post_since_ms_ = 0;
    if (next != nullptr) {
      ESP_LOGI(TAG, "Game is over: taking event %s from the season schedule", next->event_id.c_str());
      // group, colors and the record are team properties from the team
      // endpoint and stay valid; only the event moves.
      this->schedule_.event_id = next->event_id;
      this->schedule_.kickoff_epoch = next->kickoff_epoch;
      this->schedule_.time_tbd = next->time_tbd;
      this->schedule_.comp_slug = nullptr;  // the list is the league's own
      if (!this->comps_.empty()) {
        // A cup game can come before the league's next fixture.
        this->comps_[0] = this->schedule_;
        int pick = ::espn::pick_schedule(this->comps_, tnow, this->done_event_);
        if (pick > 0) {
          this->schedule_ = this->comps_[pick];
          ESP_LOGI(TAG, "A cup game is first: event %s in %s", this->schedule_.event_id.c_str(),
                   this->schedule_.comp_slug);
        }
      }
      this->schedule_fetched_ms_ = now == 0 ? 1 : now;
      this->upcoming_due_ = true;  // the list just lost an entry: refresh it
    } else {
      j.need_schedule = true;  // no list yet: a fresh boot or the end of a season
    }
  }
  j.schedule = this->schedule_;
  size_t cups = ::espn::league_cup_count(j.team->league);
  if (j.need_schedule && cups > 0) {
    j.comps = this->comps_;
    j.comps.resize(cups + 1);
  }
  if (j.need_schedule) {
    j.done_event = this->done_event_;
    // A routine six-hour read must not cut a lingering final short.
    j.no_lookahead = this->post_since_ms_ != 0;
  }
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
  if (j.need_msg) {
    this->run_msg_job_();
    return;
  }
  if (j.need_top25) {
    this->run_top25_job_();
    return;
  }
  if (j.need_news) {
    this->run_news_job_();
    return;
  }
  if (j.need_wx != 0) {
    this->run_wx_job_();
    return;
  }
  if (j.need_clock) {
    // Any status will do, a 404 or a 503 carries the date just the same.
    std::vector<http_request::Header> headers = {{"User-Agent", USER_AGENT}};
    this->espn_requests_++;
    auto container = this->http_->get(HTTP_CLOCK_URL, headers, {"date"});
    if (container == nullptr) {
      ESP_LOGW(TAG, "Request failed: %s", HTTP_CLOCK_URL);
      return;
    }
    j.clock_got_ms = millis();
    if (container->status_code > 0)
      j.clock_epoch = ::espn::parse_http_date(container->get_response_header("date"));
    container->end();
    return;
  }
  if (j.need_leaders) {
    j.leaders_ok = this->fetch_leaders_(j);
    return;
  }
  if (j.need_recap) {
    // One day's scoreboard: its finals going back, its first kickoff and that
    // game's matchup going forward. The live list is always empty for a day
    // that is not today.
    ::espn::LiveList live;
    j.recap_ok = this->fetch_live_games_((League) j.recap_league, live, j.next_kickoff,
                                         j.recap_forward ? nullptr : &j.finals,
                                         j.recap_forward ? &j.pending : nullptr, ::espn::espn_day_epoch(j.recap_day),
                                         j.top25);
    return;
  }
  if (j.need_scan) {
    ::espn::FinalList *finals = j.want_finals ? &j.finals : nullptr;
    ::espn::PendingList *pending = j.want_finals ? &j.pending : nullptr;
    j.scan_ok = true;
    for (League l : j.scan_leagues) {
      // Favorites take over: this league's favorites, for their next kickoff.
      std::vector<uint32_t> watch;
      for (const auto &f : j.fav_watch)
        if (f.league == (uint8_t) l)
          watch.push_back(f.id);
      j.scan_ok = this->fetch_live_games_(l, j.live, j.next_kickoff, finals, pending, 0, j.top25,
                                          watch.empty() ? nullptr : &watch, &j.fav_kickoff) &&
                  j.scan_ok;
    }
    return;  // the main loop picks a game, then the next cycle polls it
  }
  if (j.need_upcoming) {
    j.upcoming_ok = this->fetch_upcoming_(j.team, j.upcoming);
    return;
  }
  if (j.comp > 0) {
    this->run_cup_job_();
    return;
  }
  if (j.need_schedule) {
    Schedule s;
    j.schedule_ok = this->fetch_schedule_(j.team, s);
    if (!j.schedule_ok)
      return;
    j.schedule = s;
    ::espn::adopt_league(j.schedule, j.team->league);
    // ESPN can also still call a game live that the panel saw finish.
    bool known_over = !j.done_event.empty() && j.schedule.event_id == j.done_event;
    if ((j.schedule.next_final || known_over) && !j.no_lookahead && this->needs_lookahead_(j.team->league)) {
      // ESPN's team endpoint keeps naming a finished game until the next one
      // is near (the previous night's finals were still named at 11 AM
      // Eastern on 2026-10-08). Without a season list, read the next days'
      // scoreboards for this team's next game. A failed read ends the search
      // so a later day's game is never taken over an unread earlier one.
      int64_t now = (int64_t) this->time_->timestamp_now();
      j.lookahead = 2;
      for (int d = 0; d < LOOKAHEAD_DAYS; d++) {
        Schedule next;
        int found = this->fetch_team_day_(j.team, j.schedule.group, now + (int64_t) d * 86400, j.done_event, next);
        if (found < 0) {
          j.lookahead = -1;
          break;
        }
        if (found > 0) {
          ESP_LOGI(TAG, "Next game for %s: event %s", j.team->abbr, next.event_id.c_str());
          j.schedule.event_id = next.event_id;
          j.schedule.kickoff_epoch = next.kickoff_epoch;
          j.schedule.time_tbd = next.time_tbd;
          j.schedule.next_final = false;
          j.schedule.opp_id = next.opp_id;
          j.schedule.opp_abbr = next.opp_abbr;
          j.lookahead = 1;
          break;
        }
      }
    } else {
      this->take_game_on_now_(j.team, j.schedule, j.done_event);
    }
    if (!j.comps.empty()) {
      // Soccer: the cup reads from the last sweep still count.
      j.comps[0] = j.schedule;
      int pick = ::espn::pick_schedule(j.comps, (int64_t) this->time_->timestamp_now(), j.done_event);
      if (pick > 0)
        j.schedule = j.comps[pick];
    }
    if (j.schedule.event_id.empty()) {
      j.no_event = true;
      return;
    }
    // Both logos download while the scoreboard read below runs, so the
    // board usually has them the moment it shows.
    League league = j.team->league;
    this->logos_.prefetch(::espn::team_logo_url(league, j.team->espn_id, j.team->abbr));
    if (j.schedule.opp_id != 0)
      this->logos_.prefetch(::espn::team_logo_url(league, j.schedule.opp_id, j.schedule.opp_abbr.c_str()));
  }
  j.game_ok = this->fetch_game_(j.team, j.schedule, j.game);
}

// Main loop: fold the worker's result into the component state and render.
void GamedayComponent::apply_job_() {
  Job &j = this->job_;
  if (j.need_msg) {
    this->apply_msg_job_();  // whatever the board follows, the card is the same
    return;
  }
  if (j.need_top25) {
    this->apply_top25_job_();  // a poll is the same whatever the board follows
    return;
  }
  if (j.need_news) {
    this->apply_news_job_();  // checks its source against the panel's own
    return;
  }
  if (j.need_wx != 0) {
    this->apply_wx_job_();  // a forecast is the same whatever the board follows
    return;
  }
  if (j.need_clock) {
    this->apply_clock_job_();  // whatever the board follows now, it needs the clock
    return;
  }
  if (j.generation != this->generation_) {
    ESP_LOGD(TAG, "Discarding a fetch for the previous team");
    return;
  }
  uint32_t now = millis();
  if (j.need_leaders) {
    if (j.leaders_ok) {
      this->leaders_ = j.leaders;
      this->leaders_half_shown_ = false;
      this->emit_({});  // the page lays the card out
    } else if (this->leaders_tries_ >= ::espn::kLeadersTries) {
      ESP_LOGW(TAG, "Game leaders at the %s: giving up after %u reads", ::espn::leaders_stage_name(j.leaders_stage),
               (unsigned) this->leaders_tries_);
    }
    this->schedule_next_(0);  // whatever the mode was about to read
    return;
  }
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
      this->count_miss_();
      // Retry the scan, not the long wait.
      this->live_started_ms_ =
          ::espn::live_clock_after_failed_scan(this->live_started_ms_, this->live_fallback_, this->recap_on_);
      this->emit_({});
      this->schedule_next_(this->back_off_());
      return;
    }
    if (j.want_finals)
      this->recap_absorb_(j, (int64_t) this->time_->timestamp_now());
    this->clear_backoff_();
    // Favorites take over: when to scan for the next favorite's kickoff. The
    // pick below says again whether a favorite has the board.
    if (this->take_over_()) {
      int64_t tnow = (int64_t) this->time_->timestamp_now();
      int64_t at = ::espn::take_over_rescan_at(tnow, j.fav_kickoff);
      this->take_scan_at_ = j.scan_ok ? at : ::espn::take_over_rescan_keep(tnow, at, this->take_scan_at_);
    } else {
      this->take_scan_at_ = 0;
    }
    this->live_fav_ = -1;
    // For the night schedule: a game of the owner's on now, whichever game
    // the board picks.
    this->scan_live_sides_.clear();
    for (const auto &g : j.live) {
      this->scan_live_sides_.push_back({g.away_id, g.league});
      this->scan_live_sides_.push_back({g.home_id, g.league});
    }
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
      const ::espn::Team *saved = this->fallback_team_();
      if (saved != nullptr) {
        // Nothing live, but the owner has a team: show that team's card and
        // keep scanning. The mode they picked is untouched.
        bool already = this->live_fallback_;
        this->live_none_ = true;
        this->live_fallback_ = true;
        this->live_started_ms_ = now == 0 ? 1 : now;  // start the rescan clock
        // No freshness marking here on purpose. What is on the board in the
        // fallback is favorite #1's card, and only a poll of that card can
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
        this->live_away_id_ = 0;  // the card is oriented around favorite #1
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
    bool best = this->best_game();
    bool over = this->game_.valid && this->game_.state != GameState::IN;
    // Favorites take over: a favorite's game in progress has the board.
    std::vector<::espn::FavRef> favs;
    if (this->take_over_())
      favs = this->fav_refs_();
    // Alternate moves on only at a rotate scan, every Switch every; a kickoff
    // scan keeps the favorite that is up.
    int taken = favs.empty() ? -1
                             : ::espn::take_over_pick(j.live, favs, this->schedule_.event_id, over,
                                                      this->prefs4_.collide == 1, j.rotate);
    const ::espn::LiveGame &g =
        j.live[taken >= 0 ? (size_t) taken
                          : best ? ::espn::best_live_game(j.live, this->schedule_.event_id, over)
                                 : pick[random_uint32() % pick.size()]];
    int fav_rank = taken >= 0 ? ::espn::live_fav_rank(g, favs) : -1;
    // The favorite's side is the board's own team, as in Favorite teams mode;
    // any other game reads like a broadcast, away team first.
    bool fav_home = fav_rank >= 0 && favs[fav_rank].id == g.home_id && favs[fav_rank].id != g.away_id;
    uint32_t side_id = fav_home ? g.home_id : g.away_id;
    uint8_t side_rank = fav_home ? g.home_rank : g.away_rank;
    uint8_t other_rank = fav_home ? g.away_rank : g.home_rank;
    if ((best || taken >= 0) && !over && this->game_.valid && g.event_id == this->schedule_.event_id &&
        side_id == this->live_away_id_ && !this->live_none_ && !this->live_fallback_) {
      // Still the favorite's game, or the best game on: keep it, and look
      // again at the next scan.
      if (taken >= 0)
        ESP_LOGI(TAG, "Favorites take over: staying on %s @ %s", g.away_abbr.c_str(), g.home_abbr.c_str());
      else
        ESP_LOGI(TAG, "Still the best game: staying on %s @ %s", g.away_abbr.c_str(), g.home_abbr.c_str());
      // This break has had its look; the next one comes when play resumes.
      if (taken < 0 && ::espn::pick_break(this->game_))
        this->break_looked_ = true;
      this->live_fav_ = fav_rank;
      this->live_away_rank_ = side_rank;
      this->live_home_rank_ = other_rank;
      this->live_started_ms_ = now == 0 ? 1 : now;
      this->rebuild_state_(&this->last_fields_);
      this->schedule_next_(0);
      return;
    }
    if (taken >= 0)
      ESP_LOGI(TAG, "Favorites take over: following %s @ %s for %s", g.away_abbr.c_str(), g.home_abbr.c_str(),
               this->fav_[fav_rank].team->abbr);
    else if (best)
      ESP_LOGI(TAG, "Following %s @ %s, the best of %u (interest %d)", g.away_abbr.c_str(), g.home_abbr.c_str(),
               (unsigned) j.live.size(), ::espn::live_interest(g));
    else
      ESP_LOGI(TAG, "Following %s @ %s", g.away_abbr.c_str(), g.home_abbr.c_str());
    // Both logos download while the game is read, so its board can go up whole.
    this->logos_.prefetch(::espn::team_logo_url((League) g.league, g.away_id, g.away_abbr.c_str()));
    this->logos_.prefetch(::espn::team_logo_url((League) g.league, g.home_id, g.home_abbr.c_str()));
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
    this->live_away_id_ = side_id;
    this->live_away_rank_ = side_rank;
    this->live_home_rank_ = other_rank;
    this->live_fav_ = fav_rank;
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
  if (j.comp > 0) {
    this->apply_cup_job_(now);
    return;
  }
  if (j.need_schedule) {
    if (!j.schedule_ok) {
      this->count_miss_();
      this->emit_({});
      this->schedule_next_(this->back_off_());
      return;
    }
    this->clear_backoff_();
    this->schedule_ = j.schedule;
    this->schedule_fetched_ms_ = now == 0 ? 1 : now;  // 0 reads as "never fetched"
    // Nothing found ahead: check back hourly; a failed read: in 15 minutes.
    this->linger_backoff_ms_ = j.lookahead == 2 ? 60 * MINUTE : j.lookahead < 0 ? 15 * MINUTE : 0;
    this->force_schedule_ = false;
    this->upcoming_due_ = true;  // refreshed on the next cycle, once the board is drawn
    this->cache_unconfirmed_ = false;
    this->save_team_cache_(j.team);
    if (!j.comps.empty()) {
      this->comps_ = j.comps;
      this->comp_next_ = 1;
    }
    if (j.no_event) {
      ESP_LOGI(TAG, "No upcoming game for this team");
      this->no_game_confirmed_ = true;
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
    this->count_miss_();
    bool again = this->gone_event_ == this->schedule_.event_id;
    if (::espn::reread_team_after_poll(this->cache_unconfirmed_, j.game_gone, j.throttled, again)) {
      // The cache may be wrong, or ESPN no longer serves this game under its
      // id (a rescheduled game it renumbered): read the team endpoint now,
      // not after a back-off that would keep asking for the same game for
      // hours.
      if (this->cache_unconfirmed_)
        ESP_LOGI(TAG, "Cached game not found: reading the team again");
      else
        ESP_LOGI(TAG, "ESPN didn't serve game %s: reading the team again", this->schedule_.event_id.c_str());
      if (j.game_gone)
        this->gone_event_ = this->schedule_.event_id;
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
  this->gone_event_.clear();
  this->prev_ = this->game_;
  this->game_ = j.game;
  this->mark_good_poll_();
  if (this->upcoming_after_poll_) {
    this->upcoming_after_poll_ = false;
    this->upcoming_due_ = true;
  }
  ::espn::Splash splash =
      ::espn::decide_splash(this->prev_, this->game_, this->opponent_splashes(),
                            this->live_mode_() && !this->live_fallback_ && this->live_fav_ < 0);
  // A ranked side trailing late in the best game on: once a game, and never
  // over a score's own splash (the next poll shows it).
  if (splash.text.empty() && this->live_mode_() && !this->live_fallback_ && this->best_game() &&
      this->game_.state == GameState::IN && this->upset_event_ != this->game_.event_id &&
      ::espn::upset_alert(::espn::live_view(this->game_, this->schedule_.league, this->live_away_rank_,
                                            this->live_home_rank_))) {
    this->upset_event_ = this->game_.event_id;
    splash = ::espn::Splash{"UPSET ALERT", ::espn::kUpsetColor};
  }
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

// Worker: one cup's team read, then the pick across every read so far. The
// game is polled when the pick moves, or at the end of a sweep that left the
// board without one.
void GamedayComponent::run_cup_job_() {
  Job &j = this->job_;
  const char *slug = ::espn::league_cup(j.team->league, (size_t) (j.comp - 1));
  Schedule s;
  j.cup_ok = slug != nullptr && this->fetch_schedule_(j.team, s, slug);
  if (j.cup_ok) {
    ::espn::adopt_league(s, j.team->league);
    ::espn::adopt_comp(s, slug);
    this->take_game_on_now_(j.team, s, j.done_event, slug);
    j.comps[j.comp] = s;
  }
  int pick = ::espn::pick_schedule(j.comps, (int64_t) this->time_->timestamp_now(), j.done_event);
  Schedule next = pick >= 0 ? j.comps[pick] : j.comps[0];
  bool moved = next.event_id != j.schedule.event_id;
  bool last = j.comp + 1 >= (int) j.comps.size();
  j.schedule = next;
  if (!moved && !(last && j.need_game))
    return;
  if (next.event_id.empty()) {
    j.no_event = true;
    return;
  }
  j.polled = true;
  j.game_ok = this->fetch_game_(j.team, j.schedule, j.game);
}

// Main loop, Live games' fallback team: fold a cup read in. A new pick takes
// the board.
void GamedayComponent::apply_cup_job_(uint32_t now) {
  Job &j = this->job_;
  this->comps_ = j.comps;
  this->comp_next_ = j.comp + 1 < (int) j.comps.size() ? j.comp + 1 : -1;
  bool moved = j.schedule.event_id != this->schedule_.event_id;
  if (moved) {
    ESP_LOGI(TAG, "Next game: event %s in %s", j.schedule.event_id.c_str(),
             j.schedule.comp_slug != nullptr ? j.schedule.comp_slug : "the league");
    this->schedule_ = j.schedule;
    this->save_team_cache_(j.team);
    this->post_since_ms_ = 0;
  }
  if (j.no_event) {
    ESP_LOGI(TAG, "No upcoming game for this team");
    this->no_game_confirmed_ = true;
    this->game_ = GameSnapshot{};
    this->prev_ = GameSnapshot{};
    this->misses_ = 0;
    this->emit_({});
    this->mark_board_ready_();
  } else if (j.polled && !j.game_ok) {
    this->count_miss_();
    this->emit_({});
  } else if (j.polled) {
    this->prev_ = moved ? GameSnapshot{} : this->game_;
    this->game_ = j.game;
    this->mark_good_poll_();
    if (this->game_.state == GameState::POST) {
      if (this->post_since_ms_ == 0)
        this->post_since_ms_ = now == 0 ? 1 : now;
    } else {
      this->post_since_ms_ = 0;
    }
    this->emit_(::espn::decide_splash(this->prev_, this->game_, this->opponent_splashes(), false));
    this->mark_board_ready_();
  }
  if (::espn::cup_read_backs_off(j.cup_ok, j.polled, j.game_ok, j.throttled)) {
    this->schedule_next_(this->back_off_());
    return;
  }
  this->schedule_next_(this->comp_next_ > 0 || this->upcoming_due_ ? 0 : this->interval_for_phase_());
}

std::string GamedayComponent::no_game_why_() const {
  if (this->game_.valid || this->demo_playing_())
    return "";
  size_t favs = 0, read = 0;
  if (this->favorites_mode_()) {
    favs = this->fav_.size();
    for (const auto &e : this->fav_)
      read += e.sched.valid ? 1 : 0;
  }
  // The Live fallback shows favorite #1's card, which says why it is
  // empty as a favorite's card does.
  return ::espn::no_game_text(this->clock_set_(), this->live_mode_() && !this->live_fallback_,
                              ::espn::no_game_said(favs, read, this->no_game_confirmed_), this->misses_,
                              this->captive_, this->since_join_ms_());
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
  f.sport = (uint8_t) g.sport;
  if (g.valid && g.state == GameState::IN && g.sport == ::espn::Sport::BASEBALL) {
    f.situation = ::espn::baseball_count(g);
    f.outs = g.mlb.outs;
    f.bases = g.mlb.bases;
    f.highlight = g.mlb.bases == (::espn::Baseball::FIRST | ::espn::Baseball::SECOND | ::espn::Baseball::THIRD);
  }
  if (g.valid && g.sport == ::espn::Sport::SOCCER) {
    // The latest goal on the situation row, red cards under the logos, and
    // records that fit the label under a logo.
    if (g.state == GameState::IN)
      f.situation = ::espn::soccer_situation(g);
    if (g.state != GameState::PRE) {
      f.team_marks = g.soc.team_reds;
      f.opp_marks = g.soc.opp_reds;
    }
    f.team_record = ::espn::soccer_board_record(g.team_record);
    f.opponent_record = ::espn::soccer_board_record(g.opp_record);
  }
  if (g.valid && g.state == GameState::IN && g.sport == ::espn::Sport::BASKETBALL)
    f.situation = ::espn::basketball_series(g);
  f.team_color = ::espn::parse_color(g.team_color);
  f.opponent_color = ::espn::parse_color(g.opp_color);
#ifdef GAMEDAY_DEMO
  this->demo_badges_(f);
#endif
  f.splash_text = splash.text;
  f.splash_color = splash.color;

  TickerOptions opts;
  opts.clock = false;  // the clock has its own row on the panel; never in the ticker
  opts.down_distance = this->ticker_down_distance();
  opts.baseball_count = this->ticker_baseball_count();
  opts.soccer_stats = this->ticker_soccer_stats();
  opts.last_play = this->ticker_last_play();
  opts.odds = this->ticker_odds();
  opts.leaders = this->ticker_leaders();
  // Changing the panel count restarts the panel, so this holds until then.
  opts.records = this->panels_ != nullptr && this->panels_->cols() == 1;

  auto kickoff_of = [this](const GameSnapshot &s) -> std::string {
    if (!s.valid || s.state != GameState::PRE || s.kickoff_epoch <= 0 || this->time_ == nullptr)
      return "";
    int64_t at = s.time_tbd ? ::espn::tbd_day_epoch(s.kickoff_epoch) : s.kickoff_epoch;
    struct tm kick = ESPTime::from_epoch_local((time_t) at).to_c_tm();
    struct tm now = this->time_->now().to_c_tm();
    return ::espn::kickoff_label(kick, now, s.time_tbd);
  };
  std::string kickoff = kickoff_of(g);
  f.kickoff = kickoff;
  f.status_text = ::espn::status_text(g, opts, kickoff);
  // Halftime and the final: ESPN's headline (a final only) and the scores the
  // panel saw, behind the same switch as the last play.
  this->score_log_.update(g);
  if (opts.last_play && g.valid && (::espn::at_halftime(g) || (g.state == GameState::POST && g.completed)))
    f.status_text = ::espn::with_score_log(f.status_text, g.state == GameState::POST ? g.headline : "",
                                           this->score_log_.lines(g.event_id));
  const GameSnapshot &ha = this->ha_game_();
  f.ha_status_text = ::espn::ha_status_text(ha, kickoff_of(ha));
  f.ha_last_play = ha.last_play;
  if (ha.valid && ha.state != GameState::NOT_FOUND) {
    f.ha_live = ha.state == GameState::IN;
    if (ha.state != GameState::PRE) {
      f.ha_team_score = ha.team_score;
      f.ha_opp_score = ha.opp_score;
    }
  }
  if (ha.valid && ha.state != GameState::NOT_FOUND) {
    f.ha_team = ha.team_abbr;
    f.ha_opponent = ha.opp_abbr;
    f.ha_team_rgb = ::espn::ha_rgb_text(::espn::ha_rgb(ha.team_color));
    f.ha_opp_rgb = ::espn::ha_rgb_text(::espn::ha_rgb(ha.opp_color));
    f.ha_period = ::espn::ha_period_text(ha);
    f.ha_clock = ::espn::ha_clock_text(ha);
  }
  // Redraws repeat the same read, which the tracker sees as no change.
  this->ha_track_();
  // Before the Live mode prefixes, so a fallback team's Next line keeps "No
  // live X games, showing CHI |" in front of it.
  this->clock_fields_(f);
  this->countdown_fields_(f);
  std::string showing;  // the Live fallback's "No live X games, showing DAL | "
  if (this->live_mode_() && !this->demo_playing_()) {
    std::string league_word = this->live_word_();
    const ::espn::Team *saved = this->fallback_team_();
    if (this->recap_on_) {
      // The finals rotation follows no game, so Home Assistant gets one steady
      // line instead of a change on every card.
      f.status_text = this->recap_ticker_;
      f.ha_status_text = "No live " + league_word + "games right now";
      f.ha_last_play.clear();
      f.ha_live = false;
      f.ha_team_score = f.ha_opp_score = -1;
      f.ha_team.clear();
      f.ha_opponent.clear();
      f.ha_team_rgb.clear();
      f.ha_opp_rgb.clear();
      f.ha_period.clear();
      f.ha_clock.clear();
      f.message = this->recap_message_;
    } else if (this->live_fallback_ && saved != nullptr) {
      showing = "No live " + league_word + "games, showing " + saved->abbr + " | ";
      f.status_text = showing + f.status_text;
      f.ha_status_text = showing + f.ha_status_text;
    } else if (!g.valid || g.state == GameState::NOT_FOUND) {
      f.status_text = this->live_none_ ? "No live " + league_word + "games right now"
                                       : "Looking for a live " + league_word + "game";
      f.ha_status_text = f.status_text;
      f.message = this->live_none_ ? "No live " + league_word + "games" : "Looking for a live " + league_word + "game";
    }
  }
  // An empty board says why (espn_parse.h, no_game_text): the clock is not
  // set yet, or ESPN cannot be reached. "No upcoming game" is only right
  // once ESPN itself has said so; before that it would be a guess.
  std::string why = this->no_game_why_();
  if (!why.empty()) {
    f.status_text = showing + why;
    f.ha_status_text = showing + why;
    f.message = why;
  }
  if (this->prefs2_.mode == (uint8_t) Mode::FAVORITES && this->fav_.empty() && !this->demo_playing_()) {
    f.status_text = "Favorites: add teams in the app or on the page | " + f.status_text;
    f.ha_status_text = "Favorites: add teams in the app or on the page | " + f.ha_status_text;
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
  // Once a day while a release waits (update_nudge.h). A game going live
  // takes it off at once, not at the next tick.
  if (this->nudge_on_ && !this->followed_game_live_() && !this->demo_playing_()) {
    std::string ready = std::string(::espn::update_nudge_text(this->update_important_)) + " | ";
    f.status_text = ready + f.status_text;
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

  if (!f.splash_text.empty()) {
    ESP_LOGI(TAG, "Splash: %s", f.splash_text.c_str());
    // The message card makes way at once, and stays away for a while after.
    this->splash_ms_ = millis() == 0 ? 1 : millis();
    if (this->msg_shown_)
      this->msg_show_("");
  }
  if (this->clock_set_())
    this->drawn_day_ = ::espn::local_day(this->time_->now().to_c_tm());
  this->last_fields_ = f;
  this->rebuild_state_(&f);
  if (this->hold_for_logos_(f))
    return;
  this->draw_(f);
}

// A board with different logos from the one up now waits for them, so it goes
// up whole instead of one logo and then the other a second later. loop() draws
// it from last_fields_, which later updates keep current, once both are in or
// the wait runs out (logo_lru.h, logo_hold_over). A splash goes up at once.
bool GamedayComponent::hold_for_logos_(const UpdateFields &f) {
  bool same = f.team_logo == this->shown_team_logo_ && f.opponent_logo == this->shown_opp_logo_;
  if (same || !f.splash_text.empty() || this->demo_playing_()) {
    this->logo_held_ = false;
    return false;
  }
  if (!this->logo_held_)
    this->logo_hold_ms_ = millis();
  this->logo_held_ = !this->logo_hold_done_();
  return this->logo_held_;
}

bool GamedayComponent::logo_hold_done_() {
  const UpdateFields &f = this->last_fields_;
  return ::espn::logo_hold_over(this->logos_.waiting(f.team_logo), this->logos_.waiting(f.opponent_logo),
                                this->logo_hold_ms_, millis());
}

void GamedayComponent::draw_(const UpdateFields &f) {
  if (this->logo_held_) {
    ESP_LOGD(TAG, "Board held %u ms for its logos", (unsigned) (millis() - this->logo_hold_ms_));
    this->logo_held_ = false;
  }
  this->shown_team_logo_ = f.team_logo;
  this->shown_opp_logo_ = f.opponent_logo;
  for (auto &cb : this->callbacks_)
    cb(f);
}

// The clock card (offseason.h): favorite #1 between seasons as a Live
// mode's card when nothing is live, a Live mode whose rotation has no cards,
// or Favorites with no card at all. With Next game off (cards.h,
// clock_instead) it also stands in for the next game's card. Team colors, or
// neutral gray with no team.
void GamedayComponent::clock_fields_(UpdateFields &f) {
  if (this->demo_playing_() || !this->clock_set_())
    return;
  const GameSnapshot &g = this->game_;
  const ::espn::Team *team = this->fallback_team_();
  const bool next_on = this->card_on(::espn::Card::NEXT), clock_on = this->card_on(::espn::Card::CLOCK);
  std::string color;  // hex, from the team endpoint or the game
  if (this->live_mode_() && !this->live_fallback_) {
    // Nothing in the last week and no kickoff in the next (recap_show_).
    if (!clock_on || !this->recap_on_ || !this->recap_cards_.empty())
      return;
  } else if (this->favorites_mode_()) {
    // A favorite's game locked on has the board, playlist or not (a favorite
    // outside the playlist still takes the panel near its kickoff).
    if (this->fav_locked_)
      return;
    // Between seasons: every favorite in the playlist has been read, and each
    // has no game, or the next one past the season horizon (often next
    // season's opener). The Next line is the soonest game ahead.
    int64_t tnow = (int64_t) this->time_->timestamp_now();
    const FavEntry *first = nullptr, *next = nullptr;
    bool off_season = true;
    for (const auto &e : this->fav_) {
      if (!this->fav_in_list_(e.slot - 1))
        continue;
      if (first == nullptr)
        first = &e;
      bool polled = e.game.valid && e.game.event_id == e.sched.event_id;
      int64_t kick = polled ? e.game.kickoff_epoch : e.sched.kickoff_epoch;
      if (::espn::fav_off_season(e.sched.valid, !e.sched.event_id.empty(), polled, e.game.state, kick, tnow,
                                 ::espn::season_horizon(::espn::league_sport(e.team->league))) != 1)
        off_season = false;
      if (polled && e.game.state == GameState::PRE && e.game.kickoff_epoch > 0 &&
          (next == nullptr || e.game.kickoff_epoch < next->game.kickoff_epoch))
        next = &e;
    }
    // Unlocked, no favorite's game is under way or just over.
    if (first == nullptr || !::espn::clock_instead(next_on, clock_on, true, off_season))
      return;
    const FavEntry &shown = next != nullptr ? *next : *first;
    team = shown.team;
    color = shown.game.valid ? shown.game.team_color : shown.sched.team_color;
    if (next != nullptr)
      this->clock_next_line_(f, next->game);
  } else {
    if (team == nullptr)
      return;
    // The team endpoint named no game, as opposed to a card still loading.
    bool none = !g.valid && this->schedule_.valid && this->schedule_.event_id.empty();
    if (!none && !g.valid)
      return;
    int64_t tnow = (int64_t) this->time_->timestamp_now();
    GameState state = none ? GameState::NOT_FOUND : g.state;
    bool upcoming = state == GameState::PRE || state == GameState::NOT_FOUND;
    if (!::espn::clock_instead(next_on, clock_on, upcoming,
                               ::espn::off_season(state, g.kickoff_epoch, tnow,
                                                  ::espn::season_horizon(::espn::league_sport(team->league)))))
      return;
    color = g.valid ? g.team_color : this->schedule_.team_color;
    if (g.valid && g.state == GameState::PRE && g.kickoff_epoch > 0)
      this->clock_next_line_(f, g);
  }
  f.clock = true;
  f.team_color = ::espn::clock_color(team != nullptr, ::espn::parse_color(color));
}

// The clock's "Next: DAL vs PHI" and kickoff, for a game still ahead.
void GamedayComponent::clock_next_line_(UpdateFields &f, const GameSnapshot &g) {
  int64_t at = g.time_tbd ? ::espn::tbd_day_epoch(g.kickoff_epoch) : g.kickoff_epoch;
  struct tm kick = ESPTime::from_epoch_local((time_t) at).to_c_tm();
  struct tm now = this->time_->now().to_c_tm();
  std::string ours = panel_text(g.team_abbr), opp = panel_text(g.opp_abbr);
  f.next_line = ::espn::next_line(ours, opp, g.team_home, kick, now, "\n", g.time_tbd);
  // The ticker carries it on both layouts, since a single panel has no
  // room for it under the time.
  f.status_text = ::espn::next_line(ours, opp, g.team_home, kick, now, " ", g.time_tbd);
}

// The countdown card (countdown.h). Never over a game in progress, a locked
// favorite or the demo. A Live mode's fallback card turns between the next
// game and its countdown; Favorites gives a favorite's countdown its own turn
// after its card; the custom countdown takes the last turn of each minute in
// every mode.
::espn::CountdownCard GamedayComponent::countdown_card_(int64_t now) const {
  using ::espn::CountdownCard;
  if (this->demo_playing_() || !this->clock_set_() || !this->board_ready_)
    return CountdownCard::NONE;
  const GameSnapshot &g = this->game_;
  if (g.valid && g.state == GameState::IN)
    return CountdownCard::NONE;
  int64_t start = this->custom_start_();
  bool custom = this->card_on(::espn::Card::CUSTOM) && start != 0 && now < start + ::espn::kDayS;
  bool next = this->countdown_on() && g.valid && g.state == GameState::PRE && !g.time_tbd && g.kickoff_epoch > now;
  bool weather = this->wx_card_ready_();
  if (this->live_mode_() && !this->live_fallback_) {
    if (custom && ::espn::custom_turn(now))
      return CountdownCard::CUSTOM;
    return weather && ::espn::weather_turn(now) ? CountdownCard::WEATHER : CountdownCard::NONE;
  }
  if (this->favorites_mode_()) {
    if (this->fav_locked_)
      return CountdownCard::NONE;
    if (custom && ::espn::custom_turn(now))
      return CountdownCard::CUSTOM;
    if (weather && ::espn::weather_turn(now))
      return CountdownCard::WEATHER;
    // The second half of the card's turn (favorites.h, countdown_s).
    return next && this->fav_shown_ >= 0 && now - this->fav_shown_since_ >= ::espn::kCountdownTurnS
               ? CountdownCard::NEXT
               : CountdownCard::NONE;
  }
  return ::espn::countdown_turn(now, next, custom, weather);
}

int64_t GamedayComponent::custom_start_() const {
  uint32_t ymd = this->countdown_.ymd;
  if (ymd == 0)
    return 0;
  ESPTime t{};
  t.year = ymd / 10000;
  t.month = ymd / 100 % 100;
  t.day_of_month = ymd % 100;
  t.recalc_timestamp_local();
  return t.timestamp > 0 ? (int64_t) t.timestamp : 0;
}

void GamedayComponent::countdown_fields_(UpdateFields &f) {
  this->countdown_shown_ = ::espn::CountdownCard::NONE;
  if (!f.splash_text.empty() || !this->clock_set_())
    return;
  ::espn::CountdownCard c = this->countdown_card_((int64_t) this->time_->timestamp_now());
  if (c == ::espn::CountdownCard::NONE)
    return;
  this->countdown_shown_ = c;
  if (c == ::espn::CountdownCard::WEATHER) {
    this->wx_fields_(f);
    return;
  }
  const GameSnapshot &g = this->game_;
  bool has_team = this->fallback_team_() != nullptr;
  std::string color = g.valid ? g.team_color : this->schedule_.team_color;
  if (this->favorites_mode_()) {
    has_team = true;
    color = this->fav_[this->fav_shown_ >= 0 ? this->fav_shown_ : 0].sched.team_color;
  } else if (this->live_mode_() && !this->live_fallback_) {
    has_team = false;  // the games on the board are anyone's
  }
  if (c == ::espn::CountdownCard::NEXT) {
    f.countdown_at = f.countdown_end = g.kickoff_epoch;
    f.next_line = ::espn::matchup_line(panel_text(g.team_abbr), panel_text(g.opp_abbr), g.team_home);
    color = g.team_color;
  } else {
    f.countdown_at = this->custom_start_();
    f.countdown_end = f.countdown_at + ::espn::kDayS;
    std::string name = panel_text(this->countdown_.name);
    f.next_line = name.empty() ? ::espn::countdown_day_label(this->countdown_.ymd) : name;
  }
  f.clock = false;  // the countdown has the clock card's screen
  f.team_color = ::espn::clock_color(has_team, ::espn::parse_color(color));
}

// ---- The weather card (weather.h) --------------------------------------------

void GamedayComponent::load_wx_() {
  this->wx_pref_ = global_preferences->make_preference<PrefsWx>(fnv1_hash("gameday_wx_v1"));
  if (!this->wx_pref_.load(&this->wx_))
    this->wx_ = PrefsWx{};  // no town: no card
  this->wx_.town[::espn::kTownMax] = '\0';
  this->wx_.place[::espn::kPlaceMax] = '\0';
  std::string town = ::espn::town_clean(this->wx_.town);
  std::string place = ::espn::message_clean(::espn::utf8_only(this->wx_.place), ::espn::kPlaceMax, false);
  if (town != this->wx_.town || place != this->wx_.place || !std::isfinite(this->wx_.lat) ||
      !std::isfinite(this->wx_.lon) || this->wx_.lat < -90.0f || this->wx_.lat > 90.0f || this->wx_.lon < -180.0f ||
      this->wx_.lon > 180.0f)
    this->wx_ = PrefsWx{};  // a later firmware's blob, or damage
  this->wx_.on = this->wx_.on ? 1 : 0;
  // A town saved before its lookup answered (a power cut) is looked up now;
  // a saved place stands for a lookup that answered.
  if (this->wx_.town[0] != '\0' && !this->wx_place_known_())
    this->wx_lookup_due_ = true;
  else if (this->wx_place_known_())
    this->wx_lookup_status_ = 200;
}

void GamedayComponent::save_wx_() {
  this->wx_pref_.save(&this->wx_);
  this->sync_prefs_();
  this->countdown_tick_ms_ = 0;  // the card turns at the next loop
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::set_weather(bool on) {
  if (on == this->weather_on())
    return;
  this->wx_.on = on ? 1 : 0;
  ESP_LOGI(TAG, "Weather card: %s", on ? "on" : "off");
  this->save_wx_();
}

void GamedayComponent::set_town(const std::string &town) {
  std::string t = ::espn::town_clean(town);
  if (t == this->wx_.town) {
    // The same town again: a lookup that found nothing is tried once more.
    if (!t.empty() && !this->wx_place_known_() && !this->wx_lookup_due_) {
      this->wx_lookup_status_ = -1;
      this->wx_lookup_due_ = true;
      this->wx_fail_ms_ = 0;
      this->rebuild_state_(&this->last_fields_);
    }
    return;
  }
  memset(this->wx_.town, 0, sizeof(this->wx_.town));
  memcpy(this->wx_.town, t.data(), t.size());
  memset(this->wx_.place, 0, sizeof(this->wx_.place));
  this->wx_.lat = this->wx_.lon = 0;
  this->wx_.on = t.empty() ? 0 : 1;
  this->wx_forecast_ = ::espn::WxForecast{};
  this->wx_lookup_status_ = -1;
  this->wx_status_ = -1;
  this->wx_read_ms_ = 0;
  this->wx_fail_ms_ = 0;
  this->wx_fail_.clear();
  this->wx_lookup_due_ = !t.empty();
  ESP_LOGI(TAG, "Weather town: %s", t.empty() ? "(none)" : t.c_str());
  this->save_wx_();
}

bool GamedayComponent::wx_due_() const {
  if (this->busy_)
    return false;
  const uint32_t now = millis();
  if (this->wx_fail_ms_ != 0 && now - this->wx_fail_ms_ < this->wx_backoff_ms_)
    return false;
  if (this->wx_lookup_due_)
    return true;
  if (!this->weather_on() || !this->wx_place_known_() || !this->clock_set_() || this->demo_playing_() ||
      this->followed_game_live_())
    return false;
  return this->wx_read_ms_ == 0 || now - this->wx_read_ms_ >= ::espn::kWxReadMs;
}

bool GamedayComponent::wx_card_ready_() const {
  const bool answered = this->wx_lookup_status_ == 200;
  return ::espn::wx_card_ready(this->weather_on(), this->wx_.town, answered, this->wx_place_known_(),
                               this->wx_forecast_.valid, millis() - this->wx_read_ms_, this->wx_read_ms_ != 0);
}

void GamedayComponent::start_wx_job_() {
  this->job_ = Job{};
  this->job_.generation = this->generation_;
  if (this->wx_lookup_due_) {
    this->wx_lookup_due_ = false;
    this->job_.need_wx = 1;
    this->job_.wx_town = this->wx_.town;
    this->job_.wx_url = ::espn::geocode_url(this->wx_.town);
  } else {
    this->job_.need_wx = 2;
    this->job_.wx_lat = this->wx_.lat;
    this->job_.wx_lon = this->wx_.lon;
    this->job_.wx_url =
        ::espn::forecast_url(this->wx_.lat, this->wx_.lon, ::espn::kTimezones[this->prefs_.tz_index].iana);
  }
  this->start_worker_();
}

// Worker task.
void GamedayComponent::run_wx_job_() {
  Job &j = this->job_;
  std::vector<http_request::Header> headers = {{"User-Agent", USER_AGENT}, {"Accept", "application/json"}};
  this->wx_requests_++;
  ESP_LOGD(TAG, "Weather: %s", j.wx_url.c_str());
  auto container = this->http_->get(j.wx_url, headers, {"retry-after"});
  if (container == nullptr) {
    ESP_LOGW(TAG, "Weather: no answer from %s", j.wx_url.c_str());
    return;
  }
  j.wx_status = container->status_code;
  if (container->status_code != 200) {
    ESP_LOGW(TAG, "Weather: HTTP %d for %s", container->status_code, j.wx_url.c_str());
    j.wx_retry_after_ms = ::espn::parse_retry_after_ms(container->get_response_header("retry-after"));
    container->end();
    return;
  }
  ContainerReader reader(container, this->wx_bytes_);
  struct Source : ::espn::WxSource {
    explicit Source(ContainerReader &r) : r(r) {}
    int read() override { return this->r.read(); }
    size_t readBytes(char *dst, size_t n) override { return this->r.readBytes(dst, n); }
    ContainerReader &r;
  } source(reader);
  bool ok = j.need_wx == 1 ? ::espn::parse_geocode_from(source, j.wx_place)
                           : ::espn::parse_forecast_from(source, j.wx_forecast);
  container->end();
  if (!ok) {
    ESP_LOGW(TAG, "Weather: %s didn't parse after %u bytes", j.need_wx == 1 ? "the lookup" : "the forecast",
             (unsigned) reader.total());
    return;
  }
  j.wx_ok = true;
}

void GamedayComponent::apply_wx_job_() {
  Job &j = this->job_;
  const uint32_t now = millis();
  if (j.need_wx == 1 && j.wx_town != this->wx_.town)
    return;  // the town changed meanwhile; set_town started a new lookup
  if (j.need_wx == 2 && (j.wx_lat != this->wx_.lat || j.wx_lon != this->wx_.lon))
    return;  // a forecast for the town before; the new one's is due now
  if (!j.wx_ok) {
    this->wx_fail_ms_ = now == 0 ? 1 : now;
    this->wx_backoff_ms_ =
        this->wx_fail_.fail(::espn::is_throttle_status(j.wx_status), j.wx_retry_after_ms, random_uint32());
    ESP_LOGW(TAG, "Weather: %u failed read(s) in a row, next try in %us", (unsigned) this->wx_fail_.streak,
             (unsigned) (this->wx_backoff_ms_ / 1000));
    if (j.need_wx == 1) {
      this->wx_lookup_status_ = j.wx_status;
      this->wx_lookup_due_ = true;  // tried again after the wait
    } else {
      this->wx_status_ = j.wx_status;
    }
    this->rebuild_state_(&this->last_fields_);
    return;
  }
  this->wx_fail_ms_ = 0;
  this->wx_fail_.clear();
  if (j.need_wx == 1) {
    this->wx_lookup_status_ = 200;
    memset(this->wx_.place, 0, sizeof(this->wx_.place));
    memcpy(this->wx_.place, j.wx_place.name.data(), j.wx_place.name.size());
    this->wx_.lat = j.wx_place.lat;
    this->wx_.lon = j.wx_place.lon;
    ESP_LOGI(TAG, "Weather: %s is %s (%.4f, %.4f)", this->wx_.town,
             this->wx_place_known_() ? this->wx_.place : "not a place Open-Meteo knows", (double) this->wx_.lat,
             (double) this->wx_.lon);
    this->save_wx_();  // rebuilds the state; the forecast read is due now
    return;
  }
  this->wx_status_ = 200;
  this->wx_forecast_ = j.wx_forecast;
  this->wx_read_ms_ = now == 0 ? 1 : now;
  ESP_LOGI(TAG, "Weather: %d F now, %d/%d today, code %u", this->wx_forecast_.temp, this->wx_forecast_.today.hi,
           this->wx_forecast_.today.lo, (unsigned) this->wx_forecast_.code);
  this->rebuild_state_(&this->last_fields_);
  if (this->countdown_shown_ == ::espn::CountdownCard::WEATHER)
    this->emit_({});  // the card is up: redraw it with the new numbers
}

void GamedayComponent::wx_fields_(UpdateFields &f) {
  f.wx_card = true;
  f.clock = false;  // the card has the clock card's screen
  f.wx_text = ::espn::wx_card_text(this->weather_on(), this->wx_.town, this->wx_lookup_status_ == 200,
                                   this->wx_place_known_());
  if (!f.wx_text.empty()) {
    f.message = f.wx_text;  // in the no-game screen's words label
    return;
  }
  if (!this->wx_forecast_.valid)
    return;
  const ::espn::WxForecast &w = this->wx_forecast_;
  const bool wide = this->panels_ == nullptr || this->panels_->cols() >= 2;
  f.wx_temp = w.temp;
  f.wx_hi = w.today.hi;
  f.wx_lo = w.today.lo;
  f.wx_icon = (uint8_t) ::espn::wx_icon(w.code, w.night);
  f.wx_words = ::espn::wx_words(w.code, wide);
  f.wx_d1 = ::espn::weekday_text(w.next[0].wday);
  f.wx_d1_hilo = ::espn::wx_hilo_text(w.next[0].hi, w.next[0].lo);
  f.wx_d1_icon = (uint8_t) ::espn::wx_icon(w.next[0].code, false);
  f.wx_d2 = ::espn::weekday_text(w.next[1].wday);
  f.wx_d2_hilo = ::espn::wx_hilo_text(w.next[1].hi, w.next[1].lo);
  f.wx_d2_icon = (uint8_t) ::espn::wx_icon(w.next[1].code, false);
}

// Once a second: put the card up, or take it down, when its turn changes.
void GamedayComponent::countdown_tick_() {
  if (this->countdown_tick_ms_ != 0 && millis() - this->countdown_tick_ms_ < 1000)
    return;
  this->countdown_tick_ms_ = millis() == 0 ? 1 : millis();
  if (!this->clock_set_())
    return;
  if (this->countdown_card_((int64_t) this->time_->timestamp_now()) != this->countdown_shown_)
    this->emit_({});
  this->day_tick_();
}

// Nothing else redraws at midnight: the fallback team can go 15 minutes
// between reads before a far kickoff, and a favorite hours. A game under way
// has no kickoff label, and is redrawn every read anyway.
void GamedayComponent::day_tick_() {
  if (this->drawn_day_ == 0 || ::espn::local_day(this->time_->now().to_c_tm()) == this->drawn_day_)
    return;
  if (this->demo_playing_() || (this->game_.valid && this->game_.state == GameState::IN))
    return;
  ESP_LOGI(TAG, "New local day, redrawing the kickoff labels");
  this->redraw_board_();
}

// ---- finals rotation ------------------------------------------------------------

// Main loop: the next day the rotation needs read, if any. Going back, per
// league, until it has kRecapMinFinals or has looked a week back. Going
// forward, only when no game is left today, until a day has a kickoff. Each
// league counts days its own way (recap.h, espn_day).
bool GamedayComponent::recap_job_(Job &j, int64_t tnow) {
  const uint16_t leagues = this->recap_leagues_();
  for (uint8_t i = 0; i < ::espn::kLeagueCount; i++) {
    if (!(leagues & ::espn::recap_bit(i)))
      continue;
    if (this->recap_back_[i].back_due(::espn::espn_day(tnow, i), ::espn::recap_count(this->recap_days_, i),
                                      j.recap_day)) {
      j.need_recap = true;
      j.recap_forward = false;
      j.recap_league = i;
      return true;
    }
  }
  if (this->recap_scan_next_ != 0)
    return false;  // a game later today: that is the next kickoff
  for (uint8_t i = 0; i < ::espn::kLeagueCount; i++) {
    if (!(leagues & ::espn::recap_bit(i)))
      continue;
    if (this->recap_fwd_[i].fwd_due(::espn::espn_day(tnow, i), this->recap_fwd_next_[i], tnow, j.recap_day)) {
      j.need_recap = true;
      j.recap_forward = true;
      j.recap_league = i;
      return true;
    }
  }
  return false;
}

void GamedayComponent::apply_recap_job_() {
  Job &j = this->job_;
  if (j.recap_league >= ::espn::kLeagueCount)
    return;
  uint8_t i = j.recap_league;
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  int32_t today = ::espn::espn_day(tnow, i);
  if (!j.recap_ok) {
    // The same day again after the backoff. The rotation keeps going with
    // what it has.
    this->schedule_next_(this->back_off_());
    return;
  }
  this->clear_backoff_();
  if (j.recap_forward) {
    bool found = j.next_kickoff > tnow;
    if (found) {
      this->recap_fwd_next_[i] = j.next_kickoff;
      // The game the Next card names. The scan keeps a college day's ranked
      // games first, so the first kickoff may not be among them.
      this->recap_fwd_match_[i].clear();
      for (const auto &p : j.pending)
        if (p.kickoff_epoch == j.next_kickoff) {
          this->recap_fwd_match_[i] = ::espn::recap_matchup(p);
          break;  // the first one kept at that time: best ranked, then ESPN's order
        }
    }
    if (!this->recap_fwd_[i].fwd_read(today, found)) {
      ESP_LOGI(TAG, "No %s kickoff in the next week", ::espn::recap_league_word(i));
      this->recap_fwd_next_[i] = 0;
      this->recap_fwd_match_[i].clear();
    }
  } else {
    ::espn::RecapDay d;
    d.league = i;
    d.day = j.recap_day;
    d.finals.assign(std::make_move_iterator(j.finals.begin()), std::make_move_iterator(j.finals.end()));
    ::espn::recap_merge(this->recap_days_, std::move(d));
    this->recap_back_[i].back_read(today, ::espn::recap_count(this->recap_days_, i));
    if (this->recap_back_[i].step == ::espn::RecapWalk::kDone)
      ESP_LOGI(TAG, "Recent %s finals: %u", ::espn::recap_league_word(i),
               (unsigned) ::espn::recap_count(this->recap_days_, i));
  }
  ::espn::recap_trim(this->recap_days_, tnow);
  this->recap_rebuild_();
  this->schedule_next_(0);
}

// A scan's finals and games to come go in as today's (recap.h).
void GamedayComponent::recap_absorb_(const Job &j, int64_t tnow) {
  ::espn::recap_absorb(this->recap_days_, j.finals, j.pending, tnow);
  this->recap_scan_next_ = ::espn::recap_scan_next(j.next_kickoff, j.pending, tnow);
  this->recap_rebuild_();
}

int64_t GamedayComponent::recap_next_kickoff_(std::string *matchup) const {
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  int64_t best = this->recap_scan_next_ > tnow ? this->recap_scan_next_ : 0;
  std::string match;
  const uint16_t leagues = this->recap_leagues_();
  for (uint8_t i = 0; i < ::espn::kLeagueCount; i++) {
    if (!(leagues & ::espn::recap_bit(i)))
      continue;
    int64_t k = this->recap_fwd_next_[i];
    if (k > tnow && (best == 0 || k < best)) {
      best = k;
      match = this->recap_fwd_match_[i];
    }
  }
  if (best != 0 && best == this->recap_scan_next_)
    for (const auto &d : this->recap_days_)
      for (const auto &p : d.later)
        if (p.kickoff_epoch == best && (leagues & ::espn::recap_bit(d.league)))
          match = ::espn::recap_matchup(p);
  if (matchup != nullptr)
    *matchup = match;
  return best;
}

std::string GamedayComponent::live_word_() const {
  uint8_t m = this->effective_mode_();
  if (m == (uint8_t) Mode::LIVE_NCAA)
    return "college ";
  if (m == (uint8_t) Mode::LIVE_NFL)
    return "NFL ";
  if (m == (uint8_t) Mode::LIVE) {
    std::vector<League> leagues = this->live_leagues_();
    if (leagues.size() == 1)
      return std::string(::espn::recap_league_word((uint8_t) leagues[0])) + " ";
  }
  return "";
}

// The cards for the leagues this mode reads. The card on the board stays up
// until its time is over; a rotation with nothing up yet starts now.
void GamedayComponent::recap_rebuild_() {
  this->recap_cards_ = ::espn::recap_cards(this->recap_days_, this->recap_leagues_(), this->recap_next_kickoff_() != 0,
                                           (int64_t) this->time_->timestamp_now());
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
// no-game screen, a final or a game later today on the scoreboard. The
// ticker on a final names the day it was played, on a game later today its
// kickoff and channel.
void GamedayComponent::recap_show_() {
  this->recap_step_ms_ = millis() == 0 ? 1 : millis();
  this->prev_ = GameSnapshot{};
  this->post_since_ms_ = 0;
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  std::string word = this->live_word_();
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
      this->recap_message_ = ::espn::recap_header(this->recap_days_[c.day], tnow);
      this->recap_ticker_ = none;
      break;
    case ::espn::RecapKind::FINAL: {
      this->game_ = ::espn::final_snapshot(this->recap_days_[c.day].finals[c.final]);
      this->recap_message_.clear();
      std::string detail = this->game_.short_detail.empty() ? std::string("Final") : this->game_.short_detail;
      this->recap_ticker_ = ::espn::recap_header(this->recap_days_[c.day], tnow) + " | " + detail;
      break;
    }
    case ::espn::RecapKind::LATER_HEADER:
      this->game_ = GameSnapshot{};
      this->recap_message_ = "Later today";
      this->recap_ticker_ = none;
      break;
    case ::espn::RecapKind::LATER: {
      const ::espn::Pending &p = this->recap_days_[c.day].later[c.final];
      this->game_ = ::espn::pending_snapshot(p);
      this->recap_message_.clear();
      struct tm k = ESPTime::from_epoch_local((time_t) p.kickoff_epoch).to_c_tm();
      struct tm n = this->time_->now().to_c_tm();
      this->recap_ticker_ = ::espn::recap_later_ticker(p, ::espn::kickoff_label(k, n));
      break;
    }
    case ::espn::RecapKind::NEXT: {
      this->game_ = GameSnapshot{};
      std::string match;
      int64_t kick = this->recap_next_kickoff_(&match);
      if (kick == 0) {
        // It kicked off since the cards were made; the scan due now finds it.
        this->recap_message_ = "No live " + word + "games";
        this->recap_ticker_ = none;
        break;
      }
      struct tm k = ESPTime::from_epoch_local((time_t) kick).to_c_tm();
      struct tm n = this->time_->now().to_c_tm();
      std::string label = ::espn::recap_next_text(match, ::espn::kickoff_label(k, n));
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
  } else if (nx.kind == ::espn::RecapKind::LATER) {
    const ::espn::Pending &p = this->recap_days_[nx.day].later[nx.final];
    this->logos_.prefetch(::espn::team_logo_url((League) p.league, p.away_id, p.away_abbr.c_str()));
    this->logos_.prefetch(::espn::team_logo_url((League) p.league, p.home_id, p.home_abbr.c_str()));
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

static void put_team(JsonObject o, uint8_t league, uint32_t id) {
  for (size_t i = 0; i < ::espn::kTeamCount; i++) {
    const auto &t = ::espn::kTeams[i];
    if ((uint8_t) t.league == league && t.espn_id == id) {
      o["l"] = ::espn::league_key((League) league);
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
  doc["nick"] = this->nick_;  // "" until the owner names the panel
  doc["version"] = App.get_comment();
  doc["setup"] = this->flag_(FLAG_SETUP);
  doc["panels"] = this->panels_ != nullptr ? this->panels_->cols() : 0;
  doc["mode"] = this->prefs2_.mode;
  doc["rotate"] = this->prefs2_.rotate_minutes;
  doc["lockon"] = this->prefs4_.lockon_minutes;
  doc["release"] = this->prefs4_.release_seconds;
  doc["collide"] = this->prefs4_.collide;
  // Always the four remote slots (older apps expect four), then the rest of
  // the list up to its last entry.
  uint8_t fav_n = 4;
  for (uint8_t i = 4; i < FAV_MAX; i++)
    if (this->fav_id_(i) != 0)
      fav_n = i + 1;
  JsonArray favs = doc["favs"].to<JsonArray>();
  for (uint8_t i = 0; i < fav_n; i++) {
    JsonObject o = favs.add<JsonObject>();
    if (this->fav_id_(i) != 0)
      put_team(o, this->fav_league_(i), this->fav_id_(i));
  }
  doc["favmax"] = FAV_MAX;
  // Each playlist's teams as places in favs.
  JsonArray lists = doc["lists"].to<JsonArray>();
  for (uint8_t n = 1; n <= this->prefs9_.count; n++) {
    JsonObject o = lists.add<JsonObject>();
    o["n"] = this->list_name_(n);
    JsonArray f = o["f"].to<JsonArray>();
    for (uint8_t i = 0; i < FAV_MAX; i++)
      if (this->fav_id_(i) != 0 && ((this->prefs9_.mask[n - 1] >> i) & 1u))
        f.add(i);
  }
  doc["list"] = this->prefs9_.active;
  doc["listmax"] = ::espn::kListMax;
  // The sport rows Playing offers, worked out from the favorites, and the one
  // playing ("" for none; list is then 0).
  JsonArray sports = doc["sports"].to<JsonArray>();
  uint8_t rows = this->sport_rows_();
  for (uint8_t row = 1; row <= ::espn::kSportCount; row++)
    if (::espn::sport_row_has(rows, row))
      sports.add(::espn::kSportKeys[row - 1]);
  uint8_t sport_row = this->sport_playing_();
  doc["sport"] = sport_row != 0 ? ::espn::kSportKeys[sport_row - 1] : "";
  doc["today"] = this->today_only();
  doc["jump"] = this->jump_to_action();
  doc["banner"] = this->score_banner();
  doc["favtake"] = this->favorites_take_over();
  JsonArray live = doc["live"].to<JsonArray>();
  for (League l : this->live_leagues_())
    live.add(::espn::league_key(l));
  doc["live_auto"] = this->prefs7_.live_mask == 0;
  doc["tz"] = this->prefs_.tz_index;
  doc["tz_name"] = ::espn::kTimezones[this->prefs_.tz_index].name;
  doc["tz_iana"] = ::espn::kTimezones[this->prefs_.tz_index].iana;
  doc["tz_auto"] = this->tz_auto();
  doc["down"] = this->ticker_down_distance();
  doc["count"] = this->ticker_baseball_count();
  doc["match"] = this->ticker_soccer_stats();
  doc["play"] = this->ticker_last_play();
  doc["odds"] = this->ticker_odds();
  doc["leaders"] = this->ticker_leaders();
  doc["opp"] = this->opponent_splashes();
  doc["best"] = this->best_game();
  doc["bootaddr"] = this->show_boot_address();
  doc["countdown"] = this->countdown_on();
  doc["top25"] = this->top25_on();
  doc["news"] = this->news_on();
  {
    // The owner's pictures (picture.h), and how big the next upload can be.
    ::espn::PicInfo slots[::espn::kPicSlots];
    this->pictures_.slots(slots);
    JsonArray pics = doc["pics"].to<JsonArray>();
    for (int s = 0; s < ::espn::kPicSlots; s++) {
      if (slots[s].kind == (uint8_t) ::espn::PicKind::NONE)
        continue;
      JsonObject p = pics.add<JsonObject>();
      p["s"] = s + 1;
      p["k"] = ::espn::pic_kind_name((::espn::PicKind) slots[s].kind);
      p["w"] = slots[s].w;
      p["h"] = slots[s].h;
      p["f"] = slots[s].frames;
      p["b"] = slots[s].bytes;
      p["id"] = slots[s].id;
    }
    doc["picroom"] = (uint32_t) this->pictures_.room();
  }
  doc["cdname"] = (const char *) this->countdown_.name;
  doc["cddate"] = ::espn::countdown_date_text(this->countdown_.ymd);
  doc["gameleaders"] = this->game_leaders_on();
  // The Between games list (cards.h): on is the owner's switch, ok whether
  // the card has something to show, and hint what it needs while ok is
  // false ("" for a card that always has something).
  JsonArray cards = doc["cards"].to<JsonArray>();
  for (const auto &r : ::espn::kCards) {
    JsonObject c = cards.add<JsonObject>();
    c["id"] = r.id;
    c["name"] = r.name;
    c["on"] = this->card_on(r.card);
    c["ok"] = this->card_ok_(r.card);
    c["hint"] = r.hint;
  }
  // The night schedule (night.h). night_now is what it is doing: "" (not
  // now), "dim", "off", "game" (its hours, held off by a live game) or
  // "skipped" (the owner turned it off for the rest of the night).
  this->load_night_();
  doc["night"] = this->night_.on != 0;
  doc["nightstart"] = ::espn::night_time_text(this->night_.start);
  doc["nightend"] = ::espn::night_time_text(this->night_.end);
  doc["nightscreen"] = this->night_.screen_off == 0;
  doc["nightlevel"] = this->night_.level;
  doc["night_now"] = ::espn::night_word(this->night_now_);
#if defined(USE_UPDATE) && !defined(GAMEDAY_BAR)
  // Left out where nothing can be installed (the demo build) or there is no
  // choice (the bar build), so the page and the app hide the switch.
  doc["autoupd"] = this->auto_update();
#endif
#ifdef GAMEDAY_BETA_CHANNEL
  // Left out in builds with no beta channel (bar, demo), which hides the switch.
  doc["beta"] = this->follow_beta();
#endif
#ifdef USE_UPDATE
  // A release that rolled back here, after a manual install or an automatic
  // one; left out when there is none.
  if (this->update_mark_.failed[0] != '\0')
    doc["update_failed"] = (const char *) this->update_mark_.failed;
#endif
#if defined(USE_UPDATE) && !defined(GAMEDAY_BAR)
  // The release waiting is marked important (update_nudge.h); left out
  // otherwise, and with nothing waiting.
  if (this->update_important_)
    doc["update_important"] = true;
#endif
#ifdef GAMEDAY_BAR
  // The device page shows the bar beta note and a "Something wrong?" link.
  doc["bar"]["report"] = this->report_email_;
#endif
  // The message card (message.h). msgread is the list's last read, left out
  // with no address or before the first: its HTTP status (0 for no answer)
  // and, once a read got to the end of the file, how many entries it holds.
  doc["msg"] = this->msg_text_;
  doc["msgurl"] = this->msg_url_;
  doc["msgevery"] = this->msg_every_;
  if (!this->msg_url_.empty() && this->msg_status_ >= 0) {
    JsonObject mr = doc["msgread"].to<JsonObject>();
    mr["status"] = this->msg_status_;
    if (this->msg_total_ > 0 || this->msg_eof_)
      mr["entries"] = this->msg_total_;
  }
  // The weather card (weather.h): the town as typed and the switch; wx is
  // left out until a town is set. place is "" until the lookup finds one
  // (with lookup 200 and no place, Open-Meteo knows no such town); lookup
  // and read are the last HTTP statuses (0 no answer, -1 none yet or
  // waiting); read_s the seconds since the last good forecast read, and the
  // forecast itself, left out before the first. wait_s, while a failed read
  // or lookup waits to try again, is how long until it does; held says a
  // game the panel follows is live, so the read waits for it to end.
  doc["town"] = (const char *) this->wx_.town;
  doc["wxon"] = this->weather_on();
  if (this->wx_.town[0] != '\0') {
    JsonObject wx = doc["wx"].to<JsonObject>();
    wx["place"] = (const char *) this->wx_.place;
    wx["lat"] = this->wx_.lat;
    wx["lon"] = this->wx_.lon;
    wx["lookup"] = this->wx_lookup_status_;
    wx["read"] = this->wx_status_;
    if (this->wx_fail_ms_ != 0) {
      uint32_t since = millis() - this->wx_fail_ms_;
      wx["wait_s"] = since < this->wx_backoff_ms_ ? (this->wx_backoff_ms_ - since) / 1000 : 0;
    }
    if (this->weather_on() && this->followed_game_live_())
      wx["held"] = true;
    if (this->wx_forecast_.valid) {
      const ::espn::WxForecast &f = this->wx_forecast_;
      wx["read_s"] = (millis() - this->wx_read_ms_) / 1000;
      wx["temp"] = f.temp;
      wx["hi"] = f.today.hi;
      wx["lo"] = f.today.lo;
      wx["code"] = f.code;
      wx["cond"] = ::espn::wx_words(f.code, true);
      wx["night"] = f.night;
      JsonArray days = wx["days"].to<JsonArray>();
      for (const auto &n : f.next) {
        JsonObject d = days.add<JsonObject>();
        d["d"] = ::espn::weekday_text(n.wday);
        d["hi"] = n.hi;
        d["lo"] = n.lo;
        d["code"] = n.code;
      }
    }
  }
  // The game events (ha_events.h): the webhook's address, "" for none, the
  // reminder's lead in minutes, and with an address, how the posts went:
  // the last one's HTTP status (0 for no answer), the posts that got a 2xx
  // and the events dropped because the queue was full.
  doc["hook"] = this->hook_url_;
  doc["pregame"] = this->pregame_min_;
  if (!this->hook_url_.empty() && this->hook_status_ >= 0) {
    JsonObject hr = doc["hookpost"].to<JsonObject>();
    hr["status"] = this->hook_status_;
    hr["sent"] = this->hook_sent_;
    hr["dropped"] = this->hook_dropped_;
  }
  doc["misses"] = this->misses_;
  // The Wi-Fi wants a sign-in (captive.h); the page and the app say how to
  // get the panel onto a network that works.
  doc["captive"] = this->captive_;
  doc["espn_kb"] = (uint32_t) (this->espn_bytes() / 1000);
  doc["espn_reads"] = this->espn_requests();
  doc["stale_s"] = this->stale_seconds_();
  // The clock came from ESPN's Date header (to within a second or two) and no
  // time server has answered yet; left out once one has.
  if (this->clock_approx_)
    doc["clock_approx"] = true;
  doc["fallback"] = this->live_fallback_;
  doc["recap"] = this->recap_on_;
  // Favorites take over has the board on this favorite's game; left out otherwise.
  if (this->live_mode_() && this->live_fav_ >= 0 && this->live_fav_ < (int) this->fav_.size()) {
    JsonObject favlock = doc["favlock"].to<JsonObject>();
    put_team(favlock, (uint8_t) this->fav_[this->live_fav_].team->league, this->fav_[this->live_fav_].team->espn_id);
  }

  JsonObject game = doc["game"].to<JsonObject>();
  game["s"] = g.valid ? ::espn::state_name(g.state) : "NOT_FOUND";
  // The board's reason for being empty ("Can't reach ESPN"), so the page and
  // the app say what the panel says. Left out when the board has its default.
  std::string why = this->no_game_why_();
  if (!why.empty())
    game["why"] = why;
  const ::espn::Team *first = this->fallback_team_();
  uint8_t league = this->live_mode_() && this->schedule_.valid ? this->schedule_.league
                   : first != nullptr                         ? (uint8_t) first->league
                                                              : (uint8_t) League::NFL;
  if (this->favorites_mode_() && this->fav_shown_ >= 0)
    league = (uint8_t) this->fav_[this->fav_shown_].team->league;
  if (this->recap_on_ && this->recap_step_ < this->recap_cards_.size() &&
      (this->recap_cards_[this->recap_step_].kind == ::espn::RecapKind::FINAL ||
       this->recap_cards_[this->recap_step_].kind == ::espn::RecapKind::LATER))
    league = this->recap_days_[this->recap_cards_[this->recap_step_].day].league;
#ifdef GAMEDAY_DEMO
  if (this->demo_playing_())
    league = this->demo_league_();
#endif
  game["l"] = ::espn::league_key((League) league);
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
  if (g.time_tbd)
    game["tbd"] = true;
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
    doc["clock"] = f->clock;
    doc["splash"] = f->splash_text;
    snprintf(color, sizeof(color), "%06X", (unsigned) f->splash_color);
    doc["splash_color"] = color;
  }
  game["id"] = g.event_id;
  // The panel holds this game's leaders (game_leaders.h), from its last
  // summary read at halftime or the final.
  game["leaders"] = this->leaders_.valid && this->leaders_.any() && this->leaders_.event_id == g.event_id;
  if (g.sport != ::espn::Sport::FOOTBALL) {
    game["sport"] = (int) g.sport;
    if (f != nullptr) {
      game["sit"] = f->situation;
      game["outs"] = f->outs;
      game["bases"] = f->bases;
    }
  }
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
      if (!e.game.valid) {
        // Only a read that named no game means none is scheduled; before the
        // first answer the row has nothing to say yet.
        if (e.sched.valid && e.sched.event_id.empty())
          o["none"] = true;
        continue;
      }
      o["id"] = e.game.event_id;
      o["s"] = ::espn::state_name(e.game.state);
      o["kick"] = e.game.kickoff_epoch;
      if (e.game.time_tbd)
        o["tbd"] = true;
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
    if (u.time_tbd)
      o["tbd"] = true;
    o["oi"] = u.opp_id;
    o["oa"] = u.opp_abbr;
    o["on"] = u.opp_name;
    o["home"] = u.home;
    o["neutral"] = u.neutral;
    o["tv"] = u.tv;
  }
  PsString out;
  serializeJson(doc, out);
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  this->state_json_ = std::move(out);
}

PsString GamedayComponent::current_state_json_() {
  std::lock_guard<std::mutex> lock(this->state_mutex_);
  return this->state_json_;
}

std::string GamedayComponent::health_json_() {
  ::espn::Health h{};
  h.uptime_s = esp_timer_get_time() / 1000000;
  h.start = this->start_reason_;
  h.boots = boot_log.boots;
  h.fault = (::espn::Restart) boot_log.fault;
  h.fault_ago = ::espn::boot_log_fault_ago(boot_log, (int64_t) ::time(nullptr), h.uptime_s);
  h.heap_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024.0f;
  h.block_kb = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024.0f;
  h.psram_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024.0f;
#ifdef USE_WIFI
  auto *w = wifi::global_wifi_component;
  h.wifi = w != nullptr && w->is_connected();
  if (h.wifi)
    h.rssi = w->wifi_rssi();
#endif
  const uint32_t at = this->espn_answer_ms_.load();
  h.espn_status = this->espn_status_.load();
  h.espn_ago = at == 0 ? -1 : (int64_t) ((millis() - at) / 1000);
#ifdef ESPHOME_PROJECT_VERSION
  h.version = ESPHOME_PROJECT_VERSION;
#else
  h.version = "";
#endif
  h.built = (int64_t) App.get_build_time();
#if defined(GAMEDAY_BAR)
  h.channel = "bar";
#elif defined(GAMEDAY_BETA_CHANNEL)
  h.channel = this->follow_beta() ? "beta" : "stable";
#elif defined(USE_UPDATE)
  h.channel = "stable";
#else
  h.channel = "none";
#endif
  // Written into the reply's own heap buffer: this runs on the web server
  // task, whose stack is small.
  static constexpr size_t HEALTH_MAX = 384, GUARD_MAX = 96;
  std::string out(HEALTH_MAX + GUARD_MAX, '\0');
  int n = ::espn::health_json(&out[0], HEALTH_MAX, h);
  if (n <= 0 || n >= (int) HEALTH_MAX)
    return "{}";
  // The heat and power keys go in over its closing brace (guard.h)
  int g = ::espn::guard_json_keys(&out[n - 1], GUARD_MAX, this->heat_.last_c, this->heat_.hot,
                                  this->power_log_.brownouts, this->power_dipped_);
  if (g <= 0 || g >= (int) GUARD_MAX) {
    out[n - 1] = '}';
    out.resize(n);
    return out;
  }
  out.resize(n - 1 + g);
  out += '}';
  return out;
}

// Every loop for the link going down or up, every WIFI_SAMPLE_INTERVAL for
// the signal.
void GamedayComponent::wifi_tick_() {
#ifdef USE_WIFI
  auto *w = wifi::global_wifi_component;
  if (w == nullptr)
    return;
  const bool up = w->is_connected();
  const uint32_t now_ms = millis();
  const bool sample = now_ms - this->wifi_sample_ms_ >= WIFI_SAMPLE_INTERVAL;
  if (up == this->wifi_up_ && !sample)
    return;
  const uint32_t now_s = (uint32_t) (esp_timer_get_time() / 1000000);
  std::lock_guard<std::mutex> lock(this->wifi_mutex_);
  if (up != this->wifi_up_) {
    this->wifi_up_ = up;
    // The page shows the signal as soon as the link is back, not at the next sample.
    this->wifi_rssi_ = up ? w->wifi_rssi() : 0;
    if (up) {
      this->wifi_hist_.up(now_s);
      this->wifi_reason_seen_ = wifi_reason_seq.load();
    } else {
      // A link lost with no disconnect event (the IP went) has no cause.
      const int reason = wifi_reason_seq.load() != this->wifi_reason_seen_ ? wifi_reason.load() : 0;
      const ::espn::WifiCause cause = ::espn::wifi_cause(reason);
      this->wifi_hist_.down(now_s, cause);
      ESP_LOGI(TAG, "Wi-Fi lost: %s (reason %d), %u drop(s) since start", ::espn::wifi_cause_key(cause), reason,
               (unsigned) this->wifi_hist_.total());
    }
  }
  if (sample) {
    this->wifi_sample_ms_ = now_ms;
    this->wifi_rssi_ = up ? w->wifi_rssi() : 0;
    this->wifi_hist_.sample(now_s, up, this->wifi_rssi_);
  }
#endif
}

std::string GamedayComponent::wifi_json_() {
  const uint32_t now_s = (uint32_t) (esp_timer_get_time() / 1000000);
  std::lock_guard<std::mutex> lock(this->wifi_mutex_);
  return this->wifi_hist_.json(now_s, this->wifi_up_, this->wifi_rssi_);
}

// ---- banner and message card (message.h) -------------------------------------

// Each line through panel_text() on its own, which would make the line breaks
// spaces.
static std::string board_lines(const std::string &text) {
  std::string out;
  size_t p = 0;
  while (p <= text.size()) {
    size_t nl = text.find('\n', p);
    if (nl == std::string::npos)
      nl = text.size();
    if (!out.empty())
      out += '\n';
    out += panel_text(text.substr(p, nl - p));
    p = nl + 1;
  }
  return out;
}

void GamedayComponent::say(const std::string &text, int secs) {
  this->say_text_ = board_lines(::espn::message_clean(text, ::espn::kMsgTextMax, true));
  this->say_secs_ = ::espn::clamp_say_secs(secs);
  if (this->say_text_.empty())
    ESP_LOGI(TAG, "Banner down");
  else
    ESP_LOGI(TAG, "Banner for %ds: %s", this->say_secs_, this->say_text_.c_str());
  this->fire_action_("say");  // firmware/pages/message.yaml
}

void GamedayComponent::load_msg_() {
  this->msg_pref_ = global_preferences->make_preference<PrefsMsg>(fnv1_hash("gameday_msg_v1"));
  PrefsMsg p{};
  if (!this->msg_pref_.load(&p))
    return;  // nothing set: no card
  p.text[sizeof(p.text) - 1] = '\0';
  p.url[sizeof(p.url) - 1] = '\0';
  this->msg_text_ = p.text;
  this->msg_url_ = ::espn::msg_url_ok(p.url) ? p.url : "";
  this->msg_every_ = (uint8_t) ::espn::clamp_msg_every(p.every);
  this->msg_reset_();
}

void GamedayComponent::load_nick_() {
  this->nick_pref_ = global_preferences->make_preference<PrefsNick>(fnv1_hash("gameday_nick_v1"));
  PrefsNick p{};
  if (!this->nick_pref_.load(&p))
    return;  // never named
  p.text[sizeof(p.text) - 1] = '\0';
  this->nick_ = ::espn::message_clean(::espn::utf8_only(p.text), ::espn::kNickMax, false);
}

void GamedayComponent::set_nick_(const std::string &nick) {
  if (nick == this->nick_)
    return;
  this->nick_ = nick;
  PrefsNick p{};
  strncpy(p.text, nick.c_str(), sizeof(p.text) - 1);
  this->nick_pref_.save(&p);
  this->sync_prefs_();
  ESP_LOGI(TAG, "Nickname: %s", nick.empty() ? "(none)" : nick.c_str());
  this->rebuild_state_(&this->last_fields_);
}

// ---- Game events (ha_events.h) and the webhook (hook.h) --------------------

void GamedayComponent::load_hook_() {
  this->hook_pref_ = global_preferences->make_preference<PrefsHook>(fnv1_hash("gameday_hook_v1"));
  PrefsHook p{};
  if (!this->hook_pref_.load(&p))
    return;  // never set: no webhook, the default reminder
  p.url[sizeof(p.url) - 1] = '\0';
  this->hook_url_ = ::espn::hook_url_ok(p.url) ? p.url : "";
  this->pregame_min_ = (uint8_t) ::espn::clamp_pregame_minutes(p.pregame);
}

void GamedayComponent::set_hook_(const std::string *url, int pregame) {
  bool changed = false;
  if (url != nullptr && *url != this->hook_url_) {
    this->hook_url_ = *url;
    this->hook_queue_.clear();  // nothing meant for the old address goes to the new one
    this->hook_status_ = -1;
    changed = true;
  }
  if (pregame >= 0 && pregame != this->pregame_min_) {
    this->pregame_min_ = (uint8_t) pregame;
    changed = true;
  }
  if (!changed)
    return;
  PrefsHook p{};
  strncpy(p.url, this->hook_url_.c_str(), sizeof(p.url) - 1);
  p.pregame = this->pregame_min_;
  this->hook_pref_.save(&p);
  this->sync_prefs_();
  ESP_LOGI(TAG, "Game events: webhook %s, reminder %u min before the start",
           this->hook_url_.empty() ? "off" : this->hook_url_.c_str(), (unsigned) this->pregame_min_);
  this->rebuild_state_(&this->last_fields_);
}

std::string GamedayComponent::ha_league_() const {
#ifdef GAMEDAY_DEMO
  if (this->demo_playing_())
    return ::espn::league_key((League) this->demo_league_());
#endif
  if (this->favorites_mode_() && !this->fav_.empty()) {
    int i = this->fav_locked_ ? this->fav_shown_ : ::espn::followed_favorite(this->fav_games_());
    if (i < 0)
      i = this->fav_shown_;
    if (i >= 0 && i < (int) this->fav_.size())
      return ::espn::league_key(this->fav_[i].team->league);
  }
  if (this->live_mode_() && this->schedule_.valid)
    return ::espn::league_key((League) this->schedule_.league);
  const ::espn::Team *first = this->fallback_team_();
  return first != nullptr ? ::espn::league_key(first->league) : "";
}

// From every board update and every so often from loop(): the reminder
// before the start goes by the clock, not by a read.
void GamedayComponent::ha_track_() {
  // The finals rotation turns through other games' cards, none of them followed.
  if (this->recap_on_ && !this->demo_playing_())
    return;
  ::espn::HaContext ctx;
  ctx.neutral = this->live_mode_() && !this->live_fallback_ && !this->demo_playing_();
  ctx.league = this->ha_league_();
  ctx.now_epoch = this->clock_set_() ? (int64_t) this->time_->timestamp_now() : 0;
  ctx.pregame_minutes = this->pregame_min_;
  ctx.mono_s = millis() / 1000;
#ifdef GAMEDAY_DEMO
  // The scripted game has no ESPN id; each run is a game of its own, so it
  // starts, scores and ends like a real one, once. Demo builds are never
  // released, so these reach only a bench or a show floor's automations.
  if (this->demo_playing_()) {
    GameSnapshot g = this->game_;
    g.event_id = "demo-" + std::to_string(this->demo_run_);
    for (const auto &e : this->ha_tracker_.update(g, ctx))
      this->send_game_event_(e);
    return;
  }
#endif
  for (const auto &e : this->ha_tracker_.update(this->ha_game_(), ctx))
    this->send_game_event_(e);
}

void GamedayComponent::send_game_event_(const ::espn::HaEvent &e) {
  const char *kind = ::espn::ha_kind_name(e.kind);
  const char *what = e.kind == ::espn::HaKind::LEAD     ? e.change.c_str()
                     : e.kind == ::espn::HaKind::PERIOD ? e.period_name.c_str()
                                                        : e.play.c_str();
  ESP_LOGI(TAG, "Game event %s: %s %s %d-%d", kind, e.team.c_str(), what, e.team_score, e.opponent_score);
  for (auto &cb : this->ha_event_callbacks_)
    cb(e);
  const std::string panel = this->hostname();
#ifdef GAMEDAY_HA_EVENTS
  // Home Assistant takes events from any ESPHome device; only actions need
  // "Allow the device to perform Home Assistant actions". Nothing is sent,
  // and nothing logged, with no Home Assistant connected.
  auto *api = api::global_api_server;
  if (api != nullptr && api->is_connected()) {
    ::espn::HaFields fields = ::espn::ha_event_fields(e);
    fields.emplace_back("panel", panel);
    const std::string name = std::string("esphome.gameday_") + kind;
    api::HomeassistantActionRequest req;
    req.service = StringRef(name);
    req.is_event = true;
    req.data.init(fields.size());
    for (const auto &kv : fields) {
      auto &m = req.data.emplace_back();
      m.key = StringRef(kv.first);
      m.value = StringRef(kv.second);
    }
    api->send_homeassistant_action(req);
  }
#endif
  if (!this->hook_url_.empty())
    this->queue_hook_(::espn::ha_webhook_json(e, panel));
}

// The device page's "Send a test event": a made-up score for the game Home
// Assistant follows, or for favorite #1 when there is none, so the owner
// can watch their automation or webhook run.
void GamedayComponent::send_test_event_() {
  ::espn::HaEvent e;
  e.kind = ::espn::HaKind::TEST;
  e.league = this->ha_league_();
  const GameSnapshot *g = &this->ha_game_();
  if ((!g->valid || g->team_abbr.empty()) && !this->fav_.empty() && this->fav_[0].game.valid)
    g = &this->fav_[0].game;
  if (g->valid && !g->team_abbr.empty()) {
    e.event_id = g->event_id;
    e.team = g->team_abbr;
    e.opponent = g->opp_abbr;
    e.team_rgb = ::espn::ha_rgb(g->team_color);
    e.opponent_rgb = ::espn::ha_rgb(g->opp_color);
    e.sport = ::espn::ha_sport(g->sport);
    e.team_score = g->state == GameState::PRE ? 0 : g->team_score;
    e.opponent_score = g->state == GameState::PRE ? 0 : g->opp_score;
    switch (g->sport) {
      case ::espn::Sport::BASEBALL:
        e.play = "home_run";
        e.points = 1;
        break;
      case ::espn::Sport::HOCKEY:
      case ::espn::Sport::SOCCER:
        e.play = "goal";
        e.points = 1;
        break;
      case ::espn::Sport::BASKETBALL:
        e.play = "score";
        e.points = 3;
        break;
      default:
        e.play = "touchdown";
        e.points = 7;
        break;
    }
  } else {
    const ::espn::Team *first = this->fallback_team_();
    e.team = first != nullptr ? first->abbr : "HOME";
    e.opponent = "AWAY";
    e.sport = ::espn::ha_sport(first != nullptr ? ::espn::league_sport(first->league) : ::espn::Sport::FOOTBALL);
    e.play = "touchdown";
    e.points = 7;
  }
  e.team_score += e.points;
  e.my_team = true;
  this->send_game_event_(e);
}

void GamedayComponent::queue_hook_(std::string body) {
  if (this->hook_queue_.size() >= kHookQueueMax) {
    this->hook_queue_.erase(this->hook_queue_.begin());
    this->hook_dropped_++;
  }
  this->hook_queue_.push_back(std::move(body));
}

void GamedayComponent::hook_tick_() {
  if (this->hook_running_) {
    if (!this->hook_task_done_)
      return;
    this->hook_running_ = false;
    this->hook_status_ = this->hook_task_status_;
    unsigned stack_left = (unsigned) this->hook_task_stack_.load();
    if (this->hook_status_ >= 200 && this->hook_status_ < 300) {
      this->hook_sent_++;
      ESP_LOGD(TAG, "Webhook: HTTP %d (%u bytes of the post's stack unused)", this->hook_status_, stack_left);
    } else if (this->hook_status_ == 0) {
      ESP_LOGW(TAG, "Webhook: no answer from %s (%u bytes of the post's stack unused)", this->hook_task_url_.c_str(),
               stack_left);
    } else {
      ESP_LOGW(TAG, "Webhook: HTTP %d from %s (%u bytes of the post's stack unused)", this->hook_status_,
               this->hook_task_url_.c_str(), stack_left);
    }
    std::string().swap(this->hook_task_body_);
    this->rebuild_state_(&this->last_fields_);  // the page shows how the last post went
  }
  if (this->hook_queue_.empty())
    return;
  if (this->hook_url_.empty()) {
    this->hook_queue_.clear();
    return;
  }
  if (!network::is_connected())
    return;  // waits for Wi-Fi; the queue's cap keeps it short
  this->hook_task_url_ = this->hook_url_;
  this->hook_task_body_ = std::move(this->hook_queue_.front());
  this->hook_queue_.erase(this->hook_queue_.begin());
  this->hook_task_done_ = false;
  this->hook_running_ = true;
  // Plain HTTP and no response body: the Wi-Fi check's stack, freed when
  // the post is done.
  if (xTaskCreate(&GamedayComponent::hook_task_, "gameday_hook", 6144, this, 1, nullptr) != pdPASS) {
    ESP_LOGW(TAG, "Webhook: could not start the post (largest free internal block %u bytes)",
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    this->hook_running_ = false;
    this->hook_dropped_++;
  }
}

// The post itself, on its own task: one try, three seconds at most to
// connect and three for the answer, whose body is never read.
void GamedayComponent::hook_task_(void *arg) {
  auto *self = static_cast<GamedayComponent *>(arg);
  std::string url = self->hook_task_url_;
  std::string host;
#ifdef USE_MDNS
  // homeassistant.local and the like: lwIP would hand the name to the
  // router's DNS, which doesn't know it. Ask mDNS, post to the address it
  // gives, and keep the name in the Host header.
  std::string name = ::espn::hook_mdns_name(url);
  if (!name.empty()) {
    esp_ip4_addr_t addr{};
    size_t b, e;
    if (mdns_query_a(name.c_str(), 2000, &addr) == ESP_OK && ::espn::hook_host_span(url, b, e)) {
      char ip[16];
      snprintf(ip, sizeof(ip), IPSTR, IP2STR(&addr));
      host = url.substr(b, e - b);
      url.replace(b, e - b, ip);
    }
  }
#endif
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = 3000;
  cfg.disable_auto_redirect = true;
  cfg.user_agent = "GameDayScoreboard";
  cfg.buffer_size = 512;
  cfg.buffer_size_tx = 512;
  int status = 0;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client != nullptr) {
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (!host.empty())
      esp_http_client_set_header(client, "Host", host.c_str());
    esp_http_client_set_post_field(client, self->hook_task_body_.data(), (int) self->hook_task_body_.size());
    if (esp_http_client_perform(client) == ESP_OK)
      status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
  }
  self->hook_task_status_ = status;
  self->hook_task_stack_ = (uint32_t) uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
  self->hook_task_done_ = true;
  vTaskDelete(nullptr);
}

// Any of the three; a null pointer or a negative every leaves that one as it is.
void GamedayComponent::set_msg_(const std::string *text, const std::string *url, int every) {
  bool reset = false;
  if (text != nullptr && *text != this->msg_text_) {
    this->msg_text_ = *text;
    reset = true;
  }
  if (url != nullptr && *url != this->msg_url_) {
    this->msg_url_ = *url;
    reset = true;
  }
  if (every >= 0 && every != this->msg_every_) {
    this->msg_every_ = (uint8_t) every;
    reset = true;
  }
  if (!reset)
    return;
  PrefsMsg p{};
  strncpy(p.text, this->msg_text_.c_str(), sizeof(p.text) - 1);
  strncpy(p.url, this->msg_url_.c_str(), sizeof(p.url) - 1);
  p.every = this->msg_every_;
  this->msg_pref_.save(&p);
  this->sync_prefs_();
  ESP_LOGI(TAG, "Message card: %s, every %u min", !this->msg_url_.empty() ? this->msg_url_.c_str()
                                                  : !this->msg_text_.empty() ? "own text" : "off",
           (unsigned) this->msg_every_);
  this->msg_reset_();
  this->rebuild_state_(&this->last_fields_);
}

// The list starts over from the top, and the card comes up at the next
// chance between games rather than after a full wait.
void GamedayComponent::msg_reset_() {
  this->msg_list_.clear();
  this->msg_list_.shrink_to_fit();
  this->msg_from_ = 0;
  this->msg_n_ = 0;
  this->msg_eof_ = false;
  this->msg_list_ms_ = 0;
  this->msg_pos_ = 0;
  this->msg_read_due_ = false;
  this->msg_fail_ms_ = 0;
  this->msg_status_ = -1;
  this->msg_total_ = 0;
  if (this->msg_url_.empty() && !this->msg_text_.empty()) {
    // The saved text is the whole list.
    this->msg_list_.assign(this->msg_text_.data(), this->msg_text_.size());
    this->msg_n_ = ::espn::msg_count(this->msg_list_);
    this->msg_eof_ = true;
    this->msg_total_ = this->msg_n_;
  }
  if (this->msg_shown_)
    this->msg_show_("");
  this->msg_since_ms_ = millis() - (uint32_t) this->msg_every_ * 60u * 1000u;
}

void GamedayComponent::msg_show_(const std::string &page) {
  if (page == this->msg_card_) {
    // The same page again (a one-page list holding the board): leave it be,
    // so a page that rolls isn't sent back to its top.
    this->msg_since_ms_ = millis();
    return;
  }
  this->msg_card_ = page;
  this->msg_shown_ = !page.empty();
  this->msg_since_ms_ = millis();
  if (!page.empty())
    this->msg_roll_ms_ = 0;  // the page says how long it rolls when it goes up
  this->fire_action_("msg");  // gd_msg_card in firmware/pages/gameday-live.yaml
}

// Not over a live game, the demo, a splash or the panel's trouble line.
bool GamedayComponent::card_between_(uint32_t now) const {
  const bool trouble = ::espn::msg_trouble(this->misses_, this->captive_,
                                           ::espn::clock_late(this->clock_set_(), this->since_join_ms_()));
  return !this->demo_playing_() && this->last_fields_.game_state != "IN" && !trouble &&
         (this->splash_ms_ == 0 || now - this->splash_ms_ >= ::espn::kMsgSplashGapMs);
}

void GamedayComponent::msg_tick_() {
  const uint32_t now = millis();
  if (now - this->msg_tick_ms_ < 250)
    return;
  this->msg_tick_ms_ = now;
  const bool on = this->card_on(::espn::Card::MESSAGE) && this->card_ok_(::espn::Card::MESSAGE);
  // Nor over the news, Top 25 or picture card: the message waits for them to come down.
  const bool between = this->card_between_(now) && !this->news_shown_ &&
                       now - this->news_since_ms_ >= ::espn::kMsgPageMs && !this->top25_shown_ &&
                       !this->pic_shown_ && now - this->pic_since_ms_ >= ::espn::kMsgPageMs;
  const uint32_t page_ms = std::max(::espn::kMsgPageMs, this->msg_roll_ms_);
  ::espn::MsgTurn turn =
      ::espn::msg_turn(on, between, this->msg_shown_, now - this->msg_since_ms_, this->msg_every_, page_ms);
  if (turn == ::espn::MsgTurn::NONE)
    return;
  if (turn == ::espn::MsgTurn::HIDE) {
    this->msg_show_("");
    return;
  }
  const bool url = !this->msg_url_.empty();
  const bool may_read = url && (this->msg_fail_ms_ == 0 || now - this->msg_fail_ms_ >= ::espn::kMsgRetryMs);
  const bool stale = may_read && this->msg_list_ms_ != 0 && now - this->msg_list_ms_ >= ::espn::kMsgRefreshMs;
  ::espn::MsgPage page = ::espn::msg_next_page(this->msg_pos_, this->msg_from_, this->msg_n_, this->msg_eof_, stale,
                                               ::espn::kMsgPageLines);
  if (page.read) {
    // The card waits for the read; a page already up stays until it lands.
    if (may_read && !(this->busy_ && this->job_.need_msg)) {
      this->msg_read_due_ = true;
      this->msg_read_from_ = page.next;
    } else if (!may_read && this->msg_shown_) {
      this->msg_show_("");  // the last read failed: nothing new until the retry
    }
    return;
  }
  if (page.count == 0) {
    if (this->msg_shown_)
      this->msg_show_("");
    return;
  }
  this->msg_pos_ = page.next;
  this->msg_show_(board_lines(::espn::msg_entries(this->msg_list_, page.first - this->msg_from_, page.count)));
}

void GamedayComponent::start_msg_job_() {
  this->msg_read_due_ = false;
  this->job_ = Job{};
  this->job_.generation = this->generation_;
  this->job_.need_msg = true;
  this->job_.msg_url = this->msg_url_;
  this->job_.msg_from = this->msg_read_from_;
  this->start_worker_();
}

// Worker task. Keeps entries [msg_from, msg_from + kMsgChunkLines) and stops
// reading as soon as it has them.
void GamedayComponent::run_msg_job_() {
  Job &j = this->job_;
  std::vector<http_request::Header> headers = {{"User-Agent", USER_AGENT}, {"Accept", "text/plain"}};
  auto container = this->http_->get(j.msg_url, headers);
  if (container == nullptr) {
    ESP_LOGW(TAG, "Message list: no answer from %s", j.msg_url.c_str());
    return;
  }
  j.msg_status = container->status_code;
  if (container->status_code != 200) {
    ESP_LOGW(TAG, "Message list: HTTP %d for %s", container->status_code, j.msg_url.c_str());
    container->end();
    return;
  }
  ::espn::MsgReader<PsString> reader(j.msg_from, ::espn::kMsgChunkLines);
  uint8_t buf[512];
  bool more = true;
  bool complete = false;
  const uint32_t started = millis();
  // A server that trickles its file out is cut off well inside the loop's
  // WORKER_DEADLINE, so this task is never written off.
  while (more && reader.bytes() < ::espn::kMsgReadMaxBytes && millis() - started < MSG_READ_LIMIT) {
    int n = container->read(buf, sizeof(buf));
    if (n <= 0) {
      complete = n == 0 && container->is_read_complete();
      break;
    }
    more = reader.feed((const char *) buf, (size_t) n);
  }
  container->end();
  // A file longer than the read allows ends there: the next page is the top.
  if (more && (complete || reader.bytes() >= ::espn::kMsgReadMaxBytes))
    reader.finish();
  else if (more && reader.kept() == 0)
    return;  // cut off with nothing to show: a failed read
  j.msg_ok = true;
  j.msg_eof = reader.eof();
  j.msg_n = reader.kept();
  j.msg_seen = reader.seen();
  j.msg_list = std::move(reader.list());
}

void GamedayComponent::apply_msg_job_() {
  Job &j = this->job_;
  if (j.msg_url != this->msg_url_)
    return;  // the address changed meanwhile; msg_reset_ has started over
  const uint32_t now = millis();
  this->msg_status_ = j.msg_status;
  if (!j.msg_ok) {
    this->msg_fail_ms_ = now == 0 ? 1 : now;
    // Show what is on hand again until the retry.
    this->msg_pos_ = this->msg_from_;
    this->rebuild_state_(&this->last_fields_);
    return;
  }
  this->msg_fail_ms_ = 0;
  this->msg_list_ = std::move(j.msg_list);
  this->msg_from_ = j.msg_from;
  this->msg_n_ = j.msg_n;
  this->msg_eof_ = j.msg_eof;
  this->msg_list_ms_ = now == 0 ? 1 : now;
  this->msg_pos_ = j.msg_from;
  if (j.msg_eof)
    this->msg_total_ = j.msg_seen;
  ESP_LOGI(TAG, "Message list: entries %u to %u%s", (unsigned) j.msg_from + 1, (unsigned) (j.msg_from + j.msg_n),
           j.msg_eof ? ", the end of the file" : "");
  this->rebuild_state_(&this->last_fields_);
}

// ---- The Top 25 card (rankings.h) -------------------------------------------

// A college team among the favorites, or Live games reading a college
// league. A panel with nothing picked yet follows every league, which isn't
// following college, so it gets no card.
uint8_t GamedayComponent::top25_slots_() const {
  if (this->no_team_default())
    return 0;
  uint8_t slots = 0;
  auto add = [&slots](League l) {
    int s = ::espn::top25_slot(l);
    if (s >= 0)
      slots |= (uint8_t) (1u << s);
  };
  for (uint8_t i = 0; i < FAV_MAX; i++)
    if (this->fav_id_(i) != 0)
      add((League) this->fav_league_(i));
  if (this->live_mode_())
    for (League l : this->scan_leagues_())
      add(l);
  return slots;
}

void GamedayComponent::top25_hide_() {
  if (!this->top25_shown_)
    return;
  this->top25_shown_ = false;
  this->top25_head_.clear();
  this->top25_col_[0].clear();
  this->top25_col_[1].clear();
  this->top25_since_ms_ = millis();
  this->fire_action_("top25");  // gd_top25_card in firmware/pages/gameday-live.yaml
}

void GamedayComponent::top25_show_() {
  const League l = ::espn::top25_slot_league(this->top25_slot_);
  const ::espn::Poll &p = this->top25_[this->top25_slot_];
  this->top25_shown_ = true;
  this->top25_head_ = ::espn::top25_header(l, p);
  for (uint32_t c = 0; c < 2; c++)
    this->top25_col_[c] = c < this->top25_cols_ ? ::espn::top25_column(l, p, this->top25_page_, this->top25_cols_, c)
                                                : std::string();
  this->top25_since_ms_ = millis();
  this->fire_action_("top25");
}

void GamedayComponent::top25_tick_() {
  const uint32_t now = millis();
  if (now - this->top25_tick_ms_ < 250)
    return;
  this->top25_tick_ms_ = now;
  const uint8_t slots = this->top25_on() ? this->top25_slots_() : 0;
  // Between games only, as the message card (msg_tick_), and not over it,
  // the news card or the picture card.
  const bool trouble = ::espn::msg_trouble(this->misses_, this->captive_,
                                           ::espn::clock_late(this->clock_set_(), this->since_join_ms_()));
  const bool between = slots != 0 && this->clock_set_() && this->time_ != nullptr && !this->demo_playing_() &&
                       this->last_fields_.game_state != "IN" && !trouble && !this->msg_shown_ && !this->news_shown_ &&
                       !this->pic_shown_ && (this->splash_ms_ == 0 || now - this->splash_ms_ >= ::espn::kMsgSplashGapMs);
  if (!between) {
    this->top25_hide_();
    return;
  }
  // A poll the panel follows that has never been read, or not for a day.
  if (!this->top25_read_due_ && !(this->busy_ && this->job_.need_top25)) {
    for (uint8_t s = 0; s < 2; s++) {
      if (!(slots & (1u << s)))
        continue;
      const bool may_read = this->top25_fail_ms_[s] == 0 || now - this->top25_fail_ms_[s] >= ::espn::kTop25RetryMs;
      if (may_read && (this->top25_read_ms_[s] == 0 || now - this->top25_read_ms_[s] >= ::espn::kTop25ReadMs)) {
        this->top25_read_due_ = true;
        this->top25_read_slot_ = s;
        break;
      }
    }
  }
  const int64_t epoch = (int64_t) this->time_->now().timestamp;
  if (this->top25_shown_) {
    if (now - this->top25_since_ms_ < ::espn::kTop25PageMs)
      return;
    if (this->top25_page_ + 1 < this->top25_pages_ && (slots & (1u << this->top25_slot_)) &&
        ::espn::top25_fresh(this->top25_[this->top25_slot_], epoch)) {
      this->top25_page_++;
      this->top25_show_();
    } else {
      this->top25_hide_();
    }
    return;
  }
  // The first turn comes a minute after the card could first take one.
  if (this->top25_since_ms_ == 0)
    this->top25_since_ms_ = (now - ::espn::kTop25EveryMs + 60u * 1000u) | 1u;
  if (now - this->top25_since_ms_ < ::espn::kTop25EveryMs)
    return;
  uint8_t ready = 0;
  for (uint8_t s = 0; s < 2; s++)
    if ((slots & (1u << s)) && ::espn::top25_fresh(this->top25_[s], epoch))
      ready |= (uint8_t) (1u << s);
  const int next = ::espn::top25_next(ready, this->top25_slot_);
  if (next < 0)
    return;
  this->top25_slot_ = next;
  this->top25_page_ = 0;
  this->top25_pages_ = ::espn::top25_pages(this->top25_[next], this->top25_cols_);
  ESP_LOGI(TAG, "Top 25: a turn, %s, %u pages",
           ::espn::top25_header(::espn::top25_slot_league(next), this->top25_[next]).c_str(),
           (unsigned) this->top25_pages_);
  this->top25_show_();
}

void GamedayComponent::start_top25_job_() {
  this->top25_read_due_ = false;
  this->job_ = Job{};
  this->job_.generation = this->generation_;
  this->job_.need_top25 = true;
  this->job_.top25_slot = this->top25_read_slot_;
  this->start_worker_();
}

// Worker task. About 28 KB, streamed through the parse's filter.
void GamedayComponent::run_top25_job_() {
  Job &j = this->job_;
  auto container = this->open_(::espn::rankings_url(::espn::top25_slot_league(j.top25_slot)));
  if (container == nullptr)
    return;
  ContainerReader reader(container, this->espn_bytes_);
  j.top25_ok = ::espn::parse_rankings(reader, j.top25_poll);
  container->end();
  j.top25_bytes = (uint32_t) reader.total();
  if (!j.top25_ok)
    ESP_LOGW(TAG, "Top 25: parse failed after %u bytes", (unsigned) reader.total());
}

void GamedayComponent::apply_top25_job_() {
  Job &j = this->job_;
  const uint8_t s = j.top25_slot;
  const uint32_t now = millis() == 0 ? 1 : millis();
  if (!j.top25_ok) {
    this->top25_fail_ms_[s] = now;  // kTop25RetryMs is longer than any Retry-After ESPN has sent
    return;
  }
  this->top25_fail_ms_[s] = 0;
  this->top25_read_ms_[s] = now;
  // A turn on the old poll ends here; the next turn shows the new one.
  if (this->top25_shown_ && this->top25_slot_ == (int) s)
    this->top25_hide_();
  this->top25_[s] = std::move(j.top25_poll);
  ESP_LOGI(TAG, "Top 25: %s, %u teams (%u bytes)",
           ::espn::top25_header(::espn::top25_slot_league(s), this->top25_[s]).c_str(),
           (unsigned) this->top25_[s].rows.size(), (unsigned) j.top25_bytes);
}

// ---- The news card (news.h) -------------------------------------------------

std::vector<::espn::NewsSource> GamedayComponent::news_sources_() const {
  std::vector<::espn::NewsSource> out;
  if (this->favorites_mode_()) {
    for (const auto &e : this->fav_)
      if (e.team != nullptr && this->fav_in_list_(e.slot - 1))
        out.push_back({e.team->league, e.team->espn_id, e.team->abbr});
    if (!out.empty())
      return out;
  }
  for (League l : this->scan_leagues_())
    out.push_back({l, 0, ""});
  return out;
}

// The headlines go, and the card comes up at the next chance between games.
void GamedayComponent::news_reset_() {
  this->news_list_.clear();
  this->news_list_.shrink_to_fit();
  this->news_n_ = 0;
  this->news_list_ms_ = 0;
  this->news_fail_ms_ = 0;
  this->news_pos_ = 0;
  this->news_read_due_ = false;
  if (this->news_shown_)
    this->news_show_("");
  this->news_since_ms_ = millis() - (uint32_t) ::espn::kNewsEveryMin * 60u * 1000u;
}

void GamedayComponent::news_show_(const std::string &headline) {
  this->news_since_ms_ = millis();
  if (headline == this->news_card_)
    return;
  this->news_card_ = headline;
  this->news_head_ = headline.empty() ? std::string() : this->news_src_head_;
  this->news_shown_ = !headline.empty();
  if (this->news_shown_) {
    this->news_roll_ms_ = 0;  // the page says how long it rolls when it goes up
    ESP_LOGD(TAG, "News card: %s: %s", this->news_head_.c_str(), headline.c_str());
  }
  this->fire_action_("news");  // gd_news_card in firmware/pages/gameday-live.yaml
}

void GamedayComponent::news_tick_() {
  const uint32_t now = millis();
  if (now - this->news_tick_ms_ < 250)
    return;
  this->news_tick_ms_ = now;
  // Nor over the message card (and 10 s after it), the Top 25 card or the
  // picture card (and 10 s after it).
  const bool between = this->card_between_(now) && !this->msg_shown_ &&
                       now - this->msg_since_ms_ >= ::espn::kMsgPageMs && !this->top25_shown_ &&
                       !this->pic_shown_ && now - this->pic_since_ms_ >= ::espn::kMsgPageMs;
  const uint32_t page_ms = std::max(::espn::kMsgPageMs, this->news_roll_ms_);
  ::espn::MsgTurn turn = ::espn::msg_turn(this->news_on(), between, this->news_shown_, now - this->news_since_ms_,
                                          ::espn::kNewsEveryMin, page_ms);
  if (turn == ::espn::MsgTurn::NONE)
    return;
  if (turn != ::espn::MsgTurn::SHOW) {
    this->news_show_("");
    return;
  }
  // The card's turn. It waits for a read on its way.
  if (this->news_read_due_ || (this->busy_ && this->job_.need_news))
    return;
  std::vector<::espn::NewsSource> sources = this->news_sources_();
  const bool followed = std::find(sources.begin(), sources.end(), this->news_src_) != sources.end();
  if (!sources.empty() && ::espn::news_read_due(now, this->news_list_ms_, followed, this->news_fail_ms_)) {
    this->news_read_due_ = true;
    return;
  }
  if (!::espn::news_showable(this->news_n_, followed)) {
    // Nothing to show: look again in half a minute rather than every tick.
    this->news_since_ms_ = now - ((uint32_t) ::espn::kNewsEveryMin * 60u * 1000u - 30u * 1000u);
    return;
  }
  const uint32_t at = this->news_pos_ % this->news_n_;
  this->news_pos_ = (at + 1) % this->news_n_;
  this->news_show_(panel_text(::espn::msg_entries(this->news_list_, at, 1)));
}

void GamedayComponent::start_news_job_() {
  this->news_read_due_ = false;
  ::espn::NewsSource src;
  // The next source in turn: a read that fails moves on to the next one too.
  if (!::espn::news_source(this->news_sources_(), this->news_reads_++, src))
    return;
  const ::espn::LeagueInfo *info = ::espn::league_info(src.league);
  this->job_ = Job{};
  this->job_.generation = this->generation_;
  this->job_.need_news = true;
  this->job_.news_src = src;
  this->job_.news_head = ::espn::news_header(src.abbr, info != nullptr ? info->prefix : "");
  this->start_worker_();
}

// Worker task.
void GamedayComponent::run_news_job_() {
  Job &j = this->job_;
  std::string url = ::espn::news_url(j.news_src.league, j.news_src.id);
  auto container = this->open_(url);
  if (container == nullptr)
    return;
  ContainerReader reader(container, this->espn_bytes_);
  j.news_n = ::espn::parse_news(reader, j.news_list);
  container->end();
  j.news_bytes = (uint32_t) reader.total();
  if (j.news_n < 0)
    ESP_LOGW(TAG, "News: parse failed after %u bytes", (unsigned) j.news_bytes);
}

void GamedayComponent::apply_news_job_() {
  Job &j = this->job_;
  if (!this->news_on())
    return;  // turned off meanwhile
  const uint32_t now = millis();
  if (j.news_n <= 0) {
    // The headlines on hand stay up until the retry, which reads the next
    // source: a feed with nothing in it counts as a failed read.
    if (j.news_n == 0)
      ESP_LOGW(TAG, "News: no headlines for %s", j.news_head.c_str());
    this->news_fail_ms_ = now == 0 ? 1 : now;
    return;
  }
  this->news_fail_ms_ = 0;
  this->news_list_ = std::move(j.news_list);
  this->news_n_ = (uint32_t) j.news_n;
  this->news_src_ = j.news_src;
  this->news_src_head_ = j.news_head;
  this->news_list_ms_ = now == 0 ? 1 : now;
  this->news_pos_ = 0;
  ESP_LOGI(TAG, "News: %u headlines for %s (%u bytes)", (unsigned) this->news_n_, j.news_head.c_str(),
           (unsigned) j.news_bytes);
}

// ---- The picture card (picture.h, picture_card.h) ---------------------------

// slot -1 takes the card down.
void GamedayComponent::picture_show_(int slot) {
  this->pic_since_ms_ = millis();
  if (slot < 0) {
    if (!this->pic_shown_)
      return;
    this->pictures_.hide();
    this->pic_shown_ = false;
  } else {
    this->pic_last_ = slot;
    const bool was = this->pic_shown_;
    // A picture that won't decode waits for the next turn, which tries the next one.
    this->pic_shown_ = this->pictures_.show(slot);
    if (this->pic_shown_)
      ESP_LOGI(TAG, "Picture card: picture %d, %ux%u, up for %u s", slot + 1, this->pictures_.width(),
               this->pictures_.height(), (unsigned) (this->pictures_.page_ms() / 1000));
    if (!this->pic_shown_ && !was)
      return;
  }
  this->fire_action_("picture");  // gd_picture_card in firmware/pages/gameday-live.yaml
}

void GamedayComponent::picture_tick_() {
  const uint32_t now = millis();
  // A GIF's frames keep their own time, checked every loop.
  if (this->pic_shown_ && this->pictures_.tick(now))
    this->fire_action_("picture_frame");
  if (now - this->pic_tick_ms_ < 250)
    return;
  this->pic_tick_ms_ = now;
  ::espn::PicInfo slots[::espn::kPicSlots];
  this->pictures_.slots(slots);
  // Deleted or replaced while it was up.
  if (this->pic_shown_ && slots[this->pictures_.shown_slot()].id != this->pictures_.shown_id()) {
    this->picture_show_(-1);
    return;
  }
  // Nor over the message or news card (or 10 s after either), or the Top 25 card.
  const bool between = this->card_between_(now) && !this->msg_shown_ &&
                       now - this->msg_since_ms_ >= ::espn::kMsgPageMs && !this->news_shown_ &&
                       now - this->news_since_ms_ >= ::espn::kMsgPageMs && !this->top25_shown_;
  const int next = ::espn::pic_next_slot(slots, this->pic_last_);
  const bool on = next >= 0 && this->card_on(::espn::Card::PICTURE);
  ::espn::MsgTurn turn = ::espn::msg_turn(on, between, this->pic_shown_, now - this->pic_since_ms_,
                                          ::espn::kPicEveryMin, this->pictures_.page_ms());
  if (turn == ::espn::MsgTurn::NONE)
    return;
  this->picture_show_(turn == ::espn::MsgTurn::SHOW ? next : -1);
}

// GET ?slot=N: the file as uploaded. POST a PNG or GIF body: keep it in the
// first free slot. POST delete=N: delete that one. Slots count from 1 here.
void GamedayComponent::handle_picture_(AsyncWebServerRequest *request) {
  if (request->method() == HTTP_GET) {
    const int slot = atoi(request->arg("slot").c_str()) - 1;
    ::espn::PicInfo slots[::espn::kPicSlots];
    this->pictures_.slots(slots);
    ::espn::PsVector<uint8_t> file;
    if (slot < 0 || slot >= ::espn::kPicSlots || !this->pictures_.read(slot, file)) {
      request->send(404, "application/json", "{\"error\":\"no picture there\"}");
      return;
    }
    request->send(request->beginResponse(200, ::espn::pic_mime((::espn::PicKind) slots[slot].kind), file.data(),
                                          file.size()));
    return;
  }
  if (request->method() != HTTP_POST) {
    request->send(400, "application/json", "{\"error\":\"GET or POST\"}");
    return;
  }
  if (request->hasParam("delete")) {
    if (!this->pictures_.remove(atoi(request->arg("delete").c_str()) - 1)) {
      request->send(404, "application/json", "{\"error\":\"no picture there\"}");
      return;
    }
    this->defer([this]() { this->rebuild_state_(&this->last_fields_); });
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  PsString body;
  body.swap(this->picture_body_);
  const size_t length = request->contentLength();
  std::string error;
  int slot = -1;
  if (length > ::espn::kPicMaxBytes) {
    error = "That file is too big; a picture can be up to 64 KB.";
  } else if (body.empty() || body.size() != length) {
    // Nothing came, or what came is left from an upload that broke off.
    error = "The upload broke off. Try again.";
  } else {
    error = this->pictures_.add(reinterpret_cast<const uint8_t *>(body.data()), body.size(), slot);
  }
  if (!error.empty()) {
    ESP_LOGW(TAG, "Picture refused: %s", error.c_str());
    // 400, not 413: ESPHome's web server sends any code it has no name for
    // as 500. The texts are ours, with no quotes to escape.
    request->send(400, "application/json", ("{\"error\":\"" + error + "\"}").c_str());
    return;
  }
  // Up next between games, so the owner sees it soon.
  this->defer([this, slot]() {
    this->picture_show_(-1);
    this->pic_last_ = slot - 1;
    this->pic_since_ms_ = millis() - (uint32_t) ::espn::kPicEveryMin * 60u * 1000u;
    this->rebuild_state_(&this->last_fields_);
  });
  request->send(200, "application/json", ("{\"ok\":true,\"slot\":" + std::to_string(slot + 1) + "}").c_str());
}

// The path without its query, read in place. url_to() copies it into a
// 513-byte buffer on the web server task's small stack, and our routes have
// nothing in them to URL-decode.
static StringRef request_path(AsyncWebServerRequest *request) {
  const char *uri = static_cast<httpd_req_t *>(*request)->uri;
  const char *query = strchr(uri, '?');
  return StringRef(uri, query != nullptr ? (size_t) (query - uri) : strlen(uri));
}

bool GamedayComponent::canHandle(AsyncWebServerRequest *request) const {
  return request_path(request).starts_with("/gameday/");
}

void GamedayComponent::handleRequest(AsyncWebServerRequest *request) {
  StringRef url = request_path(request);
  if (url == StringRef("/gameday/state") && request->method() == HTTP_GET) {
    PsString body = this->current_state_json_();
    // The cached document is rebuilt on changes; the health and Wi-Fi
    // numbers move all the time, so they go in fresh at its end.
    if (body.size() >= 2 && body.back() == '}') {
      std::string health = this->health_json_(), wifi = this->wifi_json_();
      body.pop_back();
      body += ",\"health\":";
      body.append(health.data(), health.size());
      body += ",\"wifi\":";
      body.append(wifi.data(), wifi.size());
      body += '}';
    }
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
  if (url == StringRef("/gameday/settings")) {
    this->handle_settings_(request);
    return;
  }
  if (url == StringRef("/gameday/action") && request->method() == HTTP_POST) {
    std::string name = request->arg("do");
    if (name.empty()) {
      request->send(400, "application/json", "{\"error\":\"do is required\"}");
      return;
    }
    bool known = name == "refresh" || name == "qc" || name == "say" || name == "regulatory" ||
                 name == "night_skip" || name == "night_resume" || name == "event_test";
#ifdef GAMEDAY_DEMO
    known = known || name == "demo" || name == "demo_stop";
#endif
    if (!known) {
      request->send(404, "application/json", "{\"error\":\"unknown action\"}");
      return;
    }
    if (name == "say") {
      std::string text = request->arg("text");
      int secs = atoi(request->arg("secs").c_str());
      this->defer([this, text, secs]() { this->say(text, secs); });
      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }
    this->defer([this, name]() {
      if (name == "refresh")
        this->refresh_now();
      else if (name == "night_skip")
        this->night_skip("the device page");
      else if (name == "night_resume")
        this->night_resume();
      else if (name == "event_test")
        this->send_test_event_();
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
  if (url == StringRef("/gameday/picture")) {
    this->handle_picture_(request);
    return;
  }
#ifdef GAMEDAY_DEMO
  if (url == StringRef("/gameday/demo")) {
    this->handle_demo_(request);
    return;
  }
#endif
#ifdef USE_GAMEDAY_SAVED_NETWORKS
  if (url == StringRef("/gameday/wifi")) {
    this->handle_wifi_(request);
    return;
  }
#endif
#ifdef USE_GAMEDAY_CRASH_REPORT
  auto *crash = crash_report::global_crash_report;
  if (url == StringRef("/gameday/crash") && crash != nullptr) {
    if (request->method() == HTTP_POST) {
      if (request->arg("clear") != "1") {
        request->send(400, "application/json", "{\"error\":\"clear=1 is required\"}");
        return;
      }
      crash->clear();
      request->send(200, "application/json", "{\"ok\":true}");
      return;
    }
    std::string body = crash->json();
    request->send(200, "application/json", body.c_str());
    return;
  }
#endif
  request->send(404, "application/json", "{\"error\":\"unknown route\"}");
}

#ifdef USE_GAMEDAY_SAVED_NETWORKS
// The saved networks, names only: a password never leaves the panel. The
// list is in the order the panel tries them, the last one that worked first.
void GamedayComponent::handle_wifi_(AsyncWebServerRequest *request) {
  auto *nets = saved_networks::global_saved_networks;
  if (nets == nullptr) {
    request->send(404, "application/json", "{\"error\":\"unknown route\"}");
    return;
  }
  if (request->method() == HTTP_POST) {
    std::string ssid = request->arg("forget");
    if (ssid.empty()) {
      request->send(400, "application/json", "{\"error\":\"forget is required\"}");
      return;
    }
    if (!nets->forget(ssid)) {
      request->send(404, "application/json", "{\"error\":\"not a saved network\"}");
      return;
    }
    request->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (request->method() != HTTP_GET) {
    request->send(405, "application/json", "{\"error\":\"GET or POST\"}");
    return;
  }
  JsonDocument doc(::espn::detail::json_allocator());
  JsonArray list = doc["networks"].to<JsonArray>();
  for (const auto &e : nets->list()) {
    JsonObject o = list.add<JsonObject>();
    o["ssid"] = e.ssid;
    o["connected"] = e.connected;
  }
  doc["max"] = (int) saved_networks::SavedNetworks::max();
  std::string body;
  serializeJson(doc, body);
  request->send(200, "application/json", body.c_str());
}
#endif

void GamedayComponent::handle_set_(AsyncWebServerRequest *request) {
  std::vector<std::pair<std::string, std::string>> kv;
  for (const char *key : ::espn::kSetKeys)
    if (request->hasParam(key))
      kv.emplace_back(key, request->arg(key));
  if (kv.empty()) {
    request->send(400, "application/json", "{\"error\":\"nothing to set\"}");
    return;
  }
  this->defer([this, kv]() { this->apply_set_(kv); });
  request->send(200, "application/json", "{\"ok\":true}");
}

void GamedayComponent::apply_set_(const std::vector<std::pair<std::string, std::string>> &kv) {
  std::vector<::espn::SetOp> ops;
  for (const auto &p : kv) {
    ::espn::SetOp op;
    if (!::espn::parse_set(p.first, p.second, op, FAV_MAX)) {
      ESP_LOGW(TAG, "Ignoring %s=%s", p.first.c_str(), p.second.c_str());
      continue;
    }
    ops.push_back(std::move(op));
  }
  // A team or mode picked from the page or the app is somebody wanting to
  // see the panel (a backup restored through apply_ops_ isn't, nor a
  // playlist edit, whose list= only names the one playing).
  bool lists = false, pick = false;
  for (const auto &op : ops) {
    lists = lists || op.kind == ::espn::SetKind::LISTS;
    pick = pick || op.kind == ::espn::SetKind::MODE || op.kind == ::espn::SetKind::FAVS ||
           op.kind == ::espn::SetKind::FAV_SLOT || op.kind == ::espn::SetKind::LIST ||
           op.kind == ::espn::SetKind::SPORT;
  }
  if (pick && !lists)
    this->night_skip("a team pick");
  this->apply_ops_(ops);
}

void GamedayComponent::apply_ops_(const std::vector<::espn::SetOp> &ops) {
  this->sync_gate_.begin();  // the keys below share one flash write
  bool dirty = false;
  // The message card's keys are saved together, once.
  std::string msg_text, msg_url;
  uint8_t msg_keys = 0;
  int msg_every = -1;
  // So are the game events'.
  std::string hook_url;
  bool hook_url_set = false;
  int pregame = -1;
  // The weather's switch goes after its town, since a new town turns the
  // card on: "town=X&wxon=0" sets the town and leaves the card off.
  int wx_on = -1;
  // So are the night schedule's.
  this->load_night_();
  ::espn::NightPrefs night = this->night_;
  // list= with lists= names the playlist playing after the change (a rename
  // loses it by name); it doesn't switch to Favorite teams, as list= alone does.
  // sport= picking a sport row takes over list= in the same request (the
  // page sends list=0 with it); sport=none with list= leaves it to list=.
  bool with_lists = false, sport_pick = false;
  int keep_list = -1;
  for (const auto &op : ops) {
    with_lists = with_lists || op.kind == ::espn::SetKind::LISTS;
    sport_pick = sport_pick || (op.kind == ::espn::SetKind::SPORT && op.value != 0);
    if (op.kind == ::espn::SetKind::LIST)
      keep_list = op.value;
  }
  for (const auto &op : ops) {
    switch (op.kind) {
      case ::espn::SetKind::MODE:
        this->set_mode_((uint8_t) op.value);
        break;
      case ::espn::SetKind::ROTATE:
        this->set_rotate_minutes(op.value);
        break;
      case ::espn::SetKind::LOCKON:
        this->set_lockon_minutes(op.value);
        break;
      case ::espn::SetKind::RELEASE:
        this->set_release_seconds(op.value);
        break;
      case ::espn::SetKind::COLLIDE:
        this->set_collision_alternate(op.on);
        break;
      case ::espn::SetKind::TODAY:
        this->set_today_only(op.on);
        break;
      case ::espn::SetKind::JUMP:
        this->set_jump_to_action(op.on);
        break;
      case ::espn::SetKind::BANNER:
        this->set_score_banner(op.on);
        break;
      case ::espn::SetKind::FAVTAKE:
        this->set_favorites_take_over(op.on);
        break;
      case ::espn::SetKind::LIVE:
        this->set_live_mask_(op.mask);
        break;
      case ::espn::SetKind::FAVS: {
        for (const auto &bad : op.refused)
          ESP_LOGW(TAG, "Bad favorite '%s'", bad.c_str());
        std::vector<std::pair<uint8_t, uint32_t>> list;
        for (const auto &t : op.teams)
          list.emplace_back(t.league, t.id);
        this->set_favorites_(list);
        break;
      }
      case ::espn::SetKind::FAV_SLOT:
        this->set_favorite_(op.slot, op.team.league, op.team.id);
        break;
      case ::espn::SetKind::LISTS:
        this->set_lists_(::espn::parse_lists(op.text, FAV_MAX), keep_list);
        break;
      case ::espn::SetKind::LIST:
        if (with_lists || sport_pick)
          break;  // set_lists_ or the sport row took it
        if (op.value > this->prefs9_.count)
          ESP_LOGW(TAG, "No playlist %d", op.value);
        else
          this->set_active_list_((uint8_t) op.value, false);
        break;
      case ::espn::SetKind::SPORT:
        if (op.value != 0)
          this->set_sport_row_((uint8_t) op.value, false);
        else if (keep_list < 0 && this->prefs9_.sport != 0)
          this->set_active_list_(0, false);
        break;
      case ::espn::SetKind::TZ:
        this->select_timezone(::espn::kTimezones[op.value].name);
        break;
      case ::espn::SetKind::TZ_AUTO:
        this->set_tz_auto(op.on);
        dirty = true;
        break;
      case ::espn::SetKind::DOWN:
        this->set_ticker_down_distance(op.on);
        break;
      case ::espn::SetKind::COUNT:
        this->set_ticker_baseball_count(op.on);
        break;
      case ::espn::SetKind::MATCH:
        this->set_ticker_soccer_stats(op.on);
        break;
      case ::espn::SetKind::PLAY:
        this->set_ticker_last_play(op.on);
        break;
      case ::espn::SetKind::ODDS:
        this->set_ticker_odds(op.on);
        break;
      case ::espn::SetKind::LEADERS:
        this->set_ticker_leaders(op.on);
        break;
      case ::espn::SetKind::OPP:
        this->set_opponent_splashes(op.on);
        break;
      case ::espn::SetKind::BEST:
        this->set_best_game(op.on);
        break;
      case ::espn::SetKind::BOOTADDR:
        this->set_show_boot_address(op.on);
        break;
      case ::espn::SetKind::COUNTDOWN:
        this->set_countdown(op.on);
        break;
      case ::espn::SetKind::TOP25:
        this->set_top25(op.on);
        break;
      case ::espn::SetKind::NEWS:
        this->set_news(op.on);
        break;
      case ::espn::SetKind::GAMELEADERS:
        this->set_game_leaders(op.on);
        break;
      case ::espn::SetKind::CARDS:
        for (const auto &id : op.refused)
          ESP_LOGW(TAG, "Between games: no card %s here, leaving it", id.c_str());
        for (const auto &r : ::espn::kCards)
          if (op.mask & ::espn::card_bit(r.card))
            this->set_card(r.card, (op.value & ::espn::card_bit(r.card)) != 0);
        break;
      case ::espn::SetKind::CD_NAME:
        this->set_custom_countdown_name(op.text);
        break;
      case ::espn::SetKind::CD_DATE:
        this->set_custom_countdown_day(op.ymd);
        break;
      case ::espn::SetKind::AUTOUPD:
        this->set_auto_update(op.on);
        break;
      case ::espn::SetKind::BETA:
        this->set_follow_beta(op.on);
        break;
      case ::espn::SetKind::PANELS:
        if (this->panels_ != nullptr)
          this->panels_->set_cols((uint8_t) op.value);
        break;
      case ::espn::SetKind::MSG:
        msg_text = op.text;
        msg_keys |= 1;
        break;
      case ::espn::SetKind::MSGURL:
        msg_url = op.text;
        msg_keys |= 2;
        break;
      case ::espn::SetKind::MSGEVERY:
        msg_every = op.value;
        break;
      case ::espn::SetKind::NICK:
        this->set_nick_(op.text);
        break;
      case ::espn::SetKind::HOOK:
        hook_url = op.text;
        hook_url_set = true;
        break;
      case ::espn::SetKind::PREGAME:
        pregame = op.value;
        break;
      case ::espn::SetKind::TOWN:
        this->set_town(op.text);
        break;
      case ::espn::SetKind::WXON:
        wx_on = op.on ? 1 : 0;
        break;
      case ::espn::SetKind::NIGHT:
        night.on = op.on ? 1 : 0;
        break;
      case ::espn::SetKind::NIGHT_START:
        night.start = (uint16_t) op.value;
        break;
      case ::espn::SetKind::NIGHT_END:
        night.end = (uint16_t) op.value;
        break;
      case ::espn::SetKind::NIGHT_SCREEN:
        night.screen_off = op.on ? 0 : 1;
        break;
      case ::espn::SetKind::NIGHT_LEVEL:
        night.level = (uint8_t) op.value;
        break;
    }
  }
  if (::espn::night_keep_hours(night, this->night_))
    ESP_LOGW(TAG, "Night schedule: start and end can't be the same time; keeping %s to %s",
             ::espn::night_clock12(night.start).c_str(), ::espn::night_clock12(night.end).c_str());
  this->set_night_(night);
  if (hook_url_set || pregame >= 0)
    this->set_hook_(hook_url_set ? &hook_url : nullptr, pregame);
  if (wx_on >= 0)
    this->set_weather(wx_on == 1);
  if (msg_keys != 0 || msg_every >= 0)
    this->set_msg_((msg_keys & 1) ? &msg_text : nullptr, (msg_keys & 2) ? &msg_url : nullptr, msg_every);
  if (dirty)
    this->rebuild_state_(&this->last_fields_);
  this->end_sync_batch_();
}

// The import's body, or a picture's, arrives here in pieces before
// handleRequest. The web server hands over a body that isn't a form on its
// own path (the form posts of /gameday/set never come here), one request at
// a time.
void GamedayComponent::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                                  size_t total) {
  if (request_path(request) == StringRef("/gameday/picture")) {
    if (index == 0) {
      PsString().swap(this->picture_body_);
      if (total <= ::espn::kPicMaxBytes)
        this->picture_body_.reserve(total);
    }
    if (total <= ::espn::kPicMaxBytes)
      this->picture_body_.append((const char *) data, len);
    return;
  }
  if (request_path(request) != StringRef("/gameday/settings"))
    return;
  if (index == 0) {
    PsString().swap(this->settings_body_);
    if (total <= ::espn::kSettingsMaxBytes)
      this->settings_body_.reserve(total);
  }
  if (total <= ::espn::kSettingsMaxBytes)
    this->settings_body_.append((const char *) data, len);
}

void GamedayComponent::handle_settings_(AsyncWebServerRequest *request) {
  if (request->method() == HTTP_GET) {
    // From the cached state document, built on the main loop, so nothing
    // here reads a setting while the loop changes it.
    PsString state = this->current_state_json_();
    JsonDocument in(::espn::detail::json_allocator());
    // Before the first build the document is "{}", and a backup of it would be empty.
    if (deserializeJson(in, state.data(), state.size()) != DeserializationError::Ok || in["mode"].isNull()) {
      request->send(500, "application/json", "{\"error\":\"not ready\"}");
      return;
    }
    JsonDocument out(::espn::detail::json_allocator());
    int brightness = this->brightness_number_ != nullptr && this->brightness_number_->has_state()
                         ? (int) this->brightness_number_->state
                         : -1;
    int scroll =
        this->scroll_number_ != nullptr && this->scroll_number_->has_state() ? (int) this->scroll_number_->state : -1;
    // The live mask isn't in the state (its live list is what the panel scans
    // now); one 16-bit read, so the web server can take it as it is.
    ::espn::settings_from_state(in.as<JsonObjectConst>(), this->prefs7_.live_mask, brightness, scroll, out);
    PsString body;  // a few KB with 16 favorites and their playlists
    serializeJson(out, body);
    request->send(200, "application/json", body.c_str());
    return;
  }
  if (request->method() != HTTP_POST) {
    request->send(400, "application/json", "{\"error\":\"GET or POST\"}");
    return;
  }
  PsString body;
  body.swap(this->settings_body_);
  size_t length = request->contentLength();
  // 400, not 413: ESPHome's web server sends any code it has no name for as 500.
  if (length > ::espn::kSettingsMaxBytes) {
    request->send(400, "application/json", "{\"error\":\"That file is too big to be a settings backup.\"}");
    return;
  }
  ::espn::SettingsImport in;
  {
    JsonDocument doc(::espn::detail::json_allocator());
    // A body left from a request that broke off doesn't match this one's length.
    if (body.empty() || body.size() != length ||
        deserializeJson(doc, body.data(), body.size()) != DeserializationError::Ok) {
      in.error = "Not a settings backup.";
    } else {
      ::espn::SettingsTarget target;
      target.name = this->hostname();
      {
        // The nickname from the cached state document: this runs on the web
        // server's task, and the main loop owns nick_.
        JsonDocument filter(::espn::detail::json_allocator()), state(::espn::detail::json_allocator());
        filter["nick"] = true;
        PsString json = this->current_state_json_();
        if (deserializeJson(state, json.data(), json.size(), DeserializationOption::Filter(filter)) == DeserializationError::Ok)
          target.nick = state["nick"] | "";
      }
      target.fav_max = FAV_MAX;
      if (this->panels_ != nullptr) {
        target.panels = this->panels_->cols();
        target.max_panels = this->panels_->max_cols();
      }
#if defined(USE_UPDATE) && !defined(GAMEDAY_BAR)
      target.autoupd = true;
#endif
#ifdef GAMEDAY_BETA_CHANNEL
      target.beta = true;
#endif
      target.brightness = this->brightness_number_ != nullptr;
      target.scroll = this->scroll_number_ != nullptr;
      ::espn::parse_settings(doc.as<JsonVariantConst>(), target, in);
    }
  }
  std::string reply = ::espn::settings_reply(in);
  if (!in.error.empty()) {
    ESP_LOGW(TAG, "Settings import refused: %s", in.error.c_str());
    request->send(400, "application/json", reply.c_str());
    return;
  }
  this->defer([this, in]() { this->apply_settings_(in); });
  request->send(200, "application/json", reply.c_str());
}

void GamedayComponent::apply_settings_(const ::espn::SettingsImport &in) {
  ESP_LOGI(TAG, "Settings import: %u settings, %u left out", (unsigned) in.ops.size(), (unsigned) in.skipped.size());
  for (const auto &s : in.skipped)
    ESP_LOGW(TAG, "Left out of the import: %s", s.c_str());
  for (const auto &k : in.kept)
    ESP_LOGI(TAG, "Kept this panel's own %s", k.c_str());
  // One flash write for the whole restore, the two numbers' own saved
  // values included, so a panel unplugged right after "Restored" keeps it.
  this->sync_gate_.begin();
  this->apply_ops_(in.ops);
  if (in.brightness >= 0 && this->brightness_number_ != nullptr)
    this->brightness_number_->make_call().set_value((float) in.brightness).perform();
  if (in.scroll >= 0 && this->scroll_number_ != nullptr)
    this->scroll_number_->make_call().set_value((float) in.scroll).perform();
  this->sync_gate_.request();
  this->end_sync_batch_();
}

}  // namespace gameday
}  // namespace esphome
