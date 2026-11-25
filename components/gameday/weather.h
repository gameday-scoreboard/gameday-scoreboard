// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The local weather card: the owner's town, looked up once with Open-Meteo's
// geocoding API (no key), and its forecast read every half hour from
// Open-Meteo's forecast API, shown between games as a card of its own: the
// temperature now, today's high and low, an icon for the sky, and the next
// two days small underneath. Also the kickoff forecast on the next game's
// board: ESPN's own when the event has one, else Open-Meteo's for the hour
// of the kickoff at the venue's city. Temperatures are read in Fahrenheit and
// kept in tenths of a degree, so the owner's F/C setting needs no new read.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <ArduinoJson.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "espn_parse.h"
#include "message.h"

namespace espn {

// The town as typed and the place found, in bytes: one line each, cleaned
// like the nickname.
constexpr size_t kTownMax = 40;
constexpr size_t kPlaceMax = 40;
// The Weather page (weather_page.h, firmware/pages/weather.yaml) reads the
// same forecast, with the next hours and each day's sunrise and sunset in it.
// Reads every half hour; the card shows a forecast up to this old, then steps
// out of the rotation until a read works again.
constexpr uint32_t kWxReadMs = 30 * 60 * 1000;
constexpr uint32_t kWxFreshMs = 3 * 60 * 60 * 1000;
// The card's turn: seconds 40 to 49 of every minute on the wall clock, the
// slot before the custom countdown's (countdown.h), the same 10 s dwell.
constexpr int64_t kWxTurnStartS = 40;
constexpr int64_t kWxTurnS = 10;

inline bool weather_turn(int64_t now) {
  int64_t s = now % 60;
  return s >= kWxTurnStartS && s < kWxTurnStartS + kWxTurnS;
}

// What the geocoder found for the town.
struct WxPlace {
  bool valid{false};  // Open-Meteo answered; name is "" when it knows no such place
  std::string name;   // "Coppell, Texas"
  float lat{0}, lon{0};
  std::string country;  // "US", the two letters (the auto unit)
};

enum class WxIcon : uint8_t { CLOUD = 0, SUN, MOON, PART, FOG, RAIN, SNOW, STORM };
constexpr int kWxIconCount = 8;
// No icon: a forecast with neither a number nor words the panel knows.
constexpr uint8_t kWxIconNone = 255;

// Temperatures in tenths of a degree F (wx_deg turns them into the unit
// shown).
struct WxDay {
  int hi10{0}, lo10{0};
  uint8_t code{0};
  uint8_t wday{0};  // 0 Sunday
};

// One hour of the forecast, for the Weather page (weather_page.h). Small,
// since the component keeps two forecasts (its own and the read's) in
// internal RAM.
struct WxHourly {
  int16_t t10{0};     // tenths of a degree F
  uint8_t code{255};  // WMO weather code; 255 for an hour the reply left out
  int8_t pop{-1};     // the chance of rain, snow or storms, 0 to 100; -1 unknown
  bool night{false};
};
// The hours read: the one under way and the 14 after it, so a forecast read
// up to 2 hours ago still covers the page's next 12 hours.
constexpr size_t kWxHours = 15;
constexpr size_t kWxSunDays = 4;
constexpr int16_t kWxNoSun = -1;

struct WxForecast {
  bool valid{false};
  int t10{0};  // now
  bool night{false};
  uint8_t code{0};  // WMO weather code now
  WxDay today;      // the high and low, today's code
  WxDay next[2];    // tomorrow and the day after
  WxDay later;      // the day after those, for the move past midnight (wx_roll_day)
  bool has_later{false};
  int32_t day0{0};  // today's date in the reply, as days since 1970-01-01; 0 when it had none
  // The Weather page's: the hours from hour0 on (local hours since
  // 1970-01-01, wx_local_minutes / 60), one an entry, and each day's sunrise
  // and sunset from day0 on, as minutes into that day (kWxNoSun for none: a
  // polar day or night, or a reply without them).
  int32_t hour0{0};
  WxHourly hours[kWxHours];
  uint8_t n_hours{0};
  int16_t sunrise[kWxSunDays]{kWxNoSun, kWxNoSun, kWxNoSun, kWxNoSun};
  int16_t sunset[kWxSunDays]{kWxNoSun, kWxNoSun, kWxNoSun, kWxNoSun};
};

// The town as the panel keeps it: one line, printable, no emoji, at most
// kTownMax bytes cut on a whole character.
inline std::string town_clean(const std::string &v) { return message_clean(utf8_only(v), kTownMax, false); }

// A US ZIP: five digits. The lookup then asks for the US only, since "75019"
// on its own comes back as Paris.
inline bool town_is_us_zip(const std::string &town) {
  if (town.size() != 5)
    return false;
  for (char c : town)
    if (c < '0' || c > '9')
      return false;
  return true;
}

// Percent-encodes everything but the unreserved characters, so a town with a
// space, an apostrophe or an accent travels whole.
inline std::string url_encode(const std::string &s) {
  static const char *kHex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
        c == '_' || c == '~') {
      out += (char) c;
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 15];
    }
  }
  return out;
}

inline std::string geocode_url(const std::string &town) {
  std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" + url_encode(town) +
                    "&count=1&language=en&format=json";
  if (town_is_us_zip(town))
    url += "&countryCode=US";
  return url;
}

// tz_iana is the panel's timezone (timezones.h), so "today" is the panel's day.
inline std::string forecast_url(float lat, float lon, const char *tz_iana) {
  char buf[64];
  snprintf(buf, sizeof(buf), "latitude=%.4f&longitude=%.4f", (double) lat, (double) lon);
  return std::string("https://api.open-meteo.com/v1/forecast?") + buf +
         "&current=temperature_2m,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min,"
         "sunrise,sunset&hourly=temperature_2m,weather_code,precipitation_probability,is_day"
         "&temperature_unit=fahrenheit&timezone=" +
         url_encode(tz_iana) + "&forecast_days=4&forecast_hours=15";
}

namespace detail {

inline void fill_geocode_filter(JsonDocument &f) {
  JsonObject r = f["results"].add<JsonObject>();
  r["name"] = true;
  r["admin1"] = true;
  r["country"] = true;
  r["country_code"] = true;
  r["latitude"] = true;
  r["longitude"] = true;
}

inline void fill_forecast_filter(JsonDocument &f) {
  JsonObject c = f["current"].to<JsonObject>();
  c["temperature_2m"] = true;
  c["weather_code"] = true;
  c["is_day"] = true;
  JsonObject d = f["daily"].to<JsonObject>();
  d["time"] = true;
  d["weather_code"] = true;
  d["temperature_2m_max"] = true;
  d["temperature_2m_min"] = true;
  d["sunrise"] = true;
  d["sunset"] = true;
  JsonObject h = f["hourly"].to<JsonObject>();
  h["time"] = true;
  h["temperature_2m"] = true;
  h["weather_code"] = true;
  h["precipitation_probability"] = true;
  h["is_day"] = true;
}

// A temperature that could be real, in tenths of a degree. False for a
// missing or absurd one.
inline bool wx_degrees(JsonVariantConst v, int &out) {
  if (!v.is<float>())
    return false;
  float f = v.as<float>() * 10.0f;
  if (!(f > -1500.0f && f < 2000.0f))  // NaN fails too
    return false;
  out = (int) (f < 0 ? f - 0.5f : f + 0.5f);
  return true;
}

inline uint8_t wx_code(JsonVariantConst v) {
  int c = v | -1;
  return c >= 0 && c <= 99 ? (uint8_t) c : 255;
}

}  // namespace detail

// Days since 1970-01-01 of a civil date (Howard Hinnant's days_from_civil).
inline int32_t wx_days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned) (y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t) doe - 719468;
}

// "2026-11-05" as days since 1970-01-01; 0 when it isn't a date.
inline int32_t wx_days_from_date(const char *date) {
  int y = 0, m = 0, d = 0;
  if (date == nullptr || sscanf(date, "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31)
    return 0;
  return wx_days_from_civil(y, (unsigned) m, (unsigned) d);
}

// "2026-11-23T17:22" as local minutes since 1970-01-01 (the reply's times
// are the panel's own, its time zone asked for); 0 when it isn't one.
inline int32_t wx_local_minutes(const char *t) {
  int y = 0, m = 0, d = 0, hh = 0, mm = 0;
  if (t == nullptr || sscanf(t, "%4d-%2d-%2dT%2d:%2d", &y, &m, &d, &hh, &mm) != 5 || m < 1 || m > 12 || d < 1 ||
      d > 31 || hh < 0 || hh > 23 || mm < 0 || mm > 59)
    return 0;
  return wx_days_from_civil(y, (unsigned) m, (unsigned) d) * 1440 + hh * 60 + mm;
}

// The day of the week of "2026-10-29", 0 Sunday. 0 for a date that doesn't read.
inline uint8_t weekday_of(const char *date) {
  if (date == nullptr)
    return 0;
  int y = 0, m = 0, d = 0;
  if (sscanf(date, "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31)
    return 0;
  // Sakamoto's method
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3)
    y -= 1;
  return (uint8_t) ((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
}

inline const char *weekday_text(uint8_t wday) {
  static const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return kDays[wday % 7];
}

// The place name shown back: "Coppell, Texas" in the US (the state), "Paris,
// France" elsewhere, cut to kPlaceMax bytes.
inline std::string place_text(const std::string &name, const std::string &admin1, const std::string &country,
                              const std::string &country_code) {
  std::string region = country_code == "US" && !admin1.empty() ? admin1 : country;
  std::string text = region.empty() ? name : name + ", " + region;
  return message_clean(utf8_only(text), kPlaceMax, false);
}

// Parses the geocoder's answer. `input` is anything ArduinoJson can read
// from. False when the document isn't the geocoder's; true with an empty name
// when it answered and knows no such place.
template<typename TInput> bool parse_geocode(TInput &input, WxPlace &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_geocode_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(10));
  if (err || !doc.is<JsonObjectConst>())
    return false;
  WxPlace p;
  p.valid = true;
  JsonObjectConst r = doc["results"][0];
  if (!r.isNull()) {
    std::string name = detail::str_or_empty(r["name"]);
    JsonVariantConst lat = r["latitude"], lon = r["longitude"];
    if (!name.empty() && lat.is<float>() && lon.is<float>()) {
      float la = lat.as<float>(), lo = lon.as<float>();
      if (la >= -90.0f && la <= 90.0f && lo >= -180.0f && lo <= 180.0f) {
        p.name = place_text(name, detail::str_or_empty(r["admin1"]), detail::str_or_empty(r["country"]),
                            detail::str_or_empty(r["country_code"]));
        p.lat = la;
        p.lon = lo;
        std::string cc = detail::str_or_empty(r["country_code"]);
        if (cc.size() == 2)
          p.country = cc;
      }
    }
  }
  out = p;
  return true;
}

// Parses the forecast. False when it lacks the reading now or three days.
template<typename TInput> bool parse_forecast(TInput &input, WxForecast &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_forecast_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(10));
  if (err || !doc.is<JsonObjectConst>())
    return false;
  WxForecast f;
  JsonObjectConst cur = doc["current"];
  if (!detail::wx_degrees(cur["temperature_2m"], f.t10))
    return false;
  f.code = detail::wx_code(cur["weather_code"]);
  f.night = cur["is_day"].is<int>() && cur["is_day"].as<int>() == 0;
  JsonObjectConst daily = doc["daily"];
  JsonArrayConst hi = daily["temperature_2m_max"], lo = daily["temperature_2m_min"], code = daily["weather_code"],
                 time = daily["time"];
  if (hi.size() < 3 || lo.size() < 3)
    return false;
  for (size_t i = 0; i < 4; i++) {
    WxDay &d = i == 0 ? f.today : i < 3 ? f.next[i - 1] : f.later;
    if (!detail::wx_degrees(hi[i], d.hi10) || !detail::wx_degrees(lo[i], d.lo10)) {
      if (i < 3)
        return false;
      d = WxDay{};
      break;
    }
    d.code = detail::wx_code(code[i]);
    d.wday = weekday_of(time[i].as<const char *>());
    f.has_later = i == 3;
  }
  f.day0 = wx_days_from_date(time[0].as<const char *>());
  // The Weather page's hours and suns. A reply without them (an older
  // read's shape) still makes the card.
  JsonArrayConst sr = daily["sunrise"], ss = daily["sunset"];
  for (size_t i = 0; i < kWxSunDays && f.day0 != 0; i++) {
    const int32_t day = (f.day0 + (int32_t) i) * 1440;
    const int32_t rise = wx_local_minutes(sr[i].as<const char *>()), set = wx_local_minutes(ss[i].as<const char *>());
    if (rise >= day && rise < day + 1440)
      f.sunrise[i] = (int16_t) (rise - day);
    if (set >= day && set < day + 1440)
      f.sunset[i] = (int16_t) (set - day);
  }
  JsonObjectConst hourly = doc["hourly"];
  JsonArrayConst ht = hourly["time"], htemp = hourly["temperature_2m"], hcode = hourly["weather_code"],
                 hpop = hourly["precipitation_probability"], hday = hourly["is_day"];
  f.hour0 = ht.size() > 0 ? wx_local_minutes(ht[0].as<const char *>()) / 60 : 0;
  for (size_t i = 0; f.hour0 != 0 && i < ht.size() && i < kWxHours; i++) {
    f.n_hours = (uint8_t) (i + 1);
    WxHourly &h = f.hours[i];
    int t10 = 0;
    // Each entry is the hour after the one before; one that isn't, or has no
    // temperature, stays empty (code 255).
    if (wx_local_minutes(ht[i].as<const char *>()) / 60 != f.hour0 + (int32_t) i ||
        !detail::wx_degrees(htemp[i], t10))
      continue;
    h.code = detail::wx_code(hcode[i]);
    h.t10 = (int16_t) t10;
    const int pop = hpop[i] | -1;
    h.pop = (int8_t) (pop >= 0 && pop <= 100 ? pop : -1);
    h.night = hday[i].is<int>() && hday[i].as<int>() == 0;
  }
  f.valid = true;
  out = f;
  return true;
}

// After midnight, until the next read lands, the forecast's first day is
// yesterday. Moves it on a day, so today's high and low are today's; drops
// it (valid false) when it is more than a day behind or has no fourth day.
// today is the panel's date in days since 1970-01-01. True when it changed
// the forecast.
inline bool wx_roll_day(WxForecast &f, int32_t today) {
  if (!f.valid || f.day0 == 0 || today <= f.day0)
    return false;
  if (today - f.day0 > 1 || !f.has_later) {
    f.valid = false;
    return true;
  }
  f.today = f.next[0];
  f.next[0] = f.next[1];
  f.next[1] = f.later;
  f.later = WxDay{};
  f.has_later = false;
  for (size_t i = 0; i < kWxSunDays; i++) {
    f.sunrise[i] = i + 1 < kWxSunDays ? f.sunrise[i + 1] : kWxNoSun;
    f.sunset[i] = i + 1 < kWxSunDays ? f.sunset[i + 1] : kWxNoSun;
  }
  f.day0 = today;
  return true;
}

// What the firmware's parsers read from: the HTTP reply, straight from the
// socket (gameday.cpp). They are compiled once for it, in espn_parse.cpp:
// instantiated in gameday.cpp, their literal pools landed out of reach of
// their code and the link failed.
class WxSource {
 public:
  virtual int read() = 0;
  virtual size_t readBytes(char *dst, size_t n) = 0;

 protected:
  ~WxSource() = default;
};
bool parse_geocode_from(WxSource &input, WxPlace &out);
bool parse_forecast_from(WxSource &input, WxForecast &out);

// ---- The kickoff forecast ------------------------------------------------------

// Open-Meteo forecasts 16 days out and refuses an hour past that, so a game
// further off gets its forecast once it comes this close.
constexpr int64_t kWxKickAheadS = 15 * 86400;
// One read per game every three hours; a forecast past twice that is no
// longer shown.
constexpr uint32_t kWxKickReadMs = 3 * 60 * 60 * 1000;
constexpr uint32_t kWxKickStaleMs = 2 * kWxKickReadMs;
// A weather read starts only when the next ESPN read is at least this far off:
// the HTTP client's 20 s timeout and some room, so a read that hangs to its
// end still finishes before ESPN's turn.
constexpr uint32_t kWxEspnGapMs = 25 * 1000;
// The venues looked up lately, so the next poll's forecast needs no lookup,
// and the kickoff forecasts read lately, one per game: Favorite teams turns
// its cards every few seconds.
constexpr size_t kWxVenueCache = 4;
constexpr size_t kWxKickCache = 4;

// The forecast for one hour at the venue.
struct WxHour {
  bool valid{false};
  int t10{0};  // tenths of a degree F
  uint8_t code{0};
  bool night{false};
};

// A moment as a calendar date and hour, in UTC (civil_from_days, so no
// gmtime_r is needed on any host).
struct WxCivil {
  long long year;
  unsigned month, day;
  int hour;
};
inline WxCivil wx_civil(int64_t epoch) {
  int64_t days = epoch >= 0 ? epoch / 86400 : (epoch - 86399) / 86400;
  WxCivil c{};
  c.hour = (int) ((epoch - days * 86400) / 3600);
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = (unsigned) (days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  c.day = doy - (153 * mp + 2) / 5 + 1;
  c.month = mp < 10 ? mp + 3 : mp - 9;
  c.year = (long long) yoe + era * 400 + (c.month <= 2);
  return c;
}

// One hour, the kickoff's, in UTC: "2026-11-02T01:00".
inline std::string wx_utc_hour(int64_t epoch) {
  const WxCivil c = wx_civil(epoch);
  char buf[48];
  snprintf(buf, sizeof(buf), "%04lld-%02u-%02uT%02d:00", c.year, c.month, c.day, c.hour);
  return buf;
}

inline std::string hour_url(float lat, float lon, int64_t epoch) {
  char buf[64];
  snprintf(buf, sizeof(buf), "latitude=%.4f&longitude=%.4f", (double) lat, (double) lon);
  const std::string h = wx_utc_hour(epoch);
  return std::string("https://api.open-meteo.com/v1/forecast?") + buf +
         "&hourly=temperature_2m,weather_code,is_day&temperature_unit=fahrenheit&timezone=GMT&start_hour=" + h +
         "&end_hour=" + h;
}

// Parses the hour's forecast. False with no reading for it (an hour past what
// Open-Meteo forecasts answers {"error":true}).
template<typename TInput> bool parse_hour(TInput &input, WxHour &out) {
  JsonDocument filter(detail::json_allocator());
  JsonObject h = filter["hourly"].to<JsonObject>();
  h["temperature_2m"] = true;
  h["weather_code"] = true;
  h["is_day"] = true;
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(10));
  if (err || !doc.is<JsonObjectConst>())
    return false;
  WxHour w;
  JsonObjectConst hourly = doc["hourly"];
  if (!detail::wx_degrees(hourly["temperature_2m"][0], w.t10))
    return false;
  w.code = detail::wx_code(hourly["weather_code"][0]);
  if (w.code == 255)
    return false;
  JsonVariantConst day = hourly["is_day"][0];
  w.night = day.is<int>() && day.as<int>() == 0;
  w.valid = true;
  out = w;
  return true;
}
bool parse_hour_from(WxSource &input, WxHour &out);

// Whether the next game gets a forecast of the panel's own: before the start
// with a real kickoff time, in a sport played outside (football, baseball,
// soccer), not indoors (indoor 1; -1 is a venue that doesn't say), with no
// forecast from ESPN, a city to look up, and a kickoff close enough for
// Open-Meteo, or under an hour gone.
inline bool wx_kick_wanted(bool pre, bool outdoor_sport, int8_t indoor, bool espn_has, const std::string &place,
                           int64_t kickoff, int64_t now) {
  if (!pre || !outdoor_sport || indoor == 1 || espn_has || place.empty() || kickoff <= 0 || now <= 0)
    return false;
  return kickoff > now - 3600 && kickoff - now <= kWxKickAheadS;
}

// The icon for a WMO weather code. Clear skies at night get the moon.
inline WxIcon wx_icon(uint8_t code, bool night) {
  if (code <= 1)
    return night ? WxIcon::MOON : WxIcon::SUN;
  if (code == 2)
    return WxIcon::PART;
  if (code == 3)
    return WxIcon::CLOUD;
  if (code == 45 || code == 48)
    return WxIcon::FOG;
  if (code >= 51 && code <= 67)
    return WxIcon::RAIN;
  if (code >= 71 && code <= 77)
    return WxIcon::SNOW;
  if (code >= 80 && code <= 82)
    return WxIcon::RAIN;
  if (code == 85 || code == 86)
    return WxIcon::SNOW;
  if (code >= 95 && code <= 99)
    return WxIcon::STORM;
  return WxIcon::CLOUD;
}

// The words under the temperature. At most 12 characters, so they fit a
// single panel in its smallest font; the wide board gets the longer form.
inline const char *wx_words(uint8_t code, bool wide) {
  switch (code) {
    case 0:
      return "Clear";
    case 1:
      return "Mostly clear";
    case 2:
      return wide ? "Partly cloudy" : "Some clouds";
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
      return wide ? "Freezing drizzle" : "Icy drizzle";
    case 61:
    case 63:
    case 65:
      return "Rain";
    case 66:
    case 67:
      return wide ? "Freezing rain" : "Icy rain";
    case 71:
    case 73:
    case 75:
    case 77:
      return "Snow";
    case 80:
    case 81:
    case 82:
      return "Showers";
    case 85:
    case 86:
      return "Snow showers";
    case 95:
    case 96:
    case 99:
      return "Storms";
    default:
      return "";
  }
}

// "71°" (the degree sign in UTF-8, which the panel's fonts have)
inline std::string wx_temp_text(int t) { return std::to_string(t) + "\xC2\xB0"; }

// "80/66" for a day's high and low
inline std::string wx_hilo_text(int hi, int lo) { return std::to_string(hi) + "/" + std::to_string(lo); }

// What the card says when it has no forecast to draw: with no town set, or a
// town the geocoder didn't know. "" when it has a forecast, or nothing to say.
inline std::string wx_card_text(bool on, const std::string &town, bool lookup_answered, bool found) {
  if (!on)
    return "";
  if (town.empty())
    return "Set a town on the device page";
  if (lookup_answered && !found)
    return "Town not found";
  return "";
}

// Whether the card has a turn: on, and either words to say (wx_card_text) or
// a forecast read within kWxFreshMs. read_ago_ms is millis() since the last
// good read, with 0 for none yet.
inline bool wx_card_ready(bool on, const std::string &town, bool lookup_answered, bool found, bool forecast,
                          uint32_t read_ago_ms, bool ever_read) {
  if (!on)
    return false;
  if (!wx_card_text(on, town, lookup_answered, found).empty())
    return true;
  return forecast && ever_read && read_ago_ms < kWxFreshMs;
}

// ---- Degrees F or C ---------------------------------------------------------

// The wxunit setting: auto (the town's country, else the time zone), F or C.
constexpr uint8_t kWxUnitAuto = 0, kWxUnitF = 1, kWxUnitC = 2;

// Places that read Fahrenheit: the US and its territories, and a few others.
inline bool wx_country_f(const std::string &cc) {
  static const char *kF[] = {"US", "PR", "GU", "VI", "AS", "MP", "UM", "LR", "BS", "BZ", "KY", "PW", "FM", "MH"};
  for (const char *c : kF)
    if (cc == c)
      return true;
  return false;
}

// The panel's US time zones (timezones.h): Fahrenheit when no town says
// otherwise.
inline bool wx_tz_f(const char *iana) {
  if (iana == nullptr)
    return true;
  static const char *kF[] = {"America/New_York", "America/Chicago",   "America/Denver",  "America/Phoenix",
                             "America/Los_Angeles", "America/Anchorage", "Pacific/Honolulu"};
  for (const char *z : kF)
    if (strcmp(iana, z) == 0)
      return true;
  return false;
}

// F or C, for the setting, the town's country ("" when the town isn't known
// or was looked up before the panel kept it) and the panel's time zone.
inline bool wx_fahrenheit(uint8_t unit, const std::string &country, const char *tz_iana) {
  if (unit == kWxUnitF)
    return true;
  if (unit == kWxUnitC)
    return false;
  return country.empty() ? wx_tz_f(tz_iana) : wx_country_f(country);
}

// Tenths of a degree F as whole degrees in the unit shown, rounded half away
// from zero.
inline int wx_deg(int t10, bool fahrenheit) {
  // C in tenths of tenths: (F - 32) * 5 / 9, times 100
  long v = fahrenheit ? (long) t10 * 10 : ((long) t10 - 320) * 500 / 9 / 10;
  return (int) (v < 0 ? (v - 50) / 100 : (v + 50) / 100);
}

// "54F", the ticker's way of writing it, as ESPN writes its forecast.
inline std::string wx_ticker_temp(int t10, bool fahrenheit) {
  return std::to_string(wx_deg(t10, fahrenheit)) + (fahrenheit ? "F" : "C");
}

// The ticker's forecast: "54F Clear", or the temperature alone with no words.
inline std::string wx_ticker_text(int t10, bool fahrenheit, const std::string &words) {
  std::string t = wx_ticker_temp(t10, fahrenheit);
  return words.empty() ? t : t + " " + words;
}

// The words of ESPN's forecast text ("54F Clear" is "Clear"), "" without any.
inline std::string espn_wx_words(const std::string &espn_text) {
  size_t sp = espn_text.find(' ');
  return sp == std::string::npos ? std::string() : espn_text.substr(sp + 1);
}

// ---- Icons for ESPN's forecast ------------------------------------------------

// ESPN's event forecast comes from AccuWeather, whose icon number rides in
// one of its two fields ("1" Sunny, "33" Clear at night). kWxIconNone for a
// number it doesn't have.
inline uint8_t accu_icon(int id) {
  switch (id) {
    case 1:
    case 2:
    case 5:
    case 30:
    case 31:
      return (uint8_t) WxIcon::SUN;
    case 3:
    case 4:
    case 35:
    case 36:
      return (uint8_t) WxIcon::PART;
    case 6:
    case 7:
    case 8:
    case 32:
    case 38:
      return (uint8_t) WxIcon::CLOUD;
    case 11:
      return (uint8_t) WxIcon::FOG;
    case 12:
    case 13:
    case 14:
    case 18:
    case 26:
    case 39:
    case 40:
      return (uint8_t) WxIcon::RAIN;
    case 15:
    case 16:
    case 17:
    case 41:
    case 42:
      return (uint8_t) WxIcon::STORM;
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 29:
    case 43:
    case 44:
      return (uint8_t) WxIcon::SNOW;
    case 33:
    case 34:
    case 37:
      return (uint8_t) WxIcon::MOON;
    default:
      return kWxIconNone;
  }
}

// The icon for words like "Mostly sunny" or "Chance of a t-storm", for a
// forecast that carries no number. kWxIconNone when nothing matches.
inline uint8_t words_icon(const std::string &words) {
  std::string w;
  for (char c : words)
    w += (char) (c >= 'A' && c <= 'Z' ? c + 32 : c);
  auto has = [&](const char *k) { return w.find(k) != std::string::npos; };
  if (has("storm") || has("thunder"))
    return (uint8_t) WxIcon::STORM;
  if (has("snow") || has("flurr") || has("sleet") || has(" ice") || w.rfind("ice", 0) == 0)
    return (uint8_t) WxIcon::SNOW;
  if (has("rain") || has("shower") || has("drizzle"))
    return (uint8_t) WxIcon::RAIN;
  if (has("fog") || has("mist"))
    return (uint8_t) WxIcon::FOG;
  if (has("partly") || has("intermittent"))
    return (uint8_t) WxIcon::PART;
  if (has("cloud") || has("overcast") || has("dreary"))
    return (uint8_t) WxIcon::CLOUD;
  if (has("sun") || has("clear") || has("fair") || has("haz") || has("hot"))
    return (uint8_t) WxIcon::SUN;
  return kWxIconNone;
}

// The icon of ESPN's forecast: from AccuWeather's number when the forecast
// has one, else from the words.
inline uint8_t espn_wx_icon(int accu, const std::string &words) {
  uint8_t i = accu_icon(accu);
  return i != kWxIconNone ? i : words_icon(words);
}

// A sun at a kickoff after dark is the moon, and the other way round: ESPN's
// number is for the hour it was read, not always the kickoff's.
inline uint8_t wx_icon_at(uint8_t icon, bool night) {
  if (night && icon == (uint8_t) WxIcon::SUN)
    return (uint8_t) WxIcon::MOON;
  if (!night && icon == (uint8_t) WxIcon::MOON)
    return (uint8_t) WxIcon::SUN;
  return icon;
}

// Where a US venue is, to the degree: about the middle of its state's
// stadiums, from the state in its place ("Green Bay, WI", "San Diego,
// California"). False for a place outside the US or one it can't tell.
inline bool wx_state_spot(const std::string &place, float &lat, float &lon) {
  struct St {
    const char *ab, *name;
    uint8_t north, west;
  };
  static const St kStates[] = {
      {"AL", "Alabama", 33, 87}, {"AK", "Alaska", 61, 150}, {"AZ", "Arizona", 33, 112},
      {"AR", "Arkansas", 36, 93}, {"CA", "California", 35, 119}, {"CO", "Colorado", 40, 105},
      {"CT", "Connecticut", 42, 73}, {"DE", "Delaware", 39, 75}, {"DC", "District of Columbia", 39, 77},
      {"FL", "Florida", 28, 82}, {"GA", "Georgia", 33, 84}, {"HI", "Hawaii", 21, 158}, {"ID", "Idaho", 44, 116},
      {"IL", "Illinois", 41, 88}, {"IN", "Indiana", 40, 86}, {"IA", "Iowa", 42, 93}, {"KS", "Kansas", 39, 96},
      {"KY", "Kentucky", 38, 85}, {"LA", "Louisiana", 30, 91}, {"ME", "Maine", 44, 70},
      {"MD", "Maryland", 39, 77}, {"MA", "Massachusetts", 42, 71}, {"MI", "Michigan", 42, 84},
      {"MN", "Minnesota", 45, 93}, {"MS", "Mississippi", 33, 89}, {"MO", "Missouri", 39, 92},
      {"MT", "Montana", 46, 112}, {"NE", "Nebraska", 41, 97}, {"NV", "Nevada", 37, 116},
      {"NH", "New Hampshire", 43, 71}, {"NJ", "New Jersey", 41, 74}, {"NM", "New Mexico", 35, 107},
      {"NY", "New York", 42, 75}, {"NC", "North Carolina", 36, 80}, {"ND", "North Dakota", 47, 97},
      {"OH", "Ohio", 40, 83}, {"OK", "Oklahoma", 36, 97}, {"OR", "Oregon", 45, 123},
      {"PA", "Pennsylvania", 40, 77}, {"RI", "Rhode Island", 42, 71}, {"SC", "South Carolina", 34, 81},
      {"SD", "South Dakota", 44, 97}, {"TN", "Tennessee", 36, 86}, {"TX", "Texas", 31, 97},
      {"UT", "Utah", 41, 112}, {"VT", "Vermont", 44, 73}, {"VA", "Virginia", 38, 78},
      {"WA", "Washington", 47, 122}, {"WV", "West Virginia", 39, 81}, {"WI", "Wisconsin", 44, 89},
      {"WY", "Wyoming", 41, 106},
  };
  size_t comma = place.rfind(", ");
  if (comma == std::string::npos)
    return false;
  const std::string st = place.substr(comma + 2);
  for (const auto &s : kStates) {
    if (st == s.ab || st == s.name) {
      lat = s.north;
      lon = -(float) s.west;
      return true;
    }
  }
  return false;
}

// Whether the sun has set at a spot (its upper edge is under the horizon), as
// Open-Meteo's is_day has it. The Astronomical Almanac's low precision sun,
// with the equation of time for the hour angle.
inline bool wx_sun_down(int64_t t, float lat, float lon) {
  constexpr float kRad = 3.14159265f / 180.0f;
  const float d = (float) (t - 946728000) / 86400.0f;  // days since noon UTC on 2000-01-01
  const float g = (357.529f + 0.98560028f * d) * kRad;
  const float l = (280.459f + 0.98564736f * d + 1.915f * sinf(g) + 0.020f * sinf(2 * g)) * kRad;
  const float eot = -1.915f * sinf(g) - 0.020f * sinf(2 * g) + 2.466f * sinf(2 * l) - 0.053f * sinf(4 * l);
  const float sin_dec = 0.39777f * sinf(l);
  const float cos_dec = sqrtf(1 - sin_dec * sin_dec);
  const float hour = ((float) (t % 86400) / 240.0f - 180.0f + lon + eot) * kRad;
  const float sin_up = sinf(lat * kRad) * sin_dec + cosf(lat * kRad) * cos_dec * cosf(hour);
  return sin_up < -0.01454f;  // 0.833 degrees under
}

// Whether the sun is down at a kickoff at a US venue: 1 down, 0 up, -1 when
// the venue's state isn't known.
inline int wx_venue_dark(int64_t kickoff, const std::string &place) {
  float lat, lon;
  if (kickoff <= 0 || !wx_state_spot(place, lat, lon))
    return -1;
  return wx_sun_down(kickoff, lat, lon) ? 1 : 0;
}

// ---- When a weather read may start --------------------------------------------

// The ms until a read last made at last_ms, every interval_ms, is due: 0 when
// it is due now or was never made.
inline uint32_t wx_due_in_ms(uint32_t last_ms, uint32_t interval_ms, uint32_t now) {
  if (last_ms == 0)
    return 0;
  uint32_t age = now - last_ms;
  return age >= interval_ms ? 0 : interval_ms - age;
}

// A favorite's next schedule read (Favorite teams mode re-reads one every
// schedule_ms, or after retry_ms while it has none): the ms until it's due.
inline uint32_t wx_fav_read_in_ms(bool missing, uint32_t fetched_ms, uint32_t now, uint32_t retry_ms,
                                  uint32_t schedule_ms) {
  if (missing && fetched_ms == 0)
    return 0;
  return wx_due_in_ms(fetched_ms, missing ? retry_ms : schedule_ms, now);
}

// A weather read starts only when the next ESPN read is this far off or more.
inline bool wx_gap_ok(uint32_t espn_quiet_ms) { return espn_quiet_ms >= kWxEspnGapMs; }

}  // namespace espn
