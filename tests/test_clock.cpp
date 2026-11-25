// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The clock sweep: the firmware's date and clock code run over every quarter
// hour of 2026 to 2028, in every time zone the panel offers. That covers
// midnight, month ends, New Year, a leap day and both clock changes. The
// expected answers come from the C library's calendar and from rules written
// out here. Build with `make` in tests/.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "countdown.h"
#include "espn_parse.h"
#include "favorites.h"
#include "fx_playlists.h"
#include "ha_events.h"
#include "logo_lru.h"
#include "news.h"
#include "night.h"
#include "offseason.h"
#include "poll_timing.h"
#include "recap.h"
#include "season.h"
#include "settings_change.h"
#include "startup.h"
#include "timezones.h"
#include "weather.h"

using namespace espn;

static long checks = 0;
static long failures = 0;
static const char *zone_name = "";

// Print the first few failures of each check, with the moment in the zone
// under test.
static void fail_at(const char *what, int64_t t, const std::string &detail) {
  static std::map<std::string, int> seen;
  failures++;
  if (++seen[what] > 3)
    return;
  char when[40] = "";
  time_t tt = (time_t) t;
  struct tm lt{};
  if (t != 0 && localtime_r(&tt, &lt) != nullptr)
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S %a", &lt);
  printf("FAIL %s: %s %s %s\n", what, zone_name, when, detail.c_str());
}

// The detail is only built for a failure.
#define SWEEP(ok, what, t, detail) \
  do {                             \
    checks++;                      \
    if (!(ok))                     \
      fail_at(what, t, detail);    \
  } while (0)

#define SWEEP_EQ(got, want, what, t)                              \
  do {                                                            \
    const std::string g_ = (got), w_ = (want);                    \
    SWEEP(g_ == w_, what, t, "\"" + g_ + "\" != \"" + w_ + "\""); \
  } while (0)

// ---- The calendar ---------------------------------------------------------------

static bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int month_days(int y, int m) {
  static const int k[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && leap(y) ? 29 : k[m - 1];
}
// Days since 1970-01-01, counted a year and a month at a time.
static int day_number(int y, int m, int d) {
  int n = 0;
  for (int i = 1970; i < y; i++)
    n += leap(i) ? 366 : 365;
  for (int i = 1; i < m; i++)
    n += month_days(y, i);
  return n + d - 1;
}
static std::string strf(const char *fmt, const struct tm &t) {
  char buf[64];
  strftime(buf, sizeof(buf), fmt, &t);
  return buf;
}
// "3:05 PM": strftime's hour without its leading zero.
static std::string time12(const struct tm &t) {
  std::string s = strf("%I:%M %p", t);
  return s[0] == '0' ? s.substr(1) : s;
}
static std::string month_day(const struct tm &t) { return strf("%b ", t) + std::to_string(t.tm_mday); }
static std::string ymd_text(const struct tm &t) { return strf("%Y%m%d", t); }

static const int64_t kStep = 15 * 60;
static const int64_t kFrom = 1767225600;        // 2026-01-01T00:00Z
static const int64_t kTo = 1861920000;          // 2029-01-01T00:00Z
static const int64_t kMargin = 12 * 24 * 3600;  // looked up to on either side
static const int kPerDay = 24 * 4;

// One zone's local time at every quarter hour. Every clock change lands on one.
struct Zone {
  std::vector<struct tm> tm;
  std::vector<int> day;     // day_number of the local date
  std::vector<int> minute;  // local minutes after midnight
  std::vector<int32_t> off;

  static int64_t at(int i) { return kFrom - kMargin + i * kStep; }
  static int slot(int64_t t) { return (int) ((t - (kFrom - kMargin)) / kStep); }
  static int first() { return slot(kFrom); }
  static int last() { return slot(kTo); }
  int count() const { return (int) this->tm.size(); }

  explicit Zone(const char *posix) {
    setenv("TZ", posix, 1);
    tzset();
    const int n = (int) ((kTo - kFrom + 2 * kMargin) / kStep);
    struct tm prev{};
    int num = 0;
    for (int i = 0; i < n; i++) {
      time_t t = (time_t) at(i);
      struct tm lt{};
      localtime_r(&t, &lt);
      if (i == 0 || lt.tm_mday != prev.tm_mday || lt.tm_mon != prev.tm_mon || lt.tm_year != prev.tm_year)
        num = day_number(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
      prev = lt;
      this->tm.push_back(lt);
      this->day.push_back(num);
      this->minute.push_back(lt.tm_hour * 60 + lt.tm_min);
      this->off.push_back((int32_t) lt.tm_gmtoff);
    }
  }
};

// ---- The local day and the kickoff label ----------------------------------------

static void sweep_local_day(const Zone &z) {
  for (int i = Zone::first(); i < Zone::last(); i++) {
    const int64_t step = local_day(z.tm[i]) - local_day(z.tm[i - 1]);
    SWEEP(step == z.day[i] - z.day[i - 1], "local_day", Zone::at(i), "moved " + std::to_string(step));
  }
}

static std::string want_label(const Zone &z, int kick, int now, bool tbd = false) {
  const struct tm &k = z.tm[kick];
  const int ahead = z.day[kick] - z.day[now];
  std::string day = month_day(k);
  if (ahead == 0)
    day = "Today";
  else if (ahead == 1)
    day = "Tomorrow";
  else if (ahead > 1 && ahead < 7)
    day = strf("%a", k);
  return day + " " + (tbd ? std::string("TBD") : time12(k));
}

// Kickoffs from three hours ago to nine days out, looked at every hour and
// in the last and first quarter hour of every local day.
static void sweep_kickoff_label(const Zone &z) {
  static const int kAhead[] = {-12, 0, 2, 19, 41, 4 * 24, 4 * 24 + 3, 4 * 71, 4 * 139, 4 * 167, 4 * 168, 4 * 220};
  for (int i = Zone::first(); i < Zone::last(); i++) {
    const bool edge = z.day[i] != z.day[i - 1] || z.day[i] != z.day[i + 1];
    if (i % 4 != 0 && !edge)
      continue;
    for (int a : kAhead)
      SWEEP_EQ(kickoff_label(z.tm[i + a], z.tm[i]), want_label(z, i + a, i), "kickoff_label", Zone::at(i));
    if (!edge)
      continue;
    const int day_out = i + kPerDay;
    SWEEP_EQ(kickoff_label(z.tm[day_out], z.tm[i], true), want_label(z, day_out, i, true), "kickoff_label TBD",
             Zone::at(i));
    SWEEP_EQ(next_line("DAL", "PHI", true, z.tm[day_out], z.tm[i]), "Next: DAL vs PHI " + want_label(z, day_out, i),
             "next_line", Zone::at(i));
  }
}

// ---- The night schedule ---------------------------------------------------------

static const Zone *night_zone = nullptr;
static int32_t night_offset(int64_t epoch) { return night_zone->off[Zone::slot(epoch)]; }

// A skip pressed at `press` lasts to the end of its night: the first moment
// the clock is outside the hours and stays out for an hour. The panel is lit
// until then. The next night is dim again.
static void check_skip(const Zone &z, const NightPrefs &p, int64_t press, int minute, int second, int end, int next,
                       bool every_slot) {
  const int64_t until = night_skip_until(p, press, minute, second, night_offset);
  auto hours = [&] {
    return "hours " + night_time_text(p.start) + " to " + night_time_text(p.end) + ", pressed " +
           strf("%Y-%m-%d %H:%M %Z", z.tm[Zone::slot(press)]);
  };
  // The last moment that reads as the end. The hour the clocks skip has none,
  // and the hour they repeat has two.
  int last_end = end;
  while (last_end > end - 4 && z.minute[last_end] != p.end)
    last_end--;
  if (z.minute[end + 4] == p.end)
    last_end = end + 4;
  if (z.minute[last_end] == p.end)
    SWEEP(until == Zone::at(last_end), "night_skip_until", press,
          "ends " + strf("%a %H:%M", z.tm[Zone::slot(until)]) + ", " + hours());
  int64_t held = until;
  auto state = [&](int k) {
    if (held != 0 && night_skip_over(p, z.minute[k], Zone::at(k), held, night_offset))
      held = 0;
    return night_state(p, true, z.minute[k], false, held != 0);
  };
  if (every_slot)
    for (int k = Zone::slot(press) + 1; k < end; k++)
      SWEEP(!night_holding(state(k)), "a skipped night stays lit", Zone::at(k), hours());
  SWEEP(state(end) == Night::DAY, "a skipped night ends", Zone::at(end), hours());
  SWEEP(state(next) == Night::DIM, "the next night is not skipped", Zone::at(next), hours());
}

static void sweep_night(const Zone &z) {
  struct Hours {
    int start, end;
  };
  static const Hours kHours[] = {{23 * 60, 7 * 60}, {22 * 60, 6 * 60 + 30}, {0, 6 * 60}, {22 * 60 + 30, 0},
                                 {21 * 60, 90},     {22 * 60, 150},         {22 * 60, 180}, {60, 9 * 60},
                                 {90, 7 * 60},      {150, 8 * 60},          {60, 90},       {13 * 60, 15 * 60},
                                 {20 * 60, 60},     {23 * 60, 120},         {120, 180},     {180, 11 * 60}};
  night_zone = &z;
  for (const Hours &h : kHours) {
    NightPrefs p = night_defaults();
    p.on = 1;
    p.start = (uint16_t) h.start;
    p.end = (uint16_t) h.end;
    auto inside = [&](int i) {
      const int m = z.minute[i];
      return h.start < h.end ? m >= h.start && m < h.end : m >= h.start || m < h.end;
    };
    // Filled from the back: the end of the night each quarter hour is in, and
    // the next night's start.
    std::vector<int> end(z.count(), 0), next(z.count(), 0);
    int out_run = 0, night_end = 0, night_next = 0, earliest_inside = 0;
    for (int i = z.count() - 1; i >= 0; i--) {
      if (inside(i)) {
        if (out_run >= 4) {
          night_end = i + 1;
          night_next = earliest_inside;
        }
        out_run = 0;
        earliest_inside = i;
      } else {
        out_run++;
      }
      end[i] = night_end;
      next[i] = night_next;
    }
    for (int i = Zone::first(); i < Zone::last(); i++) {
      const Night want = inside(i) ? Night::DIM : Night::DAY;
      SWEEP(night_state(p, true, z.minute[i], false) == want, "night_state", Zone::at(i), night_word(want));
      if (!inside(i))
        continue;
      // Every quarter hour of the nights around a clock change, and of one
      // night in four weeks.
      const bool every = z.off[i - kPerDay] != z.off[end[i] + kPerDay] || z.day[i] % 28 == 0;
      check_skip(z, p, Zone::at(i), z.minute[i], 0, end[i], next[i], every);
      check_skip(z, p, Zone::at(i) + 7 * 60 + 20, z.minute[i] + 7, 20, end[i], next[i], every);
    }
  }
}

static void sweep_night_text() {
  for (int m = 0; m < 24 * 60; m++) {
    struct tm t{};
    t.tm_hour = m / 60;
    t.tm_min = m % 60;
    SWEEP_EQ(night_time_text((uint16_t) m), strf("%H:%M", t), "night_time_text", 0);
    SWEEP_EQ(night_clock12((uint16_t) m), time12(t), "night_clock12", 0);
    SWEEP(night_parse_time(strf("%H:%M", t)) == m, "night_parse_time", 0, strf("%H:%M", t));
  }
}

// ---- ESPN's day -----------------------------------------------------------------

static std::string url_date(const std::string &url) {
  const size_t at = url.find("dates=");
  return at == std::string::npos ? "" : url.substr(at + 6, 8);
}

// ESPN lists a game under the US Eastern date it starts on. A late game is
// still on after midnight, so the scan reads yesterday's list until 4 AM
// Eastern and today's from 5 AM. z is US Eastern.
static void sweep_scan_day(const Zone &z) {
  static const char *kDays[] = {"Sunday's",   "Monday's", "Tuesday's", "Wednesday's",
                                "Thursday's", "Friday's", "Saturday's"};
  for (int i = Zone::first(); i < Zone::last(); i++) {
    const int64_t t = Zone::at(i);
    const int hour = z.minute[i] / 60;
    if (hour != 4) {
      // About noon of the wanted date, clear of a clock change.
      const int noon = i + 12 * 4 - z.minute[i] / 15 - (hour < 4 ? kPerDay : 0);
      for (size_t l = 0; l < kLeagueCount; l++) {
        SWEEP(espn_day(t, (uint8_t) l) == z.day[noon], "espn_day", t, std::to_string(espn_day(t, (uint8_t) l)));
        if (i % 2 == 0)
          SWEEP_EQ(url_date(scan_url((League) l, t)), ymd_text(z.tm[noon]), "scan_url", t);
      }
    } else {
      const int d = espn_day(t, 0);
      SWEEP(d == z.day[i] || d == z.day[i] - 1, "espn_day at 4 AM", t, std::to_string(d));
      SWEEP(d >= espn_day(t - kStep, 0), "espn_day goes back", t, std::to_string(d));
    }
    // No league starts a game between midnight and 6 AM Eastern.
    if (hour >= 6) {
      SWEEP_EQ(url_date(scoreboard_url(League::NFL, 0, t)), ymd_text(z.tm[i]), "scoreboard_url", t);
      SWEEP_EQ(url_date(team_day_url(League::MLB, 0, t)), ymd_text(z.tm[i]), "team_day_url", t);
    }
    if (z.minute[i] != 0)
      continue;
    // Midnight Eastern is where ESPN puts a game with no kickoff time.
    SWEEP_EQ(url_date(scoreboard_url(League::NCAA, 80, t, true)), ymd_text(z.tm[i]), "scoreboard_url TBD", t);
    for (int back = 0; back <= kRecapMaxDays; back++) {
      const struct tm &noon = z.tm[i - back * kPerDay + 12 * 4];
      const std::string want = back == 0 ? "Today's" : back == 1 ? "Yesterday's" : kDays[noon.tm_wday];
      SWEEP_EQ(recap_day_word(z.day[i] - back, z.day[i]), want, "recap_day_word", t);
    }
    for (size_t l = 0; l < kLeagueCount; l++)
      SWEEP_EQ(url_date(scan_url((League) l, espn_day_epoch(z.day[i]))), ymd_text(z.tm[i]), "espn_day_epoch", t);
  }
}

// A game with no kickoff time yet shows on its Eastern date from Hawaii to
// central Europe.
static void sweep_tbd_day(const Zone &z, const Zone &eastern) {
  for (int i = Zone::first(); i < Zone::last(); i++) {
    if (eastern.minute[i] != 0)
      continue;
    const int k = Zone::slot(tbd_day_epoch(Zone::at(i)));
    SWEEP(z.day[k] == eastern.day[i], "tbd_day_epoch", Zone::at(i), ymd_text(z.tm[k]));
  }
}

// ---- Countdowns -----------------------------------------------------------------

// The digits read back as minutes, with the smallest unit they show. -1 for
// any other form.
static int64_t digits_minutes(const std::string &s, int64_t &unit) {
  long long d = 0, h = 0, m = 0;
  int n = 0;
  const int len = (int) s.size();
  unit = 60;
  if (sscanf(s.c_str(), "%lldd %lldh%n", &d, &h, &n) == 2 && n == len)
    return d >= 1 && d <= 999 && h < 24 ? d * 1440 + h * 60 : -1;
  unit = 1;
  if (sscanf(s.c_str(), "%lldh %2lldm%n", &h, &m, &n) == 2 && n == len)
    return h >= 1 && h < 24 && m < 60 ? h * 60 + m : -1;
  if (sscanf(s.c_str(), "%lldm%n", &m, &n) == 1 && n == len)
    return m >= 1 && m < 60 ? m : -1;
  unit = 1440;
  if (sscanf(s.c_str(), "%lldd%n", &d, &n) == 1 && n == len)
    return d > 999 ? d * 1440 : -1;
  return -1;
}

static void check_digits(const std::string &s, int64_t seconds, int64_t t) {
  int64_t unit = 0;
  const int64_t got = digits_minutes(s, unit), left = (seconds + 59) / 60;
  SWEEP(got > 0 && got <= left && left - got < unit, "countdown_text", t,
        "\"" + s + "\" for " + std::to_string(seconds) + " s");
}

static void sweep_countdown_text() {
  for (int64_t s = 1; s < 3 * kDayS; s += s < 7200 ? 1 : 7)
    check_digits(countdown_text(s), s, 0);
  for (int64_t d = 1; d < 1200; d++)
    for (int64_t by : {-3601, -3600, -61, -60, -59, -1, 0, 1, 59, 60, 61, 3600})
      check_digits(countdown_text(d * kDayS + by), d * kDayS + by, 0);
  SWEEP_EQ(countdown_text(0), "", "countdown_text", 0);
  SWEEP_EQ(countdown_text(-1), "", "countdown_text", 0);
}

// An owner's own day, every date of the sweep: the digits before it, "Today"
// from its midnight to the next, nothing after. The day is 23 or 25 hours
// long when the clocks change.
static void sweep_custom_day(const Zone &z) {
  int start = 0;
  for (int i = Zone::first() + 1; i < Zone::last(); i++) {
    if (z.day[i] == z.day[i - 1])
      continue;
    if (start != 0) {
      const int64_t from = Zone::at(start), to = Zone::at(i);
      for (int k = start - 8; k < i + 8; k += 2) {
        const std::string s = countdown_card_text(from, to, Zone::at(k));
        if (k < start)
          check_digits(s, from - Zone::at(k), Zone::at(k));
        else
          SWEEP_EQ(s, z.day[k] == z.day[start] ? "Today" : "", "the custom countdown's day", Zone::at(k));
      }
      check_digits(countdown_card_text(from, to, from - 1), 1, from - 1);
      SWEEP_EQ(countdown_card_text(from, to, to - 1), "Today", "the custom countdown's day", to - 1);
      SWEEP_EQ(countdown_card_text(from, to, to), "", "the custom countdown's day", to);
    }
    start = i;
  }
}

static void sweep_custom_date() {
  for (int y = 2019; y <= 2100; y++)
    for (int m = 0; m <= 13; m++)
      for (int d = 0; d <= 32; d++) {
        char text[40];
        snprintf(text, sizeof(text), "%04d-%02d-%02d", y, m, d);
        const bool real = y >= 2020 && y <= 2099 && m >= 1 && m <= 12 && d >= 1 && d <= month_days(y, m);
        uint32_t ymd = 0;
        SWEEP(parse_countdown_date(text, ymd) == real, "parse_countdown_date", 0, text);
        if (!real)
          continue;
        SWEEP_EQ(countdown_date_text(ymd), text, "countdown_date_text", 0);
        struct tm t{};
        t.tm_mon = m - 1;
        t.tm_mday = d;
        SWEEP_EQ(countdown_day_label(ymd), month_day(t), "countdown_day_label", 0);
      }
}

// ---- The off season -------------------------------------------------------------

// A daily sport is between seasons with the next game, or the last one, over
// ten days away. Football never is while it has a game.
static void sweep_off_season() {
  const int64_t ten_days = 10 * 86400;
  for (int64_t ahead = -40 * 86400; ahead <= 40 * 86400; ahead += kStep) {
    const int64_t kick = kFrom + ahead;
    for (Sport s : {Sport::FOOTBALL, Sport::BASEBALL, Sport::BASKETBALL, Sport::HOCKEY, Sport::SOCCER}) {
      const bool daily = s != Sport::FOOTBALL;
      const int64_t horizon = season_horizon(s);
      const bool before = daily && ahead > ten_days, after = daily && -ahead > ten_days;
      auto detail = [&] { return "kickoff in " + std::to_string(ahead) + " s"; };
      SWEEP(off_season(GameState::PRE, kick, kFrom, horizon) == before, "off_season ahead", 0, detail());
      SWEEP(off_season(GameState::POST, kick, kFrom, horizon) == after, "off_season behind", 0, detail());
      SWEEP(!off_season(GameState::IN, kick, kFrom, horizon), "off_season live", 0, detail());
      SWEEP(off_season(GameState::NOT_FOUND, kick, kFrom, horizon), "off_season no game", 0, detail());
      SWEEP(fav_off_season(true, true, false, GameState::IN, kick, kFrom, horizon) == (before ? 1 : 0),
            "fav_off_season", 0, detail());
    }
  }
}

static void sweep_clock_text(const Zone &z) {
  for (int i = Zone::first(); i < Zone::last(); i++) {
    if (i % 3 == 0)
      SWEEP_EQ(clock_time_text(z.tm[i]), time12(z.tm[i]), "clock_time_text", Zone::at(i));
    if (z.day[i] != z.day[i - 1] || z.day[i] != z.day[i + 1])
      SWEEP_EQ(clock_date_text(z.tm[i]), strf("%a, ", z.tm[i]) + month_day(z.tm[i]), "clock_date_text", Zone::at(i));
  }
}

// ---- Dates on the wire ----------------------------------------------------------

static void sweep_dates() {
  static const char *kShort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  int day = day_number(2020, 1, 1);
  for (int y = 2020; y <= 2099; y++)
    for (int m = 1; m <= 12; m++)
      for (int d = 1; d <= month_days(y, m); d++, day++) {
        char date[40], text[80];
        snprintf(date, sizeof(date), "%04d-%02d-%02d", y, m, d);
        for (int s : {0, 1, 12 * 3600 + 34 * 60 + 56, 86399}) {
          const time_t t = (time_t) day * 86400 + s;
          struct tm u{};
          gmtime_r(&t, &u);
          snprintf(text, sizeof(text), "%sT%02d:%02d:%02dZ", date, s / 3600, s / 60 % 60, s % 60);
          SWEEP(parse_iso8601_z(text) == (int64_t) t, "parse_iso8601_z", 0, text);
          const std::string http = strf("%a, %d %b %Y %H:%M:%S GMT", u);
          SWEEP(parse_http_date(http) == (int64_t) t, "parse_http_date", 0, http);
          SWEEP_EQ(wx_utc_hour((int64_t) t), strf("%Y-%m-%dT%H:00", u), "wx_utc_hour", 0);
        }
        SWEEP(wx_days_from_date(date) == day, "wx_days_from_date", 0, date);
        // 1970-01-01 was a Thursday.
        SWEEP_EQ(weekday_text(weekday_of(date)), kShort[(day + 4) % 7], "weekday_of", 0);
      }
}

// ---- The weather ----------------------------------------------------------------

// The daylight time rule the venue's clock uses, in the zones that keep it.
// Standard time is the smaller of the winter and summer offsets.
static void sweep_us_dst(const Zone &z) {
  const int32_t std_off = std::min(z.off[Zone::first()], z.off[Zone::first() + 182 * kPerDay]);
  for (int i = Zone::first(); i < Zone::last(); i++)
    SWEEP(wx_us_dst(Zone::at(i) + std_off) == (z.off[i] != std_off), "wx_us_dst", Zone::at(i), "");
}

// The sun's height in degrees, to a tenth or so (the Astronomical Almanac's
// low precision formula).
static double sun_height(int64_t t, double lat, double lon) {
  const double rad = 3.14159265358979 / 180, d = (double) (t - 946728000) / 86400;
  const double g = (357.529 + 0.98560028 * d) * rad;
  const double l = (280.459 + 0.98564736 * d + 1.915 * sin(g) + 0.020 * sin(2 * g)) * rad;
  const double e = 23.439 * rad;
  const double ra = atan2(cos(e) * sin(l), cos(l)), dec = asin(sin(e) * sin(l));
  const double hour = (18.697374558 + 24.06570982441908 * d) * 15 * rad + lon * rad - ra;
  return asin(sin(lat * rad) * sin(dec) + cos(lat * rad) * cos(dec) * cos(hour)) / rad;
}

// The kickoff forecast's sun or moon, afternoon to midnight, about 40 degrees
// north, the latitude its rule is made for. The rule goes by the month, so it
// gets a wide twilight on either side of sunset.
static void sweep_venue_dark(const Zone &z, const char *place, double lat, double lon) {
  for (int i = Zone::first(); i < Zone::last(); i++) {
    if (z.minute[i] < 12 * 60)
      continue;
    const double up = sun_height(Zone::at(i), lat, lon);
    const int dark = wx_venue_dark(Zone::at(i), place);
    SWEEP(dark >= 0 && !(up > 10 && dark == 1) && !(up < -15 && dark == 0), "wx_venue_dark", Zone::at(i),
          std::string(place) + (dark == 1 ? ": dark" : ": light") + ", sun at " + std::to_string(up) + " degrees");
  }
}

// A forecast, with midnight passing before the next read: whatever the card
// calls today is the forecast for the local date. Each day's high here is its
// day_number.
static void sweep_roll_day(const Zone &z) {
  for (int i = Zone::first(); i < Zone::last(); i += 13) {
    WxForecast f;
    f.valid = true;
    f.day0 = z.day[i];
    f.today.hi10 = z.day[i];
    f.next[0].hi10 = z.day[i] + 1;
    f.next[1].hi10 = z.day[i] + 2;
    f.later.hi10 = z.day[i] + 3;
    f.has_later = true;
    for (int k = i; k < i + 2 * kPerDay; k++) {
      const bool moved = wx_roll_day(f, z.day[k]);
      SWEEP(!f.valid || (f.day0 == z.day[k] && f.today.hi10 == z.day[k] && f.next[0].hi10 == z.day[k] + 1),
            "wx_roll_day", Zone::at(k), "");
      SWEEP(!moved || z.day[k] != z.day[k - 1], "wx_roll_day moves at midnight", Zone::at(k), "");
      SWEEP(f.valid || z.day[k] - z.day[i] > 1, "wx_roll_day keeps the first night", Zone::at(k), "");
    }
  }
}

// ---- millis() wraps after 49.7 days ---------------------------------------------

// A wait of `ms` that starts at `from` is over once that long has passed, on
// either side of the wrap. A stamp of 0 means never, so none starts there.
static void sweep_wait(const char *what, uint32_t ms, const std::function<bool(uint32_t, uint32_t)> &over) {
  static const uint32_t kStarts[] = {1,           1000,        0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu - 600000u,
                                     0xFFFFD8F0u, 0xFFFFFFF0u, 0xFFFFFFFFu};
  for (uint32_t from : kStarts) {
    const int64_t ages[] = {0, 1, 1000, (int64_t) ms / 2, (int64_t) ms - 1, ms, (int64_t) ms + 1, 2 * (int64_t) ms};
    for (int64_t age : ages)
      SWEEP(over((uint32_t) (from + age), from) == (age >= (int64_t) ms), what, 0,
            "started " + std::to_string(from) + ", " + std::to_string(age) + " ms ago");
  }
}

static void sweep_millis() {
  zone_name = "millis";
  using u32 = uint32_t;
  const u32 m = 60 * 1000, h = 60 * m;
  sweep_wait("schedule_due", 6 * h, [=](u32 now, u32 from) { return schedule_due(true, false, now, from, 6 * h); });
  sweep_wait("fav_read_due", 6 * h, [=](u32 now, u32 from) { return fav_read_due(false, from, now, m, 6 * h); });
  sweep_wait("fav_read_due missing", m, [=](u32 now, u32 from) { return fav_read_due(true, from, now, m, 6 * h); });
  sweep_wait("live_rescan_due", 3 * h, [=](u32 now, u32 from) { return live_rescan_due(now, from, 3 * h, false); });
  sweep_wait("logo_retry_due", 10 * m, [=](u32 now, u32 from) { return logo_retry_due(from, 10 * m, now); });
  sweep_wait("logo_hold_over", LOGO_HOLD_MS, [](u32 now, u32 from) { return logo_hold_over(true, true, from, now); });
  sweep_wait("boot_hold", BOOT_HOLD_CAP_MS, [](u32 now, u32 from) { return !boot_hold(false, from, now); });
  sweep_wait("board_first", kBoardFirstMs, [](u32 now, u32 from) { return !board_first(now, from, true); });
  sweep_wait("leaders_wait", kBoardFirstMs, [](u32 now, u32 from) { return !leaders_wait(now, from, true); });
  sweep_wait("season_read_due", kSeasonReadMs,
             [](u32 now, u32 from) { return season_read_due(now, from, 0, true, false); });
  sweep_wait("season_read_due big", kSeasonBigReadMs,
             [](u32 now, u32 from) { return season_read_due(now, from, 0, true, true); });
  sweep_wait("season_read_due retry", kSeasonRetryMs,
             [](u32 now, u32 from) { return season_read_due(now, 0, from, true, false); });
  sweep_wait("news_read_due", kNewsReadMs, [](u32 now, u32 from) { return news_read_due(now, from, true, 0); });
  sweep_wait("news_read_due retry", kNewsRetryMs, [](u32 now, u32 from) { return news_read_due(now, 0, true, from); });
  sweep_wait("wx_due_in_ms", 30 * m, [=](u32 now, u32 from) { return wx_due_in_ms(from, 30 * m, now) == 0; });
  sweep_wait("wx_fav_read_in_ms", m, [=](u32 now, u32 from) { return wx_fav_read_in_ms(true, from, now, m, 6 * h) == 0; });
  sweep_wait("fx_due", 10 * m, [=](u32 now, u32 from) {
    FxPlayer p;
    p.list = 1;
    p.next_ms = from + 10 * m;
    return fx_due(p, now);
  });
  // A score that steps back is held from Home Assistant for 90 s. Once it is
  // taken as the truth, the same points again are news.
  sweep_wait("a score that steps back", 90 * 1000, [](u32 now, u32 from) {
    HaTracker t;
    HaContext c;
    c.league = "nfl";
    GameSnapshot g;
    g.valid = true;
    g.event_id = "1";
    g.state = GameState::IN;
    g.period = 1;
    g.team_abbr = "DAL";
    g.opp_abbr = "PHI";
    g.team_score = 7;
    c.mono_ms = from - 1000;
    t.update(g, c);
    g.team_score = 0;
    c.mono_ms = from;
    t.update(g, c);
    c.mono_ms = now;
    t.update(g, c);
    g.team_score = 7;
    return !t.update(g, c).empty();
  });
  // The logo used longest ago goes first.
  for (u32 now : {100000u, 50u, 0u, 0xFFFFFFFFu, 0x80000000u}) {
    const std::vector<LogoSlot> s = {{now - 10, false, false}, {now - 70000, false, false}, {now - 60, false, false}};
    SWEEP(pick_evict(s, now) == 1, "pick_evict", 0, std::to_string(now));
  }
}

int main() {
  const clock_t began = clock();
  auto posix = [](const char *name) {
    for (const Timezone &t : kTimezones)
      if (strcmp(t.name, name) == 0)
        return t.posix;
    printf("FAIL no zone called %s\n", name);
    exit(1);
  };
  zone_name = "US Eastern";
  const Zone eastern(posix("US Eastern"));
  sweep_scan_day(eastern);

  for (const Timezone &t : kTimezones) {
    zone_name = t.name;
    const Zone z(t.posix);
    sweep_local_day(z);
    sweep_kickoff_label(z);
    sweep_night(z);
    sweep_custom_day(z);
    sweep_clock_text(z);
    sweep_roll_day(z);
    // Noon Eastern is already the next day there.
    if (strcmp(t.name, "Australia Eastern") != 0)
      sweep_tbd_day(z, eastern);
    for (const char *us : {"US Eastern", "US Central", "US Mountain", "US Pacific", "US Alaska"})
      if (strcmp(t.name, us) == 0)
        sweep_us_dst(z);
  }

  struct Venue {
    const char *zone, *place;
    double lat, lon;
  };
  static const Venue kVenues[] = {
      {"US Eastern", "Foxborough, MA", 42.09, -71.26},
      {"US Central", "Green Bay, WI", 44.50, -88.06},
      {"US Mountain", "Denver, CO", 39.74, -105.02},
      {"US Pacific", "Seattle, WA", 47.60, -122.33},
  };
  for (const Venue &v : kVenues) {
    zone_name = v.zone;
    const Zone z(posix(v.zone));
    sweep_venue_dark(z, v.place, v.lat, v.lon);
  }

  zone_name = "";
  sweep_night_text();
  sweep_countdown_text();
  sweep_custom_date();
  sweep_off_season();
  sweep_dates();
  sweep_millis();

  printf("clock sweep: %ld checks, %ld failures, %.1f s\n", checks, failures,
         (double) (clock() - began) / CLOCKS_PER_SEC);
  return failures == 0 ? 0 : 1;
}
