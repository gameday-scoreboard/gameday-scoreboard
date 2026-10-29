// The local weather card: the owner's town, looked up once with Open-Meteo's
// geocoding API (no key), and its forecast read every half hour from
// Open-Meteo's forecast API, shown between games as a card of its own: the
// temperature now, today's high and low, an icon for the sky, and the next
// two days small underneath. Fahrenheit only. Pure, with no ESPHome includes,
// so the host tests compile it directly.
#pragma once

#include <ArduinoJson.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "espn_parse.h"
#include "message.h"

namespace espn {

// The town as typed and the place found, in bytes: one line each, cleaned
// like the nickname.
constexpr size_t kTownMax = 40;
constexpr size_t kPlaceMax = 40;
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
};

enum class WxIcon : uint8_t { CLOUD = 0, SUN, MOON, PART, FOG, RAIN, SNOW, STORM };
constexpr int kWxIconCount = 8;

struct WxDay {
  int hi{0}, lo{0};
  uint8_t code{0};
  uint8_t wday{0};  // 0 Sunday
};

struct WxForecast {
  bool valid{false};
  int temp{0};  // now
  bool night{false};
  uint8_t code{0};  // WMO weather code now
  WxDay today;      // the high and low, today's code
  WxDay next[2];    // tomorrow and the day after
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
         "&current=temperature_2m,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min"
         "&temperature_unit=fahrenheit&timezone=" +
         url_encode(tz_iana) + "&forecast_days=3";
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
}

// A temperature that could be real, rounded to a whole degree. False for a
// missing or absurd one.
inline bool wx_degrees(JsonVariantConst v, int &out) {
  if (!v.is<float>())
    return false;
  float f = v.as<float>();
  if (!(f > -150.0f && f < 200.0f))  // NaN fails too
    return false;
  out = (int) (f < 0 ? f - 0.5f : f + 0.5f);
  return true;
}

inline uint8_t wx_code(JsonVariantConst v) {
  int c = v | -1;
  return c >= 0 && c <= 99 ? (uint8_t) c : 255;
}

}  // namespace detail

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
  if (!detail::wx_degrees(cur["temperature_2m"], f.temp))
    return false;
  f.code = detail::wx_code(cur["weather_code"]);
  f.night = cur["is_day"].is<int>() && cur["is_day"].as<int>() == 0;
  JsonObjectConst daily = doc["daily"];
  JsonArrayConst hi = daily["temperature_2m_max"], lo = daily["temperature_2m_min"], code = daily["weather_code"],
                 time = daily["time"];
  if (hi.size() < 3 || lo.size() < 3)
    return false;
  for (size_t i = 0; i < 3; i++) {
    WxDay &d = i == 0 ? f.today : f.next[i - 1];
    if (!detail::wx_degrees(hi[i], d.hi) || !detail::wx_degrees(lo[i], d.lo))
      return false;
    d.code = detail::wx_code(code[i]);
    d.wday = weekday_of(time[i].as<const char *>());
  }
  f.valid = true;
  out = f;
  return true;
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

}  // namespace espn
