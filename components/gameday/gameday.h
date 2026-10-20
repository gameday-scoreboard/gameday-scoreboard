#pragma once

#include <atomic>
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
#include "badge.h"
#include "captive.h"
#include "espn_parse.h"
#include "favorites.h"
#include "guard.h"
#include "ha_events.h"
#include "board_text.h"
#include "health.h"
#include "line_score.h"
#include "message.h"
#include "offseason.h"
#include "recap.h"
#include "score_log.h"
#include "poll_timing.h"
#include "set_request.h"
#include "startup.h"
#include "update_mirror.h"
#include "wifi_history.h"
#ifdef GAMEDAY_DEMO
#include "demo.h"
#endif
#if defined(GAMEDAY_BETA_CHANNEL) || defined(GAMEDAY_UPDATE_MIRROR)
#include "esphome/components/http_request/update/http_request_update.h"
#endif

namespace esphome {
namespace gameday {

using ::espn::GameSnapshot;
using ::espn::GameState;
using ::espn::League;
using ::espn::Mode;
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
  // A side with no logo shows its abbreviation badge in the logo slot (badge.h):
  // the fill is its color above, the ring this second color. Only the demo
  // build's made-up teams set these; release firmware leaves them false.
  bool team_badge{false};
  bool opponent_badge{false};
  uint32_t team_ring{0};
  uint32_t opponent_ring{0};
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
  // Home Assistant's Game Live and the two score sensors, for the same game.
  // The scores are -1 with no game or before kickoff.
  bool ha_live{false};
  int ha_team_score{-1};
  int ha_opp_score{-1};
  // The off-season clock (offseason.h) on the no-game screen, in team_color.
  // next_line is the next game for a wide board, its kickoff on a second
  // line; empty when none is known, and the page shows the date instead.
  bool clock{false};
  std::string next_line;
};

enum class SelectType : uint8_t { TEAM, TIMEZONE, MODE, FAVORITE };

// PSRAM text (logo_cache.h's allocator): the message card's list.
using PsString = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

// Favorite Teams mode holds up to this many teams. The WizMote's buttons 1-4
// jump to the first four.
static constexpr uint8_t FAV_MAX = 16;

class GamedayComponent;

using ::espn::parse_team_ref;

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
//   GET  /gameday/state              full snapshot + settings, and "wifi" (wifi_history.h)
//   GET  /gameday/heap               free heap now: internal, largest internal block, PSRAM (KiB)
//   POST /gameday/set?key=value...   team=nfl:6|none mode=0-4 rotate=2-30 fav1..fav4=nfl:6|none
//                                    tz=<name, IANA name or index> tzauto=0/1
//                                    down/count/match/play/odds/opp=0/1 panels=1/2
//                                    (down: football, count: baseball, match: soccer)
//                                    lockon=5-120 (min) release=30-3600 (s) collide=0 stick|1 alternate
//                                    autoupd=0/1 (install updates on its own, auto_update.h)
//                                    beta=0/1 (follow the beta channel; release builds only)
//   POST /gameday/action?do=refresh|qc   (qc: the factory check, firmware/pages/qc.yaml)
//   POST /gameday/action?do=say&text=...&secs=N   the banner (message.h) for N s, 10 when left out;
//                                    no text takes it down
//   POST /gameday/set?msg=...&msgurl=...&msgevery=0-30   the message card (message.h)
//   GET  /gameday/wifi               the saved Wi-Fi networks in the order they are tried,
//                                    names only (components/saved_networks)
//   POST /gameday/wifi?forget=<ssid> drops one saved network; 404 when it is not saved
//   GET  /gameday/crash              the last crash, watchdog or brownout restart and its
//                                    backtrace, kept in flash (components/crash_report)
//   POST /gameday/crash?clear=1      empties it
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
  // Scores and finals of the game Home Assistant follows (ha_events.h).
  void add_on_ha_event_callback(std::function<void(const ::espn::HaEvent &)> &&cb) {
    this->ha_event_callbacks_.push_back(std::move(cb));
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
  void set_live_mask_(uint16_t mask);
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
  bool ticker_baseball_count() const { return (this->prefs8_.ticker & TICK8_COUNT) != 0; }
  bool ticker_soccer_stats() const { return (this->prefs8_.ticker & TICK8_MATCH) != 0; }
  bool ticker_last_play() const { return this->flag_(FLAG_PLAY); }
  bool ticker_odds() const { return this->flag_(FLAG_ODDS); }
  bool opponent_splashes() const { return this->flag_(FLAG_OPP); }
  bool setup_done() const { return this->flag_(FLAG_SETUP); }
  // The first real scoreboard since boot, or since the team last changed,
  // is on the board. The boot screen waits for it (boot.yaml, boot_hide).
  bool board_ready() const { return this->board_ready_; }
  // The Wi-Fi answered ESPN with a sign-in page (captive.h) and ESPN has not
  // answered since. The boot screen says so over the board (boot.yaml).
  bool captive() const { return this->captive_; }
  // Bench only: read some other address in place of captive.h's kProbeUrl,
  // such as a host on the LAN that answers like a portal. Pass a string
  // literal: only the pointer is kept, and the probe task reads it.
  void set_probe_url(const char *url) { this->probe_url_.store(url); }
  // What the panel has downloaded from ESPN since boot: reply bodies and
  // logos. Each request also spends about 6 KB on headers and the TLS
  // handshake, which this does not see.
  uint64_t espn_bytes() const { return this->espn_bytes_.load() + this->logos_.bytes_downloaded(); }
  uint32_t espn_requests() const { return this->espn_requests_.load(); }
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
  void set_ticker_baseball_count(bool on) { this->set_ticker8_(TICK8_COUNT, on); }
  void set_ticker_soccer_stats(bool on) { this->set_ticker8_(TICK8_MATCH, on); }
  void set_ticker_last_play(bool on) { this->set_flag_(FLAG_PLAY, on); }
  void set_ticker_odds(bool on) { this->set_flag_(FLAG_ODDS, on); }
  void set_opponent_splashes(bool on) { this->set_flag_(FLAG_OPP, on); }
  // "Install updates automatically", off unless the owner turns it on.
#ifdef GAMEDAY_BAR
  // The bar build (firmware/pages/bar.yaml) always installs from the beta
  // channel; there is no setting.
  bool auto_update() const { return true; }
  void set_report_email(const std::string &email) { this->report_email_ = email; }
#else
  bool auto_update() const { return this->prefs6_.auto_update != 0; }
#endif
  void set_auto_update(bool on);
  // Heat and power protection (guard.h). guard_brightness() is what the
  // display gets for the owner's Brightness; it runs from on_boot before
  // setup(), so it loads the power log itself the first time.
  uint8_t guard_brightness(uint8_t want);
  // The chip temperature sensor's readings (firmware/pages/guard.yaml).
  void set_chip_temp(float c);
  // What the panel's corner says: 0 nothing, 1 too warm, 2 check power.
  uint8_t guard_mark() const { return (uint8_t) ::espn::guard_mark(this->heat_.step, this->power_log_.capped != 0); }
  // The owner's clear of the power cap: /gameday/action?do=power_ok.
  void clear_power_cap();
  // "Follow beta": the update entity reads the beta manifest instead of the
  // release one. Off unless the owner turns it on; only in builds with
  // beta_channel (the release builds, gameday-common.yaml).
#ifdef GAMEDAY_BETA_CHANNEL
  void set_beta_channel(http_request::HttpRequestUpdate *update, const char *stable, const char *beta) {
    this->beta_update_ = update;
    this->stable_source_ = stable;
    this->beta_source_ = beta;
  }
  bool follow_beta() const { return this->follow_beta_ != 0; }
#else
  bool follow_beta() const { return false; }
#endif
  void set_follow_beta(bool on);
  // The banner (message.h): text over everything for secs seconds (0 is
  // the default), from the device page, the app or Home Assistant. "" takes
  // it down. say_text() is what firmware/pages/message.yaml draws.
  void say(const std::string &text, int secs);
  const std::string &say_text() const { return this->say_text_; }
  int say_secs() const { return this->say_secs_; }
  // The message card's page on the scoreboard page now, "" while it is
  // down (gd_msg_card in firmware/pages/gameday-live.yaml). A page too tall
  // for the panel rolls up; the page says how long one pass takes, and the
  // card stays up at least that long.
  const std::string &msg_card() const { return this->msg_card_; }
  // The board game's line score at halftime and the final, laid out for a
  // display width_px wide (line_score.h); font 0 the rest of the time. Read by
  // gd_update in firmware/pages/gameday-live.yaml.
  ::espn::LineScoreText line_score(int width_px) const { return ::espn::line_score_text(this->game_, width_px); }
  void set_msg_roll_ms(uint32_t ms) { this->msg_roll_ms_ = ms; }
  // The update feed's second host (update_mirror.h): `mirror` is `source`
  // on the mirror, `beta_mirror` the beta manifest there (release builds).
#ifdef GAMEDAY_UPDATE_MIRROR
  void set_update_mirror(http_request::HttpRequestUpdate *update, const char *source, const char *mirror,
                         const char *beta_mirror) {
    this->mirror_update_ = update;
    this->primary_source_ = source;
    this->mirror_source_ = mirror;
    this->beta_mirror_source_ = beta_mirror;
  }
#endif

  void setup() override;
  void loop() override;
  void dump_config() override;
  // Marks the restart as asked for, in the boot log (health.h).
  void on_shutdown() override;
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
  // live_before: live_leagues_() from before the change.
  void favorites_changed_(bool first_pick, const std::vector<League> &live_before);
  uint8_t fav_league_(uint8_t i) const;  // i = 0..FAV_MAX-1
  uint32_t fav_id_(uint8_t i) const;     // 0 = empty
  void put_fav_(uint8_t i, uint8_t league, uint32_t id);  // no save
  void handle_set_(AsyncWebServerRequest *request);
#ifdef USE_GAMEDAY_SAVED_NETWORKS
  void handle_wifi_(AsyncWebServerRequest *request);
#endif
  void apply_set_(const std::vector<std::pair<std::string, std::string>> &kv);
  void rebuild_state_(const UpdateFields *f);  // main loop only
  std::string current_state_json_();
  // The "health" object, read live on each GET /gameday/state (HTTP task).
  std::string health_json_();
  ::espn::Restart start_reason_{::espn::Restart::NONE};
  bool boot_clock_noted_{false};  // boot_log_clock() ran
  // guard.h. heat_ and power_log_ are main loop only; the HTTP task reads
  // their small fields for the health block.
  void load_power_log_();
  void save_power_log_();
  // The banner and the message card (message.h). The card's settings are
  // their own blob, its text and address kept here only as long as they are.
  struct PrefsMsg {
    char text[::espn::kMsgTextMax + 1];
    char url[::espn::kMsgUrlMax + 1];
    uint8_t every;
    uint8_t spare[3];
  } __attribute__((packed));
  ESPPreferenceObject msg_pref_;
  std::string msg_text_, msg_url_;
  uint8_t msg_every_{::espn::kMsgEveryDefault};
  void load_msg_();
  void set_msg_(const std::string *text, const std::string *url, int every);
  void msg_reset_();    // a new text or address: back to the top, up soon
  void msg_tick_();     // main loop: the card's turns and the reads they need
  void msg_show_(const std::string &page);
  std::string say_text_;
  int say_secs_{::espn::kSayDefaultSecs};
  // The entries on hand, [msg_from_, msg_from_ + msg_n_), one per line:
  // the saved text, or one read's window of the list (PSRAM).
  PsString msg_list_;
  uint32_t msg_from_{0}, msg_n_{0};
  bool msg_eof_{false};
  uint32_t msg_list_ms_{0};  // millis() of the read, 0 = none
  uint32_t msg_pos_{0};      // the entry the next page starts at
  bool msg_read_due_{false};
  uint32_t msg_read_from_{0};
  uint32_t msg_fail_ms_{0};  // millis() of the last failed read, 0 = none since
  int msg_status_{-1};       // the last read's HTTP status, 0 no answer, -1 none yet
  uint32_t msg_total_{0};    // entries in the file, once a read got to its end
  std::string msg_card_;
  bool msg_shown_{false};
  uint32_t msg_since_ms_{0};  // when the card last went up or down
  uint32_t msg_tick_ms_{0};
  uint32_t msg_roll_ms_{0};
  uint32_t splash_ms_{0};  // the last splash, so the card never covers one
  ::espn::HeatGuard heat_;
  ::espn::PowerLog power_log_{};
  ESPPreferenceObject power_pref_;
  bool power_loaded_{false};
  bool power_settled_{false};  // ran kPowerSettledMs without a brownout
  // Wi-Fi history (wifi_history.h): sampled and fed link changes on the main
  // loop, read as the "wifi" object of GET /gameday/state on the HTTP task.
  void wifi_tick_();
  std::string wifi_json_();
  ::espn::WifiHistory wifi_hist_;
  std::mutex wifi_mutex_;
  uint32_t wifi_sample_ms_{0};
  bool wifi_up_{false};  // under wifi_mutex_, as is wifi_rssi_
  int8_t wifi_rssi_{0};  // the last sample
  uint32_t wifi_reason_seen_{0};  // wifi_reason_seq when the link last came up
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
  // The ticker's situation line per sport. Football's stays FLAG_DOWN in
  // Prefs; Prefs::flags is full. The spare bits are for later settings,
  // since a blob that changes size fails to load and resets.
  struct Prefs8 {
    uint8_t ticker;  // TICK8_*
    uint8_t spare[3];
  } __attribute__((packed));
  static constexpr uint8_t TICK8_COUNT = 1;  // baseball count, outs, pitcher and batter
  static constexpr uint8_t TICK8_MATCH = 2;  // soccer goals, cards, possession and shots
  void set_ticker8_(uint8_t f, bool on);

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
    bool cup_ok{false};      // the cup's team read answered
    bool polled{false};      // a cup read polled the game it picked
    std::string done_event;  // a game seen to finish, passed over by the pick
    bool no_lookahead{false};  // My team: a final is still lingering on the board
    int8_t lookahead{0};       // 0 not run, 1 found the next game, 2 nothing ahead, -1 a read failed
    // The finals rotation. A scan keeps the day's finals and the games still
    // to come too when asked, and a recap job reads one other day's
    // scoreboard for one league.
    bool want_finals{false};
    std::vector<::espn::Final> finals;
    std::vector<::espn::Pending> pending;
    bool need_recap{false};
    bool recap_forward{false};  // looking for the next kickoff, not for finals
    uint8_t recap_league{0};
    int32_t recap_day{0};
    bool recap_ok{false};
    // ESPN answered 403 or 429 to a request in this job (set by open_)
    bool throttled{false};
    uint32_t retry_after_ms{0};
    // No time server answer yet: read only the Date header of an ESPN reply.
    bool need_clock{false};
    int64_t clock_epoch{0};    // 0 when there was no reply or no usable date
    uint32_t clock_got_ms{0};  // millis() when the reply's headers arrived
    // The message card's list (message.h): kMsgChunkLines entries from
    // msg_from, read on this task so it never runs alongside an ESPN read.
    bool need_msg{false};
    std::string msg_url;
    uint32_t msg_from{0};
    PsString msg_list;
    uint32_t msg_n{0};
    uint32_t msg_seen{0};  // entries in the file up to where the read stopped
    bool msg_eof{false};
    bool msg_ok{false};
    int msg_status{0};  // HTTP status, 0 for no answer
  };
  void start_job_();
  void start_worker_();
  static void worker_(void *arg);
  void run_job_();
  void apply_job_();
  void start_clock_job_();
  void apply_clock_job_();
  void start_msg_job_();
  void run_msg_job_();
  void apply_msg_job_();
  bool fetch_schedule_(const ::espn::Team *team, Schedule &out, const char *comp_slug = nullptr);
  void run_cup_job_();
  void apply_cup_job_(uint32_t now);
  bool fetch_upcoming_(const ::espn::Team *team, std::vector<::espn::Upcoming> &out);
  bool fetch_game_(const ::espn::Team *team, const Schedule &schedule, GameSnapshot &out);
  // epoch picks the day (now when 0); finals and pending, when given, also
  // get that day's finished games and the ones not started yet.
  bool fetch_live_games_(League league, std::vector<::espn::LiveGame> &out, int64_t &next_kickoff,
                         std::vector<::espn::Final> *finals = nullptr,
                         std::vector<::espn::Pending> *pending = nullptr, int64_t epoch = 0);
  // The mode the panel runs: the saved one, except that with no team picked
  // My team (and Favorites with none set) runs as Live anything.
  uint8_t effective_mode_() const {
    return this->no_team_default() ? (uint8_t) Mode::LIVE_ANY : this->prefs2_.mode;
  }
  static bool is_live_mode_(uint8_t m) {
    return (m >= (uint8_t) Mode::LIVE_NFL && m <= (uint8_t) Mode::LIVE_ANY) || m == (uint8_t) Mode::LIVE;
  }
  bool live_mode_() const { return is_live_mode_(this->effective_mode_()); }
  // The leagues the scan reads (set_request.h). The finals rotation covers
  // the same ones, as recap_bit()s, so it never walks a league the scan skips.
  std::vector<League> scan_leagues_() const {
    uint8_t m = this->effective_mode_();
    return ::espn::scan_leagues(m, m == (uint8_t) Mode::LIVE ? this->live_leagues_() : std::vector<League>{});
  }
  uint16_t recap_leagues_() const {
    uint16_t mask = 0;
    for (League l : this->scan_leagues_())
      mask |= ::espn::recap_bit((uint8_t) l);
    return mask;
  }
  // Mode 4 with at least one favorite set; with none it behaves like My Team.
  bool favorites_mode_() const { return this->prefs2_.mode == (uint8_t) Mode::FAVORITES && !this->fav_.empty(); }
  uint32_t our_id_() const;  // team id the snapshot is oriented around
  std::shared_ptr<http_request::HttpContainer> open_(const std::string &url);
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
  bool hold_for_logos_(const UpdateFields &f);
  bool logo_hold_done_();
  void draw_(const UpdateFields &f);
  // The logos on the board, and an update with new ones waiting for them
  // (hold_for_logos_) since logo_hold_ms_.
  std::string shown_team_logo_, shown_opp_logo_;
  uint32_t logo_hold_ms_{0};
  bool logo_held_{false};
  // Turns f into the off-season clock card when the board has nothing to
  // show for months (offseason.h); leaves it alone otherwise.
  void clock_fields_(UpdateFields &f);
  void reset_game_();
  // Clears the board and scans again now, for a change in what the mode
  // follows. The caller starts the boot screen hold first.
  void restart_board_();
  // Restarts the board when Live games now scans other leagues than before.
  bool live_leagues_changed_(const std::vector<League> &before);
  void drop_team_placeholder_();
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
    this->fail_run_.clear();
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
  std::vector<std::function<void(const ::espn::HaEvent &)>> ha_event_callbacks_;
  ::espn::HaTracker ha_tracker_;
  // The board game's scores as they went by, for the halftime and final
  // tickers (score_log.h): about 1 KB of internal RAM for two games.
  ::espn::ScoreLog score_log_;

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
  int32_t auto_update_skipped_{0};   // the night it last logged skipping a rolled-back release
  // The install under way and the release that rolled back here, in its own
  // blob so Prefs6 keeps its size (auto_update.h, UpdateMark).
  ESPPreferenceObject update_mark_pref_;
  ::espn::UpdateMark update_mark_{};
  bool update_installing_{false};  // the update entity is installing, mark written
  void settle_update_mark_();
  void update_state_changed_();
#ifdef GAMEDAY_BAR
  // fnv1_hash() of the version last installed through "Install now"
  // (auto_update.h), saved before the install so a crash or power cut
  // partway through doesn't try the same version again until the night.
  ESPPreferenceObject bar_pref_;
  uint32_t now_tried_{0};
  // Where the bar's "Something wrong?" link on the device page writes to.
  std::string report_email_;
#endif
#ifdef GAMEDAY_BETA_CHANNEL
  http_request::HttpRequestUpdate *beta_update_{nullptr};
  const char *stable_source_{nullptr};
  const char *beta_source_{nullptr};
  ESPPreferenceObject beta_pref_;
  uint8_t follow_beta_{0};  // 1 = read the beta manifest
  void apply_channel_();
#endif
#ifdef GAMEDAY_UPDATE_MIRROR
  http_request::HttpRequestUpdate *mirror_update_{nullptr};
  const char *primary_source_{nullptr};
  const char *mirror_source_{nullptr};
  const char *beta_mirror_source_{nullptr};
  uint32_t mirror_since_ms_{0};
  bool on_mirror_{false};
  bool mirror_answered_{false};
  void mirror_tick_();
  void use_primary_();
#endif
  void auto_update_tick_();
  bool followed_game_live_() const;
  ESPPreferenceObject pref7_;
  Prefs7 prefs7_{};
  ESPPreferenceObject pref8_;
  Prefs8 prefs8_{};
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
  uint32_t http_clock_ms_{0};   // millis() of the last Date header read, 0 = none since Wi-Fi came up
  bool clock_approx_{false};    // the clock came from ESPN's Date header and no time server has answered since
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
  static_assert(::espn::leagues_fill_indexes(), "League values must run 0 to kLeagueCount - 1");
  // Per league, indexed by League. walk: next day back (or ahead) to read,
  // 1 to kRecapMaxDays; 0 not started; kRecapMaxDays + 1 done. walk_day:
  // the league's ESPN day a walk finished, so a fruitless one is tried again
  // the next day rather than every scan. fwd_match: "BOS at NYR", the first
  // game on the day the forward walk found.
  uint8_t recap_back_[::espn::kLeagueCount]{};
  int32_t recap_back_day_[::espn::kLeagueCount]{};
  uint8_t recap_fwd_[::espn::kLeagueCount]{};
  int32_t recap_fwd_day_[::espn::kLeagueCount]{};
  int64_t recap_fwd_next_[::espn::kLeagueCount]{};
  std::string recap_fwd_match_[::espn::kLeagueCount];
  bool recap_job_(Job &j, int64_t tnow);  // fills j with the next walk step, if any
  void apply_recap_job_();
  void recap_absorb_(const Job &j, int64_t tnow);  // a scan's finals, games to come and next kickoff
  // The next kickoff after today's games, and its matchup when known.
  int64_t recap_next_kickoff_(std::string *matchup = nullptr) const;
  // "college ", "NHL ", or "" when the mode reads several leagues: the word
  // in "No live NHL games".
  std::string live_word_() const;
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
  void demo_badges_(UpdateFields &f) const;  // made-up teams: badges in the logo slots
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
  ::espn::Backoff fail_run_;
  uint32_t backoff_since_ms_{0};
  uint32_t backoff_ms_{0};      // 0 = no wait running
  uint32_t last_good_ms_{0};  // millis() of the last successful game poll, 0 = never
  // ESPN itself answered that the team on the board has no game. Until it
  // has, an empty board after a failed read means ESPN could not be reached.
  bool no_game_confirmed_{false};
  // What an empty board says in place of "No upcoming game" (no_game_text),
  // for the board and for the page and the app. Empty when the board shows
  // a game, or its default or a live mode's words.
  std::string no_game_why_() const;
  uint32_t busy_since_ms_{0};  // millis() when the worker task was started
  uint32_t gave_up_ms_{0};     // millis() when the loop last wrote a worker off, 0 = never
  uint32_t worker_seq_{0};     // bumped when a worker is abandoned
  Job job_;
  uint32_t generation_{0};
  bool busy_{false};
  bool selects_published_{false};
  volatile bool job_done_{false};
  // Written by the fetch task, read by the main loop.
  std::atomic<uint64_t> espn_bytes_{0};
  std::atomic<uint32_t> espn_requests_{0};
  // The last ESPN read through open_() (every read but logos and the probe):
  // its HTTP status (0 for no answer)
  // and millis() when it came, 0 before the first. Fetch task, HTTP task.
  std::atomic<int> espn_status_{0};
  std::atomic<uint32_t> espn_answer_ms_{0};

  // Sign-in Wi-Fi (captive.h). The probe runs on its own short task, since
  // it is needed before the clock is set, when no fetch job runs.
  void captive_tick_();
  void set_captive_(bool on);
  void start_probe_();
  static void probe_task_(void *arg);
  bool captive_{false};
  // The probe saw a sign-in page while ESPN reads were still fine: ESPN is
  // asked at once, and captive_ is set only if that read fails.
  bool captive_suspect_{false};
  uint32_t read_fails_{0};        // back_off_() calls this boot, main loop only
  uint32_t suspect_fails_mark_{0};
  bool probe_running_{false};     // main loop: a probe task is out
  bool probe_join_done_{false};   // probed since Wi-Fi last came up
  uint32_t probe_ms_{0};          // millis() the last probe started
  uint32_t probe_join_ms_{0};     // millis() Wi-Fi came up, 0 while it is down
  uint32_t probe_ok_mark_{0};     // espn_ok_reads_ when the last probe started
  std::atomic<const char *> probe_url_{::espn::kProbeUrl};
  // Written by the probe task, read by the main loop once probe_done_ is set.
  std::atomic<bool> probe_done_{false};
  std::atomic<uint8_t> probe_result_{0};
  // ESPN answered 200 over HTTPS (open_), from any task: the network is open,
  // whatever the probe said.
  std::atomic<uint32_t> espn_ok_reads_{0};
};

class UpdateTrigger : public Trigger<const UpdateFields &> {
 public:
  explicit UpdateTrigger(GamedayComponent *parent) {
    parent->add_on_update_callback([this](const UpdateFields &f) { this->trigger(f); });
  }
};

// Fire on the main loop when the game Home Assistant follows scores or ends.
class ScoreTrigger : public Trigger<const ::espn::HaEvent &> {
 public:
  explicit ScoreTrigger(GamedayComponent *parent) {
    parent->add_on_ha_event_callback([this](const ::espn::HaEvent &e) {
      if (e.play != "final")
        this->trigger(e);
    });
  }
};

class FinalTrigger : public Trigger<const ::espn::HaEvent &> {
 public:
  explicit FinalTrigger(GamedayComponent *parent) {
    parent->add_on_ha_event_callback([this](const ::espn::HaEvent &e) {
      if (e.play == "final")
        this->trigger(e);
    });
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
