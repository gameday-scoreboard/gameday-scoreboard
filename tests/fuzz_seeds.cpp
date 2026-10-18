// Builds the fuzz seeds from tests/fixtures: each ESPN fixture cut down to
// the fields one parser's filter keeps, with at most a few events, so a
// mutation lands on something a parser reads instead of one of the hundreds
// of fields it skips. The set requests come from set_cases.json.
//   fuzz_seeds <fixtures folder> <espn seed folder> <set seed folder>
#include <ArduinoJson.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>

#include "espn_parse.h"

using namespace espn;
namespace fs = std::filesystem;

static std::string slurp(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static const size_t kMaxEvents = 4;

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: fuzz_seeds <fixtures> <espn out> <set out>\n");
    return 2;
  }
  fs::path fixtures = argv[1], espn_out = argv[2], set_out = argv[3];
  fs::create_directories(espn_out);
  fs::create_directories(set_out);

  std::vector<std::function<void(JsonDocument &)>> filters = {
      [](JsonDocument &f) {
        JsonObject ev = f.to<JsonObject>();
        espn::detail::fill_event_filter(ev);
        espn::detail::fill_baseball_filter(ev);
        espn::detail::fill_soccer_filter(ev);
        espn::detail::fill_basketball_filter(ev);
        espn::detail::fill_hockey_filter(ev);
      },
      [](JsonDocument &f) { espn::detail::fill_scoreboard_filter(f); },
      [](JsonDocument &f) { espn::detail::fill_scan_filter(f, true, true); },
      [](JsonDocument &f) { espn::detail::fill_team_filter(f); },
      [](JsonDocument &f) { espn::detail::fill_upcoming_filter(f); },
  };
  std::set<std::string> seen;
  size_t espn_seeds = 0;
  for (const auto &e : fs::directory_iterator(fixtures)) {
    if (e.path().extension() != ".json" || e.path().filename() == "set_cases.json")
      continue;
    std::string json = slurp(e.path());
    for (size_t i = 0; i < filters.size(); i++) {
      JsonDocument filter, doc;
      filters[i](filter);
      if (deserializeJson(doc, json, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(40)))
        continue;
      JsonArray events = doc["events"];
      while (events.size() > kMaxEvents)
        events.remove(kMaxEvents);
      std::string out;
      serializeJson(doc, out);
      if (out.size() < 32 || !seen.insert(out).second)
        continue;
      std::ofstream(espn_out / (e.path().stem().string() + "_" + std::to_string(i) + ".json"), std::ios::binary)
          << out;
      espn_seeds++;
    }
  }

  JsonDocument cases;
  if (deserializeJson(cases, slurp(fixtures / "set_cases.json"))) {
    fprintf(stderr, "set_cases.json does not parse\n");
    return 1;
  }
  size_t set_seeds = 0;
  for (JsonObjectConst c : cases["cases"].as<JsonArrayConst>()) {
    std::string body;
    for (JsonArrayConst kv : c["set"].as<JsonArrayConst>())
      body += (body.empty() ? "" : "&") + std::string(kv[0] | "") + "=" + std::string(kv[1] | "");
    std::ofstream(set_out / ("case_" + std::to_string(set_seeds++) + ".txt"), std::ios::binary) << body;
  }
  printf("%zu ESPN seeds, %zu set seeds\n", espn_seeds, set_seeds);
  return 0;
}
