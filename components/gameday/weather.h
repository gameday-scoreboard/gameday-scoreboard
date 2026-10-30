// Weather on the board, from Open-Meteo (no key): the local weather card that
// takes turns with the board between games, and the forecast at an outdoor
// stadium for kickoff on the next-game card when ESPN's event has none.
//   - The place: a city or ZIP the owner types, geocoded once and saved;
//     unset, the time zone's city from the table below, with no request.
//   - Temperatures are read in Celsius and kept in tenths, so the F/C
//     setting needs no new read.
//   - Reads run on the fetch task between ESPN reads (gameday.cpp), every
//     kWxRefreshMs, backing off after a failure. Nothing shows until a read
//     succeeds, and a forecast older than kWxStaleMs stops showing.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <ArduinoJson.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace espn {

// The icons the card draws (wx_icons.h).
enum class WxIcon : uint8_t {
  NONE,
  SUN,
  MOON,
  PARTLY_DAY,
  PARTLY_NIGHT,
  CLOUD,
  FOG,
  DRIZZLE,
  RAIN,
  SNOW,
  SLEET,
  STORM,
};

constexpr int16_t kWxNoTemp = INT16_MIN;
constexpr uint8_t kWxDays = 5;          // the strip on a wide board; a single panel shows 3
constexpr size_t kWxQueryMax = 48;      // the owner's city or ZIP, in bytes
constexpr size_t kWxPlaceMax = 24;      // the name the geocoder gave it, cut to fit the card
constexpr uint32_t kWxRefreshMs = 30u * 60u * 1000u;
constexpr uint32_t kWxStaleMs = 3u * 60u * 60u * 1000u;
constexpr uint32_t kWxKickRefreshMs = 3u * 60u * 60u * 1000u;
// Open-Meteo forecasts 16 days out and refuses an hour past that.
constexpr int64_t kWxKickMaxAheadS = 15 * 86400;
constexpr int kWxEveryDefault = 5;  // minutes between the card's turns
constexpr int kWxEveryMax = 30;
constexpr uint32_t kWxPageMs = 8000;  // each of the card's two pages
// A reply past this is not Open-Meteo's: the read stops and fails.
constexpr size_t kWxBodyMax = 8 * 1024;
constexpr uint8_t kWxUnitAuto = 0, kWxUnitF = 1, kWxUnitC = 2;

inline int clamp_wx_every(int minutes) {
  return minutes < 1 ? 1 : minutes > kWxEveryMax ? kWxEveryMax : minutes;
}

// After n failed reads in a row: 2, 5, 10, 20, 40, then 60 minutes.
inline uint32_t wx_backoff_ms(uint32_t fails) {
  static const uint8_t kMin[] = {2, 5, 10, 20, 40, 60};
  if (fails == 0)
    return 0;
  uint32_t i = fails - 1 < sizeof(kMin) ? fails - 1 : sizeof(kMin) - 1;
  return (uint32_t) kMin[i] * 60u * 1000u;
}

// The card's local weather: now, and today plus the next days.
struct WxDay {
  int16_t hi{kWxNoTemp}, lo{kWxNoTemp};  // tenths of a degree C
  uint8_t code{0};                       // WMO weather code
  uint8_t wday{0};                       // 0 Sunday
};

struct WxNow {
  bool ok{false};
  int16_t temp{kWxNoTemp};
  uint8_t code{0};
  bool day{true};
  uint8_t ndays{0};
  WxDay days[kWxDays];
};

// The forecast for one hour (the kickoff).
struct WxHour {
  bool ok{false};
  int16_t temp{kWxNoTemp};
  uint8_t code{0};
};

struct WxPlace {
  bool ok{false};
  float lat{0}, lon{0};
  std::string name;     // "Coppell"
  std::string country;  // "US", two letters
};

namespace wxd {

inline int16_t tenths(JsonVariantConst v) {
  if (!v.is<float>())
    return kWxNoTemp;
  float f = v.as<float>();
  if (!(f > -90.0f && f < 70.0f))  // NaN fails too
    return kWxNoTemp;
  return (int16_t) lroundf(f * 10.0f);
}

inline bool code_ok(JsonVariantConst v) { return v.is<int>() && v.as<int>() >= 0 && v.as<int>() <= 99; }

// 0 Sunday, from "2026-10-27" (days_from_civil). -1 when it isn't a date.
inline int weekday(const char *ymd) {
  int y, m, d;
  if (ymd == nullptr || sscanf(ymd, "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31)
    return -1;
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned) (y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = (long) era * 146097 + (long) doe - 719468;
  return (int) (((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday
}

}  // namespace wxd

// Open-Meteo's geocoding answer: its first match. False with none.
inline bool parse_wx_geocode(const char *json, size_t len, WxPlace &out) {
  out = WxPlace{};
  JsonDocument filter;
  JsonObject r = filter["results"].add<JsonObject>();
  r["name"] = true;
  r["latitude"] = true;
  r["longitude"] = true;
  r["country_code"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter)))
    return false;
  JsonObjectConst first = doc["results"][0];
  if (first.isNull() || !first["latitude"].is<float>() || !first["longitude"].is<float>())
    return false;
  float lat = first["latitude"].as<float>(), lon = first["longitude"].as<float>();
  if (!(lat >= -90.0f && lat <= 90.0f && lon >= -180.0f && lon <= 180.0f))
    return false;
  out.lat = lat;
  out.lon = lon;
  const char *name = first["name"] | "";
  out.name = name;
  if (out.name.size() > kWxPlaceMax) {
    size_t cut = kWxPlaceMax;
    while (cut > 0 && ((unsigned char) out.name[cut] & 0xC0) == 0x80)
      cut--;
    out.name.resize(cut);
  }
  const char *cc = first["country_code"] | "";
  if (strlen(cc) == 2)
    out.country = cc;
  out.ok = true;
  return true;
}

// The card's read: current temperature_2m, weather_code and is_day, and the
// daily weather_code, temperature_2m_max and temperature_2m_min.
inline bool parse_wx_forecast(const char *json, size_t len, WxNow &out) {
  out = WxNow{};
  JsonDocument filter;
  filter["error"] = true;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["is_day"] = true;
  filter["daily"]["time"] = true;
  filter["daily"]["weather_code"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter)) || (doc["error"] | false))
    return false;
  JsonObjectConst cur = doc["current"];
  out.temp = wxd::tenths(cur["temperature_2m"]);
  if (out.temp == kWxNoTemp || !wxd::code_ok(cur["weather_code"]))
    return false;
  out.code = (uint8_t) cur["weather_code"].as<int>();
  out.day = (cur["is_day"] | 1) != 0;
  JsonObjectConst daily = doc["daily"];
  JsonArrayConst t = daily["time"], c = daily["weather_code"], hi = daily["temperature_2m_max"],
                 lo = daily["temperature_2m_min"];
  for (size_t i = 0; i < t.size() && out.ndays < kWxDays; i++) {
    WxDay d;
    int wd = wxd::weekday(t[i].as<const char *>());
    d.hi = wxd::tenths(hi[i]);
    d.lo = wxd::tenths(lo[i]);
    if (wd < 0 || d.hi == kWxNoTemp || d.lo == kWxNoTemp || !wxd::code_ok(c[i]))
      break;  // the strip stops at the first day it can't show
    d.wday = (uint8_t) wd;
    d.code = (uint8_t) c[i].as<int>();
    out.days[out.ndays++] = d;
  }
  out.ok = true;
  return true;
}

// The kickoff read: hourly temperature_2m and weather_code for one hour.
inline bool parse_wx_hour(const char *json, size_t len, WxHour &out) {
  out = WxHour{};
  JsonDocument filter;
  filter["error"] = true;
  filter["hourly"]["temperature_2m"] = true;
  filter["hourly"]["weather_code"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter)) || (doc["error"] | false))
    return false;
  out.temp = wxd::tenths(doc["hourly"]["temperature_2m"][0]);
  JsonVariantConst code = doc["hourly"]["weather_code"][0];
  if (out.temp == kWxNoTemp || !wxd::code_ok(code))
    return false;
  out.code = (uint8_t) code.as<int>();
  out.ok = true;
  return true;
}

// Percent-encoding for a query parameter.
inline std::string wx_url_encode(const std::string &s) {
  static const char *kHex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += (char) c;
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 15];
    }
  }
  return out;
}

inline bool wx_is_zip(const std::string &q) {
  if (q.size() != 5)
    return false;
  for (char c : q)
    if (c < '0' || c > '9')
      return false;
  return true;
}

// The owner's text as the panel keeps it: one line, trimmed, control
// characters out, at most kWxQueryMax bytes (never cut inside a character).
inline std::string wx_query_clean(const std::string &in) {
  std::string out;
  for (unsigned char c : in)
    if (c >= 0x20 && c != 0x7F && !(c == ' ' && (out.empty() || out.back() == ' ')))
      out += (char) c;
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  if (out.size() > kWxQueryMax) {
    size_t cut = kWxQueryMax;
    while (cut > 0 && ((unsigned char) out[cut] & 0xC0) == 0x80)
      cut--;
    out.resize(cut);
  }
  return out;
}

// "Coppell, TX", "London, England" and "75019" all find the place; a bare
// five-digit number is a US ZIP (Open-Meteo alone finds Paris for 75019).
inline std::string wx_geocode_url(const std::string &query) {
  std::string url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=" +
                    wx_url_encode(query);
  if (wx_is_zip(query))
    url += "&countryCode=US";
  return url;
}

inline std::string wx_forecast_url(float lat, float lon) {
  char buf[320];
  snprintf(buf, sizeof(buf),
           "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&current=temperature_2m,weather_code,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=%u",
           lat, lon, (unsigned) kWxDays);
  return buf;
}

// One hour's forecast at epoch, the hour it falls in, in UTC.
inline std::string wx_hour_url(float lat, float lon, int64_t epoch) {
  // civil_from_days: no gmtime_r on every host compiler.
  int64_t days = epoch >= 0 ? epoch / 86400 : (epoch - 86399) / 86400;
  int hour = (int) ((epoch - days * 86400) / 3600);
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = (unsigned) (days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  const long long y = (long long) yoe + era * 400 + (m <= 2);
  char buf[300];
  snprintf(buf, sizeof(buf),
           "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&hourly=temperature_2m,weather_code&timezone=GMT"
           "&start_hour=%04lld-%02u-%02uT%02d:00&end_hour=%04lld-%02u-%02uT%02d:00",
           lat, lon, y, m, d, hour, y, m, d, hour);
  return buf;
}

// The WMO code's icon. Night swaps the sun for the moon.
inline WxIcon wmo_icon(int code, bool day) {
  if (code <= 1)
    return day ? WxIcon::SUN : WxIcon::MOON;
  if (code == 2)
    return day ? WxIcon::PARTLY_DAY : WxIcon::PARTLY_NIGHT;
  if (code == 3)
    return WxIcon::CLOUD;
  if (code == 45 || code == 48)
    return WxIcon::FOG;
  if (code >= 51 && code <= 55)
    return WxIcon::DRIZZLE;
  if (code == 56 || code == 57 || code == 66 || code == 67)
    return WxIcon::SLEET;
  if ((code >= 61 && code <= 65) || (code >= 80 && code <= 82))
    return WxIcon::RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86)
    return WxIcon::SNOW;
  if (code >= 95)
    return WxIcon::STORM;
  return WxIcon::CLOUD;
}

// Short enough for a 128-wide board in the small font (21 characters).
inline const char *wmo_words(int code) {
  switch (code) {
    case 0:
      return "Clear";
    case 1:
      return "Mostly clear";
    case 2:
      return "Partly cloudy";
    case 3:
      return "Cloudy";
    case 45:
    case 48:
      return "Fog";
    case 51:
    case 53:
    case 55:
      return "Drizzle";
    case 56:
    case 57:
      return "Freezing drizzle";
    case 61:
      return "Light rain";
    case 63:
      return "Rain";
    case 65:
      return "Heavy rain";
    case 66:
    case 67:
      return "Freezing rain";
    case 71:
      return "Light snow";
    case 73:
      return "Snow";
    case 75:
      return "Heavy snow";
    case 77:
      return "Snow grains";
    case 80:
    case 81:
      return "Showers";
    case 82:
      return "Heavy showers";
    case 85:
    case 86:
      return "Snow showers";
    case 95:
      return "Thunderstorms";
    case 96:
    case 99:
      return "Storms, hail";
    default:
      return "";
  }
}

// ESPN's event forecast comes from AccuWeather: one of its fields carries
// AccuWeather's icon number ("1" Sunny, "34" Mostly clear). NONE for one it
// doesn't know.
inline WxIcon accu_icon(int id) {
  switch (id) {
    case 1: case 2: case 5: case 30: case 31: case 32:
      return WxIcon::SUN;
    case 3: case 4: case 6:
      return WxIcon::PARTLY_DAY;
    case 7: case 8:
    case 38:
      return WxIcon::CLOUD;
    case 11:
      return WxIcon::FOG;
    case 12: case 13: case 14: case 18: case 39: case 40:
      return WxIcon::RAIN;
    case 15: case 16: case 17: case 41: case 42:
      return WxIcon::STORM;
    case 19: case 20: case 21: case 22: case 23: case 43: case 44:
      return WxIcon::SNOW;
    case 24: case 25: case 26: case 29:
      return WxIcon::SLEET;
    case 33: case 34: case 37:
      return WxIcon::MOON;
    case 35: case 36:
      return WxIcon::PARTLY_NIGHT;
    default:
      return WxIcon::NONE;
  }
}

// The icon for words like "Mostly sunny" or "Chance of rain", for a forecast
// whose number field is missing. NONE when nothing matches.
inline WxIcon words_icon(const std::string &words) {
  std::string w;
  for (char c : words)
    w += (char) (c >= 'A' && c <= 'Z' ? c + 32 : c);
  auto has = [&](const char *k) { return w.find(k) != std::string::npos; };
  if (has("storm") || has("thunder"))
    return WxIcon::STORM;
  if (has("sleet") || has("freezing") || has(" ice") || w.rfind("ice", 0) == 0)
    return WxIcon::SLEET;
  if (has("snow") || has("flurr"))
    return WxIcon::SNOW;
  if (has("drizzle"))
    return WxIcon::DRIZZLE;
  if (has("rain") || has("shower"))
    return WxIcon::RAIN;
  if (has("fog") || has("haz") || has("mist"))
    return WxIcon::FOG;
  if (has("partly") || has("intermittent") || has("mostly sunny"))
    return WxIcon::PARTLY_DAY;
  if (has("cloud") || has("overcast") || has("dreary"))
    return WxIcon::CLOUD;
  if (has("sun") || has("clear") || has("fair") || has("hot"))
    return WxIcon::SUN;
  return WxIcon::NONE;
}

// The night form of a day icon (a kickoff after dark), else the same icon.
inline WxIcon wx_at_night(WxIcon icon, bool night) {
  if (!night)
    return icon;
  return icon == WxIcon::SUN ? WxIcon::MOON : icon == WxIcon::PARTLY_DAY ? WxIcon::PARTLY_NIGHT : icon;
}

// Places that read Fahrenheit.
inline bool wx_country_f(const std::string &cc) {
  static const char *kF[] = {"US", "PR", "GU", "VI", "AS", "MP", "LR", "BS", "BZ", "KY", "PW", "FM", "MH"};
  for (const char *c : kF)
    if (cc == c)
      return true;
  return false;
}

// Unset places: the time zone's city, by its IANA name.
struct WxTzCity {
  const char *iana;
  const char *name;
  float lat, lon;
  bool fahrenheit;
};

constexpr WxTzCity kWxTzCities[] = {
    {"America/New_York", "New York", 40.713f, -74.006f, true},
    {"America/Chicago", "Chicago", 41.878f, -87.630f, true},
    {"America/Denver", "Denver", 39.739f, -104.990f, true},
    {"America/Phoenix", "Phoenix", 33.448f, -112.074f, true},
    {"America/Los_Angeles", "Los Angeles", 34.052f, -118.244f, true},
    {"America/Anchorage", "Anchorage", 61.218f, -149.900f, true},
    {"Pacific/Honolulu", "Honolulu", 21.307f, -157.858f, true},
    {"America/Halifax", "Halifax", 44.649f, -63.575f, false},
    {"America/St_Johns", "St. John's", 47.562f, -52.713f, false},
    {"America/Mexico_City", "Mexico City", 19.433f, -99.133f, false},
    {"Europe/London", "London", 51.507f, -0.128f, false},
    {"Europe/Berlin", "Berlin", 52.520f, 13.405f, false},
    {"Australia/Sydney", "Sydney", -33.869f, 151.209f, false},
};

// nullptr for a zone with no city (UTC): no card until a place is set.
inline const WxTzCity *wx_tz_city(const char *iana) {
  if (iana == nullptr)
    return nullptr;
  for (const auto &c : kWxTzCities)
    if (strcmp(c.iana, iana) == 0)
      return &c;
  return nullptr;
}

// Whole degrees in the unit shown, from tenths of a degree C.
inline int wx_degrees(int16_t t10, bool fahrenheit) {
  double c = t10 / 10.0;
  return (int) lround(fahrenheit ? c * 9.0 / 5.0 + 32.0 : c);
}

// "72°" (the card), with the letter "72°F" (the ticker). "" for no reading.
inline std::string wx_temp_text(int16_t t10, bool fahrenheit, bool letter = false) {
  if (t10 == kWxNoTemp)
    return "";
  char buf[16];
  snprintf(buf, sizeof(buf), "%d\xC2\xB0%s", wx_degrees(t10, fahrenheit), letter ? (fahrenheit ? "F" : "C") : "");
  return buf;
}

inline const char *wx_day_name(uint8_t wday) {
  static const char *kNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return kNames[wday % 7];
}

// What the card shows, built on the main loop from the last good read.
// page 0 is down; 1 is now, 2 the days.
struct WxCardDay {
  WxIcon icon{WxIcon::NONE};
  std::string name, hi, lo;
};

struct WxCard {
  uint8_t page{0};
  WxIcon icon{WxIcon::NONE};
  std::string temp, words, hilo, place;
  uint8_t ndays{0};
  WxCardDay days[kWxDays];
};

inline WxCard wx_card(const WxNow &n, bool fahrenheit, const std::string &place) {
  WxCard c;
  if (!n.ok)
    return c;
  c.icon = wmo_icon(n.code, n.day);
  c.temp = wx_temp_text(n.temp, fahrenheit);
  c.words = wmo_words(n.code);
  if (n.ndays > 0)
    c.hilo = "H " + wx_temp_text(n.days[0].hi, fahrenheit) + " L " + wx_temp_text(n.days[0].lo, fahrenheit);
  c.place = place;
  c.ndays = n.ndays;
  for (uint8_t i = 0; i < n.ndays; i++) {
    c.days[i].icon = wmo_icon(n.days[i].code, true);
    c.days[i].name = wx_day_name(n.days[i].wday);
    c.days[i].hi = wx_temp_text(n.days[i].hi, fahrenheit);
    c.days[i].lo = wx_temp_text(n.days[i].lo, fahrenheit);
  }
  return c;
}

// The next-game card's forecast: a small icon and "54°F" on the board, and
// the words for the ticker ("54°F Clear"). Empty text: nothing to show.
struct WxBadge {
  WxIcon icon{WxIcon::NONE};
  std::string text;   // "54°F"
  std::string words;  // "54°F Light rain", the ticker's part before " at <venue>"
};

// From ESPN's event forecast, which venue_line() shows as "54F Clear": its
// temperature in F, and the words after it.
inline WxBadge wx_badge_espn(int temp_f, const std::string &espn_text, WxIcon icon, bool fahrenheit) {
  WxBadge b;
  if (temp_f < -80 || temp_f > 150)
    return b;
  int16_t t10 = (int16_t) lround((temp_f - 32) * 50.0 / 9.0);
  b.icon = icon;
  b.text = wx_temp_text(t10, fahrenheit, true);
  size_t sp = espn_text.find(' ');
  b.words = sp == std::string::npos ? b.text : b.text + espn_text.substr(sp);
  return b;
}

inline WxBadge wx_badge_hour(const WxHour &h, bool night, bool fahrenheit) {
  WxBadge b;
  if (!h.ok)
    return b;
  b.icon = wmo_icon(h.code, !night);
  b.text = wx_temp_text(h.temp, fahrenheit, true);
  const char *w = wmo_words(h.code);
  b.words = *w ? b.text + " " + w : b.text;
  return b;
}

// The card's turns between games, like the message card's (message.h):
// page 0 is down; it goes up every `every` minutes for `pages` pages of
// page_ms each. It waits while another card is up, and never shows outside
// `between`.
enum class WxTurn : uint8_t { NONE, SHOW, NEXT, HIDE };

inline WxTurn wx_turn(bool on, bool between, uint8_t page, uint8_t pages, uint32_t since_ms, int every,
                      uint32_t page_ms, bool other_up) {
  if (!on || !between || pages == 0)
    return page != 0 ? WxTurn::HIDE : WxTurn::NONE;
  if (page != 0) {
    if (since_ms < page_ms)
      return WxTurn::NONE;
    return page < pages ? WxTurn::NEXT : WxTurn::HIDE;
  }
  if (other_up)
    return WxTurn::NONE;
  return since_ms >= (uint32_t) every * 60u * 1000u ? WxTurn::SHOW : WxTurn::NONE;
}

// The kickoff forecast is worth a read: an outdoor game (indoor 0) or one
// whose venue doesn't say (-1) in a sport played outside, not started, no
// forecast from ESPN, a venue city to look up, and a kickoff close enough
// for Open-Meteo to forecast.
inline bool wx_kick_wanted(int8_t indoor, bool outdoor_sport, bool espn_has, bool pre, const std::string &city,
                           int64_t kickoff, int64_t now) {
  if (!pre || espn_has || city.empty() || kickoff <= 0 || now <= 0)
    return false;
  if (indoor == 1 || (indoor < 0 && !outdoor_sport))
    return false;
  return kickoff > now - 3600 && kickoff - now <= kWxKickMaxAheadS;
}

}  // namespace espn
