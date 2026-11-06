// Curling's feed, ticker, clock row and splashes. Pure, no ESPHome
// includes. Curling is not an ESPN sport: these documents come from the
// Grand Slam of Curling site's data feed (thegrandslamofcurling.com, the
// Sportsnet curling data), plain JSON over HTTPS with no key and no login.
// The feed carries the Grand Slam events for both sides, and in their
// seasons the Brier (men), the Scotties (women), the world championships
// and the Olympics, all as one list of series.
#include "espn_parse.h"

#include <atomic>
#include <cstdlib>

namespace espn {

using detail::add_part;
using detail::pregame_records;
using detail::records_line;

// A pointer to a literal, like set_site_root: one atomic store when a bench
// build swaps it under a read on the worker task.
static std::atomic<const char *> curling_root{"https://thegrandslamofcurling.com"};

void set_curling_root(const char *root) { curling_root.store(root, std::memory_order_relaxed); }

std::string curling_series_url() {
  return std::string(curling_root.load(std::memory_order_relaxed)) + "/curling/static/json/series_list.json";
}

std::string curling_standings_url(int series_id) {
  return std::string(curling_root.load(std::memory_order_relaxed)) + "/curling/live/json/" +
         std::to_string(series_id) + "_standings.json";
}

std::string curling_match_url(int match_id) {
  return std::string(curling_root.load(std::memory_order_relaxed)) + "/curling/live/json/" +
         std::to_string(match_id) + ".json";
}

bool parse_curling_series_str(const std::string &json, int64_t now_epoch, CurlingSeries &out) {
  return parse_curling_series(json, now_epoch, out);
}

bool parse_curling_schedule_str(const std::string &json, uint32_t our_team_id, Schedule &out, CurlingRange &range) {
  return parse_curling_schedule(json, our_team_id, out, range);
}

bool parse_curling_match_str(const std::string &json, uint32_t our_team_id, GameSnapshot &out,
                             const std::string &event_id) {
  return parse_curling_match(json, our_team_id, out, event_id);
}

std::string curling_situation(const GameSnapshot &s, size_t max) {
  if (s.state != GameState::IN || !s.cur.hammer_known)
    return "";
  std::string out = (s.cur.hammer_ours ? s.team_abbr : s.opp_abbr) + " hammer";
  return out.size() > max ? out.substr(0, max) : out;
}

// No clock runs in curling: the end on now, the draw before the start, and
// the final's word ("Final", "Final/EE", "Final/SO") after it. The parser
// leaves all three in short_detail.
std::string curling_clock_text(const GameSnapshot &s) { return s.short_detail; }

std::string curling_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local) {
  std::string out;
  switch (s.state) {
    case GameState::NOT_FOUND:
      return "No upcoming game";
    case GameState::PRE:
      add_part(out, s.game_name);  // "GSOC Masters", "Brier"
      add_part(out, kickoff_local.empty() ? s.short_detail : kickoff_local);
      add_part(out, s.cur.stage);  // "Round Robin"
      if (o.records)
        add_part(out, pregame_records(s));
      add_part(out, detail::venue_line(s, o));
      return out;
    case GameState::POST:
      out = s.short_detail;  // "Final", "Final/EE", "Final/SO"
      add_part(out, s.game_name);
      add_part(out, s.cur.stage);
      add_part(out, records_line(s));
      return out;
    case GameState::IN:
      break;
  }
  if (o.clock)
    add_part(out, s.short_detail);  // "End 3"
  if (s.cur.hammer_known)
    add_part(out, (s.cur.hammer_ours ? s.team_abbr : s.opp_abbr) + " hammer");
  add_part(out, s.cur.stage);
  if (out.empty())
    out = s.short_detail.empty() ? "In Progress" : s.short_detail;
  return out;
}

// An end's points land at once, so the splash names the score. A tied game
// settled by the draw to the button still splashes its winner (cur.shootout,
// with the feed's winner in team_winner). The feed has no team colors, so
// parse_color("") gives its white.
Splash curling_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral) {
  Splash none;
  if (!prev.valid || !cur.valid || prev.event_id != cur.event_id)
    return none;
  if (prev.state == GameState::IN && cur.state == GameState::POST) {
    if (!cur.completed)
      return none;
    if (cur.team_winner || cur.team_score > cur.opp_score)
      return Splash{cur.team_abbr + " WINS!", parse_color(cur.team_color)};
    if (neutral && (cur.opp_score > cur.team_score || (cur.cur.shootout && !cur.team_winner)))
      return Splash{cur.opp_abbr + " WINS!", parse_color(cur.opp_color)};
    return none;
  }
  if (prev.state != GameState::IN || cur.state != GameState::IN)
    return none;
  const int delta = cur.team_score - prev.team_score;
  if (delta > 0)
    return Splash{neutral ? cur.team_abbr + " SCORES " + std::to_string(delta)
                          : "SCORES " + std::to_string(delta) + "!",
                  parse_color(cur.team_color)};
  const int odelta = cur.opp_score - prev.opp_score;
  if (odelta > 0 && opponent_splashes)
    return Splash{cur.opp_abbr + " SCORES " + std::to_string(odelta), parse_color(cur.opp_color)};
  return none;
}

}  // namespace espn
