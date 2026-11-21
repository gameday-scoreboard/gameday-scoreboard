// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The Flights page: the planes near the owner's home, read from adsb.lol's
// point API or from the owner's own ADS-B receiver (readsb, tar1090 or
// dump1090, which share adsb.lol's field names), and a plane's route from
// adsbdb. Home is the weather town's latitude and longitude. This file holds
// the parsers, the geometry and the text the page draws. Plane and Sky are
// fixed-size so a read allocates nothing outside its JSON document.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <ArduinoJson.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "espn_parse.h"
#include "weather.h"

namespace espn {

// The page keeps this many planes from a read, the nearest.
constexpr size_t kSkyMax = 16;
// A position older than this is dropped.
constexpr float kSeenMaxS = 60.0f;
constexpr int32_t kAltUnknown = INT32_MIN;
// adsb.lol's point API takes at most this radius.
constexpr int kPointMaxNm = 100;
// A route is shown when flying it past the plane adds at most 15% and
// 150 km to the trip.
constexpr double kRouteFitFactor = 1.15;
constexpr double kRouteFitSlackKm = 150.0;
// "Overhead in 40 s": the plane passes within 2 km in the next 3 minutes.
constexpr float kOverheadKm = 2.0f;
constexpr float kOverheadS = 180.0f;
// The owner's window takes in 60 degrees either side of where it faces.
constexpr float kFacingHalfDeg = 60.0f;
// The card keeps its plane until another is nearer by more than 25% of its
// distance, and by at least 1 km.
constexpr float kKeepRatio = 0.25f;
constexpr float kKeepKm = 1.0f;
// Climbing or descending at this rate or more gets an arrow.
constexpr int16_t kVrateLevelFpm = 250;
constexpr double kEarthKm = 6371.0088;
constexpr double kKmPerNm = 1.852;
constexpr double kKmPerMi = 1.609344;

// The flrad setting's steps, the default second.
constexpr uint8_t kFlRadiusSteps = 5;
constexpr uint8_t kFlRadiusDefault = 1;
constexpr uint8_t kFlRadiusMi[kFlRadiusSteps] = {5, 10, 15, 25, 50};
constexpr uint8_t kFlRadiusKm[kFlRadiusSteps] = {10, 15, 25, 40, 80};

struct Plane {
  char hex[8]{};   // ICAO address, lowercase; a leading "~" marks one the receiver made up
  char call[9]{};  // "AAL2192", trailing spaces trimmed; "" when it sent none
  char reg[9]{};   // "N7605P"
  char type[5]{};  // ICAO type code, "A321"
  int32_t alt_ft{kAltUnknown};
  bool ground{false};
  bool emergency{false};
  uint8_t military{0};
  int16_t vrate_fpm{0};
  float gs_kt{NAN};
  float track{NAN};  // degrees true
  double lat{0}, lon{0};
  float dist_km{0};  // from home
  float brg_deg{0};  // from home to the plane
};

// One read: the nearest kSkyMax planes, nearest first. Planes on the ground
// stay only when nothing is in the air.
struct Sky {
  Plane p[kSkyMax];
  uint8_t n{0};
  uint16_t heard{0};     // every plane within the radius, ground included
  uint16_t airborne{0};  // the ones in the air
};

// A route from adsbdb. Airports are IATA codes, ICAO when there is none.
struct Route {
  bool found{false};  // false: adsbdb doesn't know the callsign
  char call[9]{};
  char from[5]{}, to[5]{};
  float from_lat{NAN}, from_lon{NAN}, to_lat{NAN}, to_lon{NAN};
};

// When the plane passes closest to home, and how close.
struct Cpa {
  float secs{NAN};  // negative once it has passed; NaN when it isn't moving
  float km{0};
};

// ---- Geometry ----------------------------------------------------------------

constexpr double kFlPi = 3.14159265358979323846;

inline double fl_rad(double deg) { return deg * kFlPi / 180.0; }
inline double fl_deg(double rad) { return rad * 180.0 / kFlPi; }

// Great-circle distance.
inline double haversine_km(double lat1, double lon1, double lat2, double lon2) {
  double dp = fl_rad(lat2 - lat1), dl = fl_rad(lon2 - lon1);
  double h = sin(dp / 2) * sin(dp / 2) + cos(fl_rad(lat1)) * cos(fl_rad(lat2)) * sin(dl / 2) * sin(dl / 2);
  return 2 * kEarthKm * asin(sqrt(h < 1 ? h : 1));
}

// Initial bearing from the first point to the second, 0 to 360.
inline double bearing_deg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = fl_rad(lat1), p2 = fl_rad(lat2), dl = fl_rad(lon2 - lon1);
  double b = fl_deg(atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)));
  return b < 0 ? b + 360.0 : b;
}

inline const char *compass8(float deg) {
  static const char *const kDirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  if (!std::isfinite(deg))
    return "";
  float d = fmodf(deg, 360.0f);
  if (d < 0)
    d += 360.0f;
  return kDirs[(int) ((d + 22.5f) / 45.0f) % 8];
}

// Whether a bearing is in view of a window facing `facing`: 0 any way, 1 to 8
// for N, NE, E, SE, S, SW, W, NW.
inline bool facing_ok(float brg_deg, uint8_t facing) {
  if (facing == 0 || facing > 8)
    return true;
  float diff = fabsf(fmodf(brg_deg - (facing - 1) * 45.0f + 540.0f, 360.0f) - 180.0f);
  return diff <= kFacingHalfDeg;
}

inline void plane_locate(Plane &p, double home_lat, double home_lon) {
  p.dist_km = (float) haversine_km(home_lat, home_lon, p.lat, p.lon);
  p.brg_deg = (float) bearing_deg(home_lat, home_lon, p.lat, p.lon);
}

inline bool plane_moving(const Plane &p) { return !p.ground && std::isfinite(p.track) && std::isfinite(p.gs_kt); }

// Dead reckoning: moves the plane along its track at its ground speed for
// secs, and works out its distance and bearing from home again. Advance a
// copy of the read by the time since it, so rounding doesn't add up.
inline void plane_advance(Plane &p, float secs, double home_lat, double home_lon) {
  if (plane_moving(p) && secs != 0) {
    double d = p.gs_kt * kKmPerNm * secs / 3600.0 / kEarthKm;
    double t = fl_rad(p.track), p1 = fl_rad(p.lat), l1 = fl_rad(p.lon);
    double p2 = asin(sin(p1) * cos(d) + cos(p1) * sin(d) * cos(t));
    double l2 = l1 + atan2(sin(t) * sin(d) * cos(p1), cos(d) - sin(p1) * sin(p2));
    p.lat = fl_deg(p2);
    p.lon = fmod(fl_deg(l2) + 540.0, 360.0) - 180.0;
  }
  plane_locate(p, home_lat, home_lon);
}

// Closest approach on a straight line at the current speed, on a flat map
// around home (the page reads at most 185 km out).
inline Cpa plane_cpa(const Plane &p) {
  Cpa c;
  c.km = p.dist_km;
  if (!plane_moving(p) || p.gs_kt < 1.0f)
    return c;
  double b = fl_rad(p.brg_deg), t = fl_rad(p.track);
  double x = p.dist_km * sin(b), y = p.dist_km * cos(b);  // km east and north of home
  double v = p.gs_kt * kKmPerNm / 3600.0;                  // km a second
  double vx = v * sin(t), vy = v * cos(t);
  double secs = -(x * vx + y * vy) / (v * v);
  c.secs = (float) secs;
  if (secs > 0)
    c.km = (float) hypot(x + vx * secs, y + vy * secs);
  return c;
}

inline bool cpa_overhead(const Cpa &c) { return c.km < kOverheadKm && c.secs > 0 && c.secs < kOverheadS; }

// ---- Which plane gets the card -----------------------------------------------

// The index of the plane for the card, -1 for none. With a facing set, only
// planes in view of the window count, unless none is. The current plane
// (current_hex, "" or nullptr for none) stays while it is in the read and in
// view, until another is nearer by the keep margin.
inline int pick_card(const Sky &sky, const char *current_hex, uint8_t facing) {
  bool any_in_view = false;
  for (uint8_t i = 0; i < sky.n && !any_in_view; i++)
    any_in_view = facing_ok(sky.p[i].brg_deg, facing);
  auto eligible = [&](uint8_t i) { return !any_in_view || facing_ok(sky.p[i].brg_deg, facing); };
  int best = -1;
  for (uint8_t i = 0; i < sky.n; i++)
    if (eligible(i) && (best < 0 || sky.p[i].dist_km < sky.p[best].dist_km))
      best = i;
  if (best < 0 || current_hex == nullptr || current_hex[0] == '\0')
    return best;
  for (uint8_t i = 0; i < sky.n; i++) {
    if (strcmp(sky.p[i].hex, current_hex) != 0)
      continue;
    if (!eligible(i))
      return best;
    float cur = sky.p[i].dist_km;
    float margin = fmaxf(cur * kKeepRatio, kKeepKm);
    return sky.p[best].dist_km < cur - margin ? best : i;
  }
  return best;
}

// ---- Names -------------------------------------------------------------------

inline bool plane_emergency(const char *squawk, const char *emergency) {
  if (squawk != nullptr && (strcmp(squawk, "7500") == 0 || strcmp(squawk, "7600") == 0 || strcmp(squawk, "7700") == 0))
    return true;
  return emergency != nullptr && emergency[0] != '\0' && strcmp(emergency, "none") != 0;
}

// What the page calls the plane: its callsign, else its registration, else
// its address.
inline const char *plane_label(const Plane &p) { return p.call[0] ? p.call : p.reg[0] ? p.reg : p.hex; }

inline bool fl_upper(char c) { return c >= 'A' && c <= 'Z'; }
inline bool fl_digit(char c) { return c >= '0' && c <= '9'; }

// An airline flight's callsign: three letters and a flight number, "AAL2192".
// Only these get a route lookup.
inline bool airline_call(const char *call) {
  return call != nullptr && fl_upper(call[0]) && fl_upper(call[1]) && fl_upper(call[2]) && fl_digit(call[3]);
}

struct AirlineName {
  char icao[4];
  const char *name;
};

// Short names for the airlines seen most in the US, Canada and Europe, by
// ICAO code. At most 14 characters.
constexpr AirlineName kAirlines[] = {
    // US
    {"AAL", "American"},
    {"UAL", "United"},
    {"DAL", "Delta"},
    {"SWA", "Southwest"},
    {"ASA", "Alaska"},
    {"JBU", "JetBlue"},
    {"NKS", "Spirit"},
    {"FFT", "Frontier"},
    {"AAY", "Allegiant"},
    {"HAL", "Hawaiian"},
    {"SCX", "Sun Country"},
    {"VXP", "Avelo"},
    {"MXY", "Breeze"},
    // US regionals
    {"ENY", "Envoy"},
    {"SKW", "SkyWest"},
    {"RPA", "Republic"},
    {"ASH", "Mesa"},
    {"EDV", "Endeavor"},
    {"JIA", "PSA"},
    {"PDT", "Piedmont"},
    {"AWI", "Air Wisconsin"},
    {"QXE", "Horizon"},
    {"GJS", "GoJet"},
    {"CPZ", "Compass"},
    {"UCA", "CommutAir"},
    // Cargo
    {"FDX", "FedEx"},
    {"UPS", "UPS"},
    {"GTI", "Atlas"},
    {"ABX", "ABX"},
    {"ATN", "ATI"},
    {"PAC", "Polar"},
    {"CKS", "Kalitta"},
    {"AMF", "Ameriflight"},
    // Private jets
    {"EJA", "NetJets"},
    {"LXJ", "Flexjet"},
    {"WUP", "Wheels Up"},
    // Canada and Mexico
    {"ACA", "Air Canada"},
    {"ROU", "Rouge"},
    {"JZA", "Jazz"},
    {"WJA", "WestJet"},
    {"TSC", "Air Transat"},
    {"POE", "Porter"},
    {"SWG", "Sunwing"},
    {"AMX", "Aeromexico"},
    {"VOI", "Volaris"},
    {"VIV", "Viva"},
    // Europe
    {"BAW", "British"},
    {"VIR", "Virgin"},
    {"EZY", "easyJet"},
    {"EJU", "easyJet"},
    {"RYR", "Ryanair"},
    {"DLH", "Lufthansa"},
    {"AFR", "Air France"},
    {"KLM", "KLM"},
    {"IBE", "Iberia"},
    {"ITY", "ITA"},
    {"SAS", "SAS"},
    {"SWR", "Swiss"},
    {"AUA", "Austrian"},
    {"EIN", "Aer Lingus"},
    {"TAP", "TAP"},
    {"FIN", "Finnair"},
    {"ICE", "Icelandair"},
    {"THY", "Turkish"},
    {"EWG", "Eurowings"},
    {"VLG", "Vueling"},
    {"WZZ", "Wizz"},
    {"BEL", "Brussels"},
    {"LOT", "LOT"},
    {"ELY", "El Al"},
    // Middle East, Asia and the Pacific
    {"UAE", "Emirates"},
    {"QTR", "Qatar"},
    {"ETD", "Etihad"},
    {"JAL", "JAL"},
    {"ANA", "ANA"},
    {"KAL", "Korean Air"},
    {"CPA", "Cathay"},
    {"SIA", "Singapore"},
    {"QFA", "Qantas"},
    {"ANZ", "Air NZ"},
    {"CCA", "Air China"},
    {"CES", "China Eastern"},
    {"CSN", "China Southern"},
    {"EVA", "EVA Air"},
    {"CAL", "China Airlines"},
    // Latin America
    {"AVA", "Avianca"},
    {"CMP", "Copa"},
    {"LAN", "LATAM"},
    {"TAM", "LATAM"},
};

// The airline of a callsign ("AAL2192") or code ("AAL"); nullptr when it
// isn't one the table knows.
inline const char *airline_short(const char *call) {
  if (call == nullptr || !fl_upper(call[0]) || !fl_upper(call[1]) || !fl_upper(call[2]))
    return nullptr;
  if (call[3] != '\0' && !fl_digit(call[3]))
    return nullptr;
  for (const auto &a : kAirlines)
    if (memcmp(a.icao, call, 3) == 0)
      return a.name;
  return nullptr;
}

struct TypeName {
  char code[5];
  const char *narrow;  // at most 9 characters, for a single panel
  const char *wide;    // at most 20, for two panels
};

constexpr TypeName kTypes[] = {
    // Airbus
    {"A318", "A318", "Airbus A318"},
    {"A319", "A319", "Airbus A319"},
    {"A320", "A320", "Airbus A320"},
    {"A321", "A321", "Airbus A321"},
    {"A19N", "A319neo", "Airbus A319neo"},
    {"A20N", "A320neo", "Airbus A320neo"},
    {"A21N", "A321neo", "Airbus A321neo"},
    {"BCS1", "A220-100", "Airbus A220-100"},
    {"BCS3", "A220-300", "Airbus A220-300"},
    {"A332", "A330-200", "Airbus A330-200"},
    {"A333", "A330-300", "Airbus A330-300"},
    {"A339", "A330-900", "Airbus A330-900"},
    {"A343", "A340-300", "Airbus A340-300"},
    {"A359", "A350-900", "Airbus A350-900"},
    {"A35K", "A350-1000", "Airbus A350-1000"},
    {"A388", "A380", "Airbus A380"},
    // Boeing
    {"B712", "717", "Boeing 717"},
    {"B733", "737-300", "Boeing 737-300"},
    {"B735", "737-500", "Boeing 737-500"},
    {"B737", "737-700", "Boeing 737-700"},
    {"B738", "737-800", "Boeing 737-800"},
    {"B739", "737-900", "Boeing 737-900"},
    {"B37M", "737 MAX 7", "Boeing 737 MAX 7"},
    {"B38M", "737 MAX 8", "Boeing 737 MAX 8"},
    {"B39M", "737 MAX 9", "Boeing 737 MAX 9"},
    {"B3XM", "737 MAX10", "Boeing 737 MAX 10"},
    {"B752", "757-200", "Boeing 757-200"},
    {"B753", "757-300", "Boeing 757-300"},
    {"B762", "767-200", "Boeing 767-200"},
    {"B763", "767-300", "Boeing 767-300"},
    {"B764", "767-400", "Boeing 767-400"},
    {"B772", "777-200", "Boeing 777-200"},
    {"B77L", "777-200LR", "Boeing 777-200LR"},
    {"B773", "777-300", "Boeing 777-300"},
    {"B77W", "777-300ER", "Boeing 777-300ER"},
    {"B788", "787-8", "Boeing 787-8"},
    {"B789", "787-9", "Boeing 787-9"},
    {"B78X", "787-10", "Boeing 787-10"},
    {"B744", "747-400", "Boeing 747-400"},
    {"B748", "747-8", "Boeing 747-8"},
    {"MD11", "MD-11", "MD-11"},
    // Regional jets and turboprops
    {"E135", "ERJ-135", "Embraer ERJ-135"},
    {"E145", "ERJ-145", "Embraer ERJ-145"},
    {"E45X", "ERJ-145XR", "Embraer ERJ-145XR"},
    {"E170", "E170", "Embraer 170"},
    {"E75L", "E175", "Embraer 175"},
    {"E75S", "E175", "Embraer 175"},
    {"E190", "E190", "Embraer 190"},
    {"E195", "E195", "Embraer 195"},
    {"E290", "E190-E2", "Embraer 190-E2"},
    {"E295", "E195-E2", "Embraer 195-E2"},
    {"CRJ2", "CRJ-200", "Bombardier CRJ-200"},
    {"CRJ7", "CRJ-700", "Bombardier CRJ-700"},
    {"CRJ9", "CRJ-900", "Bombardier CRJ-900"},
    {"CRJX", "CRJ-1000", "Bombardier CRJ-1000"},
    {"DH8A", "Dash 8", "De Havilland Dash 8"},
    {"DH8D", "Q400", "De Havilland Q400"},
    {"AT72", "ATR 72", "ATR 72"},
    {"AT76", "ATR 72", "ATR 72-600"},
    {"AT45", "ATR 42", "ATR 42"},
    // Business jets
    {"E50P", "Phenom100", "Embraer Phenom 100"},
    {"E55P", "Phenom300", "Embraer Phenom 300"},
    {"CL30", "Challengr", "Challenger 300"},
    {"CL35", "Challengr", "Challenger 350"},
    {"CL60", "Challengr", "Challenger 600"},
    {"GLEX", "Global", "Bombardier Global"},
    {"GL7T", "Global", "Global 7500"},
    {"GLF4", "G-IV", "Gulfstream G-IV"},
    {"GLF5", "G-V", "Gulfstream G-V"},
    {"GLF6", "G650", "Gulfstream G650"},
    {"G280", "G280", "Gulfstream G280"},
    {"C25A", "CJ2", "Citation CJ2"},
    {"C25B", "CJ3", "Citation CJ3"},
    {"C25C", "CJ4", "Citation CJ4"},
    {"C56X", "Excel", "Citation Excel"},
    {"C68A", "Latitude", "Citation Latitude"},
    {"C700", "Longitude", "Citation Longitude"},
    {"C750", "Citation", "Citation X"},
    {"HDJT", "HondaJet", "HondaJet"},
    {"SF50", "Vision", "Cirrus Vision Jet"},
    {"PC12", "PC-12", "Pilatus PC-12"},
    {"PC24", "PC-24", "Pilatus PC-24"},
    // Small planes
    {"C152", "C152", "Cessna 152"},
    {"C172", "C172", "Cessna 172 Skyhawk"},
    {"C182", "C182", "Cessna 182 Skylane"},
    {"C208", "Caravan", "Cessna Caravan"},
    {"P28A", "PA-28", "Piper Cherokee"},
    {"PA24", "PA-24", "Piper Comanche"},
    {"PA32", "PA-32", "Piper Saratoga"},
    {"PA44", "PA-44", "Piper Seminole"},
    {"SR20", "SR20", "Cirrus SR20"},
    {"SR22", "SR22", "Cirrus SR22"},
    {"BE36", "Bonanza", "Beech Bonanza"},
    {"BE58", "Baron", "Beech Baron"},
    {"BE9L", "King Air", "King Air 90"},
    {"BE20", "King Air", "King Air 200"},
    {"B350", "King Air", "King Air 350"},
    // Helicopters
    {"B06", "Bell 206", "Bell 206 JetRanger"},
    {"B407", "Bell 407", "Bell 407"},
    {"AS50", "AStar", "Airbus AStar"},
    {"EC30", "H130", "Airbus H130"},
    {"EC35", "H135", "Airbus H135"},
    {"EC45", "H145", "Airbus H145"},
    {"R44", "R44", "Robinson R44"},
    {"S76", "S-76", "Sikorsky S-76"},
    // Military
    {"C17", "C-17", "C-17 Globemaster"},
    {"C130", "C-130", "C-130 Hercules"},
    {"C30J", "C-130J", "C-130J Hercules"},
    {"K35R", "KC-135", "KC-135 Stratotanker"},
};

// The type's name, narrow or wide; the code itself when the table doesn't
// have it.
inline const char *plane_type_name(const char *code, bool wide) {
  if (code == nullptr)
    return "";
  for (const auto &t : kTypes)
    if (strcmp(t.code, code) == 0)
      return wide ? t.wide : t.narrow;
  return code;
}

// ---- Text --------------------------------------------------------------------

// v with a comma every three digits, "12,400"
inline void fl_grouped(char *out, size_t n, long v) {
  char digits[24];
  snprintf(digits, sizeof(digits), "%ld", v < 0 ? -v : v);
  size_t len = strlen(digits), at = 0;
  if (v < 0 && at + 1 < n)
    out[at++] = '-';
  for (size_t i = 0; i < len && at + 1 < n; i++) {
    out[at++] = digits[i];
    size_t left = len - i - 1;
    if (left > 0 && left % 3 == 0 && at + 1 < n)
      out[at++] = ',';
  }
  if (n > 0)
    out[at < n ? at : n - 1] = '\0';
}

// "12,400 ft" or "3,780 m"; "Ground" on the ground, "" when unknown.
inline const char *alt_text(char *out, size_t n, const Plane &p, bool metric) {
  if (n == 0)
    return out;
  out[0] = '\0';
  if (p.ground) {
    snprintf(out, n, "Ground");
    return out;
  }
  if (p.alt_ft == kAltUnknown)
    return out;
  char num[24];
  fl_grouped(num, sizeof(num), metric ? lround(p.alt_ft * 0.3048) : (long) p.alt_ft);
  snprintf(out, n, "%s %s", num, metric ? "m" : "ft");
  return out;
}

// Ground speed, "310 mph" or "574 km/h"; "" when unknown.
inline const char *speed_text(char *out, size_t n, float gs_kt, bool metric) {
  if (n == 0)
    return out;
  out[0] = '\0';
  if (!std::isfinite(gs_kt))
    return out;
  snprintf(out, n, "%ld %s", lround(gs_kt * (metric ? kKmPerNm : kKmPerNm / kKmPerMi)), metric ? "km/h" : "mph");
  return out;
}

// "2.4 mi" or "3.9 km", whole numbers from 10 up.
inline const char *dist_text(char *out, size_t n, float km, bool metric) {
  if (n == 0)
    return out;
  double v = metric ? km : km / kKmPerMi;
  if (v < 9.95)
    snprintf(out, n, "%.1f %s", v, metric ? "km" : "mi");
  else
    snprintf(out, n, "%ld %s", lround(v), metric ? "km" : "mi");
  return out;
}

// "2.4 mi SW", where the plane is from home.
inline const char *where_text(char *out, size_t n, const Plane &p, bool metric) {
  char d[16];
  dist_text(d, sizeof(d), p.dist_km, metric);
  snprintf(out, n, "%s %s", d, compass8(p.brg_deg));
  return out;
}

// 1 climbing, -1 descending, 0 level.
inline int vrate_dir(int16_t fpm) { return fpm >= kVrateLevelFpm ? 1 : fpm <= -kVrateLevelFpm ? -1 : 0; }

// ---- URLs --------------------------------------------------------------------

// The flrad step as a radius in km; miles or km steps by the owner's unit.
inline float fl_radius_km(uint8_t step, bool metric) {
  if (step >= kFlRadiusSteps)
    step = kFlRadiusDefault;
  return metric ? (float) kFlRadiusKm[step] : (float) (kFlRadiusMi[step] * kKmPerMi);
}

// The radius adsb.lol is asked for: whole nautical miles, rounded up.
inline int radius_nm_for(float km) {
  int nm = (int) ceil(km / kKmPerNm - 1e-6);
  return nm < 1 ? 1 : nm > kPointMaxNm ? kPointMaxNm : nm;
}

// adsb.lol's planes around a point. Home goes out rounded to two decimals,
// about a kilometer, so the request doesn't carry the owner's address.
inline std::string point_url(double lat, double lon, int radius_nm) {
  double la = round(lat * 100.0) / 100.0 + 0.0, lo = round(lon * 100.0) / 100.0 + 0.0;  // + 0.0 turns -0 into 0
  char buf[96];
  snprintf(buf, sizeof(buf), "https://api.adsb.lol/v2/point/%.2f/%.2f/%d", la, lo,
           radius_nm < 1 ? 1 : radius_nm > kPointMaxNm ? kPointMaxNm : radius_nm);
  return buf;
}

inline bool fl_ends_with(const std::string &s, const char *tail) {
  size_t n = strlen(tail);
  return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

// The owner's receiver (the flrx setting): a bare host or IP reads tar1090's
// aircraft.json; a full http:// address ending in .json is used as is. ""
// for nothing set or an address with a space in it.
inline std::string receiver_url(const std::string &addr) {
  size_t a = addr.find_first_not_of(" \t"), b = addr.find_last_not_of(" \t");
  if (a == std::string::npos)
    return "";
  std::string s = addr.substr(a, b - a + 1);
  for (char c : s)
    if ((unsigned char) c <= ' ' || c == 127)
      return "";
  bool scheme = s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
  if (!scheme)
    s = "http://" + s;
  if (fl_ends_with(s, ".json"))
    return s;
  while (!s.empty() && s.back() == '/')
    s.pop_back();
  return s + "/tar1090/data/aircraft.json";
}

// adsbdb's route for a callsign; the callsign goes out upper case, letters
// and digits only.
inline std::string route_url(const char *call) {
  std::string url = "https://api.adsbdb.com/v0/callsign/";
  for (const char *c = call; c != nullptr && *c; c++) {
    char u = (*c >= 'a' && *c <= 'z') ? (char) (*c - 32) : *c;
    if (fl_upper(u) || fl_digit(u))
      url += u;
  }
  return url;
}

// ---- Parsing -----------------------------------------------------------------

namespace detail {

inline void fill_plane_filter(JsonObject a) {
  for (const char *k : {"hex", "flight", "r", "t", "alt_baro", "alt_geom", "gs", "track", "baro_rate", "geom_rate",
                        "squawk", "emergency", "lat", "lon", "seen_pos", "dbFlags"})
    a[k] = true;
}

// adsb.lol lists the planes under "ac", readsb and tar1090 under "aircraft".
inline void fill_sky_filter(JsonDocument &f) {
  fill_plane_filter(f["ac"].add<JsonObject>());
  fill_plane_filter(f["aircraft"].add<JsonObject>());
}

inline void fill_route_filter(JsonDocument &f) {
  JsonObject r = f["response"]["flightroute"].to<JsonObject>();
  r["callsign"] = true;
  for (const char *end : {"origin", "destination"}) {
    JsonObject e = r[end].to<JsonObject>();
    e["iata_code"] = true;
    e["icao_code"] = true;
    e["latitude"] = true;
    e["longitude"] = true;
  }
}

// Copy printable ASCII, cut to fit, without leading or trailing spaces.
inline void fl_copy(char *dst, size_t n, const char *src, bool lower) {
  size_t at = 0;
  if (src != nullptr) {
    while (*src == ' ')
      src++;
    for (; *src && at + 1 < n; src++) {
      char c = *src;
      if (c < ' ' || c > '~')
        continue;
      dst[at++] = lower && c >= 'A' && c <= 'Z' ? (char) (c + 32) : c;
    }
  }
  while (at > 0 && dst[at - 1] == ' ')
    at--;
  dst[at] = '\0';
}

inline float fl_number(JsonVariantConst v, float lo, float hi) {
  if (!v.is<float>())
    return NAN;
  float f = v.as<float>();
  return f >= lo && f <= hi ? f : NAN;
}

// One aircraft from the list, and the age of its position. False for one
// with no position, an old one or one out of range.
inline bool read_plane(JsonObjectConst a, Plane &p, float &seen_s) {
  JsonVariantConst lat = a["lat"], lon = a["lon"];
  if (!lat.is<double>() || !lon.is<double>())
    return false;
  p = Plane{};
  p.lat = lat.as<double>();
  p.lon = lon.as<double>();
  if (!(p.lat >= -90 && p.lat <= 90 && p.lon >= -180 && p.lon <= 180))
    return false;
  float seen = fl_number(a["seen_pos"], 0, 1e9f);
  if (seen > kSeenMaxS)
    return false;
  fl_copy(p.hex, sizeof(p.hex), a["hex"].as<const char *>(), true);
  fl_copy(p.call, sizeof(p.call), a["flight"].as<const char *>(), false);
  fl_copy(p.reg, sizeof(p.reg), a["r"].as<const char *>(), false);
  fl_copy(p.type, sizeof(p.type), a["t"].as<const char *>(), false);
  JsonVariantConst alt = a["alt_baro"];
  const char *alt_s = alt.as<const char *>();
  float alt_f = fl_number(alt, -2000, 100000);
  if (alt_s != nullptr && strcmp(alt_s, "ground") == 0) {
    p.ground = true;
    p.alt_ft = 0;
  } else if (std::isfinite(alt_f)) {
    p.alt_ft = (int32_t) lroundf(alt_f);
  } else if (std::isfinite(alt_f = fl_number(a["alt_geom"], -2000, 100000))) {
    p.alt_ft = (int32_t) lroundf(alt_f);
  }
  p.gs_kt = fl_number(a["gs"], 0, 2000);
  p.track = fl_number(a["track"], 0, 360);
  float rate = fl_number(a["baro_rate"], -30000, 30000);
  if (!std::isfinite(rate))
    rate = fl_number(a["geom_rate"], -30000, 30000);
  p.vrate_fpm = std::isfinite(rate) ? (int16_t) lroundf(rate) : 0;
  p.emergency = plane_emergency(a["squawk"].as<const char *>(), a["emergency"].as<const char *>());
  p.military = (uint8_t) ((a["dbFlags"] | 0) & 1);
  seen_s = std::isfinite(seen) ? seen : 0.0f;
  return true;
}

// Airborne before ground, then nearest first.
inline bool fl_before(const Plane &a, const Plane &b) {
  if (a.ground != b.ground)
    return !a.ground;
  return a.dist_km < b.dist_km;
}

inline void sky_insert(Sky &s, const Plane &p) {
  size_t at;
  if (s.n < kSkyMax) {
    at = s.n++;
  } else if (fl_before(p, s.p[kSkyMax - 1])) {
    at = kSkyMax - 1;
  } else {
    return;
  }
  for (; at > 0 && fl_before(p, s.p[at - 1]); at--)
    s.p[at] = s.p[at - 1];
  s.p[at] = p;
}

}  // namespace detail

// Parses a list of aircraft, adsb.lol's or a receiver's, into out: the planes
// within radius_km of home, each moved on by the age of its position. False
// when the document has no list of aircraft; out is untouched then.
template<typename TInput>
bool parse_sky(TInput &input, double home_lat, double home_lon, float radius_km, Sky &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_sky_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(10));
  if (err)
    return false;
  JsonArrayConst list = doc["ac"];
  if (list.isNull())
    list = doc["aircraft"];
  if (list.isNull())
    return false;
  out.n = 0;
  out.heard = 0;
  out.airborne = 0;
  Plane p;
  float seen;
  for (JsonObjectConst a : list) {
    if (!detail::read_plane(a, p, seen))
      continue;
    plane_advance(p, seen, home_lat, home_lon);
    if (!(p.dist_km <= radius_km))
      continue;
    if (out.heard < UINT16_MAX)
      out.heard++;
    if (!p.ground && out.airborne < UINT16_MAX)
      out.airborne++;
    detail::sky_insert(out, p);
  }
  if (out.airborne > 0)
    while (out.n > 0 && out.p[out.n - 1].ground)
      out.n--;
  return true;
}

// Parses adsbdb's answer. True for adsbdb's document: found with the route,
// or not found for a callsign it doesn't know. False for anything else; out
// is untouched then.
template<typename TInput> bool parse_route(TInput &input, Route &out) {
  JsonDocument filter(detail::json_allocator());
  detail::fill_route_filter(filter);
  JsonDocument doc(detail::json_allocator());
  DeserializationError err = deserializeJson(doc, input, DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(10));
  if (err || !doc.is<JsonObjectConst>())
    return false;
  // The filter keeps the "response" key with a null value when it is a string
  // ("unknown callsign").
  bool has_response = false;
  for (JsonPairConst kv : doc.as<JsonObjectConst>())
    has_response = has_response || strcmp(kv.key().c_str(), "response") == 0;
  if (!has_response)
    return false;
  Route r;
  JsonObjectConst fr = doc["response"]["flightroute"];
  if (!fr.isNull()) {
    JsonObjectConst o = fr["origin"], d = fr["destination"];
    const char *from = o["iata_code"].as<const char *>(), *to = d["iata_code"].as<const char *>();
    detail::fl_copy(r.from, sizeof(r.from), from != nullptr && from[0] ? from : o["icao_code"].as<const char *>(),
                    false);
    detail::fl_copy(r.to, sizeof(r.to), to != nullptr && to[0] ? to : d["icao_code"].as<const char *>(), false);
    detail::fl_copy(r.call, sizeof(r.call), fr["callsign"].as<const char *>(), false);
    r.from_lat = detail::fl_number(o["latitude"], -90, 90);
    r.from_lon = detail::fl_number(o["longitude"], -180, 180);
    r.to_lat = detail::fl_number(d["latitude"], -90, 90);
    r.to_lon = detail::fl_number(d["longitude"], -180, 180);
    r.found = r.from[0] && r.to[0];
  }
  out = r;
  return true;
}

// Whether the plane could be flying the route: going by the plane adds at
// most kRouteFitFactor and kRouteFitSlackKm to the trip. adsbdb keeps one
// route per callsign, and airlines reuse a flight number for other legs.
inline bool route_fits(const Route &r, const Plane &p) {
  if (!r.found || !std::isfinite(r.from_lat) || !std::isfinite(r.from_lon) || !std::isfinite(r.to_lat) ||
      !std::isfinite(r.to_lon))
    return false;
  double trip = haversine_km(r.from_lat, r.from_lon, r.to_lat, r.to_lon);
  double via = haversine_km(r.from_lat, r.from_lon, p.lat, p.lon) + haversine_km(p.lat, p.lon, r.to_lat, r.to_lon);
  return via <= kRouteFitFactor * trip + kRouteFitSlackKm;
}

// The firmware's entry points, reading the HTTP reply through the same
// WxSource as the weather reads. Compiled once, in espn_parse.cpp, for the
// reason weather.h gives.
bool parse_sky_from(WxSource &input, double home_lat, double home_lon, float radius_km, Sky &out);
bool parse_route_from(WxSource &input, Route &out);

}  // namespace espn
