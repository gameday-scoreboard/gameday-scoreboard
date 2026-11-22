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

// The Flights page runs its code a few times a second at most, so it is
// built for size, not speed (the firmware builds -O2): it keeps the page's
// flash small.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif

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
constexpr float kRouteFitFactor = 1.15f;
constexpr float kRouteFitSlackKm = 150.0f;
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
// The geometry is all single precision: the ESP32-S3 does float in hardware,
// and a float is good to a meter or two at these distances.
constexpr float kEarthKm = 6371.0088f;
constexpr float kKmPerNm = 1.852f;
constexpr float kKmPerMi = 1.609344f;

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
  float lat{0}, lon{0};
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
  // One copy of the code that sets up 16 planes, not one in every place
  // that makes a Sky.
  __attribute__((noinline)) Sky() {}
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

constexpr float kFlRadPerDeg = 3.14159265f / 180.0f;
constexpr float kFlKmPerDeg = kEarthKm * kFlRadPerDeg;  // along a meridian

inline float fl_rad(float deg) { return deg * kFlRadPerDeg; }

// Great-circle distance.
inline float haversine_km(float lat1, float lon1, float lat2, float lon2) {
  float sp = sinf(fl_rad(lat2 - lat1) / 2), sl = sinf(fl_rad(lon2 - lon1) / 2);
  float h = sp * sp + cosf(fl_rad(lat1)) * cosf(fl_rad(lat2)) * sl * sl;
  h = h < 1 ? h : 1;
  return 2 * kEarthKm * atan2f(sqrtf(h), sqrtf(1 - h));  // asin(sqrt(h)), with the atan2f bearing_deg uses
}

// Initial bearing from the first point to the second, 0 to 360.
inline float bearing_deg(float lat1, float lon1, float lat2, float lon2) {
  float p1 = fl_rad(lat1), p2 = fl_rad(lat2), dl = fl_rad(lon2 - lon1);
  float b = atan2f(sinf(dl) * cosf(p2), cosf(p1) * sinf(p2) - sinf(p1) * cosf(p2) * cosf(dl)) / kFlRadPerDeg;
  return b < 0 ? b + 360.0f : b;
}

// An angle in degrees as 0 to 360; NaN for NaN, an infinity or anything
// 100 turns or more out.
inline float fl_wrap360(float d) {
  if (!(fabsf(d) < 36000.0f))
    return NAN;
  while (d >= 360.0f)
    d -= 360.0f;
  while (d < 0)
    d += 360.0f;
  return d;
}

inline const char *compass8(float deg) {
  static const char *const kDirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  float d = fl_wrap360(deg);
  return std::isfinite(d) ? kDirs[(int) ((d + 22.5f) / 45.0f) % 8] : "";
}

// Whether a bearing is in view of a window facing `facing`: 0 any way, 1 to 8
// for N, NE, E, SE, S, SW, W, NW.
inline bool facing_ok(float brg_deg, uint8_t facing) {
  if (facing == 0 || facing > 8)
    return true;
  float diff = fabsf(fl_wrap360(brg_deg - (facing - 1) * 45.0f + 180.0f) - 180.0f);
  return diff <= kFacingHalfDeg;
}

inline void plane_locate(Plane &p, float home_lat, float home_lon) {
  p.dist_km = haversine_km(home_lat, home_lon, p.lat, p.lon);
  p.brg_deg = bearing_deg(home_lat, home_lon, p.lat, p.lon);
}

inline bool plane_moving(const Plane &p) { return !p.ground && std::isfinite(p.track) && std::isfinite(p.gs_kt); }

// Dead reckoning: moves the plane along its track at its ground speed for
// secs, and works out its distance and bearing from home again. Advance a
// copy of the read by the time since it, so rounding doesn't add up. The
// step is taken on a flat map around the plane: over the few minutes a read
// is drawn for, that is meters off a great circle.
inline void plane_advance(Plane &p, float secs, float home_lat, float home_lon) {
  if (plane_moving(p) && secs != 0) {
    float deg = p.gs_kt * kKmPerNm * secs / 3600.0f / kFlKmPerDeg;
    float t = fl_rad(p.track);
    p.lat += deg * cosf(t);
    p.lat = p.lat > 90.0f ? 90.0f : p.lat < -90.0f ? -90.0f : p.lat;
    float c = cosf(fl_rad(p.lat));
    p.lon += deg * sinf(t) / (c > 0.01f ? c : 0.01f);
    if (p.lon > 180.0f)
      p.lon -= 360.0f;
    else if (p.lon < -180.0f)
      p.lon += 360.0f;
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
  float b = fl_rad(p.brg_deg), t = fl_rad(p.track);
  float x = p.dist_km * sinf(b), y = p.dist_km * cosf(b);  // km east and north of home
  float v = p.gs_kt * kKmPerNm / 3600.0f;                   // km a second
  float vx = v * sinf(t), vy = v * cosf(t);
  float secs = -(x * vx + y * vy) / (v * v);
  c.secs = secs;
  if (secs > 0) {
    float ex = x + vx * secs, ey = y + vy * secs;
    c.km = sqrtf(ex * ex + ey * ey);
  }
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

// Short names for the airlines seen most in the US, Canada and Europe, by
// ICAO code, at most 14 characters. Each row is the code and the name
// with a NUL after it; the string's own NUL after the last row ends the
// table. One string, so a row costs only its letters.
constexpr char kAirlines[] =
    // US
    "AALAmerican\0"
    "UALUnited\0"
    "DALDelta\0"
    "SWASouthwest\0"
    "ASAAlaska\0"
    "JBUJetBlue\0"
    "NKSSpirit\0"
    "FFTFrontier\0"
    "AAYAllegiant\0"
    "HALHawaiian\0"
    "SCXSun Country\0"
    "VXPAvelo\0"
    "MXYBreeze\0"
    // US regionals
    "ENYEnvoy\0"
    "SKWSkyWest\0"
    "RPARepublic\0"
    "ASHMesa\0"
    "EDVEndeavor\0"
    "JIAPSA\0"
    "PDTPiedmont\0"
    "AWIAir Wisconsin\0"
    "QXEHorizon\0"
    "GJSGoJet\0"
    "CPZCompass\0"
    "UCACommutAir\0"
    // Cargo
    "FDXFedEx\0"
    "UPSUPS\0"
    "GTIAtlas\0"
    "ABXABX\0"
    "ATNATI\0"
    "PACPolar\0"
    "CKSKalitta\0"
    "AMFAmeriflight\0"
    // Private jets
    "EJANetJets\0"
    "LXJFlexjet\0"
    "WUPWheels Up\0"
    // Canada and Mexico
    "ACAAir Canada\0"
    "ROURouge\0"
    "JZAJazz\0"
    "WJAWestJet\0"
    "TSCAir Transat\0"
    "POEPorter\0"
    "SWGSunwing\0"
    "AMXAeromexico\0"
    "VOIVolaris\0"
    "VIVViva\0"
    // Europe
    "BAWBritish\0"
    "VIRVirgin\0"
    "EZYeasyJet\0"
    "EJUeasyJet\0"
    "RYRRyanair\0"
    "DLHLufthansa\0"
    "AFRAir France\0"
    "KLMKLM\0"
    "IBEIberia\0"
    "ITYITA\0"
    "SASSAS\0"
    "SWRSwiss\0"
    "AUAAustrian\0"
    "EINAer Lingus\0"
    "TAPTAP\0"
    "FINFinnair\0"
    "ICEIcelandair\0"
    "THYTurkish\0"
    "EWGEurowings\0"
    "VLGVueling\0"
    "WZZWizz\0"
    "BELBrussels\0"
    "LOTLOT\0"
    "ELYEl Al\0"
    // Middle East, Asia and the Pacific
    "UAEEmirates\0"
    "QTRQatar\0"
    "ETDEtihad\0"
    "JALJAL\0"
    "ANAANA\0"
    "KALKorean Air\0"
    "CPACathay\0"
    "SIASingapore\0"
    "QFAQantas\0"
    "ANZAir NZ\0"
    "CCAAir China\0"
    "CESChina Eastern\0"
    "CSNChina Southern\0"
    "EVAEVA Air\0"
    "CALChina Airlines\0"
    // Latin America
    "AVAAvianca\0"
    "CMPCopa\0"
    "LANLATAM\0"
    "TAMLATAM\0";

// The airline of a callsign ("AAL2192") or code ("AAL"); nullptr when it
// isn't one the table knows.
inline const char *airline_short(const char *call) {
  if (call == nullptr || !fl_upper(call[0]) || !fl_upper(call[1]) || !fl_upper(call[2]))
    return nullptr;
  if (call[3] != '\0' && !fl_digit(call[3]))
    return nullptr;
  for (const char *e = kAirlines; *e; e += strlen(e) + 1)
    if (memcmp(e, call, 3) == 0)
      return e + 3;
  return nullptr;
}

// The makers a wide type name can start with, each with a NUL after it.
constexpr char kTypeMakers[] =
    "Boeing\0" "Airbus\0" "Citation\0" "Bombardier\0" "Gulfstream\0" "Embraer\0" "De Havilland\0" "Pilatus\0"
    "Cirrus\0" "Beech\0";

// Type names by ICAO type code: each row is the code, the name for a single
// panel (at most 9 characters) and the name for two (at most 20), each with
// a NUL after it; the string's own NUL after the last row ends the table.
// The wide name is empty when it is the narrow one, and a single byte 1 to
// 10 when it is that maker in kTypeMakers, a space and the narrow one.
constexpr char kTypes[] =
    // Airbus
    "A318\0" "A318\0" "\2\0"
    "A319\0" "A319\0" "\2\0"
    "A320\0" "A320\0" "\2\0"
    "A321\0" "A321\0" "\2\0"
    "A19N\0" "A319neo\0" "\2\0"
    "A20N\0" "A320neo\0" "\2\0"
    "A21N\0" "A321neo\0" "\2\0"
    "BCS1\0" "A220-100\0" "\2\0"
    "BCS3\0" "A220-300\0" "\2\0"
    "A332\0" "A330-200\0" "\2\0"
    "A333\0" "A330-300\0" "\2\0"
    "A339\0" "A330-900\0" "\2\0"
    "A343\0" "A340-300\0" "\2\0"
    "A359\0" "A350-900\0" "\2\0"
    "A35K\0" "A350-1000\0" "\2\0"
    "A388\0" "A380\0" "\2\0"
    // Boeing
    "B712\0" "717\0" "\1\0"
    "B733\0" "737-300\0" "\1\0"
    "B735\0" "737-500\0" "\1\0"
    "B737\0" "737-700\0" "\1\0"
    "B738\0" "737-800\0" "\1\0"
    "B739\0" "737-900\0" "\1\0"
    "B37M\0" "737 MAX 7\0" "\1\0"
    "B38M\0" "737 MAX 8\0" "\1\0"
    "B39M\0" "737 MAX 9\0" "\1\0"
    "B3XM\0" "737 MAX10\0" "Boeing 737 MAX 10\0"
    "B752\0" "757-200\0" "\1\0"
    "B753\0" "757-300\0" "\1\0"
    "B762\0" "767-200\0" "\1\0"
    "B763\0" "767-300\0" "\1\0"
    "B764\0" "767-400\0" "\1\0"
    "B772\0" "777-200\0" "\1\0"
    "B77L\0" "777-200LR\0" "\1\0"
    "B773\0" "777-300\0" "\1\0"
    "B77W\0" "777-300ER\0" "\1\0"
    "B788\0" "787-8\0" "\1\0"
    "B789\0" "787-9\0" "\1\0"
    "B78X\0" "787-10\0" "\1\0"
    "B744\0" "747-400\0" "\1\0"
    "B748\0" "747-8\0" "\1\0"
    "MD11\0" "MD-11\0" "\0"
    // Regional jets and turboprops
    "E135\0" "ERJ-135\0" "\6\0"
    "E145\0" "ERJ-145\0" "\6\0"
    "E45X\0" "ERJ-145XR\0" "\6\0"
    "E170\0" "E170\0" "Embraer 170\0"
    "E75L\0" "E175\0" "Embraer 175\0"
    "E75S\0" "E175\0" "Embraer 175\0"
    "E190\0" "E190\0" "Embraer 190\0"
    "E195\0" "E195\0" "Embraer 195\0"
    "E290\0" "E190-E2\0" "Embraer 190-E2\0"
    "E295\0" "E195-E2\0" "Embraer 195-E2\0"
    "CRJ2\0" "CRJ-200\0" "\4\0"
    "CRJ7\0" "CRJ-700\0" "\4\0"
    "CRJ9\0" "CRJ-900\0" "\4\0"
    "CRJX\0" "CRJ-1000\0" "\4\0"
    "DH8A\0" "Dash 8\0" "\7\0"
    "DH8D\0" "Q400\0" "\7\0"
    "AT72\0" "ATR 72\0" "\0"
    "AT76\0" "ATR 72\0" "ATR 72-600\0"
    "AT45\0" "ATR 42\0" "\0"
    // Business jets
    "E50P\0" "Phenom100\0" "Embraer Phenom 100\0"
    "E55P\0" "Phenom300\0" "Embraer Phenom 300\0"
    "CL30\0" "Challengr\0" "Challenger 300\0"
    "CL35\0" "Challengr\0" "Challenger 350\0"
    "CL60\0" "Challengr\0" "Challenger 600\0"
    "GLEX\0" "Global\0" "\4\0"
    "GL7T\0" "Global\0" "Global 7500\0"
    "GLF4\0" "G-IV\0" "\5\0"
    "GLF5\0" "G-V\0" "\5\0"
    "GLF6\0" "G650\0" "\5\0"
    "G280\0" "G280\0" "\5\0"
    "C25A\0" "CJ2\0" "\3\0"
    "C25B\0" "CJ3\0" "\3\0"
    "C25C\0" "CJ4\0" "\3\0"
    "C56X\0" "Excel\0" "\3\0"
    "C68A\0" "Latitude\0" "\3\0"
    "C700\0" "Longitude\0" "\3\0"
    "C750\0" "Citation\0" "Citation X\0"
    "HDJT\0" "HondaJet\0" "\0"
    "SF50\0" "Vision\0" "Cirrus Vision Jet\0"
    "PC12\0" "PC-12\0" "\10\0"
    "PC24\0" "PC-24\0" "\10\0"
    // Small planes
    "C152\0" "C152\0" "Cessna 152\0"
    "C172\0" "C172\0" "Cessna 172 Skyhawk\0"
    "C182\0" "C182\0" "Cessna 182 Skylane\0"
    "C208\0" "Caravan\0" "Cessna Caravan\0"
    "P28A\0" "PA-28\0" "Piper Cherokee\0"
    "PA24\0" "PA-24\0" "Piper Comanche\0"
    "PA32\0" "PA-32\0" "Piper Saratoga\0"
    "PA44\0" "PA-44\0" "Piper Seminole\0"
    "SR20\0" "SR20\0" "\11\0"
    "SR22\0" "SR22\0" "\11\0"
    "BE36\0" "Bonanza\0" "\12\0"
    "BE58\0" "Baron\0" "\12\0"
    "BE9L\0" "King Air\0" "King Air 90\0"
    "BE20\0" "King Air\0" "King Air 200\0"
    "B350\0" "King Air\0" "King Air 350\0"
    // Helicopters
    "B06\0" "Bell 206\0" "Bell 206 JetRanger\0"
    "B407\0" "Bell 407\0" "\0"
    "AS50\0" "AStar\0" "\2\0"
    "EC30\0" "H130\0" "\2\0"
    "EC35\0" "H135\0" "\2\0"
    "EC45\0" "H145\0" "\2\0"
    "R44\0" "R44\0" "Robinson R44\0"
    "S76\0" "S-76\0" "Sikorsky S-76\0"
    // Military
    "C17\0" "C-17\0" "C-17 Globemaster\0"
    "C130\0" "C-130\0" "C-130 Hercules\0"
    "C30J\0" "C-130J\0" "C-130J Hercules\0"
    "K35R\0" "KC-135\0" "KC-135 Stratotanker\0";

// The type's name, narrow or wide, into out; the code itself when the
// table doesn't have it. Returns out.
inline const char *plane_type_name(char *out, size_t n, const char *code, bool wide) {
  const char *name = code != nullptr ? code : "", *maker = nullptr;
  for (const char *e = kTypes; code != nullptr && *e;) {
    const char *narrow = e + strlen(e) + 1, *w = narrow + strlen(narrow) + 1;
    if (strcmp(e, code) == 0) {
      name = wide && (uint8_t) *w > ' ' ? w : narrow;
      if (wide && *w != '\0' && (uint8_t) *w <= ' ') {
        maker = kTypeMakers;
        for (int i = 1; i < *w; i++)
          maker += strlen(maker) + 1;
      }
      break;
    }
    e = w + strlen(w) + 1;
  }
  snprintf(out, n, "%s%s%s", maker != nullptr ? maker : "", maker != nullptr ? " " : "", name);
  return out;
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

// Feet to whole meters, rounded half away from zero.
inline long fl_ft_to_m(int32_t ft) { return ((long) ft * 3048 + (ft < 0 ? -5000 : 5000)) / 10000; }

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
  fl_grouped(num, sizeof(num), metric ? fl_ft_to_m(p.alt_ft) : (long) p.alt_ft);
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
  snprintf(out, n, "%ld %s", lroundf(gs_kt * (metric ? kKmPerNm : kKmPerNm / kKmPerMi)), metric ? "km/h" : "mph");
  return out;
}

// "2.4 mi" or "3.9 km", whole numbers from 10 up.
inline const char *dist_text(char *out, size_t n, float km, bool metric) {
  if (n == 0)
    return out;
  float v = metric ? km : km / kKmPerMi;
  if (v < 9.95f)
    snprintf(out, n, "%.1f %s", (double) v, metric ? "km" : "mi");
  else
    snprintf(out, n, "%ld %s", lroundf(v), metric ? "km" : "mi");
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
  return metric ? (float) kFlRadiusKm[step] : kFlRadiusMi[step] * kKmPerMi;
}

// The radius adsb.lol is asked for: whole nautical miles, rounded up.
inline int radius_nm_for(float km) {
  int nm = (int) ceilf(km / kKmPerNm - 1e-4f);
  return nm < 1 ? 1 : nm > kPointMaxNm ? kPointMaxNm : nm;
}

// adsb.lol's planes around a point. Home goes out rounded to two decimals,
// about a kilometer, so the request doesn't carry the owner's address.
inline std::string point_url(float lat, float lon, int radius_nm) {
  // In hundredths, so a home just west of 0 goes out as 0.00, not -0.00.
  long la = lroundf(lat * 100.0f), lo = lroundf(lon * 100.0f);
  char buf[96];
  snprintf(buf, sizeof(buf), "https://api.adsb.lol/v2/point/%s%ld.%02ld/%s%ld.%02ld/%d", la < 0 ? "-" : "",
           labs(la) / 100, labs(la) % 100, lo < 0 ? "-" : "", labs(lo) / 100, labs(lo) % 100,
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

// A member's number from lo to hi, NaN for anything else, and a member's
// text, nullptr for none. Out of line, so each field read is a call.
__attribute__((noinline)) inline float fl_num(JsonObjectConst o, const char *k, float lo, float hi) {
  return fl_number(o[k], lo, hi);
}
__attribute__((noinline)) inline const char *fl_text(JsonObjectConst o, const char *k) {
  return o[k].as<const char *>();
}

// One aircraft from the list, and the age of its position. False for one
// with no position, an old one or one out of range.
inline bool read_plane(JsonObjectConst a, Plane &p, float &seen_s) {
  float lat = fl_num(a, "lat", -90, 90), lon = fl_num(a, "lon", -180, 180);
  if (!std::isfinite(lat) || !std::isfinite(lon))
    return false;
  float seen = fl_num(a, "seen_pos", 0, 1e9f);
  if (seen > kSeenMaxS)
    return false;
  p = Plane{};
  p.lat = lat;
  p.lon = lon;
  fl_copy(p.hex, sizeof(p.hex), fl_text(a, "hex"), true);
  fl_copy(p.call, sizeof(p.call), fl_text(a, "flight"), false);
  fl_copy(p.reg, sizeof(p.reg), fl_text(a, "r"), false);
  fl_copy(p.type, sizeof(p.type), fl_text(a, "t"), false);
  const char *alt_s = fl_text(a, "alt_baro");
  float alt_f = fl_num(a, "alt_baro", -2000, 100000);
  if (alt_s != nullptr && strcmp(alt_s, "ground") == 0) {
    p.ground = true;
    p.alt_ft = 0;
  } else if (std::isfinite(alt_f) || std::isfinite(alt_f = fl_num(a, "alt_geom", -2000, 100000))) {
    p.alt_ft = (int32_t) lroundf(alt_f);
  }
  p.gs_kt = fl_num(a, "gs", 0, 2000);
  p.track = fl_num(a, "track", 0, 360);
  float rate = fl_num(a, "baro_rate", -30000, 30000);
  if (!std::isfinite(rate))
    rate = fl_num(a, "geom_rate", -30000, 30000);
  p.vrate_fpm = std::isfinite(rate) ? (int16_t) lroundf(rate) : 0;
  p.emergency = plane_emergency(fl_text(a, "squawk"), fl_text(a, "emergency"));
  float flags = fl_num(a, "dbFlags", 0, 65535);
  p.military = (uint8_t) (std::isfinite(flags) ? (int) flags & 1 : 0);
  seen_s = std::isfinite(seen) ? seen : 0.0f;
  return true;
}

// One end of adsbdb's route: its IATA code, ICAO when it has none, and where
// it is.
inline void route_end(JsonObjectConst e, char (&code)[5], float &lat, float &lon) {
  const char *iata = fl_text(e, "iata_code");
  fl_copy(code, sizeof(code), iata != nullptr && iata[0] ? iata : fl_text(e, "icao_code"), false);
  lat = fl_num(e, "latitude", -90, 90);
  lon = fl_num(e, "longitude", -180, 180);
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
bool parse_sky(TInput &input, float home_lat, float home_lon, float radius_km, Sky &out) {
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
    detail::route_end(fr["origin"], r.from, r.from_lat, r.from_lon);
    detail::route_end(fr["destination"], r.to, r.to_lat, r.to_lon);
    detail::fl_copy(r.call, sizeof(r.call), detail::fl_text(fr, "callsign"), false);
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
  float trip = haversine_km(r.from_lat, r.from_lon, r.to_lat, r.to_lon);
  float via = haversine_km(r.from_lat, r.from_lon, p.lat, p.lon) + haversine_km(p.lat, p.lon, r.to_lat, r.to_lon);
  return via <= kRouteFitFactor * trip + kRouteFitSlackKm;
}

// The firmware's entry points, reading the HTTP reply through the same
// WxSource as the weather reads. Compiled once, in espn_parse.cpp, for the
// reason weather.h gives.
bool parse_sky_from(WxSource &input, float home_lat, float home_lon, float radius_km, Sky &out);
bool parse_route_from(WxSource &input, Route &out);

}  // namespace espn

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
