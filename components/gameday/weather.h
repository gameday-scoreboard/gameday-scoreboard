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

struct WxForecast {
  bool valid{false};
  int t10{0};  // now
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
  for (size_t i = 0; i < 3; i++) {
    WxDay &d = i == 0 ? f.today : f.next[i - 1];
    if (!detail::wx_degrees(hi[i], d.hi10) || !detail::wx_degrees(lo[i], d.lo10))
      return false;
    d.code = detail::wx_code(code[i]);
    d.wday = weekday_of(time[i].as<const char *>());
  }
  f.valid = true;
  out = f;
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

// Dark at this local hour, roughly: after 8 PM from May to August, after 7 PM
// the rest of the year, and before 6 AM. month 1 to 12.
inline bool wx_dark_at(int month, int hour) {
  if (hour < 6)
    return true;
  return hour >= (month >= 5 && month <= 8 ? 20 : 19);
}

// A US venue's standard UTC offset in hours, from the state in its place
// ("Green Bay, WI", "San Diego, California"), and whether it keeps daylight
// time. 0 for a place outside the US or one it can't tell: no swap then. A
// state split between two zones gets the one most of its stadiums are in.
inline int wx_state_offset(const std::string &place, bool &dst) {
  struct St {
    const char *ab, *name;
    int8_t off;
  };
  static const St kStates[] = {
      {"AL", "Alabama", -6},      {"AK", "Alaska", -9},         {"AZ", "Arizona", -7},
      {"AR", "Arkansas", -6},     {"CA", "California", -8},     {"CO", "Colorado", -7},
      {"CT", "Connecticut", -5},  {"DE", "Delaware", -5},       {"DC", "District of Columbia", -5},
      {"FL", "Florida", -5},      {"GA", "Georgia", -5},        {"HI", "Hawaii", -10},
      {"ID", "Idaho", -7},        {"IL", "Illinois", -6},       {"IN", "Indiana", -5},
      {"IA", "Iowa", -6},         {"KS", "Kansas", -6},         {"KY", "Kentucky", -5},
      {"LA", "Louisiana", -6},    {"ME", "Maine", -5},          {"MD", "Maryland", -5},
      {"MA", "Massachusetts", -5}, {"MI", "Michigan", -5},      {"MN", "Minnesota", -6},
      {"MS", "Mississippi", -6},  {"MO", "Missouri", -6},       {"MT", "Montana", -7},
      {"NE", "Nebraska", -6},     {"NV", "Nevada", -8},         {"NH", "New Hampshire", -5},
      {"NJ", "New Jersey", -5},   {"NM", "New Mexico", -7},     {"NY", "New York", -5},
      {"NC", "North Carolina", -5}, {"ND", "North Dakota", -6}, {"OH", "Ohio", -5},
      {"OK", "Oklahoma", -6},     {"OR", "Oregon", -8},         {"PA", "Pennsylvania", -5},
      {"RI", "Rhode Island", -5}, {"SC", "South Carolina", -5}, {"SD", "South Dakota", -6},
      {"TN", "Tennessee", -6},    {"TX", "Texas", -6},          {"UT", "Utah", -7},
      {"VT", "Vermont", -5},      {"VA", "Virginia", -5},       {"WA", "Washington", -8},
      {"WV", "West Virginia", -5}, {"WI", "Wisconsin", -6},     {"WY", "Wyoming", -7},
  };
  dst = true;
  size_t comma = place.rfind(", ");
  if (comma == std::string::npos)
    return 0;
  const std::string st = place.substr(comma + 2);
  for (const auto &s : kStates) {
    if (st == s.ab || st == s.name) {
      dst = strcmp(s.ab, "AZ") != 0 && strcmp(s.ab, "HI") != 0;
      return s.off;
    }
  }
  return 0;
}

// Whether a kickoff is after dark at a US venue, by the venue's own clock:
// 1 dark, 0 light, -1 when the venue's time zone isn't known. Daylight time
// from the second Sunday in March to the first in November, to within a week.
inline int wx_venue_dark(int64_t kickoff, const std::string &place) {
  bool dst;
  int off = wx_state_offset(place, dst);
  if (off == 0 || kickoff <= 0)
    return -1;
  WxCivil c = wx_civil(kickoff + (int64_t) off * 3600);
  if (dst && ((c.month > 3 && c.month < 11) || (c.month == 3 && c.day >= 8) || (c.month == 11 && c.day < 7)))
    c = wx_civil(kickoff + (int64_t) (off + 1) * 3600);
  return wx_dark_at((int) c.month, c.hour) ? 1 : 0;
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
