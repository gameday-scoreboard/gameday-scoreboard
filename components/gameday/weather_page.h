// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// What the Weather page (firmware/pages/weather.yaml) draws, as text for its
// labels: the weather now on the left (icon, temperature, words, today's high
// and low) and the next 6 hours on the right, three columns two hours apart
// (time, icon, temperature, chance of rain). A single panel shows the two in
// turns. A bottom row under both never moves: a live game of the owner's,
// else rain or storms coming in the next 12 hours, else late in the evening
// tomorrow's high and low, else the next sunrise or sunset. The forecast is
// the weather card's own read (weather.h); this file only turns it into
// words, so the host tests cover every screen.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "espn_parse.h"
#include "weather.h"

namespace espn {

// A single panel's turns: the weather now, then the next hours, this long
// each. Two live games take turns on the bottom row this long each.
constexpr int64_t kWxPageTurnS = 10;
constexpr int64_t kWxLiveTurnS = 5;
// The bottom row's rain line: a chance this high in the next this many hours.
constexpr int kWxRainPop = 50;
constexpr int kWxRainHours = 12;
// From this hour on (local, 24-hour) the bottom row has tomorrow's high and
// low instead of the sunrise, until midnight.
constexpr int kWxLateHour = 21;
// The next hours: this many columns, this many hours apart.
constexpr size_t kWxCols = 3;
constexpr int kWxColStep = 2;
// The bottom row in Spleen 5x8: 25 characters on two panels, 13 on one.
constexpr size_t kWxRowWide = 25;
constexpr size_t kWxRowNarrow = 13;

enum class WxPageScreen : uint8_t { MESSAGE, NOW, NEXT, BOTH };

struct WxCol {
  bool on{false};  // the forecast has this hour
  char time[8]{};  // "4PM"
  uint8_t icon{0};
  char temp[16]{};  // "71°"
  char pop[6]{};   // "20%", "" when unknown
};

struct WxPageView {
  WxPageScreen screen{WxPageScreen::MESSAGE};
  // MESSAGE: a title and the line under it.
  char title[16]{};
  char sub[40]{};
  // NOW (and BOTH): the weather now.
  uint8_t icon{0};
  char temp[16]{};   // "71°"
  char words[16]{};  // "Some clouds"
  char hilo[32]{};   // "Hi 80 Lo 66"
  // NEXT (and BOTH): the next hours.
  WxCol col[kWxCols];
  // Every screen: the bottom row, "" for none, in its color.
  char bottom[48]{};
  uint32_t bottom_rgb{0xFFFFFF};
};

struct WxPageIn {
  bool town{false};       // a town is set
  bool not_found{false};  // the lookup answered and knows no such place
  bool fresh{false};      // a forecast read within kWxFreshMs
  bool failing{false};    // the last read or lookup failed
  bool fahrenheit{true};
  bool wide{true};      // two panels, 128 wide
  int32_t now_min{0};   // the panel's local time, minutes since 1970-01-01
  int64_t epoch{0};     // for the turns
  // Live games of the owner's, theirs first on each: up to two.
  uint8_t n_live{0};
  const GameSnapshot *live[2]{nullptr, nullptr};
};

// Colors of the bottom row and the chance of rain.
constexpr uint32_t kWxRgbLive = 0xFFFFFF;
constexpr uint32_t kWxRgbRain = 0x50A8FF;
constexpr uint32_t kWxRgbStorm = 0xFFD040;
constexpr uint32_t kWxRgbSnow = 0xD8E8FF;
constexpr uint32_t kWxRgbSun = 0xFFA040;
constexpr uint32_t kWxRgbTomorrow = 0xC0C0C0;

// "4PM", "12AM" for a local hour 0 to 23.
inline void wx_hour_text(char *out, size_t n, int hour) {
  hour = ((hour % 24) + 24) % 24;
  snprintf(out, n, "%d%s", hour % 12 == 0 ? 12 : hour % 12, hour < 12 ? "AM" : "PM");
}

// "5:22 PM", or "5:22" without the AM or PM, for minutes into the day.
inline void wx_clock_text(char *out, size_t n, int minutes, bool ampm) {
  const int h = minutes / 60 % 24, m = minutes % 60;
  snprintf(out, n, ampm ? "%d:%02d %s" : "%d:%02d", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM");
}

// The hour h (local hours since 1970) of the forecast, nullptr when the read
// doesn't have it.
inline const WxHourly *wx_hour_at(const WxForecast &f, int32_t h) {
  const int32_t k = h - f.hour0;
  if (f.hour0 == 0 || k < 0 || k >= (int32_t) f.n_hours || f.hours[k].code == 255)
    return nullptr;
  return &f.hours[k];
}

// The live game's line: "DAL 21 PHI 17  4:12 - 3rd" on two panels, ESPN's
// own status after the score while it fits; "DAL 21-17" on one panel, the
// owner's side and the score.
inline void wx_live_text(char *out, size_t n, const GameSnapshot &g, bool wide) {
  const size_t max = wide ? kWxRowWide : kWxRowNarrow;
  char score[40];
  if (wide)
    snprintf(score, sizeof(score), "%.6s %d %.6s %d", g.team_abbr.c_str(), g.team_score, g.opp_abbr.c_str(),
             g.opp_score);
  else
    snprintf(score, sizeof(score), "%.6s %d-%d", g.team_abbr.c_str(), g.team_score, g.opp_score);
  const std::string &detail = g.short_detail;
  if (!detail.empty() && strlen(score) + 2 + detail.size() <= max)
    snprintf(out, n, "%s  %s", score, detail.c_str());
  else if (!detail.empty() && strlen(score) + 1 + detail.size() <= max)
    snprintf(out, n, "%s %s", score, detail.c_str());
  else
    snprintf(out, n, "%s", score);
}

// The rain line: the first hour in the next kWxRainHours with a chance of
// kWxRainPop or more, "Storms after 4 PM" ("Storms 4 PM" on one panel), or
// "Rain now". False when none is that likely.
inline bool wx_rain_text(char *out, size_t n, uint32_t &rgb, const WxForecast &f, int32_t now_hour, bool wide) {
  for (int32_t h = now_hour; h < now_hour + kWxRainHours; h++) {
    const WxHourly *w = wx_hour_at(f, h);
    if (w == nullptr || w->pop < kWxRainPop)
      continue;
    const WxIcon icon = wx_icon(w->code, false);
    const char *word = icon == WxIcon::STORM ? "Storms" : icon == WxIcon::SNOW ? "Snow" : "Rain";
    rgb = icon == WxIcon::STORM ? kWxRgbStorm : icon == WxIcon::SNOW ? kWxRgbSnow : kWxRgbRain;
    if (h == now_hour) {
      snprintf(out, n, "%s now", word);
      return true;
    }
    const int hour = (int) (h % 24);
    snprintf(out, n, wide ? "%s after %d %s" : "%s %d %s", word, hour % 12 == 0 ? 12 : hour % 12,
             hour < 12 ? "AM" : "PM");
    return true;
  }
  return false;
}

// The next sunrise or sunset after now: "Sunset 5:22 PM" ("Sunset 5:22" on
// one panel). False when the forecast has neither ahead.
inline bool wx_sun_text(char *out, size_t n, const WxForecast &f, int32_t now_min, bool wide) {
  if (f.day0 == 0)
    return false;
  for (size_t i = 0; i < kWxSunDays; i++) {
    const int32_t day = (f.day0 + (int32_t) i) * 1440;
    const int32_t rise = f.sunrise[i] != kWxNoSun ? day + f.sunrise[i] : 0;
    const int32_t set = f.sunset[i] != kWxNoSun ? day + f.sunset[i] : 0;
    const bool rise_next = rise > now_min && (set <= now_min || rise < set);
    if (rise_next || set > now_min) {
      char t[16];
      wx_clock_text(t, sizeof(t), rise_next ? f.sunrise[i] : f.sunset[i], wide);
      snprintf(out, n, "%s %s", rise_next ? "Sunrise" : "Sunset", t);
      return true;
    }
  }
  return false;
}

// Tomorrow's high and low: "Tomorrow 81 / 63" ("Tmrw 81/63" on one panel).
// False when the forecast has no tomorrow.
inline bool wx_tomorrow_text(char *out, size_t n, const WxForecast &f, int32_t now_min, bool fahrenheit, bool wide) {
  if (f.day0 == 0)
    return false;
  const int32_t ahead = now_min / 1440 + 1 - f.day0;
  if (ahead < 1 || ahead > 2)
    return false;
  const WxDay &d = f.next[ahead - 1];
  snprintf(out, n, wide ? "Tomorrow %d / %d" : "Tmrw %d/%d", wx_deg(d.hi10, fahrenheit), wx_deg(d.lo10, fahrenheit));
  return true;
}

inline void wx_page_message(WxPageView &v, const char *sub) {
  v.screen = WxPageScreen::MESSAGE;
  snprintf(v.title, sizeof(v.title), "Weather");
  snprintf(v.sub, sizeof(v.sub), "%s", sub);
}

// The page for now.
inline void wx_page_compose(const WxPageIn &in, const WxForecast &f, WxPageView &v) {
  v = WxPageView{};
  const bool wide = in.wide;
  // The bottom row's live game comes first on every screen, messages too.
  if (in.n_live > 0) {
    const GameSnapshot *g = in.live[in.n_live > 1 ? (in.epoch / kWxLiveTurnS) % 2 : 0];
    if (g != nullptr) {
      wx_live_text(v.bottom, sizeof(v.bottom), *g, wide);
      v.bottom_rgb = kWxRgbLive;
    }
  }
  if (!in.town) {
    wx_page_message(v, "Set your town on the device page");
    return;
  }
  if (in.not_found) {
    wx_page_message(v, "Town not found");
    return;
  }
  if (!in.fresh || !f.valid) {
    wx_page_message(v, in.failing ? "Can't reach the weather" : "Getting the forecast");
    return;
  }

  v.screen = wide ? WxPageScreen::BOTH : (in.epoch / kWxPageTurnS) % 2 == 0 ? WxPageScreen::NOW : WxPageScreen::NEXT;
  const bool fahr = in.fahrenheit;
  v.icon = (uint8_t) wx_icon(f.code, f.night);
  snprintf(v.temp, sizeof(v.temp), "%d\xC2\xB0", wx_deg(f.t10, fahr));
  snprintf(v.words, sizeof(v.words), "%s", wx_words(f.code, false));
  snprintf(v.hilo, sizeof(v.hilo), "Hi %d Lo %d", wx_deg(f.today.hi10, fahr), wx_deg(f.today.lo10, fahr));

  const int32_t now_hour = in.now_min >= 0 ? in.now_min / 60 : (in.now_min - 59) / 60;
  for (size_t i = 0; i < kWxCols; i++) {
    const int32_t h = now_hour + kWxColStep * (int32_t) (i + 1);
    WxCol &c = v.col[i];
    wx_hour_text(c.time, sizeof(c.time), (int) (h % 24));
    const WxHourly *w = wx_hour_at(f, h);
    if (w == nullptr)
      continue;
    c.on = true;
    c.icon = (uint8_t) wx_icon(w->code, w->night);
    snprintf(c.temp, sizeof(c.temp), "%d\xC2\xB0", wx_deg(w->t10, fahr));
    if (w->pop >= 0)
      snprintf(c.pop, sizeof(c.pop), "%d%%", (int) w->pop);
  }

  if (v.bottom[0] != '\0')
    return;
  uint32_t rgb = kWxRgbRain;
  if (wx_rain_text(v.bottom, sizeof(v.bottom), rgb, f, now_hour, wide)) {
    v.bottom_rgb = rgb;
    return;
  }
  const int local_hour = (int) (((now_hour % 24) + 24) % 24);
  if (local_hour >= kWxLateHour && wx_tomorrow_text(v.bottom, sizeof(v.bottom), f, in.now_min, fahr, wide)) {
    v.bottom_rgb = kWxRgbTomorrow;
    return;
  }
  if (wx_sun_text(v.bottom, sizeof(v.bottom), f, in.now_min, wide)) {
    v.bottom_rgb = kWxRgbSun;
    return;
  }
  if (wx_tomorrow_text(v.bottom, sizeof(v.bottom), f, in.now_min, fahr, wide))
    v.bottom_rgb = kWxRgbTomorrow;
}

// Set by firmware/pages/weather.yaml around a page change the panel makes
// itself (After the game), so the page select's callback doesn't take it
// for somebody picking Weather.
struct WxPageFlags {
  bool own_change{false};
};
inline WxPageFlags &wxp_flags() {
  static WxPageFlags f;
  return f;
}

}  // namespace espn
