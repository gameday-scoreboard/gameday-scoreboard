// The device API's shape against tests/fixtures/api/, which the companion
// app's tests read. Fails when the firmware sends a key, takes a set
// parameter or handles an action the fixtures lack, and when a fixture lists
// one the firmware no longer has.
//
// rebuild_state_ and handle_wifi_ need the whole ESPHome component, so their
// keys come from gameday.cpp's source: every string-literal write
// (doc["key"], o["key"]) in the function, with the path each JsonObject and
// JsonArray variable stands for, and the keys of the static JsonObject
// helpers it calls (put_team). The health and wifi objects come from the real
// health.h, guard.h and wifi_history.h with every optional key on. The set
// parameters are set_request.h's kSetKeys. Paths read "game.s", "favs[].abbr"
// ([] is any element of an array).
#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "guard.h"
#include "health.h"
#include "set_request.h"
#include "wifi_history.h"

namespace {

int checks = 0;
int failures = 0;

#define SCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

const char *const kSource = "../components/gameday/gameday.cpp";
const char *const kHint = "(the companion app's tests read this file)";

std::string slurp(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Line comments out, so a key named in a comment doesn't count. String
// literals are skipped over whole, for the "//" in a URL.
std::string strip_comments(const std::string &src) {
  std::string out;
  out.reserve(src.size());
  for (size_t i = 0; i < src.size(); i++) {
    char c = src[i];
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < src.size() && src[j] != c) {
        if (src[j] == '\\')
          j++;
        j++;
      }
      out.append(src, i, j - i + 1);
      i = j;
    } else if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
      while (i < src.size() && src[i] != '\n')
        i++;
      out += '\n';
    } else {
      out += c;
    }
  }
  return out;
}

// A function's text, from its signature to the closing brace in column 0.
std::string function_body(const std::string &src, const std::string &signature) {
  size_t start = src.find(signature);
  if (start == std::string::npos)
    return "";
  size_t end = src.find("\n}\n", start);
  return src.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::string join(const std::string &base, const std::string &key) { return base.empty() ? key : base + "." + key; }

// ["a"]["b"] to {"a", "b"}
std::vector<std::string> subscripts(const std::string &s) {
  std::vector<std::string> out;
  static const std::regex re(R"re(\["(\w+)"\])re");
  for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it)
    out.push_back((*it)[1]);
  return out;
}

using Helpers = std::map<std::string, std::set<std::string>>;

// The keys a function writes, as paths from `root` (the JsonDocument).
// unknown collects variables written with a key that the scan can't place.
std::set<std::string> written_keys(const std::string &body, const std::string &root, const Helpers &helpers,
                                   std::set<std::string> &unknown) {
  std::set<std::string> keys;
  std::map<std::string, std::string> vars{{root, ""}};
  std::string calls;
  for (const auto &h : helpers)
    calls += (calls.empty() ? "" : "|") + h.first;
  // 1-5: JsonObject x = y["a"].to<JsonArray>() or = y.add<JsonObject>()
  // 6-7: a write, y["a"] or y["a"]["b"]
  // 8-9: a helper call, put_team(y, ...)
  const std::regex re(R"re(\b(JsonObject|JsonArray)\s+(\w+)\s*=\s*(\w+)((?:\["\w+"\])*)\s*\.(to<(?:JsonObject|JsonArray)>|add<JsonObject>)\(\))re"
                      R"re(|\b(\w+)((?:\["\w+"\])+))re"
                      R"re(|\b()re" + (calls.empty() ? std::string("$^") : calls) + R"re()\(\s*(\w+)\s*,)re");
  for (std::sregex_iterator it(body.begin(), body.end(), re), end; it != end; ++it) {
    const std::smatch &m = *it;
    if (m[2].matched) {
      auto v = vars.find(m[3]);
      if (v == vars.end()) {
        unknown.insert(m[3]);
        continue;
      }
      std::string path = v->second;
      for (const auto &k : subscripts(m[4])) {
        path = join(path, k);
        keys.insert(path);
      }
      if (m[5] == "to<JsonArray>")
        path += "[]";
      vars[m[2]] = path;
    } else if (m[6].matched) {
      auto v = vars.find(m[6]);
      if (v == vars.end()) {
        unknown.insert(m[6]);
        continue;
      }
      std::string path = v->second;
      for (const auto &k : subscripts(m[7])) {
        path = join(path, k);
        keys.insert(path);
      }
    } else if (m[8].matched) {
      auto v = vars.find(m[9]);
      if (v == vars.end()) {
        unknown.insert(m[9]);
        continue;
      }
      for (const auto &k : helpers.at(m[8]))
        keys.insert(join(v->second, k));
    }
  }
  return keys;
}

// gameday.cpp's static helpers that fill a JsonObject, with the keys each writes.
Helpers object_helpers(const std::string &src) {
  Helpers out;
  static const std::regex re(R"re(\nstatic void (\w+)\(JsonObject (\w+)\b)re");
  for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end; ++it) {
    std::set<std::string> unknown;
    std::string body = function_body(src, (*it)[0].str().substr(1));
    out[(*it)[1]] = written_keys(body, (*it)[2], {}, unknown);
  }
  return out;
}

// Every key path in a JSON document, in the notation above.
void fixture_keys(JsonVariantConst v, const std::string &prefix, std::set<std::string> &out) {
  if (v.is<JsonObjectConst>()) {
    for (JsonPairConst p : v.as<JsonObjectConst>()) {
      std::string path = join(prefix, p.key().c_str());
      out.insert(path);
      if (p.value().is<JsonArrayConst>()) {
        for (JsonVariantConst e : p.value().as<JsonArrayConst>())
          fixture_keys(e, path + "[]", out);
      } else {
        fixture_keys(p.value(), path, out);
      }
    }
  }
}

bool load_fixture(const char *path, JsonDocument &doc) {
  std::string text = slurp(path);
  DeserializationError err = deserializeJson(doc, text);
  SCHECK(!text.empty() && !err, "%s doesn't read as JSON (%s)", path, err.c_str());
  return !text.empty() && !err;
}

std::set<std::string> string_array(const char *path) {
  std::set<std::string> out;
  JsonDocument doc;
  if (!load_fixture(path, doc))
    return out;
  SCHECK(doc.is<JsonArrayConst>(), "%s should be a JSON array of names", path);
  for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
    SCHECK(v.is<const char *>(), "%s should hold only strings", path);
    if (v.is<const char *>())
      out.insert(v.as<const char *>());
  }
  return out;
}

// what: "key" or "set parameter"; firmware and fixture name the same things.
void compare(const char *fixture, const char *what, const std::set<std::string> &firmware,
             const std::set<std::string> &listed) {
  for (const auto &k : firmware)
    SCHECK(listed.count(k), "the firmware has %s %s, missing from the fixture: add it to %s %s", what, k.c_str(),
           fixture, kHint);
  for (const auto &k : listed)
    SCHECK(firmware.count(k), "%s lists %s %s, which the firmware no longer has: take it out %s", fixture, what,
           k.c_str(), kHint);
}

// The health object with every optional key: a fault with its age, Wi-Fi up,
// an ESPN read, a temperature. Spliced the way health_json_ does.
std::string full_health_json() {
  espn::Health h{};
  h.uptime_s = 3600;
  h.start = espn::Restart::RESTART;
  h.boots = 2;
  h.fault = espn::Restart::CRASH;
  h.fault_ago = 7200;
  h.heap_kb = 90.0f;
  h.block_kb = 30.0f;
  h.psram_kb = 5700.0f;
  h.wifi = true;
  h.rssi = -55;
  h.espn_status = 200;
  h.espn_ago = 5;
  h.version = "2026.10.0";
  h.built = 1792000000;
  h.channel = "stable";
  char buf[384];
  int n = espn::health_json(buf, sizeof(buf), h);
  espn::PowerLog log{};
  char guard[96];
  int g = espn::guard_json_keys(guard, sizeof(guard), 41.5f, 0, log);
  SCHECK(n > 0 && n < (int) sizeof(buf) && g > 0 && g < (int) sizeof(guard), "health JSON didn't fit");
  std::string out(buf, n - 1);
  out.append(guard, g);
  out += '}';
  return out;
}

// The wifi object with every optional key: connected, a drop of each counted
// cause in the last day, the newest one over.
std::string full_wifi_json() {
  static int8_t rssi[espn::kWifiMinutes];
  static espn::WifiDrop drops[espn::kWifiDrops];
  espn::WifiHistory w;
  w.attach(rssi, drops);
  uint32_t t = 0;
  w.up(t);
  const espn::WifiCause causes[] = {espn::WifiCause::SIGNAL, espn::WifiCause::ROUTER, espn::WifiCause::PASSWORD,
                                    espn::WifiCause::ROAM, espn::WifiCause::OTHER};
  for (espn::WifiCause c : causes) {
    for (int i = 0; i < 12; i++, t += 10)
      w.sample(t, true, -60);
    w.down(t, c);
    t += 30;
    w.up(t);
  }
  w.sample(t + 60, true, -60);
  return w.json(t + 60, true, -60);
}

void test_state() {
  const std::string src = strip_comments(slurp(kSource));
  SCHECK(!src.empty(), "can't read %s", kSource);
  const Helpers helpers = object_helpers(src);
  SCHECK(helpers.count("put_team") && !helpers.at("put_team").empty(), "found no keys in put_team");

  std::set<std::string> unknown;
  const std::string body = function_body(src, "void GamedayComponent::rebuild_state_(");
  SCHECK(!body.empty(), "can't find rebuild_state_ in %s; update test_api.cpp", kSource);
  std::set<std::string> firmware = written_keys(body, "doc", helpers, unknown);
  for (const auto &v : unknown)
    SCHECK(false, "rebuild_state_ writes keys through %s, which test_api.cpp can't follow; teach written_keys it",
           v.c_str());

  // GET /gameday/state appends objects to the cached document: ,"health":...
  size_t at = src.find("StringRef(\"/gameday/state\")");
  size_t next = src.find("StringRef(\"/gameday/", at + 1);
  SCHECK(at != std::string::npos, "can't find the /gameday/state route in %s; update test_api.cpp", kSource);
  std::set<std::string> appended;
  if (at != std::string::npos) {
    const std::string route = src.substr(at, next == std::string::npos ? std::string::npos : next - at);
    static const std::regex re(R"re(\\"(\w+)\\":)re");
    for (std::sregex_iterator it(route.begin(), route.end(), re), end; it != end; ++it)
      appended.insert((*it)[1]);
  }
  SCHECK(appended.count("health") && appended.count("wifi"), "the /gameday/state route no longer appends health and wifi");
  for (const auto &k : appended)
    firmware.insert(k);
  // Their contents, from the headers that write them
  const std::pair<const char *, std::string> parts[] = {{"health", full_health_json()}, {"wifi", full_wifi_json()}};
  for (const auto &p : parts) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, p.second);
    SCHECK(!err, "the %s object doesn't read as JSON: %s", p.first, p.second.c_str());
    fixture_keys(doc.as<JsonVariantConst>(), p.first, firmware);
  }
  SCHECK(firmware.size() > 100 && firmware.count("game.s") && firmware.count("next[].t.abbr") &&
             firmware.count("health.fault_ago") && firmware.count("wifi.last_down"),
         "the source scan found only %zu keys; update test_api.cpp", firmware.size());

  JsonDocument doc;
  std::set<std::string> listed;
  if (load_fixture("fixtures/api/state.json", doc))
    fixture_keys(doc.as<JsonVariantConst>(), "", listed);
  compare("tests/fixtures/api/state.json", "key", firmware, listed);
}

void test_wifi_route() {
  const std::string src = strip_comments(slurp(kSource));
  const std::string body = function_body(src, "void GamedayComponent::handle_wifi_(");
  SCHECK(!body.empty(), "can't find handle_wifi_ in %s; update test_api.cpp", kSource);
  std::set<std::string> unknown;
  std::set<std::string> firmware = written_keys(body, "doc", {}, unknown);
  for (const auto &v : unknown)
    SCHECK(false, "handle_wifi_ writes keys through %s, which test_api.cpp can't follow", v.c_str());
  SCHECK(firmware.count("networks[].ssid"), "the source scan found no network keys in handle_wifi_");
  JsonDocument doc;
  std::set<std::string> listed;
  if (load_fixture("fixtures/api/wifi.json", doc))
    fixture_keys(doc.as<JsonVariantConst>(), "", listed);
  compare("tests/fixtures/api/wifi.json", "key", firmware, listed);
}

void test_set_params() {
  std::set<std::string> firmware(std::begin(espn::kSetKeys), std::end(espn::kSetKeys));
  compare("tests/fixtures/api/set_params.json", "set parameter", firmware,
          string_array("fixtures/api/set_params.json"));
}

// The verbs the /gameday/action route compares `name` with, demo builds' too.
void test_actions() {
  const std::string src = strip_comments(slurp(kSource));
  size_t at = src.find("StringRef(\"/gameday/action\")");
  size_t next = src.find("StringRef(\"/gameday/", at + 1);
  SCHECK(at != std::string::npos, "can't find the /gameday/action route in %s; update test_api.cpp", kSource);
  std::set<std::string> firmware;
  if (at != std::string::npos) {
    const std::string route = src.substr(at, next == std::string::npos ? std::string::npos : next - at);
    static const std::regex re(R"re(\bname\s*==\s*"(\w+)")re");
    for (std::sregex_iterator it(route.begin(), route.end(), re), end; it != end; ++it)
      firmware.insert((*it)[1]);
  }
  SCHECK(firmware.count("refresh"), "the source scan found no action verbs; update test_api.cpp");
  compare("tests/fixtures/api/actions.json", "action", firmware, string_array("fixtures/api/actions.json"));
}

}  // namespace

int main() {
  test_state();
  test_wifi_route();
  test_set_params();
  test_actions();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
