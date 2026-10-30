// Weather on the board (weather.h): the card between games and the next
// game's forecast. Reads run as their own job on the fetch task, like the
// message list's, only while the next ESPN read is a while off, so a poll is
// never held up for more than the one short read already under way.
#include "gameday.h"

#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/time.h"

#include <cstring>

namespace esphome {
namespace gameday {

static const char *const TAG = "gameday.weather";
// A weather read starts only when the next ESPN read is at least this far off.
static const uint32_t WX_ESPN_GAP_MS = 8 * 1000;
// Like the ESPN reads: nothing new for a minute after a written-off worker.
static const uint32_t WX_AFTER_GIVE_UP_MS = 60 * 1000;
// One reply never takes longer than this to read, well inside WORKER_DEADLINE.
static const uint32_t WX_READ_LIMIT_MS = 15 * 1000;

void GamedayComponent::load_wx_() {
  this->wx_pref_ = global_preferences->make_preference<PrefsWx>(fnv1_hash("gameday_wx_v1"));
  PrefsWx p{};
  if (!this->wx_pref_.load(&p))
    p = PrefsWx{};  // never set: the card on, units on auto, the time zone's city
  p.query[sizeof(p.query) - 1] = '\0';
  p.place[sizeof(p.place) - 1] = '\0';
  p.country[sizeof(p.country) - 1] = '\0';
  if (p.off > 1)
    p.off = 0;
  if (p.unit > ::espn::kWxUnitC)
    p.unit = ::espn::kWxUnitAuto;
  if (p.every > ::espn::kWxEveryMax)
    p.every = 0;
  if (p.have && (p.query[0] == '\0' || !(p.lat >= -90.0f && p.lat <= 90.0f && p.lon >= -180.0f && p.lon <= 180.0f)))
    p.have = 0;
  this->wx_ = p;
}

void GamedayComponent::save_wx_() {
  this->wx_pref_.save(&this->wx_);
  this->sync_prefs_();
}

// Any of the four; -1 or a null pointer leaves that one as it is.
void GamedayComponent::set_wx_(int on, const std::string *query, int unit, int every) {
  bool changed = false, moved = false;
  if (on >= 0 && (uint8_t) (on ? 0 : 1) != this->wx_.off) {
    this->wx_.off = on ? 0 : 1;
    changed = true;
  }
  if (query != nullptr && *query != this->wx_.query) {
    memset(this->wx_.query, 0, sizeof(this->wx_.query));
    strncpy(this->wx_.query, query->c_str(), sizeof(this->wx_.query) - 1);
    this->wx_.have = 0;
    memset(this->wx_.place, 0, sizeof(this->wx_.place));
    memset(this->wx_.country, 0, sizeof(this->wx_.country));
    changed = moved = true;
  }
  if (unit >= 0 && unit != this->wx_.unit) {
    this->wx_.unit = (uint8_t) unit;
    changed = true;
  }
  if (every >= 0 && every != this->wx_every_()) {
    this->wx_.every = (uint8_t) every;
    changed = true;
  }
  if (!changed)
    return;
  this->save_wx_();
  if (moved) {
    // A new place: what the card had is for the old one.
    this->wx_gen_++;
    this->wx_now_ = ::espn::WxNow{};
    this->wx_ok_ms_ = this->wx_try_ms_ = 0;
    this->wx_fails_ = 0;
    this->wx_status_ = -1;
    this->wx_geo_missing_ = false;
  }
  ESP_LOGI(TAG, "Weather card %s, %s, every %d min, place %s", this->wx_on_() ? "on" : "off",
           this->wx_.unit == ::espn::kWxUnitF ? "F" : this->wx_.unit == ::espn::kWxUnitC ? "C" : "auto units",
           this->wx_every_(), this->wx_.query[0] ? this->wx_.query : "(the time zone's city)");
  if (this->wx_card_.page != 0)
    this->wx_card_show_(this->wx_on_() && this->wx_now_.ok ? this->wx_card_.page : 0);
  this->rebuild_state_(&this->last_fields_);
}

bool GamedayComponent::wx_fahrenheit_() const {
  if (this->wx_.unit != ::espn::kWxUnitAuto)
    return this->wx_.unit == ::espn::kWxUnitF;
  if (this->wx_.query[0] != '\0')
    return this->wx_.have ? ::espn::wx_country_f(this->wx_.country) : true;
  const ::espn::WxTzCity *c = ::espn::wx_tz_city(::espn::kTimezones[this->prefs_.tz_index].iana);
  return c == nullptr || c->fahrenheit;
}

bool GamedayComponent::wx_where_(float &lat, float &lon, std::string &name) const {
  if (this->wx_.query[0] != '\0') {
    if (!this->wx_.have)
      return false;  // not found yet, or not found at all
    lat = this->wx_.lat;
    lon = this->wx_.lon;
    name = this->wx_.place;
    return true;
  }
  const ::espn::WxTzCity *c = ::espn::wx_tz_city(::espn::kTimezones[this->prefs_.tz_index].iana);
  if (c == nullptr)
    return false;
  lat = c->lat;
  lon = c->lon;
  name = c->name;
  return true;
}

void GamedayComponent::wx_show_now() {
  if (!this->wx_now_.ok)
    ESP_LOGI(TAG, "Weather card asked for, but there is no forecast yet");
  this->wx_force_ = true;
}

void GamedayComponent::wx_card_show_(uint8_t page) {
  if (page == 0) {
    this->wx_card_ = ::espn::WxCard{};
  } else {
    float lat, lon;
    std::string name;
    this->wx_where_(lat, lon, name);
    this->wx_card_ = ::espn::wx_card(this->wx_now_, this->wx_fahrenheit_(), name);
    this->wx_card_.page = page;
  }
  this->wx_since_ms_ = millis();
  this->fire_action_("wx");  // gd_wx_card in firmware/pages/gameday-live.yaml
}

void GamedayComponent::wx_tick_() {
  const uint32_t now = millis();
  if (now - this->wx_tick_ms_ < 250)
    return;
  this->wx_tick_ms_ = now;
  if (this->wx_now_.ok && this->wx_ok_ms_ != 0 && now - this->wx_ok_ms_ >= ::espn::kWxStaleMs) {
    ESP_LOGW(TAG, "Forecast is %u h old: the weather card stops until a read gets through",
             (unsigned) ((now - this->wx_ok_ms_) / 3600000));
    this->wx_now_ = ::espn::WxNow{};
    this->wx_ok_ms_ = 0;
    this->rebuild_state_(&this->last_fields_);
  }
  // The message card's rules: not over a live game, the demo, a splash or the
  // panel's trouble line.
  const bool trouble = ::espn::msg_trouble(this->misses_, this->captive_,
                                           ::espn::clock_late(this->clock_set_(), this->since_join_ms_()));
  const bool between = !this->demo_playing_() && this->last_fields_.game_state != "IN" && !trouble &&
                       (this->splash_ms_ == 0 || now - this->splash_ms_ >= ::espn::kMsgSplashGapMs);
  const uint8_t pages = !this->wx_now_.ok ? 0 : this->wx_now_.ndays > 0 ? 2 : 1;
  // The message card's page up, or just taken down: wait a page's time so the
  // two never run back to back.
  const bool other = this->msg_shown_ || now - this->msg_since_ms_ < ::espn::kWxPageMs;
  if (this->wx_force_) {
    this->wx_force_ = false;
    if (this->wx_on_() && between && pages > 0 && !this->msg_shown_ && this->wx_card_.page == 0) {
      this->wx_card_show_(1);
      return;
    }
  }
  switch (::espn::wx_turn(this->wx_on_(), between, this->wx_card_.page, pages, now - this->wx_since_ms_,
                          this->wx_every_(), ::espn::kWxPageMs, other)) {
    case ::espn::WxTurn::SHOW:
      this->wx_card_show_(1);
      break;
    case ::espn::WxTurn::NEXT:
      this->wx_card_show_(this->wx_card_.page + 1);
      break;
    case ::espn::WxTurn::HIDE:
      this->wx_card_show_(0);
      break;
    case ::espn::WxTurn::NONE:
      break;
  }
}

uint8_t GamedayComponent::wx_job_due_() {
  const uint32_t now = millis();
  if (now - this->wx_due_ms_ < 1000)
    return 0;
  this->wx_due_ms_ = now;
  if (this->busy_ || !network::is_connected() || !this->clock_set_() || this->demo_playing_() ||
      this->time_ == nullptr)
    return 0;
  if (this->gave_up_ms_ != 0 && now - this->gave_up_ms_ < WX_AFTER_GIVE_UP_MS)
    return 0;
  if ((int32_t) (this->next_fetch_ms_ - now) < (int32_t) WX_ESPN_GAP_MS)
    return 0;
  if (this->wx_on_()) {
    float lat, lon;
    std::string name;
    const bool geocode = this->wx_.query[0] != '\0' && !this->wx_.have && !this->wx_geo_missing_;
    if (geocode || this->wx_where_(lat, lon, name)) {
      uint32_t wait = this->wx_fails_ != 0  ? ::espn::wx_backoff_ms(this->wx_fails_)
                      : this->wx_ok_ms_ != 0 ? ::espn::kWxRefreshMs
                                             : 0;
      if (this->wx_try_ms_ == 0 || now - this->wx_try_ms_ >= wait)
        return 1;
    }
  }
  const GameSnapshot &g = this->game_;
  const bool outdoor_sport = g.sport == ::espn::Sport::FOOTBALL || g.sport == ::espn::Sport::BASEBALL ||
                             g.sport == ::espn::Sport::SOCCER;
  if (!::espn::wx_kick_wanted(g.venue_indoor, outdoor_sport, !g.weather.empty(),
                              g.valid && g.state == GameState::PRE && !g.time_tbd, g.venue_place, g.kickoff_epoch,
                              (int64_t) this->time_->timestamp_now()))
    return 0;
  for (const auto &geo : this->wx_geo_)
    if (!geo.ok && !geo.q.empty() && geo.q == g.venue_place)
      return 0;  // the geocoder doesn't know the venue's place
  if (this->wx_kick_event_ != g.event_id || this->wx_kick_ms_ == 0)
    return 2;
  uint32_t wait = this->wx_kick_fails_ != 0 ? ::espn::wx_backoff_ms(this->wx_kick_fails_) : ::espn::kWxKickRefreshMs;
  return now - this->wx_kick_ms_ >= wait ? 2 : 0;
}

void GamedayComponent::start_wx_job_(uint8_t kind) {
  const uint32_t now = millis() == 0 ? 1 : millis();
  this->job_ = Job{};
  this->job_.generation = this->generation_;
  this->job_.wx = kind;
  this->job_.wx_gen = this->wx_gen_;
  Job &j = this->job_;
  if (kind == 1) {
    this->wx_try_ms_ = now;
    if (this->wx_.query[0] != '\0' && !this->wx_.have) {
      j.wx_query = this->wx_.query;
    } else {
      std::string name;
      this->wx_where_(j.wx_lat, j.wx_lon, name);
      j.wx_have = true;
    }
  } else {
    const GameSnapshot &g = this->game_;
    if (this->wx_kick_event_ != g.event_id) {
      this->wx_kick_event_ = g.event_id;
      this->wx_kick_ = ::espn::WxHour{};
      this->wx_kick_fails_ = 0;
      this->wx_kick_ok_ms_ = 0;
    }
    this->wx_kick_ms_ = now;
    j.wx_event = g.event_id;
    j.wx_at = g.kickoff_epoch;
    j.wx_query = g.venue_place;
    for (const auto &geo : this->wx_geo_) {
      if (geo.ok && geo.q == g.venue_place) {
        j.wx_have = true;
        j.wx_lat = geo.lat;
        j.wx_lon = geo.lon;
      }
    }
  }
  this->start_worker_();
}

// Worker task: one GET into body, 200 only, at most kWxBodyMax bytes.
bool GamedayComponent::wx_get_(const std::string &url, PsString &body, int &status) {
  body.clear();
  status = 0;
  std::vector<http_request::Header> headers = {{"User-Agent", "GameDayScoreboard"}, {"Accept", "application/json"}};
  auto container = this->http_->get(url, headers);
  if (container == nullptr) {
    ESP_LOGW(TAG, "No answer from %s", url.c_str());
    return false;
  }
  status = container->status_code;
  if (status != 200) {
    ESP_LOGW(TAG, "HTTP %d for %s", status, url.c_str());
    container->end();
    return false;
  }
  uint8_t buf[256];
  bool complete = false;
  const uint32_t started = millis();
  while (body.size() <= ::espn::kWxBodyMax && millis() - started < WX_READ_LIMIT_MS) {
    int n = container->read(buf, sizeof(buf));
    if (n <= 0) {
      complete = n == 0 && container->is_read_complete();
      break;
    }
    body.append((const char *) buf, (size_t) n);
  }
  container->end();
  if (!complete || body.size() > ::espn::kWxBodyMax) {
    ESP_LOGW(TAG, "Reply cut off or too long (%u bytes) from %s", (unsigned) body.size(), url.c_str());
    return false;
  }
  return true;
}

void GamedayComponent::run_wx_job_() {
  Job &j = this->job_;
  PsString body;
  if (!j.wx_have) {
    if (!this->wx_get_(::espn::wx_geocode_url(j.wx_query), body, j.wx_status))
      return;
    // A JSON answer with no match is the geocoder's "not found"; anything
    // else (a sign-in page) is a failed read, tried again later.
    if (!::espn::parse_wx_geocode(body.data(), body.size(), j.wx_place)) {
      j.wx_no_place = !body.empty() && body[0] == '{';
      return;
    }
    j.wx_lat = j.wx_place.lat;
    j.wx_lon = j.wx_place.lon;
  }
  std::string url = j.wx == 1 ? ::espn::wx_forecast_url(j.wx_lat, j.wx_lon)
                              : ::espn::wx_hour_url(j.wx_lat, j.wx_lon, j.wx_at);
  if (!this->wx_get_(url, body, j.wx_status))
    return;
  j.wx_ok = j.wx == 1 ? ::espn::parse_wx_forecast(body.data(), body.size(), j.wx_now)
                      : ::espn::parse_wx_hour(body.data(), body.size(), j.wx_hour);
  if (!j.wx_ok)
    ESP_LOGW(TAG, "Open-Meteo's answer didn't read (%u bytes)", (unsigned) body.size());
}

void GamedayComponent::apply_wx_job_() {
  Job &j = this->job_;
  const uint32_t now = millis() == 0 ? 1 : millis();
  if (j.wx == 2) {
    if (!j.wx_have && (j.wx_place.ok || j.wx_no_place)) {
      WxGeo &geo = this->wx_geo_[this->wx_geo_next_];
      this->wx_geo_next_ = (uint8_t) ((this->wx_geo_next_ + 1) % 4);
      geo.q = j.wx_query;
      geo.ok = j.wx_place.ok;
      geo.lat = j.wx_place.lat;
      geo.lon = j.wx_place.lon;
      if (!geo.ok)
        ESP_LOGW(TAG, "No place found for the venue's %s: no forecast of our own", j.wx_query.c_str());
    }
    if (j.wx_event != this->wx_kick_event_)
      return;  // the board moved on to another game
    if (!j.wx_ok) {
      if (!j.wx_no_place)
        this->wx_kick_fails_++;
      return;
    }
    this->wx_kick_fails_ = 0;
    this->wx_kick_ = j.wx_hour;
    this->wx_kick_ok_ms_ = now;
    ESP_LOGI(TAG, "Kickoff forecast for %s: %.1f C, code %u", j.wx_query.c_str(), j.wx_hour.temp / 10.0f,
             (unsigned) j.wx_hour.code);
    if (this->game_.valid && this->game_.event_id == j.wx_event && !this->demo_playing_())
      this->emit_({});  // the ticker and the board's badge now
    return;
  }
  if (j.wx_gen != this->wx_gen_)
    return;  // the owner picked another place meanwhile
  this->wx_status_ = j.wx_status;
  if (!j.wx_have && j.wx_place.ok) {
    this->wx_.have = 1;
    this->wx_.lat = j.wx_place.lat;
    this->wx_.lon = j.wx_place.lon;
    memset(this->wx_.place, 0, sizeof(this->wx_.place));
    strncpy(this->wx_.place, j.wx_place.name.c_str(), sizeof(this->wx_.place) - 1);
    memset(this->wx_.country, 0, sizeof(this->wx_.country));
    strncpy(this->wx_.country, j.wx_place.country.c_str(), sizeof(this->wx_.country) - 1);
    this->save_wx_();
    ESP_LOGI(TAG, "Weather place: %s is %s, %s (%.3f, %.3f)", this->wx_.query, this->wx_.place, this->wx_.country,
             this->wx_.lat, this->wx_.lon);
  } else if (!j.wx_have && j.wx_no_place) {
    this->wx_geo_missing_ = true;
    ESP_LOGW(TAG, "Weather place: nothing found for %s", this->wx_.query);
    this->rebuild_state_(&this->last_fields_);
    return;
  }
  if (!j.wx_ok) {
    this->wx_fails_++;
    ESP_LOGW(TAG, "Forecast read failed (%u in a row), next try in %u min", (unsigned) this->wx_fails_,
             (unsigned) (::espn::wx_backoff_ms(this->wx_fails_) / 60000));
    this->rebuild_state_(&this->last_fields_);
    return;
  }
  this->wx_fails_ = 0;
  this->wx_now_ = j.wx_now;
  this->wx_ok_ms_ = now;
  ESP_LOGI(TAG, "Forecast: %.1f C, code %u, %u days", this->wx_now_.temp / 10.0f, (unsigned) this->wx_now_.code,
           (unsigned) this->wx_now_.ndays);
  if (this->wx_card_.page != 0)
    this->wx_card_show_(this->wx_card_.page);  // the new numbers on the page that is up
  this->rebuild_state_(&this->last_fields_);
}

void GamedayComponent::wx_badge_update_() {
  ::espn::WxBadge b;
  const GameSnapshot &g = this->game_;
  if (g.valid && g.state == GameState::PRE && !this->demo_playing_()) {
    const bool fahr = this->wx_fahrenheit_();
    if (!g.weather.empty()) {
      b = ::espn::wx_badge_espn(g.weather_f, g.weather, (::espn::WxIcon) g.weather_icon, fahr);
    } else if (this->wx_kick_.ok && this->wx_kick_event_ == g.event_id && this->wx_kick_ok_ms_ != 0 &&
               millis() - this->wx_kick_ok_ms_ < 2 * ::espn::kWxKickRefreshMs) {
      // Night by the panel's clock: close enough for a sun or a moon.
      bool night = false;
      if (g.kickoff_epoch > 0) {
        int h = ESPTime::from_epoch_local((time_t) g.kickoff_epoch).hour;
        night = h < 6 || h >= 19;
      }
      b = ::espn::wx_badge_hour(this->wx_kick_, night, fahr);
    }
  }
  const bool changed = b.text != this->wx_badge_.text || b.icon != this->wx_badge_.icon;
  this->wx_badge_ = b;
  if (changed)
    this->fire_action_("wxb");  // gd_wx_badge in firmware/pages/gameday-live.yaml
}

}  // namespace gameday
}  // namespace esphome
