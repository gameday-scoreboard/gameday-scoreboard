// weather.h and wx_icons.h: Open-Meteo's replies (fixtures/weather/, recorded
// 2026-10-27), the addresses the panel asks, the icons and words per code,
// the F/C choice, the card's turns and the backoff. `test_weather --png <dir>`
// also writes every icon's frames as PPM files, for looking at by eye.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "weather.h"
#include "wx_icons.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define CHECK(cond)                                           \
  do {                                                        \
    checks++;                                                 \
    if (!(cond)) {                                            \
      failures++;                                             \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
    }                                                         \
  } while (0)

std::string slurp(const std::string &name) {
  std::ifstream f("fixtures/weather/" + name, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  if (ss.str().empty())
    printf("missing fixture %s\n", name.c_str());
  return ss.str();
}

void test_geocode() {
  WxPlace p;
  std::string zip = slurp("geocode_zip.json");
  CHECK(parse_wx_geocode(zip.data(), zip.size(), p));
  CHECK(p.ok && p.name == "Coppell" && p.country == "US");
  CHECK(p.lat > 32.9f && p.lat < 33.0f && p.lon < -96.9f && p.lon > -97.1f);
  // Without countryCode, 75019 is a Paris postcode: why wx_geocode_url adds it.
  std::string paris = slurp("geocode_zip_no_country.json");
  CHECK(parse_wx_geocode(paris.data(), paris.size(), p) && p.country == "FR");
  std::string london = slurp("geocode_london.json");
  CHECK(parse_wx_geocode(london.data(), london.size(), p) && p.name == "London" && p.country == "GB");
  std::string none = slurp("geocode_none.json");
  CHECK(!parse_wx_geocode(none.data(), none.size(), p) && !p.ok);
  CHECK(!parse_wx_geocode("<html>", 6, p));
  const char *bad = R"({"results":[{"name":"X","latitude":123,"longitude":5}]})";
  CHECK(!parse_wx_geocode(bad, strlen(bad), p));
  const char *longname = R"({"results":[{"name":"Llanfairpwllgwyngyllgogerychwyrndrobwll","latitude":53.2,"longitude":-4.2,"country_code":"GB"}]})";
  CHECK(parse_wx_geocode(longname, strlen(longname), p) && p.name.size() == kWxPlaceMax);
}

void test_forecast() {
  WxNow n;
  std::string c = slurp("forecast_coppell.json");
  CHECK(parse_wx_forecast(c.data(), c.size(), n));
  CHECK(n.ok && n.temp == 231 && n.code == 0 && !n.day);
  CHECK(n.ndays == 5);
  CHECK(n.days[0].hi == 282 && n.days[0].lo == 173 && n.days[0].code == 3);
  CHECK(n.days[0].wday == 2);  // 2026-10-27 is a Tuesday
  CHECK(n.days[4].code == 81 && n.days[4].wday == 6);
  CHECK(wx_temp_text(n.temp, true) == "74\xC2\xB0");
  CHECK(wx_temp_text(n.temp, false, true) == "23\xC2\xB0" "C");
  std::string s = slurp("forecast_svalbard.json");
  CHECK(parse_wx_forecast(s.data(), s.size(), n));
  CHECK(n.temp == 16 && n.code == 51 && n.days[0].lo == -19 && n.days[4].lo == -36);
  CHECK(wx_temp_text(n.days[4].lo, false) == "-4\xC2\xB0");
  CHECK(wx_temp_text(n.days[0].lo, false) == "-2\xC2\xB0");
  CHECK(wx_temp_text(kWxNoTemp, true).empty());
  // An error, a cut-off reply and a day with a hole in it
  const char *err = R"({"error":true,"reason":"Latitude must be in range of -90 to 90"})";
  CHECK(!parse_wx_forecast(err, strlen(err), n));
  CHECK(!parse_wx_forecast(c.data(), c.size() / 2, n));
  const char *hole =
      R"({"current":{"temperature_2m":10,"weather_code":2,"is_day":1},"daily":{"time":["2026-10-27","2026-10-28","2026-10-29"],)"
      R"("weather_code":[1,null,3],"temperature_2m_max":[12,13,14],"temperature_2m_min":[1,2,3]}})";
  CHECK(parse_wx_forecast(hole, strlen(hole), n) && n.ndays == 1 && n.day);
  const char *nocur = R"({"daily":{}})";
  CHECK(!parse_wx_forecast(nocur, strlen(nocur), n));
}

void test_hour() {
  WxHour h;
  std::string e = slurp("hour_evanston.json");
  CHECK(parse_wx_hour(e.data(), e.size(), h) && h.temp == 118 && h.code == 61);
  std::string far = slurp("hour_out_of_range.json");
  CHECK(!parse_wx_hour(far.data(), far.size(), h) && !h.ok);
}

void test_urls() {
  CHECK(wx_geocode_url("75019") ==
        "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=75019&countryCode=US");
  CHECK(wx_geocode_url("Coppell, TX") ==
        "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=Coppell%2C%20TX");
  CHECK(wx_geocode_url("750190").find("countryCode") == std::string::npos);
  CHECK(wx_url_encode("S\xC3\xA3o Paulo") == "S%C3%A3o%20Paulo");
  std::string f = wx_forecast_url(32.9546f, -97.015f);
  CHECK(f.find("latitude=32.955&longitude=-97.015") != std::string::npos);
  CHECK(f.find("forecast_days=5") != std::string::npos && f.find("temperature_unit") == std::string::npos);
  // 2026-11-01 18:25 UTC asks for the 18:00 hour
  std::string h = wx_hour_url(42.041f, -87.69f, 1793557500);
  CHECK(h.find("start_hour=2026-11-01T18:00&end_hour=2026-11-01T18:00") != std::string::npos);
  CHECK(wx_query_clean("  Coppell,\tTX  ") == "Coppell,TX");
  CHECK(wx_query_clean("New   York") == "New York");
  CHECK(wx_query_clean(std::string(80, 'a')).size() == kWxQueryMax);
}

void test_icons_words() {
  CHECK(wmo_icon(0, true) == WxIcon::SUN && wmo_icon(0, false) == WxIcon::MOON);
  CHECK(wmo_icon(2, true) == WxIcon::PARTLY_DAY && wmo_icon(2, false) == WxIcon::PARTLY_NIGHT);
  CHECK(wmo_icon(3, true) == WxIcon::CLOUD && wmo_icon(45, true) == WxIcon::FOG);
  CHECK(wmo_icon(53, true) == WxIcon::DRIZZLE && wmo_icon(63, true) == WxIcon::RAIN);
  CHECK(wmo_icon(81, true) == WxIcon::RAIN && wmo_icon(73, true) == WxIcon::SNOW);
  CHECK(wmo_icon(86, true) == WxIcon::SNOW && wmo_icon(67, true) == WxIcon::SLEET);
  CHECK(wmo_icon(95, true) == WxIcon::STORM && wmo_icon(99, false) == WxIcon::STORM);
  for (int code : {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 71, 73, 75, 77, 80, 81, 82, 85, 86, 95,
                   96, 99}) {
    CHECK(strlen(wmo_words(code)) > 0 && strlen(wmo_words(code)) <= 21);
  }
  CHECK(strlen(wmo_words(42)) == 0);
  // ESPN's fields: AccuWeather's numbers, or words
  CHECK(accu_icon(1) == WxIcon::SUN && accu_icon(34) == WxIcon::MOON && accu_icon(15) == WxIcon::STORM);
  CHECK(accu_icon(7) == WxIcon::CLOUD && accu_icon(22) == WxIcon::SNOW && accu_icon(99) == WxIcon::NONE);
  CHECK(words_icon("Sunny") == WxIcon::SUN && words_icon("Clear") == WxIcon::SUN);
  CHECK(words_icon("Mostly Cloudy") == WxIcon::CLOUD && words_icon("Partly sunny") == WxIcon::PARTLY_DAY);
  CHECK(words_icon("Chance of Rain") == WxIcon::RAIN && words_icon("Thunderstorms") == WxIcon::STORM);
  CHECK(words_icon("Snow showers") == WxIcon::SNOW && words_icon("Ice") == WxIcon::SLEET);
  CHECK(words_icon("Nice") == WxIcon::NONE && words_icon("") == WxIcon::NONE);
  CHECK(wx_at_night(WxIcon::SUN, true) == WxIcon::MOON && wx_at_night(WxIcon::RAIN, true) == WxIcon::RAIN);
  CHECK(wx_at_night(WxIcon::PARTLY_DAY, false) == WxIcon::PARTLY_DAY);
}

void test_units_places() {
  CHECK(wx_country_f("US") && wx_country_f("PR") && !wx_country_f("GB") && !wx_country_f(""));
  const WxTzCity *c = wx_tz_city("America/Chicago");
  CHECK(c != nullptr && strcmp(c->name, "Chicago") == 0 && c->fahrenheit);
  CHECK(wx_tz_city("Europe/London") != nullptr && !wx_tz_city("Europe/London")->fahrenheit);
  CHECK(wx_tz_city("UTC") == nullptr && wx_tz_city(nullptr) == nullptr);
  CHECK(wx_degrees(-178, true) == 0 && wx_degrees(370, true) == 99 && wx_degrees(-5, false) == -1);
  CHECK(strcmp(wx_day_name(0), "Sun") == 0 && strcmp(wx_day_name(6), "Sat") == 0);
  CHECK(wxd::weekday("2026-12-25") == 5 && wxd::weekday("2000-02-29") == 2 && wxd::weekday("nope") == -1);
}

void test_turns() {
  const uint32_t P = kWxPageMs, MIN = 60000;
  // Down: up after every minutes, not while another card is up or a game is on
  CHECK(wx_turn(true, true, 0, 2, 5 * MIN, 5, P, false) == WxTurn::SHOW);
  CHECK(wx_turn(true, true, 0, 2, 5 * MIN - 1, 5, P, false) == WxTurn::NONE);
  CHECK(wx_turn(true, true, 0, 2, 9 * MIN, 5, P, true) == WxTurn::NONE);
  CHECK(wx_turn(true, false, 0, 2, 9 * MIN, 5, P, false) == WxTurn::NONE);
  CHECK(wx_turn(false, true, 0, 2, 9 * MIN, 5, P, false) == WxTurn::NONE);
  CHECK(wx_turn(true, true, 0, 0, 9 * MIN, 5, P, false) == WxTurn::NONE);  // no forecast
  // Up: page by page, then down; a game starting takes it down at once
  CHECK(wx_turn(true, true, 1, 2, P - 1, 5, P, false) == WxTurn::NONE);
  CHECK(wx_turn(true, true, 1, 2, P, 5, P, false) == WxTurn::NEXT);
  CHECK(wx_turn(true, true, 2, 2, P, 5, P, false) == WxTurn::HIDE);
  CHECK(wx_turn(true, false, 1, 2, 10, 5, P, false) == WxTurn::HIDE);
  CHECK(wx_turn(false, true, 2, 2, 10, 5, P, false) == WxTurn::HIDE);
  CHECK(clamp_wx_every(0) == 1 && clamp_wx_every(99) == kWxEveryMax && clamp_wx_every(7) == 7);
  CHECK(wx_backoff_ms(0) == 0 && wx_backoff_ms(1) == 2 * MIN && wx_backoff_ms(3) == 10 * MIN);
  CHECK(wx_backoff_ms(6) == 60 * MIN && wx_backoff_ms(60) == 60 * MIN);
}

void test_kick() {
  const int64_t now = 1793500000;
  CHECK(wx_kick_wanted(0, false, false, true, "Evanston, IL", now + 86400, now));
  CHECK(wx_kick_wanted(-1, true, false, true, "London, England", now + 86400, now));
  CHECK(!wx_kick_wanted(-1, false, false, true, "Boston, MA", now + 86400, now));  // an arena sport
  CHECK(!wx_kick_wanted(1, true, false, true, "Atlanta, GA", now + 86400, now));   // a dome
  CHECK(!wx_kick_wanted(0, true, true, true, "Evanston, IL", now + 86400, now));   // ESPN has one
  CHECK(!wx_kick_wanted(0, true, false, false, "Evanston, IL", now + 86400, now)); // started
  CHECK(!wx_kick_wanted(0, true, false, true, "", now + 86400, now));
  CHECK(!wx_kick_wanted(0, true, false, true, "Evanston, IL", now + 16 * 86400, now));
  CHECK(!wx_kick_wanted(0, true, false, true, "Evanston, IL", now + 86400, 0));
}

void test_card() {
  WxNow n;
  std::string c = slurp("forecast_coppell.json");
  CHECK(parse_wx_forecast(c.data(), c.size(), n));
  WxCard card = wx_card(n, true, "Coppell");
  CHECK(card.icon == WxIcon::MOON && card.temp == "74\xC2\xB0" && card.words == "Clear");
  CHECK(card.hilo == "H 83\xC2\xB0 L 63\xC2\xB0" && card.place == "Coppell");
  CHECK(card.ndays == 5 && card.days[0].name == "Tue" && card.days[1].name == "Wed");
  CHECK(card.days[3].icon == WxIcon::DRIZZLE && card.days[4].icon == WxIcon::RAIN && card.days[4].hi == "72\xC2\xB0");
  CHECK(wx_card(n, false, "").days[4].hi == "22\xC2\xB0");
  CHECK(wx_card(WxNow{}, true, "X").icon == WxIcon::NONE && wx_card(WxNow{}, true, "X").ndays == 0);
  // ESPN's "54F Clear" in either unit, and an Open-Meteo hour
  WxBadge b = wx_badge_espn(54, "54F Clear", WxIcon::SUN, true);
  CHECK(b.text == "54\xC2\xB0" "F" && b.words == "54\xC2\xB0" "F Clear" && b.icon == WxIcon::SUN);
  b = wx_badge_espn(54, "54F Clear", WxIcon::SUN, false);
  CHECK(b.text == "12\xC2\xB0" "C" && b.words == "12\xC2\xB0" "C Clear");
  CHECK(wx_badge_espn(33, "33F", WxIcon::NONE, true).words == "33\xC2\xB0" "F");
  CHECK(wx_badge_espn(-999, "x", WxIcon::SUN, true).text.empty());
  WxHour h;
  std::string e = slurp("hour_evanston.json");
  CHECK(parse_wx_hour(e.data(), e.size(), h));
  b = wx_badge_hour(h, false, true);
  CHECK(b.icon == WxIcon::RAIN && b.text == "53\xC2\xB0" "F" && b.words == "53\xC2\xB0" "F Light rain");
  CHECK(wx_badge_hour(WxHour{}, false, true).text.empty());
}

struct Img {
  int w, h;
  std::vector<uint32_t> px;
  void operator()(int x, int y, uint32_t c) {
    if (x >= 0 && y >= 0 && x < w && y < h)
      px[y * w + x] = c;
    else
      out_of_bounds++;
  }
  int out_of_bounds{0};
};

void test_draw() {
  // Every icon at every size draws something and stays in its square.
  for (int i = 1; i <= (int) WxIcon::STORM; i++) {
    for (int s : {12, 24, 32}) {
      for (uint32_t f : {0u, 1u, 7u, 29u, 1000u, 0xFFFFFFFFu}) {
        Img img{s + 4, s + 4, std::vector<uint32_t>((s + 4) * (s + 4), 0)};
        int lit = 0;
        wx_draw_icon(img, (WxIcon) i, 2, 2, s, f);
        for (int y = 0; y < img.h; y++)
          for (int x = 0; x < img.w; x++) {
            bool edge = x < 2 || y < 2 || x >= s + 2 || y >= s + 2;
            if (img.px[y * img.w + x] != 0) {
              lit++;
              CHECK(!edge);
            }
          }
        CHECK(lit > s / 2);
        CHECK(img.out_of_bounds == 0);
      }
    }
  }
}

// Every icon's first 30 frames at 32 and 12, side by side, as PPM files.
void write_ppm(const char *dir) {
  for (int i = 1; i <= (int) WxIcon::STORM; i++) {
    for (int f = 0; f < 30; f++) {
      Img img{48, 32, std::vector<uint32_t>(48 * 32, 0)};
      wx_draw_icon(img, (WxIcon) i, 0, 0, 32, f);
      wx_draw_icon(img, (WxIcon) i, 35, 10, 12, f);
      char path[256];
      snprintf(path, sizeof(path), "%s/icon%02d_%02d.ppm", dir, i, f);
      FILE *o = fopen(path, "wb");
      if (o == nullptr)
        return;
      fprintf(o, "P6 %d %d 255\n", img.w, img.h);
      for (uint32_t c : img.px) {
        unsigned char rgb[3] = {(unsigned char) (c >> 16), (unsigned char) (c >> 8), (unsigned char) c};
        fwrite(rgb, 1, 3, o);
      }
      fclose(o);
    }
  }
}

}  // namespace

int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "--png") {
    write_ppm(argv[2]);
    return 0;
  }
  test_geocode();
  test_forecast();
  test_hour();
  test_urls();
  test_icons_words();
  test_units_places();
  test_turns();
  test_kick();
  test_card();
  test_draw();
  printf("test_weather: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
