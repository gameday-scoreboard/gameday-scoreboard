// The multi-game board: several games at once as tiles (multi_board.h).
// Live games feeds the tiles from its scans, with a scan of its own between
// the followed game's polls while tiles are up; Favorite teams feeds them
// from the favorites' cards. The page draws what tiles() says (gd_tiles in
// firmware/pages/gameday-live.yaml) when the tiles action fires.
#include "gameday.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "team_colors.h"

namespace esphome {
namespace gameday {

static const char *const TAG = "gameday.tiles";
// How often the tiles' own scan runs while two or more tiles are live, and
// while fewer are (then a kickoff is what it waits for).
static const uint32_t TILE_LIVE_SCAN_MS = 45 * 1000;
static const uint32_t TILE_IDLE_SCAN_MS = 3 * 60 * 1000;

// ESPN's primary color for a team (team_colors.h), white when it has none.
static uint32_t tile_team_rgb(uint8_t league, uint32_t espn_id) {
  for (size_t i = 0; i < ::espn::kTeamColorCount; i++)
    if ((uint8_t) ::espn::kTeamColors[i].league == league && ::espn::kTeamColors[i].espn_id == espn_id)
      return ::espn::kTeamColors[i].rgb;
  return 0xFFFFFF;
}

void GamedayComponent::set_tiles(uint8_t v) {
  if (v > (uint8_t) ::espn::Tiles::AUTO || v == this->prefs8_.spare[0])
    return;
  this->prefs8_.spare[0] = v;
  this->pref8_.save(&this->prefs8_);
  this->sync_prefs_();
  ESP_LOGI(TAG, "Games on the board: %s", ::espn::tiles_name(v));
  this->tile_scan_ms_ = 0;  // Live games: the tiles' scan runs on the next cycle
  if (this->live_mode_())
    this->schedule_next_(0);  // and the next cycle is now, not after the followed game's wait
  if (this->favorites_mode_())
    this->tiles_from_favorites_();
  else
    this->tiles_publish_();
  this->rebuild_state_(&this->last_fields_);
}

bool GamedayComponent::tiles_want_() const {
  return this->tiles_setting() != (uint8_t) ::espn::Tiles::ONE && this->display_width_ >= 128 &&
         !this->demo_playing_() && (this->live_mode_() || this->favorites_mode_());
}

// Live games only. The fallback card and the finals rotation rescan on their
// own and those scans feed the tiles, so no scan of the tiles' own then.
bool GamedayComponent::tiles_scan_due_(uint32_t now) const {
  if (!this->tiles_want_() || !this->live_mode_() || this->live_fallback_ || this->recap_on_)
    return false;
  uint32_t wait = this->tile_live_n_ >= 2 ? TILE_LIVE_SCAN_MS : TILE_IDLE_SCAN_MS;
  return this->tile_scan_ms_ == 0 || now - this->tile_scan_ms_ >= wait;
}

// A scan's live games, and with want_finals the day's finals and the games
// to come, become the candidates. A game that was live at the last scan and
// is not in this one ended: it stays as a final the panel saw end, so its
// tile shows "F" for the hold rather than vanishing.
void GamedayComponent::tiles_absorb_scan_(const Job &j) {
  uint32_t now = millis();
  this->tile_scan_ms_ = now == 0 ? 1 : now;
  if (!this->tiles_want_())
    return;
  int64_t tnow = (int64_t) this->time_->timestamp_now();
  std::vector<::espn::FavRef> favs = this->fav_refs_();
  ::espn::PsVector<::espn::TileGame> next;
  next.reserve(j.live.size() + 8);
  for (const auto &g : j.live) {
    ::espn::TileGame t;
    t.event_id = g.event_id;
    t.league = g.league;
    t.state = GameState::IN;
    t.away_abbr = g.away_abbr;
    t.home_abbr = g.home_abbr;
    t.away_id = g.away_id;
    t.home_id = g.home_id;
    t.away_score = g.away_score;
    t.home_score = g.home_score;
    t.period = g.period;
    t.clock_s = g.clock_s;
    t.detail = g.detail;
    t.possession = g.possession;
    t.fav_rank = ::espn::live_fav_rank(g, favs);
    next.push_back(std::move(t));
  }
  auto listed = [&](const std::string &id) {
    for (const auto &t : next)
      if (t.event_id == id)
        return true;
    return false;
  };
  // What the last list knew that this scan does not say again.
  for (const auto &old : this->tile_cands_) {
    if (listed(old.event_id))
      continue;
    if (old.state == GameState::IN) {
      ::espn::TileGame t = old;
      t.state = GameState::POST;
      t.final_epoch = tnow;
      if (t.detail.find("Final") == std::string::npos)
        t.detail = "Final";
      next.push_back(std::move(t));
    } else if (!j.want_finals && (old.state == GameState::POST || old.state == GameState::PRE)) {
      next.push_back(old);  // a lean scan: the finals and games to come stay as they were
    }
  }
  // A final from the scan has no event id (recap.h keeps them small): the
  // sides name it, and a game the list above already has as a final the
  // panel saw end is left alone.
  auto same_sides = [&](uint8_t league, uint32_t away, uint32_t home) {
    for (const auto &t : next)
      if (t.league == league && t.away_id == away && t.home_id == home)
        return true;
    return false;
  };
  if (j.want_finals) {
    for (const auto &f : j.finals) {
      if (same_sides(f.league, f.away_id, f.home_id))
        continue;
      ::espn::TileGame t;
      t.event_id = "final:" + std::to_string(f.league) + ":" + std::to_string(f.away_id) + ":" + std::to_string(f.home_id);
      t.league = f.league;
      t.state = GameState::POST;
      t.kickoff_epoch = f.kickoff_epoch;
      t.away_abbr = f.away_abbr;
      t.home_abbr = f.home_abbr;
      t.away_id = f.away_id;
      t.home_id = f.home_id;
      t.away_score = f.away_score;
      t.home_score = f.home_score;
      t.detail = f.detail;
      for (const auto &old : this->tile_cands_)
        if (old.event_id == t.event_id)
          t.final_epoch = old.final_epoch;
      next.push_back(std::move(t));
    }
    for (const auto &p : j.pending) {
      if (listed(p.event_id))
        continue;
      ::espn::TileGame t;
      t.event_id = p.event_id;
      t.league = p.league;
      t.state = GameState::PRE;
      t.kickoff_epoch = p.kickoff_epoch;
      t.away_abbr = p.away_abbr;
      t.home_abbr = p.home_abbr;
      t.away_id = p.away_id;
      t.home_id = p.home_id;
      next.push_back(std::move(t));
    }
  }
  int live_n = 0;
  for (const auto &t : next)
    live_n += t.state == GameState::IN ? 1 : 0;
  this->tile_cands_ = std::move(next);
  this->tile_live_n_ = live_n;
  this->tiles_publish_();
}

// Favorite teams: every favorite's card is a candidate, the favorite's own
// list position its rank. Two favorites in the same game share one tile.
void GamedayComponent::tiles_from_favorites_() {
  ::espn::PsVector<::espn::TileGame> next;
  int live_n = 0;
  for (size_t i = 0; i < this->fav_.size(); i++) {
    const FavEntry &e = this->fav_[i];
    const GameSnapshot &g = e.game;
    if (!g.valid || g.event_id.empty())
      continue;
    bool dup = false;
    for (const auto &t : next)
      dup = dup || t.event_id == g.event_id;
    if (dup)
      continue;
    ::espn::TileGame t;
    t.event_id = g.event_id;
    t.league = (uint8_t) e.team->league;
    t.state = g.state;
    t.kickoff_epoch = g.kickoff_epoch;
    t.time_tbd = g.time_tbd;
    // The card is oriented around the favorite; a tile reads away at home.
    bool team_away = !g.team_home;
    t.away_abbr = team_away ? g.team_abbr : g.opp_abbr;
    t.home_abbr = team_away ? g.opp_abbr : g.team_abbr;
    t.away_id = team_away ? g.team_id : g.opp_id;
    t.home_id = team_away ? g.opp_id : g.team_id;
    t.away_score = team_away ? g.team_score : g.opp_score;
    t.home_score = team_away ? g.opp_score : g.team_score;
    t.period = (uint8_t) (g.period < 0 ? 0 : g.period > 255 ? 255 : g.period);
    t.clock_s = ::espn::clock_seconds(g.display_clock);
    t.detail = g.short_detail;
    if (g.state == GameState::IN) {
      if (g.sport == ::espn::Sport::BASEBALL) {
        if (g.mlb.half == ::espn::Half::TOP)
          t.possession = 1;
        else if (g.mlb.half == ::espn::Half::BOTTOM)
          t.possession = 2;
      } else if (g.possession == 1) {
        t.possession = team_away ? 1 : 2;
      } else if (g.possession == 2) {
        t.possession = team_away ? 2 : 1;
      }
      live_n++;
    }
    t.fav_rank = (int) i;
    t.final_epoch = e.final_epoch;
    next.push_back(std::move(t));
  }
  this->tile_cands_ = std::move(next);
  this->tile_live_n_ = live_n;
  this->tiles_publish_();
}

void GamedayComponent::tiles_clear_() {
  this->tile_cands_.clear();
  this->tile_slots_.clear();
  this->tile_live_n_ = 0;
  this->tile_scan_ms_ = 0;
  bool was_up = this->tile_board_.count > 1;
  this->tile_board_ = TileBoard{};
  this->logos_.pin_tiles({});
  if (was_up)
    this->fire_action_("tiles");
}

// The plan for this poll, the flashes against the board up now, and the
// hand-off to the page. Fires the tiles action every time: the page is cheap
// and only rewrites what changed.
void GamedayComponent::tiles_publish_() {
  int64_t tnow = this->clock_set_() ? (int64_t) this->time_->timestamp_now() : 0;
  int count = this->tiles_want_() ? ::espn::tile_count(this->tiles_setting(), this->display_width_, this->tile_live_n_,
                                                        (int) this->tile_cands_.size())
                                  : 1;
  ::espn::TileRules rules;
  std::vector<int> plan = ::espn::plan_tiles(this->tile_cands_, this->tile_slots_, count, tnow, rules);
  TileBoard board;
  board.count = count;
  std::vector<std::string> slots, urls;
  struct tm now_local{};
  bool have_now = false;
  if (this->clock_set_()) {
    now_local = this->time_->now().to_c_tm();
    have_now = true;
  }
  for (size_t s = 0; s < plan.size() && s < (size_t) ::espn::kTilesMax; s++) {
    if (plan[s] < 0) {
      slots.push_back("");
      continue;
    }
    const ::espn::TileGame &g = this->tile_cands_[(size_t) plan[s]];
    Tile &t = board.tiles[s];
    t.used = true;
    t.live = g.state == GameState::IN;
    t.event_id = g.event_id;
    t.away_abbr = g.away_abbr;
    t.home_abbr = g.home_abbr;
    t.away_color = tile_team_rgb(g.league, g.away_id);
    t.home_color = tile_team_rgb(g.league, g.home_id);
    t.away_score = g.away_score;
    t.home_score = g.home_score;
    t.possession = t.live ? g.possession : 0;
    if (g.away_id != 0)
      t.away_logo = ::espn::team_logo_url((League) g.league, g.away_id, g.away_abbr.c_str());
    if (g.home_id != 0)
      t.home_logo = ::espn::team_logo_url((League) g.league, g.home_id, g.home_abbr.c_str());
    struct tm kick{};
    if (g.state == GameState::PRE && have_now && g.kickoff_epoch > 0)
      kick = ESPTime::from_epoch_local((time_t) g.kickoff_epoch).to_c_tm();
    t.status = ::espn::tile_status(g, g.state == GameState::PRE && have_now && g.kickoff_epoch > 0 ? &kick : nullptr,
                                   have_now ? &now_local : nullptr);
    // A score since the board up now showed this game.
    for (const Tile &old : this->tile_board_.tiles) {
      if (!old.used || old.event_id != g.event_id)
        continue;
      if (g.away_score > old.away_score)
        t.flash = 1;
      else if (g.home_score > old.home_score)
        t.flash = 2;
    }
    slots.push_back(g.event_id);
    if (count > 1) {
      if (!t.away_logo.empty())
        urls.push_back(t.away_logo);
      if (!t.home_logo.empty())
        urls.push_back(t.home_logo);
    }
  }
  this->tile_slots_ = slots;
  this->logos_.pin_tiles(urls);
  for (const auto &u : urls)
    this->logos_.prefetch(u);
  bool changed = board.count != this->tile_board_.count;
  this->tile_board_ = board;
  if (changed)
    ESP_LOGI(TAG, "Board: %d game(s) at once, %d live of %u", count, this->tile_live_n_,
             (unsigned) this->tile_cands_.size());
  if (count > 1 || changed)
    this->fire_action_("tiles");
}

}  // namespace gameday
}  // namespace esphome
