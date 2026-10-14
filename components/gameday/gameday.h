#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/panel_layout/panel_layout.h"
#include "esphome/components/select/select.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/web_server_base/web_server_base.h"

#include "logo_cache.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/preferences.h"

#include "auto_update.h"
#include "espn_parse.h"
#include "favorites.h"
#include "recap.h"
#include "poll_timing.h"
#include "startup.h"
#ifdef GAMEDAY_DEMO
#include "demo.h"
#endif

namespace esphome {
namespace gameday {

using ::espn::GameSnapshot;
using ::espn::GameState;
using ::espn::League;
using ::espn::Schedule;
using ::espn::Splash;
using ::espn::TickerOptions;

// Exactly the fields the Home Assistant blueprint used to push to the page.
struct UpdateFields {
  std::string game_state;
  std::string team_abbr;
  int team_score{0};
  std::string opponent_abbr;
  int opponent_score{0};
  std::string status_text;
  std::string team_logo;
  std::string opponent_logo;
  int possession{0};
  int team_timeouts{0};
  int opponent_timeouts{0};
  std::string team_record;
  std::string opponent_record;
  std::string splash_text;
  uint32_t splash_color{0};
  std::string last_play;
  std::string clock_text;  // "5:36 - 3rd" while the game is live, else empty
  std::string down_distance;  // "3rd & 10" while live, else empty
  bool is_red_zone{false};
  uint32_t team_color{0xFFFFFF};
  uint32_t opponent_color{0xFFFFFF};
  std::string kickoff;  // "Sun 3:25 PM" style label while PRE, else empty
  // Other sports (football leaves these at their defaults).
  uint8_t sport{0};        // ::espn::Sport
  std::string situation;   // baseball: the count "2-1" while a half inning is on
                           // basketball: the playoff series "NY lead 2-1"
  int outs{-1};            // baseball: outs in the half inning, -1 when none
  uint8_t bases{0};        // baseball: 1 first, 2 second, 4 third
  bool highlight{false};   // the situation row stands out (bases loaded)
  int team_marks{0};       // soccer: red cards, drawn under each side's logo
  int opp_marks{0};
  std::string message;  // the no-game screen's text; empty leaves the page's default
  // For Home Assistant's Game Status and Last Play. They follow one game and
  // leave out the ticker's per-play parts (espn::ha_status_text), so the
  // recorder isn't writing a row every poll or every playlist card.
  std::string ha_status_text;
  std::string ha_last_play;
};

enum class SelectType : uint8_t { TEAM, TIMEZONE, MODE, FAVORITE };

// Favorite Teams mode holds up to this many teams. The WizMote's buttons 1-4
// jump to the first four.
static constexpr uint8_t FAV_MAX = 16;

// LIVE (5) follows live games in the leagues of the live mask; modes 1-3 are
// the football-only live modes from before, kept for panels that saved them.
enum class Mode : uint8_t { MY_TEAM = 0, LIVE_NFL = 1, LIVE_NCAA = 2, LIVE_ANY = 3, FAVORITES = 4, LIVE = 5 };

class GamedayComponent;
class EspnBody;

// "nfl:6" -> league + id. False for "none"/"" (id 0) or garbage.
bool parse_team_ref(const std::string &v, uint8_t &league, uint32_t &id);

class GamedaySelect : public select::Select, public Component {
 public:
  void set_type(SelectType type) { this->type_ = type; }
  void set_slot(uint8_t slot) { this->slot_ = slot; }
  void set_parent(GamedayComponent *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override;
  SelectType type_{SelectType::TEAM};
  uint8_t slot_{0};
  GamedayComponent *parent_{nullptr};
};

// The component is also the web handler for /gameday/*: the device page reads
// one JSON document and posts settings there instead of driving entities.
//   GET  /gameday/state              full snapshot + settings
//   GET  /gameday/heap               free heap now: internal, largest internal block, PSRAM (KiB)
//   POST /gameday/set?key=value...   team=nfl:6|none mode=0-4 rotate=2-30 fav1..fav4=nfl:6|none
//                                    tz=<name, IANA name or index> tzauto=0/1
//                                    down/play/odds/opp=0/1 panels=1/2
//                                    lockon=5-120 (min) release=30-3600 (s) collide=0 stick|1 alternate
//                                    autoupd=0/1 (install updates on its own, auto_update.h)
//   POST /gameday/action?do=refresh|qc   (qc: the factory check, firmware/pages/qc.yaml)
// The demo build (firmware/gameday-demo.yaml) adds do=demo|demo_stop and
// GET/POST /gameday/demo (demo.cpp).
// Requests arrive on the HTTP task; settings are applied on the main loop.
class GamedayComponent : public Component, public AsyncWebHandler {
 public:
  void set_http(http_request::HttpRequestComponent *http) {
    this->http_ = http;
    this->logos_.set_http(http);
  }
  // Team logos for the board (gameday-common.yaml, gd_show_logos)
  LogoCache &logos() { return this->logos_; }
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  void set_web_server_base(web_server_base::WebServerBase *base) { this->base_ = base; }
  void set_panel_layout(panel_layout::PanelLayout *p) { this->panels_ = p; }
  void add_on_action_callback(std::function<void(std::string)> &&cb) {
    this->action_callbacks_.push_back(std::move(cb));
  }
  void set_team_select(select::Select *s) { this->team_select_ = s; }
  void set_timezone_select(select::Select *s) { this->timezone_select_ = s; }
  void set_mode_select(select::Select *s) { this->mode_select_ = s; }
  void set_favorite_select(uint8_t slot, select::Select *s) {
    if (slot >= 1 && slot <= 4)
      this->favorite_selects_[slot - 1] = s;
  }
  void add_on_update_callback(std::function<void(const UpdateFields &)> &&cb) {
    this->callbacks_.push_back(std::move(cb));
  }

  // Called by the selects and by template entities in YAML.
  void select_team(const std::string &option);
  void select_team_id(League league, uint32_t id);
  void select_timezone(const std::string &option);
  void select_mode(const std::string &option);
  std::string hostname() const;  // the device name, with its MAC suffix
  // Favorites: four team slots for the remote's numbered buttons.
  void select_favorite(uint8_t slot, const std::string &option);
  void press_favorite(uint8_t slot);
  void set_rotate_minutes(int minutes);
  int rotate_minutes() const { return this->prefs2_.rotate_minutes; }
  // Favorite Teams mode timing (see favorites.h for the rules).
  void set_lockon_minutes(int minutes);
  void set_release_seconds(int seconds);
  void set_collision_alternate(bool alternate);
  void set_today_only(bool on);
  // Live mode's leagues as keys ("nfl,mlb"); "" = the leagues you follow.
  void set_live_leagues(const std::string &keys);
  std::vector<League> live_leagues_() const;  // the leagues Live mode scans
  bool today_only() const { return (this->prefs7_.flags & FAV7_TODAY) != 0; }
  void refresh_now();
  // WizMote buttons 1-4. Favorites in release firmware; the demo build gives
  // them to the demo (demo.cpp).
  void remote_number(uint8_t n);
  // No team picked and nothing else chosen: the panel follows live games and
  // the finals rotation, and the boot screen gives the app's QR code a turn
  // every few minutes (boot.yaml, boot_app_qr_turn) until a team is picked.
  bool no_team_default() const;
  bool app_qr_turns() const { return this->no_team_default() && !this->demo_playing_(); }
#ifdef GAMEDAY_DEMO
  // Loops a scripted game through the real splash and render path, for
  // showing the panel off without a live game, until stop_demo().
  // kind: 1 pro football, 2 college (espn::DemoKind).
  void start_demo(uint8_t kind);
  void stop_demo();
#endif

  bool ticker_clock() const { return this->flag_(FLAG_CLOCK); }
  bool ticker_down_distance() const { return this->flag_(FLAG_DOWN); }
  bool ticker_last_play() const { return this->flag_(FLAG_PLAY); }
  bool ticker_odds() const { return this->flag_(FLAG_ODDS); }
  bool opponent_splashes() const { return this->flag_(FLAG_OPP); }
  bool setup_done() const { return this->flag_(FLAG_SETUP); }
  // The first real scoreboard since boot, or since the team last changed,
  // is on the board. The boot screen waits for it (boot.yaml, boot_hide).
  bool board_ready() const { return this->board_ready_; }
  // True while a boot screen hold that started at since_ms should go on
  // (startup.h). 0 means no hold is running.
  bool boot_hold(uint32_t since_ms) const { return ::espn::boot_hold(this->board_ready_, since_ms, millis()); }
  // Stored inverted so panels from before this setting keep showing it.
  bool show_boot_address() const { return !this->flag_(FLAG_NOBOOTADDR); }
  void set_show_boot_address(bool on) { this->set_flag_(FLAG_NOBOOTADDR, !on); }
  // Stored inverted (a "manual" bit) so panels flashed before this existed
  // come up with auto on without touching their saved preferences.
  bool tz_auto() const { return !this->flag_(FLAG_TZMANUAL); }
  void set_tz_auto(bool on) { this->set_flag_(FLAG_TZMANUAL, !on); }
  void set_ticker_clock(bool on) { this->set_flag_(FLAG_CLOCK, on); }
  void set_ticker_down_distance(bool on) { this->set_flag_(FLAG_DOWN, on); }
  void set_ticker_last_play(bool on) { this->set_flag_(FLAG_PLAY, on); }
  void set_ticker_odds(bool on) { this->set_flag_(FLAG_ODDS, on); }
  void set_opponent_splashes(bool on) { this->set_flag_(FLAG_OPP, on); }
  // "Install updates automatically", off unless the owner turns it on.
  bool auto_update() const { return this->prefs6_.auto_update != 0; }
  void set_auto_update(bool on);

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // AsyncWebHandler (HTTP task)
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  void apply_team_(const ::espn::Team &t);
  void clear_team_();
  bool mark_setup_done_();
  void fire_action_(const std::string &name);
  void set_favorite_(uint8_t slot, uint8_t league, uint32_t id);
  // The whole list at once, in priority order (the page's favs= key).
  void set_favorites_(const std::vector<std::pair<uint8_t, uint32_t>> &list);
  void favorites_changed_(bool first_pick);
  uint8_t fav_league_(uint8_t i) const;  // i = 0..FAV_MAX-1
  uint32_t fav_id_(uint8_t i) const;     // 0 = empty
  void put_fav_(uint8_t i, uint8_t league, uint32_t id);  // no save
  void handle_set_(AsyncWebServerRequest *request);
  void apply_set_(const std::vector<std::pair<std::string, std::string>> &kv);
  void rebuild_state_(const UpdateFields *f);  // main loop only
  std::string current_state_json_();
  std::mutex state_mutex_;
  std::string state_json_{"{}"};
  UpdateFields last_fields_;
  web_server_base::WebServerBase *base_{nullptr};
  panel_layout::PanelLayout *panels_{nullptr};
  std::vector<std::function<void(std::string)>> action_callbacks_;
  Splash last_splash_;  // for the page: the splash goes out once per emit
  static constexpr uint8_t FLAG_CLOCK = 1;
  static constexpr uint8_t FLAG_DOWN = 2;
  static constexpr uint8_t FLAG_PLAY = 4;
  static constexpr uint8_t FLAG_ODDS = 8;
  static constexpr uint8_t FLAG_OPP = 16;
  static constexpr uint8_t FLAG_TZMANUAL = 32;  // timezone was picked by hand; page must not override
  static constexpr uint8_t FLAG_SETUP = 64;     // a team, mode or favorite has been picked at least once
  static constexpr uint8_t FLAG_NOBOOTADDR = 128;  // skip the address flash on a configured panel's boot
  static constexpr uint8_t FLAGS_DEFAULT = FLAG_DOWN | FLAG_PLAY | FLAG_ODDS | FLAG_OPP;

  struct Prefs {
    uint8_t league;
    uint32_t team_id;
    uint8_t tz_index;
    uint8_t flags;
  } __attribute__((packed));
  // Kept separate so adding fields never invalidates the original blob.
  struct Prefs2 {
    uint8_t mode;
    uint8_t rotate_minutes;
  } __attribute__((packed));
  struct Prefs3 {
    uint8_t fav_league[4];
    uint32_t fav_id[4];  // 0 = slot empty
  } __attribute__((packed));
  struct Prefs4 {
    uint8_t lockon_minutes;
    uint16_t release_seconds;
    uint8_t collide;  // 0 = stick with the higher slot, 1 = alternate on the rotate timer
  } __attribute__((packed));
  // Its presence says team_id 0 means "no team" on this panel. Firmware
  // before it saved Dallas as the team on first boot (recap.h,
  // legacy_team_unpicked).
  struct Prefs5 {
    uint8_t no_team_ok;
  } __attribute__((packed));
  struct Prefs6 {
    uint8_t auto_update;  // 1 = install new releases on its own
    int32_t tried_day;    // espn::auto_update_day() of the last install try, 0 = never
  } __attribute__((packed));
  // Favorites 5-16 (1-4 stay in Prefs3, so older panels keep theirs) and the
  // settings that came with the longer list.
  // Its size is fixed: a blob that changes size fails to load and resets every
  // setting in it. More favorites would need a new blob, not a bigger one.
  static constexpr uint8_t PREFS7_FAVS = 12;
  static_assert(FAV_MAX == 4 + PREFS7_FAVS, "Prefs7 holds favorites 5-16; grow the list with a new blob");
  struct Prefs7 {
    uint8_t fav_league[PREFS7_FAVS];
    uint32_t fav_id[PREFS7_FAVS];  // 0 = empty
    uint8_t flags;                 // FAV7_TODAY
    uint16_t live_mask;            // Live mode: 1 << League per league, 0 = the leagues you follow
  } __attribute__((packed));
  static constexpr uint8_t FAV7_TODAY = 1;  // idle playlist shows only today's games

  bool flag_(uint8_t f) const { return (this->prefs_.flags & f) != 0; }
  void set_flag_(uint8_t f, bool on);
  void save_prefs_();
  void publish_selects_();
  void publish_team_select_(const std::string &option);
  void apply_timezone_();
  const ::espn::Team *current_team_() const;
  std::string team_option_() const;

  // One fetch cycle runs on its own FreeRTOS task so the display loop never
  // waits on the network. The main loop fills a Job, the worker performs the
  // HTTP requests and parsing into it, and the main loop applies the result.
  struct Job {
    uint32_t generation{0};
    const ::espn::Team *team{nullptr};
    uint8_t mode{0};  // effective_mode_() when the job was made; the worker reads this, not the prefs
    // live-game modes: scan the day's games first, then follow one
    bool need_scan{false};
    std::vector<League> scan_leagues;
    std::vector<::espn::LiveGame> live;
    bool scan_ok{false};
    int64_t next_kickoff{0};  // earliest kickoff not started yet, 0 for none
    bool need_schedule{false};
    Schedule schedule;
    bool schedule_ok{false};
    // the "Up next" list rides along with the schedule refresh
    bool need_upcoming{false};
    std::vector<::espn::Upcoming> upcoming;
    bool upcoming_ok{false};
    bool no_event{false};
    GameSnapshot game;
    bool game_ok{false};
    // Favorite Teams mode: which cached entry this cycle refreshes or polls
    int fav_index{-1};
    uint32_t our_id{0};
    // Soccer: the team's reads per competition (comps[0] the league's, then
    // its kCups in order), and the one cup this cycle reads (1.., -1 none).
    std::vector<Schedule> comps;
    int comp{-1};
    bool need_game{false};   // the board has no game for the current schedule yet
    bool polled{false};      // a cup read polled the game it picked
    std::string done_event;  // a game seen to finish, passed over by the pick
    bool no_lookahead{false};  // My team: a final is still lingering on the board
    int8_t lookahead{0};       // 0 not run, 1 found the next game, 2 nothing ahead, -1 a read failed
    // The finals rotation. A scan keeps the day's finals too when asked, and
    // a recap job reads one other day's scoreboard for one league.
    bool want_finals{false};
    std::vector<::espn::Final> finals;
    bool need_recap{false};
    bool recap_forward{false};  // looking for the next kickoff, not for finals
    uint8_t recap_league{0};
    int32_t recap_day{0};
    bool recap_ok{false};
    // ESPN answered 403 or 429 to a request in this job (set by open_)
    bool throttled{false};
    uint32_t retry_after_ms{0};
  };
  void start_job_();
  void start_worker_();
  static void worker_(void *arg);
  void run_job_();
  void apply_job_();
  bool fetch_schedule_(const ::espn::Team *team, Schedule &out, const char *comp_slug = nullptr);
  void run_cup_job_();
  void apply_cup_job_(uint32_t now);
  bool fetch_upcoming_(const ::espn::Team *team, std::vector<::espn::Upcoming> &out);
  bool fetch_game_(const ::espn::Team *team, const Schedule &schedule, GameSnapshot &out);
  // epoch picks the day (now when 0); finals, when given, also gets that
  // day's finished games.
  bool fetch_live_games_(League league, std::vector<::espn::LiveGame> &out, int64_t &next_kickoff,
                         std::vector<::espn::Final> *finals = nullptr, int64_t epoch = 0);
  // The mode the panel runs: the saved one, except that with no team picked
  // My team (and Favorites with none set) runs as Live anything.
  uint8_t effective_mode_() const {
    return this->no_team_default() ? (uint8_t) Mode::LIVE_ANY : this->prefs2_.mode;
  }
  static bool is_live_mode_(uint8_t m) {
    return (m >= (uint8_t) Mode::LIVE_NFL && m <= (uint8_t) Mode::LIVE_ANY) || m == (uint8_t) Mode::LIVE;
  }
  bool live_mode_() const { return is_live_mode_(this->effective_mode_()); }
  // The finals rotation covers the football leagues the mode reads; Live
  // games with no football league in it has no rotation.
  uint8_t recap_leagues_() const {
    return (this->mode_reads_(League::NFL) ? ::espn::kRecapNfl : 0) |
           (this->mode_reads_(League::NCAA) ? ::espn::kRecapCollege : 0);
  }
  bool mode_reads_(League league) const {
    uint8_t m = this->effective_mode_();
    if (m == (uint8_t) Mode::LIVE) {
      for (League l : this->live_leagues_())
        if (l == league)
          return true;
      return false;
    }
    return m == (uint8_t) Mode::LIVE_ANY || m == (league == League::NFL ? (uint8_t) Mode::LIVE_NFL
                                                                         : (uint8_t) Mode::LIVE_NCAA);
  }
  // Mode 4 with at least one favorite set; with none it behaves like My Team.
  bool favorites_mode_() const { return this->prefs2_.mode == (uint8_t) Mode::FAVORITES && !this->fav_.empty(); }
  uint32_t our_id_() const;  // team id the snapshot is oriented around
  // Sends the request and, on a 200, hands the reply to body to read.
  bool open_(const std::string &url, EspnBody &body);
  void schedule_next_(uint32_t ms) { this->next_fetch_ms_ = millis() + ms; }
  uint32_t interval_for_phase_() const;
  uint32_t linger_ms_() const;  // how long a final stays up before the next game
  std::string lingered_event_;  // the final the last linger ended on (My team)
  uint32_t linger_backoff_ms_{0};  // a longer linger on that final when no next game turned up
  // Non-football leagues without a season list find the next game in the
  // coming days' scoreboards when the team endpoint names a finished one.
  bool needs_lookahead_(League league) const;
  // 1 found the next game, 0 the team is off that day, -1 the read failed.
  int fetch_team_day_(const ::espn::Team *team, uint32_t group, int64_t day_epoch, const std::string &skip_event,
                      Schedule &out, const char *comp_slug = nullptr);
  // A team read that names a game not started yet: swap in the team's game on
  // now from today's scoreboard (the league's, or the cup's), if it has one.
  void take_game_on_now_(const ::espn::Team *team, Schedule &s, const std::string &done_event,
                         const char *comp_slug = nullptr);
  void emit_(const ::espn::Splash &splash);
  void reset_game_();
  // A game poll came back clean: clear the miss count and start the clock over.
  void mark_good_poll_() {
    this->misses_ = 0;
    this->last_good_ms_ = millis() == 0 ? 1 : millis();
    this->clear_backoff_();
  }
  // A read failed: start the next, longer wait (poll_timing.h) and return it.
  uint32_t back_off_();
  // ESPN answered: the next failure starts the backoff over.
  void clear_backoff_() {
    this->fail_streak_ = 0;
    this->throttled_ = false;
    this->backoff_ms_ = 0;
  }
  // Inside a wait back_off_ started. Only Favorite Teams mode needs to ask:
  // it ticks every second for the playlist and must not read ESPN on each one.
  bool backing_off_() const {
    return this->backoff_ms_ != 0 && (millis() - this->backoff_since_ms_) < this->backoff_ms_;
  }
  // Someone asked for fresh data (Refresh Now, a new team, mode or favorite):
  // read now, but keep the failure count so another failure waits longer.
  void end_backoff_wait_() { this->backoff_ms_ = 0; }
  // True once a poll has actually put game data on the board this boot.
  bool ever_polled_good_() const { return this->last_good_ms_ != 0; }
  // Seconds since the last good poll. With nothing ever polled, report the
  // time since boot rather than 0: the board has had no update for its whole
  // uptime, and 0 would read to the app as "fresh". Callers that need to
  // phrase it for a person test ever_polled_good_() first.
  uint32_t stale_seconds_() const {
    if (this->last_good_ms_ == 0)
      return millis() / 1000;
    return (millis() - this->last_good_ms_) / 1000;
  }

  http_request::HttpRequestComponent *http_{nullptr};
  LogoCache logos_;
  time::RealTimeClock *time_{nullptr};
  select::Select *team_select_{nullptr};
  select::Select *timezone_select_{nullptr};
  std::vector<std::function<void(const UpdateFields &)>> callbacks_;

  ESPPreferenceObject pref_;
  Prefs prefs_{};
  ESPPreferenceObject pref2_;
  Prefs2 prefs2_{};
  select::Select *mode_select_{nullptr};
  select::Select *favorite_selects_[4]{nullptr, nullptr, nullptr, nullptr};
  ESPPreferenceObject pref3_;
  Prefs3 prefs3_{};
  ESPPreferenceObject pref4_;
  Prefs4 prefs4_{};
  ESPPreferenceObject pref5_;
  ESPPreferenceObject pref6_;
  Prefs6 prefs6_{};
  // Install updates automatically (auto_update.h), run from loop().
  uint32_t auto_update_ms_{0};      // last look
  int32_t auto_update_checked_{0};  // the night the manifest was read for it
  uint32_t auto_update_check_ms_{0};
  bool auto_update_waiting_{false};  // logged "waiting for the game" tonight
  void auto_update_tick_();
  bool followed_game_live_() const;
  ESPPreferenceObject pref7_;
  Prefs7 prefs7_{};
  // The team endpoint's answer for the saved team (startup.h), so a boot can
  // skip that read and go straight to the scoreboard.
  ESPPreferenceObject team_cache_pref_;
  bool team_cache_tried_{false};     // one look per boot
  bool cache_unconfirmed_{false};    // schedule_ came from the cache and no poll has confirmed it
  bool upcoming_after_poll_{false};  // booted from the cache: season list after the first board
  bool load_team_cache_(const ::espn::Team *team);
  void save_team_cache_(const ::espn::Team *team);
  // Boot, before the first fetch: Wi-Fi is up but the clock is not set yet.
  void wait_for_clock_();
  bool clock_set_() const { return this->time_ != nullptr && this->time_->now().is_valid(); }
  uint32_t wifi_up_ms_{0};      // millis() when the loop first saw Wi-Fi this boot, 0 = not yet
  uint32_t sntp_asked_ms_{0};   // millis() of the last nudge to the time server, 0 = none since Wi-Fi came up
  bool clock_wait_shown_{false};
  bool board_ready_{false};
  uint32_t board_pending_ms_{0};  // millis() of the last team change, 0 = waiting since boot
  void mark_board_ready_();
  std::string favorite_option_(uint8_t slot) const;
  // Favorite Teams mode: one cached next-game card per set slot, in slot
  // order. The worker refreshes one entry per cycle; the main loop picks
  // which one the panel shows (favorites.h) and polls only that game.
  struct FavEntry {
    const ::espn::Team *team{nullptr};
    uint8_t slot{0};  // 1-4, the remote button
    Schedule sched;
    GameSnapshot game;
    uint32_t fetched_ms{0};  // last schedule attempt
    int64_t final_epoch{0};  // when the panel saw the game go final, 0 if it was fetched final
    bool stale{false};       // released after a final: fetch the next game
    std::string stuck_event;  // a final the team endpoint kept naming: no minute-by-minute retries
    uint32_t stuck_ms{0};      // millis() when that back-off started
    uint32_t stuck_for_ms{0};  // and how long it lasts
    std::vector<Schedule> comps;  // soccer: reads per competition, as Job::comps
    int comp_next{-1};            // the cup read due next, -1 when none
  };
  // The favorite cards are ~1 KB each and internal heap is the panel's
  // scarcest resource: PSRAM (logo_cache.h's allocator).
  using FavList = std::vector<FavEntry, PsramAllocator<FavEntry>>;
  FavList fav_;
  int fav_shown_{-1};
  int64_t fav_shown_since_{0};  // epoch seconds
  int fav_pinned_{-1};
  bool fav_locked_{false};
  uint32_t fav_poll_ms_{0};
  void rebuild_favorites_(bool keep_cards);
  ::espn::FavRules fav_rules_() const;
  std::vector<::espn::FavGame> fav_games_() const;
  bool fav_choose_(int64_t now_epoch);  // true when the shown entry changed
  void fav_no_event_(FavEntry &e, bool shown, int64_t now_epoch);
  const GameSnapshot &ha_game_() const;  // the game Home Assistant's sensors follow
  void start_fav_job_(uint32_t now);
  void apply_fav_job_(uint32_t now);
  uint32_t live_away_id_{0};      // the followed live game's away team
  uint32_t live_started_ms_{0};   // when the current live game was picked,
                                  // or, in the fallback, when the last scan ran
  bool live_none_{false};         // last scan found nothing in progress
  // With nothing live, the wait until the next scan: up to the next kickoff.
  uint32_t live_rescan_ms_{2 * 60 * 1000};
  // A live mode with nothing live: the board shows the saved team's card and
  // keeps scanning. The mode the owner picked does not change.
  bool live_fallback_{false};

  // The finals rotation (recap.h): a live mode with nothing live on a panel
  // with no team. Finals are kept across live spells and only re-read for a
  // day the panel has not seen; the cards are rebuilt from them.
  bool recap_on_{false};
  std::vector<::espn::RecapDay> recap_days_;    // every league seen, newest day first
  std::vector<::espn::RecapCard> recap_cards_;  // the leagues this mode reads; index into recap_days_
  size_t recap_step_{0};
  uint32_t recap_step_ms_{0};  // when the card on the board went up, 0 = none yet
  std::string recap_message_, recap_ticker_;
  int64_t recap_scan_next_{0};  // the last scan's next kickoff today, 0 for none
  // Per league (0 NFL, 1 college). walk: next day back (or ahead) to read,
  // 1 to kRecapMaxDays; 0 not started; kRecapMaxDays + 1 done. walk_day:
  // the ESPN day a walk finished, so a fruitless one is tried again
  // the next day rather than every scan.
  uint8_t recap_back_[2]{0, 0};
  int32_t recap_back_day_[2]{0, 0};
  uint8_t recap_fwd_[2]{0, 0};
  int32_t recap_fwd_day_[2]{0, 0};
  int64_t recap_fwd_next_[2]{0, 0};
  bool recap_job_(Job &j, int32_t today);  // fills j with the next walk step, if any
  void apply_recap_job_();
  void recap_absorb_(const Job &j, int32_t today);  // a scan's finals and next kickoff
  int64_t recap_next_kickoff_() const;
  void recap_rebuild_();
  void recap_tick_();
  void recap_show_();

  Schedule schedule_;
  uint32_t schedule_fetched_ms_{0};
  // Soccer: the saved team's reads per competition (as Job::comps), the cup
  // read due next (-1 when none) and the last game seen to finish.
  std::vector<Schedule> comps_;
  int comp_next_{-1};
  std::string done_event_;
  // Refresh Now: re-read the team endpoint on the next cycle, whatever
  // the timestamp says. Cleared once that read succeeds, so a refresh
  // that fails on a flaky network is retried rather than dropped.
  bool force_schedule_{false};
  std::vector<::espn::Upcoming> upcoming_;  // next games after the one on the board
  bool upcoming_due_{false};  // fetch the list on its own cycle, after the board has its game
  // True while the demo build's scripted game owns the board. Always false
  // in release firmware, so the checks on it compile away.
  bool demo_playing_() const {
#ifdef GAMEDAY_DEMO
    return this->demo_kind_ != 0 && !this->demo_gap_;
#else
    return false;
#endif
  }
#ifdef GAMEDAY_DEMO
  // demo.cpp
  struct DemoPrefs {
    uint32_t pro_home, pro_away;          // NFL ESPN ids, 0 = the default pick
    uint32_t college_home, college_away;  // NCAA ESPN ids, 0 = the default pick
    uint8_t madeup;                       // 1 = made-up teams, no logos
    uint8_t running;                      // the demo playing at power off, 0 = none
  } __attribute__((packed));
  ESPPreferenceObject demo_pref_;
  DemoPrefs demo_prefs_{};
  uint8_t demo_kind_{0};    // what is playing, 0 = off
  bool demo_gap_{false};    // between runs: the normal board's turn
  uint8_t demo_resume_{0};  // a demo to pick back up after boot
  uint8_t demo_step_{0};
  uint32_t demo_next_ms_{0};
  GameSnapshot demo_saved_game_, demo_saved_prev_;
  std::string demo_json_{"{}"};  // GET /gameday/demo, under state_mutex_
  void demo_setup_();
  bool demo_loop_();  // true while the scripted game owns the board
  void demo_tick_();
  void demo_show_step_(bool splash);
  ::espn::DemoMatchup demo_matchup_(uint8_t kind) const;
  uint8_t demo_league_() const;
  void rebuild_demo_json_();
  void handle_demo_(AsyncWebServerRequest *request);
  void apply_demo_(const std::vector<std::pair<std::string, std::string>> &kv);
#endif
  GameSnapshot game_;
  GameSnapshot prev_;
  uint32_t post_since_ms_{0};
  uint32_t next_fetch_ms_{0};
  uint8_t misses_{0};
  uint8_t fail_streak_{0};      // ESPN reads failed in a row, for the backoff
  bool throttled_{false};       // ESPN said 403 or 429 since the last good read
  uint32_t backoff_since_ms_{0};
  uint32_t backoff_ms_{0};      // 0 = no wait running
  uint32_t last_good_ms_{0};  // millis() of the last successful game poll, 0 = never
  uint32_t busy_since_ms_{0};  // millis() when the worker task was started
  uint32_t worker_seq_{0};     // bumped when a worker is abandoned
  Job job_;
  uint32_t generation_{0};
  bool busy_{false};
  bool selects_published_{false};
  volatile bool job_done_{false};
};

class UpdateTrigger : public Trigger<const UpdateFields &> {
 public:
  explicit UpdateTrigger(GamedayComponent *parent) {
    parent->add_on_update_callback([this](const UpdateFields &f) { this->trigger(f); });
  }
};

// Fires on the main loop for POST /gameday/action?do=<name>.
class ActionTrigger : public Trigger<std::string> {
 public:
  explicit ActionTrigger(GamedayComponent *parent) {
    parent->add_on_action_callback([this](std::string name) { this->trigger(std::move(name)); });
  }
};

}  // namespace gameday
}  // namespace esphome
