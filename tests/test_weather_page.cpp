// Host tests for the Weather page: the forecast's hours and suns
// (weather.h), what the page says (weather_page.h), and its place under
// Showing and After the game (showing.h, wake.h). Build with `make` in tests/.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "showing.h"
#include "wake.h"
#include "weather.h"
#include "weather_page.h"

using namespace espn;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                          \
  do {                                                       \
    checks++;                                                \
    if (!(cond)) {                                           \
      failures++;                                            \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                           \
  do {                                                                           \
    checks++;                                                                    \
    auto va = (a);                                                               \
    auto vb = (b);                                                               \
    if (!(va == vb)) {                                                           \
      failures++;                                                                \
      std::ostringstream os;                                                     \
      os << va << " != " << vb;                                                  \
      printf("FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #a, os.str().c_str()); \
    }                                                                            \
  } while (0)

#define CHECK_STR(a, b) CHECK_EQ(std::string(a), std::string(b))

static std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// 2026-11-23, the day the Coppell fixtures were read (3:15 AM Central).
static const int32_t kDay = wx_days_from_civil(2026, 11, 23);
static int32_t at(int hour, int minute) { return kDay * 1440 + hour * 60 + minute; }

static WxForecast coppell() {
  WxForecast f;
  std::string json = slurp("fixtures/wx_forecast_hourly_coppell.json");
  CHECK(parse_forecast(json, f));
  return f;
}

static WxPageIn page_in(int32_t now_min, bool wide = true) {
  WxPageIn in;
  in.town = true;
  in.fresh = true;
  in.fahrenheit = true;
  in.wide = wide;
  in.now_min = now_min;
  in.epoch = 0;
  return in;
}

static void test_parse_hours() {
  WxForecast f = coppell();
  CHECK(f.valid);
  CHECK_EQ(wx_deg(f.t10, true), 43);
  CHECK_EQ(f.day0, kDay);
  // The hour under way and the 14 after it
  CHECK_EQ(f.hour0, at(3, 0) / 60);
  CHECK_EQ((int) f.n_hours, 15);
  CHECK_EQ((int) f.hours[0].t10, 434);
  CHECK_EQ((int) f.hours[0].code, 0);
  CHECK_EQ((int) f.hours[0].pop, 0);
  CHECK(f.hours[0].night);
  CHECK(!f.hours[5].night);  // 8 AM, after the 7:05 sunrise
  CHECK_EQ((int) f.hours[14].t10, 507);
  // Each day's sunrise and sunset, minutes into the day
  CHECK_EQ((int) f.sunrise[0], 7 * 60 + 5);
  CHECK_EQ((int) f.sunset[0], 17 * 60 + 22);
  CHECK_EQ((int) f.sunrise[3], 7 * 60 + 8);
  CHECK(wx_hour_at(f, at(3, 0) / 60) == &f.hours[0]);
  CHECK(wx_hour_at(f, at(17, 0) / 60) == &f.hours[14]);
  CHECK(wx_hour_at(f, at(18, 0) / 60) == nullptr);
  CHECK(wx_hour_at(f, at(2, 0) / 60) == nullptr);
  CHECK_EQ(wx_local_minutes("2026-11-23T17:22"), at(17, 22));
  CHECK_EQ(wx_local_minutes("2026-11-23"), 0);
  CHECK_EQ(wx_local_minutes(nullptr), 0);

  // An older reply's shape, with no hours or suns, still makes the card, and
  // the page has no hours to show
  WxForecast old;
  std::string json = slurp("fixtures/wx_forecast_coppell.json");
  CHECK(parse_forecast(json, old));
  CHECK(old.valid);
  CHECK_EQ((int) old.n_hours, 0);
  CHECK_EQ((int) old.sunrise[0], (int) kWxNoSun);
  CHECK(wx_hour_at(old, old.hour0) == nullptr);

  // A missing or out-of-step hour stays empty; a null chance is unknown
  json = "{\"current\":{\"temperature_2m\":50,\"weather_code\":0,\"is_day\":1},\"daily\":{\"time\":[\"2026-11-23\","
         "\"2026-11-24\",\"2026-11-25\"],\"weather_code\":[0,3,61],\"temperature_2m_max\":[60,61,62],"
         "\"temperature_2m_min\":[40,41,42]},\"hourly\":{\"time\":[\"2026-11-23T10:00\",\"2026-11-23T11:00\","
         "\"2026-11-23T13:00\"],\"temperature_2m\":[50,null,52],\"weather_code\":[1,2,3],"
         "\"precipitation_probability\":[null,5,500],\"is_day\":[1,1,1]}}";
  WxForecast gaps;
  CHECK(parse_forecast(json, gaps));
  CHECK_EQ((int) gaps.n_hours, 3);
  CHECK_EQ((int) gaps.hours[0].pop, -1);
  CHECK(wx_hour_at(gaps, at(10, 0) / 60) != nullptr);
  CHECK(wx_hour_at(gaps, at(11, 0) / 60) == nullptr);  // no temperature
  CHECK(wx_hour_at(gaps, at(12, 0) / 60) == nullptr);  // the reply skipped 12
  CHECK_EQ((int) gaps.hours[2].code, 255);             // 1 PM sits where 12 should

  // After midnight the suns move on a day with the rest
  WxForecast rolled = f;
  CHECK(wx_roll_day(rolled, kDay + 1));
  CHECK_EQ((int) rolled.sunrise[0], 7 * 60 + 6);
  CHECK_EQ((int) rolled.sunrise[2], 7 * 60 + 8);
  CHECK_EQ((int) rolled.sunrise[3], (int) kWxNoSun);
  CHECK_EQ(rolled.hour0, f.hour0);  // hours are kept by the clock, not the day
}

static void test_text() {
  char b[32];
  wx_hour_text(b, sizeof(b), 0);
  CHECK_STR(b, "12AM");
  wx_hour_text(b, sizeof(b), 12);
  CHECK_STR(b, "12PM");
  wx_hour_text(b, sizeof(b), 13);
  CHECK_STR(b, "1PM");
  wx_hour_text(b, sizeof(b), 27);
  CHECK_STR(b, "3AM");
  wx_clock_text(b, sizeof(b), 17 * 60 + 22, true);
  CHECK_STR(b, "5:22 PM");
  wx_clock_text(b, sizeof(b), 7 * 60 + 5, false);
  CHECK_STR(b, "7:05");

  GameSnapshot g;
  g.valid = true;
  g.state = GameState::IN;
  g.team_abbr = "DAL";
  g.opp_abbr = "PHI";
  g.team_score = 21;
  g.opp_score = 17;
  g.short_detail = "4:12 - 3rd";
  wx_live_text(b, sizeof(b), g, true);
  CHECK_STR(b, "DAL 21 PHI 17  4:12 - 3rd");
  CHECK(strlen(b) <= kWxRowWide);
  wx_live_text(b, sizeof(b), g, false);
  CHECK_STR(b, "DAL 21-17");
  // A status too long for the row leaves it out
  g.team_abbr = "MICH";
  g.opp_abbr = "UCLA";
  g.team_score = 101;
  g.opp_score = 99;
  g.short_detail = "8:22 - 2nd Half";
  wx_live_text(b, sizeof(b), g, true);
  CHECK_STR(b, "MICH 101 UCLA 99");
  g.short_detail = "Halftime";
  wx_live_text(b, sizeof(b), g, true);
  CHECK_STR(b, "MICH 101 UCLA 99 Halftime");  // one space, so it fits
  CHECK(strlen(b) <= kWxRowWide);
  g.short_detail = "Halftime!";
  wx_live_text(b, sizeof(b), g, true);
  CHECK_STR(b, "MICH 101 UCLA 99");
  g.short_detail = "Top 5th";
  wx_live_text(b, sizeof(b), g, false);
  CHECK_STR(b, "MICH 101-99");
}

static void test_page() {
  const WxForecast f = coppell();
  WxPageView v;

  // 3:20 AM on two panels: now on the left, 5, 7 and 9 AM on the right
  wx_page_compose(page_in(at(3, 20)), f, v);
  CHECK(v.screen == WxPageScreen::BOTH);
  CHECK_STR(v.temp, "43\xC2\xB0");
  CHECK_STR(v.words, "Clear");
  CHECK_EQ((int) v.icon, (int) WxIcon::MOON);
  CHECK_STR(v.hilo, "Hi 56 Lo 40");
  CHECK_STR(v.col[0].time, "5AM");
  CHECK_STR(v.col[1].time, "7AM");
  CHECK_STR(v.col[2].time, "9AM");
  CHECK(v.col[0].on && v.col[1].on && v.col[2].on);
  CHECK_STR(v.col[0].temp, "42\xC2\xB0");
  CHECK_EQ((int) v.col[0].icon, (int) WxIcon::MOON);  // clear, before sunrise
  CHECK_EQ((int) v.col[1].icon, (int) WxIcon::PART);
  CHECK_EQ((int) v.col[2].icon, (int) WxIcon::CLOUD);
  CHECK_STR(v.col[2].pop, "0%");
  // Dry: the next sunrise
  CHECK_STR(v.bottom, "Sunrise 7:05 AM");
  CHECK_EQ(v.bottom_rgb, kWxRgbSun);

  // One panel: the same without the AM, now and the next hours in turns
  WxPageIn narrow = page_in(at(3, 20), false);
  wx_page_compose(narrow, f, v);
  CHECK(v.screen == WxPageScreen::NOW);
  CHECK_STR(v.bottom, "Sunrise 7:05");
  CHECK(strlen(v.bottom) <= kWxRowNarrow);
  narrow.epoch = kWxPageTurnS;
  wx_page_compose(narrow, f, v);
  CHECK(v.screen == WxPageScreen::NEXT);
  narrow.epoch = 2 * kWxPageTurnS + 3;
  wx_page_compose(narrow, f, v);
  CHECK(v.screen == WxPageScreen::NOW);

  // Noon: the sunset; 6 PM is past what the read has
  wx_page_compose(page_in(at(12, 0)), f, v);
  CHECK_STR(v.col[0].time, "2PM");
  CHECK_STR(v.col[2].time, "6PM");
  CHECK(v.col[1].on);
  CHECK(!v.col[2].on);
  CHECK_STR(v.bottom, "Sunset 5:22 PM");

  // After the sunset, tomorrow's sunrise
  wx_page_compose(page_in(at(18, 30)), f, v);
  CHECK_STR(v.bottom, "Sunrise 7:06 AM");
  // Late in the evening, tomorrow's high and low
  wx_page_compose(page_in(at(21, 0)), f, v);
  CHECK_STR(v.bottom, "Tomorrow 48 / 36");
  CHECK_EQ(v.bottom_rgb, kWxRgbTomorrow);
  wx_page_compose(page_in(at(23, 59), false), f, v);
  CHECK_STR(v.bottom, "Tmrw 48/36");
  // Past midnight before the forecast moves on, the sunrise again
  wx_page_compose(page_in(at(24, 30)), f, v);
  CHECK_STR(v.bottom, "Sunrise 7:06 AM");

  // Celsius
  WxPageIn c = page_in(at(3, 20));
  c.fahrenheit = false;
  wx_page_compose(c, f, v);
  CHECK_STR(v.temp, "6\xC2\xB0");
  CHECK_STR(v.hilo, "Hi 13 Lo 4");

  // Storms at 4 PM: inside the next 12 hours from 5 AM on, not at 3:20
  WxForecast s;
  std::string json = slurp("fixtures/wx_forecast_hourly_storms.json");
  CHECK(parse_forecast(json, s));
  wx_page_compose(page_in(at(3, 20)), s, v);
  CHECK_STR(v.bottom, "Sunrise 7:05 AM");
  wx_page_compose(page_in(at(5, 0)), s, v);
  CHECK_STR(v.bottom, "Storms after 4 PM");
  CHECK_EQ(v.bottom_rgb, kWxRgbStorm);
  wx_page_compose(page_in(at(10, 15), false), s, v);
  CHECK_STR(v.bottom, "Storms 4 PM");
  CHECK(strlen(v.bottom) <= kWxRowNarrow);
  wx_page_compose(page_in(at(16, 40)), s, v);
  CHECK_STR(v.bottom, "Storms now");
  // A 30% shower isn't enough for the row, but its column says so
  wx_page_compose(page_in(at(9, 0)), s, v);
  CHECK_STR(v.col[0].time, "11AM");
  CHECK_STR(v.col[0].pop, "30%");
  CHECK_EQ((int) v.col[0].icon, (int) WxIcon::RAIN);

  // A live game of the owner's comes first, still; two take turns
  GameSnapshot a, b;
  a.valid = b.valid = true;
  a.state = b.state = GameState::IN;
  a.team_abbr = "DAL";
  a.opp_abbr = "PHI";
  a.team_score = 21;
  a.opp_score = 17;
  a.short_detail = "4:12 - 3rd";
  b.team_abbr = "TEX";
  b.opp_abbr = "OU";
  b.team_score = 3;
  b.opp_score = 0;
  b.short_detail = "Top 2nd";
  WxPageIn live = page_in(at(5, 0));
  live.n_live = 1;
  live.live[0] = &a;
  wx_page_compose(live, s, v);
  CHECK_STR(v.bottom, "DAL 21 PHI 17  4:12 - 3rd");
  CHECK_EQ(v.bottom_rgb, kWxRgbLive);
  live.n_live = 2;
  live.live[1] = &b;
  live.epoch = kWxLiveTurnS;
  wx_page_compose(live, s, v);
  CHECK_STR(v.bottom, "TEX 3 OU 0  Top 2nd");
  live.epoch = 2 * kWxLiveTurnS + 4;
  wx_page_compose(live, s, v);
  CHECK_STR(v.bottom, "DAL 21 PHI 17  4:12 - 3rd");

  // Messages, the live game still under them
  WxPageIn m = page_in(at(5, 0));
  m.town = false;
  wx_page_compose(m, f, v);
  CHECK(v.screen == WxPageScreen::MESSAGE);
  CHECK_STR(v.title, "Weather");
  CHECK_STR(v.sub, "Set your town on the device page");
  CHECK_STR(v.bottom, "");
  m.town = true;
  m.not_found = true;
  wx_page_compose(m, f, v);
  CHECK_STR(v.sub, "Town not found");
  m.not_found = false;
  m.fresh = false;
  wx_page_compose(m, f, v);
  CHECK_STR(v.sub, "Getting the forecast");
  m.failing = true;
  m.n_live = 1;
  m.live[0] = &a;
  wx_page_compose(m, f, v);
  CHECK_STR(v.sub, "Can't reach the weather");
  CHECK_STR(v.bottom, "DAL 21 PHI 17  4:12 - 3rd");
  // A read with no hours (the card's older shape): columns empty, the
  // bottom row tomorrow's
  WxForecast old;
  json = slurp("fixtures/wx_forecast_coppell.json");
  CHECK(parse_forecast(json, old));
  wx_page_compose(page_in(old.day0 * 1440 + 9 * 60), old, v);
  CHECK(v.screen == WxPageScreen::BOTH);
  CHECK(!v.col[0].on);
  CHECK_STR(v.col[0].temp, "");
  CHECK_STR(v.bottom, "Tomorrow 73 / 66");
}

static void test_showing_and_after() {
  CHECK(showing_now("Weather", false, false, false, true) == Showing::PICKED);
  CHECK(showing_now("Weather", false, false, false, false) == Showing::AUTO);
  CHECK(showing_now("Flights", false, false, false, true) == Showing::AUTO);  // Weather's pin is its own
  CHECK_EQ(after_parse("weather"), 6);
  CHECK_STR(after_word((uint8_t) After::WEATHER), "weather");
  CHECK_EQ((int) kAfterCount, 7);
  WakePrefs p = wake_defaults();
  p.after = 7;  // a later firmware's
  wake_sanitize(p);
  CHECK_EQ((int) p.after, (int) After::SCOREBOARD);
  p.after = (uint8_t) After::WEATHER;
  wake_sanitize(p);
  CHECK_EQ((int) p.after, (int) After::WEATHER);
  WakeState s;
  WakeInputs in;
  in.clock_valid = true;
  in.has_team = true;
  in.now = 1000000;
  CHECK(wake_weather(p, s, in));
  CHECK(!wake_flights(p, s, in));
  s.held = true;
  CHECK(!wake_weather(p, s, in));
  CHECK_STR(wake_word(false, false, false, false, false, true), "weather");
  CHECK_STR(wake_word(true, false, false, false, false, true), "asleep");

  // The picked page's reads of the owner's games
  const int64_t now = 1800000000;
  CHECK_EQ(weather_watch_every_ms(true, now, 0, 15), 60u * 1000u);
  CHECK_EQ(weather_watch_every_ms(false, now, 0, 15), 6u * 3600u * 1000u);
  CHECK_EQ(weather_watch_every_ms(false, now, now + 3 * 3600, 15), 3600u * 1000u);
  CHECK_EQ(weather_watch_every_ms(false, now, now + 10 * 60, 15), 60u * 1000u);
}

int main() {
  test_parse_hours();
  test_text();
  test_page();
  test_showing_and_after();
  printf("test_weather_page: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
