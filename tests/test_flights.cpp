// Host tests for the Flights page's core (components/gameday/flights.h): the
// adsb.lol, receiver and adsbdb parsers, the geometry, the card pick and the
// text. Build with `make` in tests/.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "flights.h"
#include "flights_page.h"

using namespace espn;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    checks++;                                                                  \
    auto va = (a);                                                             \
    auto vb = (b);                                                             \
    if (!(va == vb)) {                                                         \
      failures++;                                                              \
      std::ostringstream os;                                                   \
      os << va << " != " << vb;                                                \
      printf("FAIL %s:%d: %s: %s\n", __FILE__, __LINE__, #a, os.str().c_str()); \
    }                                                                          \
  } while (0)

#define CHECK_STR(a, b) CHECK_EQ(std::string(a), std::string(b))
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((double) (a) - (double) (b)) <= (tol))

static std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// The point the adsb.lol fixture was read around, 25 nm.
static const double kHomeLat = 32.96, kHomeLon = -96.99;
static const float kRadius25nm = 25 * 1.852f;

static int find_hex(const Sky &s, const char *hex) {
  for (int i = 0; i < s.n; i++)
    if (strcmp(s.p[i].hex, hex) == 0)
      return i;
  return -1;
}

static void test_urls() {
  CHECK_STR(point_url(32.95457, -97.01501, 25), "https://api.adsb.lol/v2/point/32.95/-97.02/25");
  CHECK_STR(point_url(-0.001, 0.004, 5), "https://api.adsb.lol/v2/point/0.00/0.00/5");
  CHECK_STR(point_url(51.4775, -0.4614, 250), "https://api.adsb.lol/v2/point/51.48/-0.46/100");
  CHECK_STR(point_url(51.4775, -0.4614, 0), "https://api.adsb.lol/v2/point/51.48/-0.46/1");
  // The flrad steps: 10 mi asks for 9 nm, 25 nm stays 25.
  CHECK_NEAR(fl_radius_km(1, false), 16.0934, 0.001);
  CHECK_NEAR(fl_radius_km(1, true), 15, 0.001);
  CHECK_NEAR(fl_radius_km(4, false), 80.4672, 0.001);
  CHECK_NEAR(fl_radius_km(9, true), 15, 0.001);
  CHECK_EQ(radius_nm_for(fl_radius_km(1, false)), 9);
  CHECK_EQ(radius_nm_for(25 * 1.852f), 25);
  CHECK_EQ(radius_nm_for(500), 100);
  CHECK_EQ(radius_nm_for(0), 1);

  CHECK_STR(receiver_url("192.168.1.50"), "http://192.168.1.50/tar1090/data/aircraft.json");
  CHECK_STR(receiver_url(" adsb.local:8080/ "), "http://adsb.local:8080/tar1090/data/aircraft.json");
  CHECK_STR(receiver_url("http://10.0.0.5/data/aircraft.json"), "http://10.0.0.5/data/aircraft.json");
  CHECK_STR(receiver_url("http://10.0.0.5:8504"), "http://10.0.0.5:8504/tar1090/data/aircraft.json");
  CHECK_STR(receiver_url("10.0.0.5/skyaware/data/aircraft.json"), "http://10.0.0.5/skyaware/data/aircraft.json");
  CHECK_STR(receiver_url(""), "");
  CHECK_STR(receiver_url("   "), "");
  CHECK_STR(receiver_url("my pi"), "");

  CHECK_STR(route_url("aal2192 "), "https://api.adsbdb.com/v0/callsign/AAL2192");
  CHECK_STR(route_url("SWA/1939"), "https://api.adsbdb.com/v0/callsign/SWA1939");
}

static void test_sky_adsblol() {
  std::string json = slurp("fixtures/flights_adsblol_point_dfw25.json");
  Sky s;
  CHECK(parse_sky(json, kHomeLat, kHomeLon, kRadius25nm, s));
  // 69 planes in the reply, all within 25 nm; 19 of them on the ground.
  CHECK_EQ((int) s.heard, 69);
  CHECK_EQ((int) s.airborne, 50);
  CHECK_EQ((int) s.n, (int) kSkyMax);
  for (int i = 0; i < s.n; i++) {
    CHECK(!s.p[i].ground);
    if (i > 0)
      CHECK(s.p[i - 1].dist_km <= s.p[i].dist_km);
  }
  // Nearest first, as adsb.lol's own "dst" orders them.
  CHECK_STR(s.p[0].hex, "aa0a6e");
  CHECK_STR(s.p[0].call, "SKW6306");
  CHECK_STR(s.p[0].type, "CRJ7");
  CHECK_EQ(s.p[0].alt_ft, 1650);
  CHECK_NEAR(s.p[0].dist_km, 1.922 * 1.852, 0.05);
  CHECK_STR(s.p[1].call, "ENY4010");
  CHECK_STR(s.p[2].call, "AAL2253");
  CHECK_STR(s.p[15].hex, "ac692a");
  int aal = find_hex(s, "a695ea");
  CHECK(aal >= 0);
  if (aal >= 0) {
    const Plane &p = s.p[aal];
    CHECK_STR(p.call, "AAL2192");
    CHECK_STR(p.reg, "N523UW");
    CHECK_EQ(p.alt_ft, 1850);
    CHECK_EQ((int) p.vrate_fpm, -768);
    CHECK_NEAR(p.gs_kt, 146.8, 0.01);
    CHECK_NEAR(p.track, 139.14, 0.01);
    CHECK_NEAR(p.brg_deg, 268.2, 0.5);
    CHECK(!p.emergency);
    CHECK_EQ((int) p.military, 0);
  }
  // A plane with no "t" keeps an empty type.
  CHECK_EQ(find_hex(s, "a760b3"), -1);  // on the ground, so not kept

  // The radius: 10 km holds 20 planes, 3 of them in the air.
  CHECK(parse_sky(json, kHomeLat, kHomeLon, 10.0f, s));
  CHECK_EQ((int) s.heard, 20);
  CHECK_EQ((int) s.airborne, 3);
  CHECK_EQ((int) s.n, 3);
  CHECK_STR(s.p[2].call, "AAL2253");
  CHECK(parse_sky(json, kHomeLat, kHomeLon, 40.0f, s));
  CHECK_EQ((int) s.heard, 59);
  CHECK_EQ((int) s.airborne, 40);

  // Standing at the airport's gates, only planes on the ground: they stay,
  // nearest first, and one without a callsign is named by its registration.
  CHECK(parse_sky(json, 32.908424, -97.033928, 0.5f, s));
  CHECK_EQ((int) s.airborne, 0);
  CHECK(s.n >= 1);
  CHECK(s.heard == s.n);
  CHECK_STR(s.p[0].hex, "acb470");
  CHECK(s.p[0].ground);
  CHECK_STR(s.p[0].call, "");
  CHECK_STR(plane_label(s.p[0]), "N918AN");
  CHECK(s.p[0].dist_km < 0.01f);
  for (int i = 0; i < s.n; i++)
    CHECK(s.p[i].ground);

  // Nobody within a few hundred meters out in the country.
  CHECK(parse_sky(json, 33.5, -96.0, 1.0f, s));
  CHECK_EQ((int) s.n, 0);
  CHECK_EQ((int) s.heard, 0);
}

static void test_sky_receiver() {
  std::string json = slurp("fixtures/flights_readsb_small.json");
  Sky s;
  CHECK(parse_sky(json, kHomeLat, kHomeLon, 25.0f, s));
  // No position, a 75 s old position and one 115 km out are dropped; the
  // plane on the ground counts as heard but gives way to the two in the air.
  CHECK_EQ((int) s.heard, 3);
  CHECK_EQ((int) s.airborne, 2);
  CHECK_EQ((int) s.n, 2);
  // The made-up TIS-B address, its altitude from alt_geom, 2 s of flying
  // south at 120 kt added.
  const Plane &a = s.p[0];
  CHECK_STR(a.hex, "~2b0c41");
  CHECK_STR(a.call, "");
  CHECK_STR(plane_label(a), "N12345");
  CHECK_EQ(a.alt_ft, 2500);
  CHECK_EQ((int) a.vrate_fpm, -800);
  CHECK(a.emergency);  // "minfuel"
  CHECK_NEAR(a.lat, 32.93 - 120 * 1.852 * 2 / 3600 / 111.195, 1e-5);
  const Plane &b = s.p[1];
  CHECK_STR(b.hex, "ae1234");
  CHECK_STR(b.call, "RCH123");
  CHECK(b.emergency);  // squawk 7700
  CHECK_EQ((int) b.military, 1);
  CHECK_EQ(vrate_dir(b.vrate_fpm), 1);
  CHECK(a.dist_km < b.dist_km);

  // Ground only, at the ground plane's spot; its address comes out lower case.
  CHECK(parse_sky(json, 32.90, -97.04, 1.0f, s));
  CHECK_EQ((int) s.n, 1);
  CHECK_EQ((int) s.airborne, 0);
  CHECK_STR(s.p[0].hex, "a0b0c0");
  CHECK(s.p[0].ground);
  CHECK_EQ(s.p[0].alt_ft, 0);

  // Not a list of aircraft: false, and the last read stays.
  Sky keep = s;
  std::string bad = "{\"msg\":\"No error\"}";
  CHECK(!parse_sky(bad, kHomeLat, kHomeLon, 25.0f, s));
  bad = "<html><body>502</body></html>";
  CHECK(!parse_sky(bad, kHomeLat, kHomeLon, 25.0f, s));
  bad = "{\"ac\":7}";
  CHECK(!parse_sky(bad, kHomeLat, kHomeLon, 25.0f, s));
  CHECK_EQ((int) s.n, (int) keep.n);
  CHECK_STR(s.p[0].hex, keep.p[0].hex);
  // An empty sky is a good read.
  std::string empty = "{\"ac\":[],\"msg\":\"No error\",\"total\":0}";
  CHECK(parse_sky(empty, kHomeLat, kHomeLon, 25.0f, s));
  CHECK_EQ((int) s.n, 0);

  // The firmware's entry point reads through a WxSource.
  struct StringSource : WxSource {
    explicit StringSource(const std::string &s) : s(s) {}
    int read() override { return pos < s.size() ? (unsigned char) s[pos++] : -1; }
    size_t readBytes(char *dst, size_t n) override {
      size_t take = std::min(n, s.size() - pos);
      memcpy(dst, s.data() + pos, take);
      pos += take;
      return take;
    }
    const std::string &s;
    size_t pos{0};
  };
  StringSource src(json);
  CHECK(parse_sky_from(src, kHomeLat, kHomeLon, 25.0f, s) && s.n == 2);
  std::string route = slurp("fixtures/flights_adsbdb_aal2192.json");
  StringSource rsrc(route);
  Route r;
  CHECK(parse_route_from(rsrc, r) && r.found);
}

static void test_routes() {
  Route r;
  std::string json = slurp("fixtures/flights_adsbdb_aal2192.json");
  CHECK(parse_route(json, r));
  CHECK(r.found);
  CHECK_STR(r.call, "AAL2192");
  CHECK_STR(r.from, "ORD");
  CHECK_STR(r.to, "SFO");
  CHECK_NEAR(r.from_lat, 41.9786, 1e-4);
  CHECK_NEAR(r.to_lon, -122.375, 1e-4);

  // adsbdb says AAL2192 flies Chicago to San Francisco, but the plane is
  // descending into DFW at 1,850 ft: the route doesn't fit.
  std::string sky_json = slurp("fixtures/flights_adsblol_point_dfw25.json");
  Sky s;
  CHECK(parse_sky(sky_json, kHomeLat, kHomeLon, kRadius25nm, s));
  int i = find_hex(s, "a695ea");
  CHECK(i >= 0);
  if (i >= 0) {
    CHECK(!route_fits(r, s.p[i]));
    // The same plane on DFW to Chicago fits.
    Route dfw_ord = r;
    strcpy(dfw_ord.from, "DFW");
    strcpy(dfw_ord.to, "ORD");
    dfw_ord.from_lat = 32.8968f;
    dfw_ord.from_lon = -97.0380f;
    dfw_ord.to_lat = 41.9786f;
    dfw_ord.to_lon = -87.9048f;
    CHECK(route_fits(dfw_ord, s.p[i]));
    // A route with no coordinates can't be checked, so it isn't shown.
    dfw_ord.to_lat = NAN;
    CHECK(!route_fits(dfw_ord, s.p[i]));
  }
  // A plane over Kansas fits Chicago to San Francisco a little off the line.
  Plane over;
  over.lat = 39.5;
  over.lon = -100.0;
  CHECK(route_fits(r, over));

  // A callsign adsbdb doesn't know is a good answer with no route.
  json = slurp("fixtures/flights_adsbdb_unknown.json");
  r.found = true;
  CHECK(parse_route(json, r));
  CHECK(!r.found);
  CHECK(!route_fits(r, over));
  // An airport with no IATA code goes by its ICAO one.
  json =
      "{\"response\":{\"flightroute\":{\"callsign\":\"N123\",\"origin\":{\"iata_code\":null,\"icao_code\":\"KADS\","
      "\"latitude\":32.97,\"longitude\":-96.84},\"destination\":{\"iata_code\":\"AUS\",\"icao_code\":\"KAUS\","
      "\"latitude\":30.19,\"longitude\":-97.67}}}}";
  CHECK(parse_route(json, r));
  CHECK(r.found);
  CHECK_STR(r.from, "KADS");
  CHECK_STR(r.to, "AUS");
  // Not adsbdb's document.
  r = Route{};
  strcpy(r.from, "XXX");
  json = "<html>";
  CHECK(!parse_route(json, r));
  json = "{\"error\":\"rate limited\"}";
  CHECK(!parse_route(json, r));
  CHECK_STR(r.from, "XXX");
}

static void test_geometry() {
  CHECK_STR(compass8(0), "N");
  CHECK_STR(compass8(22.4f), "N");
  CHECK_STR(compass8(22.5f), "NE");
  CHECK_STR(compass8(90), "E");
  CHECK_STR(compass8(180), "S");
  CHECK_STR(compass8(225), "SW");
  CHECK_STR(compass8(315), "NW");
  CHECK_STR(compass8(359), "N");
  CHECK_STR(compass8(360), "N");
  CHECK_STR(compass8(-90), "W");
  CHECK_STR(compass8(NAN), "");

  // DFW to ORD, about 1,290 km and a bit east of north-east.
  CHECK_NEAR(haversine_km(32.8968, -97.0380, 41.9786, -87.9048), 1291, 5);
  CHECK_NEAR(bearing_deg(32.8968, -97.0380, 41.9786, -87.9048), 37, 2);
  CHECK_NEAR(bearing_deg(0, 0, -1, 0), 180, 1e-6);

  // A minute north at 360 kt is 11.112 km.
  Plane p;
  p.lat = 33.0;
  p.lon = -97.0;
  p.track = 0;
  p.gs_kt = 360;
  plane_advance(p, 0, 33.0, -97.0);
  CHECK_NEAR(p.dist_km, 0, 1e-6);
  plane_advance(p, 60, 33.0, -97.0);
  CHECK_NEAR(p.dist_km, 11.112, 0.01);
  CHECK_NEAR(p.brg_deg, 0, 0.01);
  CHECK_NEAR(p.lon, -97.0, 1e-9);
  // A plane on the ground or with no track stays put.
  Plane g = p;
  g.ground = true;
  plane_advance(g, 60, 33.0, -97.0);
  CHECK_NEAR(g.lat, p.lat, 1e-12);
  g = p;
  g.track = NAN;
  plane_advance(g, 60, 33.0, -97.0);
  CHECK_NEAR(g.lat, p.lat, 1e-12);
}

static Plane at(const char *hex, float dist_km, float brg_deg, float track = NAN, float gs_kt = NAN) {
  Plane p;
  snprintf(p.hex, sizeof(p.hex), "%s", hex);
  p.dist_km = dist_km;
  p.brg_deg = brg_deg;
  p.track = track;
  p.gs_kt = gs_kt;
  return p;
}

static void test_cpa() {
  // 10 km south heading north at 360 kt (0.1852 km/s): over the house in 54 s.
  Cpa c = plane_cpa(at("a", 10, 180, 0, 360));
  CHECK_NEAR(c.secs, 54.0, 0.1);
  CHECK_NEAR(c.km, 0, 0.01);
  CHECK(cpa_overhead(c));
  // Crossing 1.5 km east of home.
  Plane p = at("a", 10, 180, 0, 360);
  float x = 1.5f, y = -10.0f;
  p.dist_km = std::hypot(x, y);
  p.brg_deg = (float) (std::atan2(x, y) * 180.0 / 3.14159265358979);
  c = plane_cpa(p);
  CHECK_NEAR(c.km, 1.5, 0.01);
  CHECK(cpa_overhead(c));
  // 3 km abeam is not overhead.
  p.dist_km = std::hypot(3.0f, y);
  p.brg_deg = (float) (std::atan2(3.0, y) * 180.0 / 3.14159265358979);
  CHECK(!cpa_overhead(plane_cpa(p)));
  // Flying across the line to home: closest now.
  c = plane_cpa(at("a", 10, 180, 90, 360));
  CHECK_NEAR(c.secs, 0, 0.01);
  CHECK(!cpa_overhead(c));
  // Flying away: already passed.
  c = plane_cpa(at("a", 1, 180, 180, 360));
  CHECK(c.secs < 0);
  CHECK_NEAR(c.km, 1, 1e-6);
  CHECK(!cpa_overhead(c));
  // Over the house, but 324 s away.
  c = plane_cpa(at("a", 60, 180, 0, 360));
  CHECK_NEAR(c.secs, 324, 0.5);
  CHECK(!cpa_overhead(c));
  // On the ground, or no speed: no approach.
  p = at("a", 1, 180, 0, 20);
  p.ground = true;
  c = plane_cpa(p);
  CHECK(std::isnan(c.secs));
  CHECK(!cpa_overhead(c));
  CHECK(!cpa_overhead(plane_cpa(at("a", 1, 180, 0, NAN))));
}

static Sky sky_of(std::initializer_list<Plane> planes) {
  Sky s;
  for (const Plane &p : planes)
    s.p[s.n++] = p;
  return s;
}

static void test_pick() {
  CHECK(facing_ok(123, 0));
  CHECK(facing_ok(0, 1));
  CHECK(facing_ok(60, 1));
  CHECK(!facing_ok(61, 1));
  CHECK(facing_ok(300, 1));
  CHECK(!facing_ok(299, 1));
  CHECK(facing_ok(180, 5));
  CHECK(facing_ok(260, 7));
  CHECK(!facing_ok(90, 7));

  Sky empty;
  CHECK_EQ(pick_card(empty, "", 0), -1);
  CHECK_EQ(pick_card(empty, "abc", 3), -1);

  // Keeps the current plane against one only a little nearer.
  Sky s = sky_of({at("b", 4.5f, 10), at("a", 5, 20)});
  CHECK_EQ(pick_card(s, nullptr, 0), 0);
  CHECK_EQ(pick_card(s, "", 0), 0);
  CHECK_EQ(pick_card(s, "a", 0), 1);
  // Nearer by more than 25% and 1 km: switches.
  s = sky_of({at("b", 3.5f, 10), at("a", 5, 20)});
  CHECK_EQ(pick_card(s, "a", 0), 0);
  // Far out, 25% is the margin: 20 km against 16 stays, against 14 switches.
  s = sky_of({at("b", 16, 10), at("a", 20, 20)});
  CHECK_EQ(pick_card(s, "a", 0), 1);
  s = sky_of({at("b", 14, 10), at("a", 20, 20)});
  CHECK_EQ(pick_card(s, "a", 0), 0);
  // Close in, the 1 km floor: 2 km against 1.2 stays.
  s = sky_of({at("b", 1.2f, 10), at("a", 2, 20)});
  CHECK_EQ(pick_card(s, "a", 0), 1);
  // The current plane left the read: the nearest.
  CHECK_EQ(pick_card(s, "gone", 0), 0);

  // Facing: the window looks north, so the plane to the north gets the card
  // though one to the south is nearer.
  s = sky_of({at("s", 3, 180), at("n", 8, 10)});
  CHECK_EQ(pick_card(s, "", 1), 1);
  CHECK_EQ(pick_card(s, "", 5), 0);
  // Nothing in view to the east: the nearest overall.
  CHECK_EQ(pick_card(s, "", 3), 0);
  // The current plane out of view gives way.
  CHECK_EQ(pick_card(s, "s", 1), 1);
}

static void test_names() {
  CHECK(airline_call("AAL2192"));
  CHECK(airline_call("WNG23A"));
  CHECK(!airline_call("N716DP"));
  CHECK(!airline_call("AAL"));
  CHECK(!airline_call("aal1"));
  CHECK(!airline_call(""));
  CHECK(!airline_call(nullptr));

  CHECK_STR(airline_short("AAL2192"), "American");
  CHECK_STR(airline_short("AAL"), "American");
  CHECK_STR(airline_short("SKW6306"), "SkyWest");
  CHECK_STR(airline_short("ENY4010"), "Envoy");
  CHECK_STR(airline_short("JIA5117"), "PSA");
  CHECK_STR(airline_short("FFT1342"), "Frontier");
  CHECK_STR(airline_short("FDX1"), "FedEx");
  CHECK_STR(airline_short("BAW283"), "British");
  CHECK_STR(airline_short("ACA1"), "Air Canada");
  CHECK(airline_short("N716DP") == nullptr);
  CHECK(airline_short("CXK215") == nullptr);
  CHECK(airline_short("AALX") == nullptr);
  CHECK(airline_short("") == nullptr);
  CHECK(airline_short(nullptr) == nullptr);
  // The table: one row a code, names that fit.
  size_t na = sizeof(kAirlines) / sizeof(kAirlines[0]);
  CHECK(na >= 60);
  bool fits = true, unique = true;
  for (size_t i = 0; i < na; i++) {
    fits = fits && strlen(kAirlines[i].icao) == 3 && strlen(kAirlines[i].name) <= 14;
    for (size_t k = i + 1; k < na; k++)
      unique = unique && strcmp(kAirlines[i].icao, kAirlines[k].icao) != 0;
  }
  CHECK(fits);
  CHECK(unique);

  CHECK_STR(plane_type_name("A321", false), "A321");
  CHECK_STR(plane_type_name("A321", true), "Airbus A321");
  CHECK_STR(plane_type_name("B738", false), "737-800");
  CHECK_STR(plane_type_name("B738", true), "Boeing 737-800");
  CHECK_STR(plane_type_name("E75L", false), "E175");
  CHECK_STR(plane_type_name("CRJ9", false), "CRJ-900");
  CHECK_STR(plane_type_name("B38M", true), "Boeing 737 MAX 8");
  CHECK_STR(plane_type_name("ZZZZ", true), "ZZZZ");
  CHECK_STR(plane_type_name("", false), "");
  CHECK_STR(plane_type_name(nullptr, false), "");
  size_t nt = sizeof(kTypes) / sizeof(kTypes[0]);
  CHECK(nt >= 70);
  fits = unique = true;
  for (size_t i = 0; i < nt; i++) {
    fits = fits && strlen(kTypes[i].narrow) <= 9 && strlen(kTypes[i].wide) <= 20;
    for (size_t k = i + 1; k < nt; k++)
      unique = unique && strcmp(kTypes[i].code, kTypes[k].code) != 0;
  }
  CHECK(fits);
  CHECK(unique);

  CHECK(plane_emergency("7700", nullptr));
  CHECK(plane_emergency("7600", "none"));
  CHECK(plane_emergency("7500", "none"));
  CHECK(plane_emergency(nullptr, "general"));
  CHECK(!plane_emergency("1200", "none"));
  CHECK(!plane_emergency("1200", ""));
  CHECK(!plane_emergency(nullptr, nullptr));

  Plane p;
  CHECK_STR(plane_label(p), "");
  strcpy(p.hex, "a1b2c3");
  CHECK_STR(plane_label(p), "a1b2c3");
  strcpy(p.reg, "N1");
  CHECK_STR(plane_label(p), "N1");
  strcpy(p.call, "DAL1");
  CHECK_STR(plane_label(p), "DAL1");
}

static void test_text() {
  char buf[24];
  Plane p;
  CHECK_STR(alt_text(buf, sizeof(buf), p, false), "");
  p.alt_ft = 12400;
  CHECK_STR(alt_text(buf, sizeof(buf), p, false), "12,400 ft");
  CHECK_STR(alt_text(buf, sizeof(buf), p, true), "3,780 m");
  p.alt_ft = 900;
  CHECK_STR(alt_text(buf, sizeof(buf), p, false), "900 ft");
  p.alt_ft = 37000;
  CHECK_STR(alt_text(buf, sizeof(buf), p, true), "11,278 m");
  p.alt_ft = 1234567;
  CHECK_STR(alt_text(buf, sizeof(buf), p, false), "1,234,567 ft");
  p.alt_ft = -200;
  CHECK_STR(alt_text(buf, sizeof(buf), p, false), "-200 ft");
  p.ground = true;
  CHECK_STR(alt_text(buf, sizeof(buf), p, false), "Ground");
  char tiny[5];
  p.ground = false;
  p.alt_ft = 12400;
  CHECK_STR(alt_text(tiny, sizeof(tiny), p, false), "12,4");

  CHECK_STR(speed_text(buf, sizeof(buf), 269.4f, false), "310 mph");
  CHECK_STR(speed_text(buf, sizeof(buf), 269.4f, true), "499 km/h");
  CHECK_STR(speed_text(buf, sizeof(buf), NAN, true), "");

  CHECK_STR(dist_text(buf, sizeof(buf), 3.9f, true), "3.9 km");
  CHECK_STR(dist_text(buf, sizeof(buf), 3.86f, false), "2.4 mi");
  CHECK_STR(dist_text(buf, sizeof(buf), 16.0934f, false), "10 mi");
  CHECK_STR(dist_text(buf, sizeof(buf), 9.94f, true), "9.9 km");
  CHECK_STR(dist_text(buf, sizeof(buf), 9.96f, true), "10 km");
  CHECK_STR(dist_text(buf, sizeof(buf), 0.0f, false), "0.0 mi");

  p.dist_km = 3.86f;
  p.brg_deg = 225;
  CHECK_STR(where_text(buf, sizeof(buf), p, false), "2.4 mi SW");

  CHECK_EQ(vrate_dir(0), 0);
  CHECK_EQ(vrate_dir(249), 0);
  CHECK_EQ(vrate_dir(-249), 0);
  CHECK_EQ(vrate_dir(250), 1);
  CHECK_EQ(vrate_dir(-768), -1);
}

// The page's screens (flights_page.h).
static void test_page() {
  FlPageIn in;
  in.radius = "10 mi";
  FlView v;
  Sky empty;
  fl_compose(in, empty, -1, nullptr, v);
  CHECK(v.screen == FlScreen::MESSAGE);
  CHECK_STR(v.sub, "Set your town on the device page");

  in.home = true;
  fl_compose(in, empty, -1, nullptr, v);
  CHECK_STR(v.sub, "Looking for planes");
  in.err = FlErr::NET;
  fl_compose(in, empty, -1, nullptr, v);
  CHECK_STR(v.sub, "Can't reach flight data");
  in.err = FlErr::LIMIT;
  in.wait_s = 3 * 3600 - 10;
  fl_compose(in, empty, -1, nullptr, v);
  CHECK_STR(v.title, "Flight data paused");
  CHECK_STR(v.sub, "Daily limit, back in 3h");
  in.wait_s = 90;
  fl_compose(in, empty, -1, nullptr, v);
  CHECK_STR(v.sub, "Back in 2 min");
  in.wait_s = 0;
  in.fresh = true;
  fl_compose(in, empty, -1, nullptr, v);
  CHECK_STR(v.sub, "No planes within 10 mi");
  in.err = FlErr::NONE;

  // The card: an airline flight climbing out, with a route that fits.
  Plane a = at("a1b2c3", 3.86f, 225, 90, 310);
  snprintf(a.call, sizeof(a.call), "AAL2192");
  snprintf(a.reg, sizeof(a.reg), "N7605P");
  snprintf(a.type, sizeof(a.type), "B738");
  a.alt_ft = 12400;
  a.vrate_fpm = 1800;
  Plane b = at("d4e5f6", 8, 10, 180, 120);
  snprintf(b.reg, sizeof(b.reg), "N123AB");
  b.alt_ft = 3500;
  Sky s = sky_of({a, b});
  Route r;
  r.found = true;
  snprintf(r.from, sizeof(r.from), "DFW");
  snprintf(r.to, sizeof(r.to), "ORD");
  fl_compose(in, s, 0, &r, v);
  CHECK(v.screen == FlScreen::CARD);
  CHECK_STR(v.call, "AAL2192");
  CHECK_STR(v.airline, "American");
  CHECK_STR(v.line2, "DFW > ORD");
  CHECK(v.route);
  CHECK_STR(v.type, "Boeing 737-800");
  CHECK_STR(v.alt, "12,400 ft  357 mph");
  CHECK_STR(v.where, "2.4 mi SW");
  CHECK_EQ((int) v.vdir, 1);
  CHECK(!v.emergency);
  // No route: the registration. One panel: the narrow names, speed apart.
  in.wide = false;
  fl_compose(in, s, 0, nullptr, v);
  CHECK_STR(v.line2, "N7605P");
  CHECK(!v.route);
  CHECK_STR(v.airline, "");
  CHECK_STR(v.type, "737-800");
  CHECK_STR(v.alt, "12,400 ft");
  CHECK_STR(v.speed, "357 mph");
  // A plane with no callsign goes by its registration, with nothing under it.
  fl_compose(in, s, 1, nullptr, v);
  CHECK_STR(v.call, "N123AB");
  CHECK_STR(v.line2, "");
  in.wide = true;

  // The list's turn: the nearest four, a row each, or two rows on one panel.
  in.list_turn = true;
  fl_compose(in, s, 0, nullptr, v);
  CHECK(v.screen == FlScreen::LIST);
  CHECK_EQ((int) v.rows, 2);
  CHECK_STR(v.row[0], "AAL2192 12,400 2.4 mi");
  CHECK_STR(v.row[1], "N123AB  3,500  5.0 mi");
  in.wide = false;
  fl_compose(in, s, 0, nullptr, v);
  CHECK_STR(v.row[0], "AAL2192");
  CHECK_STR(v.row2[0], "12,400 2.4mi");
  in.wide = true;
  // One plane: the card stays.
  Sky one = sky_of({a});
  fl_compose(in, one, 0, nullptr, v);
  CHECK(v.screen == FlScreen::CARD);

  // About to pass over the house: the card keeps the screen and says when.
  Plane o = at("0a0b0c", 1.0f, 180, 0, 360);
  snprintf(o.call, sizeof(o.call), "SWA100");
  o.alt_ft = 3000;
  o.emergency = true;
  Sky over = sky_of({o, b});
  fl_compose(in, over, 0, nullptr, v);
  CHECK(v.screen == FlScreen::CARD);
  CHECK(v.overhead);
  CHECK(v.emergency);
  CHECK_STR(v.where, "Overhead in 5 s");
  CHECK_EQ((int) v.vdir, 0);
  in.wide = false;
  fl_compose(in, over, 0, nullptr, v);
  CHECK_STR(v.where, "Overhead 5s");

  CHECK_STR(fl_err_word(FlErr::LIMIT), "limit");
  CHECK_STR(fl_err_word(FlErr::NONE), "none");
}

int main() {
  test_urls();
  test_sky_adsblol();
  test_sky_receiver();
  test_routes();
  test_geometry();
  test_cpa();
  test_pick();
  test_names();
  test_text();
  test_page();
  printf("test_flights: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
