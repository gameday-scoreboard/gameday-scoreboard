// The off-season clock: when the board shows a clock in place of a game, the
// words under it, and its color. The page draws it on the no-game screen
// (gameday-live.yaml) and keeps the time itself, once a minute, with no
// fetch. Pure, with no ESPHome includes, so the host tests compile it
// directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

#include "espn_parse.h"

namespace espn {

// How far off a daily sport's next game can be and still count as this
// season; the Favorites playlist drops a card past it too. Football has none,
// so a bye week never looks like the off season.
static const int64_t kSeasonHorizon = 10 * 24 * 3600;

inline int64_t season_horizon(Sport sport) { return sport == Sport::FOOTBALL ? 0 : kSeasonHorizon; }

// The team's card is between seasons. NOT_FOUND means the team endpoint named
// no next game; the caller passes it only once a read has said so, not while
// the card is still loading. horizon_s 0 means no horizon.
inline bool off_season(GameState state, int64_t kickoff_epoch, int64_t now, int64_t horizon_s) {
  if (state == GameState::NOT_FOUND)
    return true;
  if (horizon_s <= 0)
    return false;
  if (state == GameState::PRE)
    return kickoff_epoch - now > horizon_s;
  if (state == GameState::POST)
    return now - kickoff_epoch > horizon_s;
  return false;
}

// "Next: DAL vs PHI" at home, "Next: at PHI" away, then sep and the kickoff
// ("Mar 26 7:05 PM", or "Mar 26 TBD" with no time yet, when kick_local is
// from tbd_day_epoch). The board puts the kickoff on its own line; the ticker
// takes it as one line.
inline std::string next_line(const std::string &our_abbr, const std::string &opp_abbr, bool home,
                             const struct tm &kick_local, const struct tm &now_local, const char *sep = " ",
                             bool time_tbd = false) {
  std::string out = "Next:";
  if (opp_abbr.empty())
    out = "Next game";
  else if (!home)
    out += " at " + opp_abbr;
  else if (our_abbr.empty())
    out += " vs " + opp_abbr;
  else
    out += " " + our_abbr + " vs " + opp_abbr;
  return out + sep + kickoff_label(kick_local, now_local, time_tbd);
}

// "3:25 PM": 12-hour, no seconds.
inline std::string clock_time_text(const struct tm &t) {
  int h = t.tm_hour % 12;
  if (h == 0)
    h = 12;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d %s", h, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
  return buf;
}

// "Wed, Oct 15": under the time when no next game is known.
inline std::string clock_date_text(const struct tm &t) {
  static const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char buf[24];
  snprintf(buf, sizeof(buf), "%s, %s %d", kDays[(t.tm_wday % 7 + 7) % 7], kMonths[(t.tm_mon % 12 + 12) % 12],
           t.tm_mday);
  return buf;
}

// Neutral gray on a panel with no team.
static const uint32_t kClockNeutral = 0xA0A0A0;

// The team's color, lifted the way the page lifts team abbreviations: a dark
// color vanishes on the panel, and black would not draw at all.
inline uint32_t clock_color(bool has_team, uint32_t rgb) {
  if (!has_team)
    return kClockNeutral;
  uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  uint32_t m = r > g ? (r > b ? r : b) : (g > b ? g : b);
  if (m == 0)
    return 0xFFFFFF;
  if (m < 0xC0) {
    r = r * 0xC0 / m;
    g = g * 0xC0 / m;
    b = b * 0xC0 / m;
  }
  return (r << 16) | (g << 8) | b;
}

}  // namespace espn
