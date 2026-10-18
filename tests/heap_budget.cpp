// Memory each ESPN read takes, for the CI budget (firmware/budget.json).
// Runs every parser the panel uses over a fixture like the read it stands for
// and prints, per case, as JSON:
//   json   the most bytes the JSON documents (filter and parse) held at once,
//          through the allocator the firmware points at PSRAM
//   heap   the most bytes everything else held at once (the result vectors
//          and strings), which on the panel come out of internal RAM
// These are host figures (64-bit pointers, glibc), larger than the panel's,
// and the same on every run of the same code, so CI can compare them with a
// budget and with the base branch. scripts/size_budget.py reads the output.
//   make -C tests heap
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "espn_parse.h"
#include "espn_parse_impl.h"

using namespace espn;

// ---- byte counting -------------------------------------------------------------

// Every block carries its size in front, so a free knows what it gives back.
static const size_t kHead = alignof(std::max_align_t);

struct Counter {
  size_t live = 0;
  size_t peak = 0;
  void add(size_t n) {
    live += n;
    if (live > peak)
      peak = live;
  }
  void sub(size_t n) { live -= n; }
  void reset() { peak = live; }
};

static Counter heap_count;  // operator new
static Counter json_count;  // the JSON allocator

static void *counted_alloc(Counter &c, size_t n) {
  char *p = static_cast<char *>(malloc(n + kHead));
  if (p == nullptr)
    return nullptr;
  *reinterpret_cast<size_t *>(p) = n;
  c.add(n);
  return p + kHead;
}

static void counted_free(Counter &c, void *ptr) {
  if (ptr == nullptr)
    return;
  char *p = static_cast<char *>(ptr) - kHead;
  c.sub(*reinterpret_cast<size_t *>(p));
  free(p);
}

static void *counted_realloc(Counter &c, void *ptr, size_t n) {
  if (ptr == nullptr)
    return counted_alloc(c, n);
  char *p = static_cast<char *>(ptr) - kHead;
  size_t old = *reinterpret_cast<size_t *>(p);
  char *q = static_cast<char *>(realloc(p, n + kHead));
  if (q == nullptr)
    return nullptr;
  *reinterpret_cast<size_t *>(q) = n;
  c.sub(old);
  c.add(n);
  return q + kHead;
}

void *operator new(size_t n) {
  void *p = counted_alloc(heap_count, n);
  if (p == nullptr)
    throw std::bad_alloc();
  return p;
}
void *operator new[](size_t n) { return operator new(n); }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return counted_alloc(heap_count, n); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return counted_alloc(heap_count, n); }
void operator delete(void *p) noexcept { counted_free(heap_count, p); }
void operator delete[](void *p) noexcept { counted_free(heap_count, p); }
void operator delete(void *p, size_t) noexcept { counted_free(heap_count, p); }
void operator delete[](void *p, size_t) noexcept { counted_free(heap_count, p); }

struct JsonCounter : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return counted_alloc(json_count, n); }
  void deallocate(void *p) override { counted_free(json_count, p); }
  void *reallocate(void *p, size_t n) override { return counted_realloc(json_count, p, n); }
};

// ---- cases ---------------------------------------------------------------------

static std::string slurp(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static int failures = 0;
static bool first = true;

// Runs one read with the input already in memory (the panel streams it, so
// the input is not counted) and prints its two peaks.
template<typename F> static void measure(const char *name, F read) {
  heap_count.reset();
  json_count.reset();
  size_t heap_before = heap_count.live, json_before = json_count.live;
  bool ok = read();
  if (!ok) {
    fprintf(stderr, "heap_budget: %s did not parse\n", name);
    failures++;
  }
  printf("%s\n  \"%s\": {\"json\": %zu, \"heap\": %zu}", first ? "" : ",", name, json_count.peak - json_before,
         heap_count.peak - heap_before);
  first = false;
}

// The live rotation's scan of one league's day: live games, finals and the
// games still to come, as GamedayComponent::scan_live_() reads it.
static void scan(const char *name, const std::string &file) {
  std::string json = slurp("fixtures/" + file);
  measure(name, [&] {
    std::vector<LiveGame> live;
    std::vector<Final> finals;
    std::vector<Pending> pending;
    int64_t next = 0;
    return parse_live_games(json, live, &next, &finals, &pending);
  });
}

// A single-event game poll. The tracked team is the event's first competitor.
static void event(const char *name, const std::string &file, Sport sport) {
  std::string json = slurp("fixtures/" + file);
  JsonDocument doc;
  deserializeJson(doc, json);
  std::string event_id = doc["id"] | "";
  uint32_t team = (uint32_t) atol(doc["competitions"][0]["competitors"][0]["id"] | "0");
  doc.clear();
  doc.shrinkToFit();
  measure(name, [&] {
    GameSnapshot g;
    return parse_event(json, sport, team, g, event_id);
  });
}

int main() {
  JsonCounter json_alloc;
  set_json_allocator(&json_alloc);
  printf("{");

  scan("scan_ncaa_live", "scoreboard_ncaa_live.json");
  scan("scan_ncaa_day", "day_ncaa_20261017.json");
  scan("scan_nba_day", "day_nba_20261015.json");
  scan("scan_mlb_day", "day_mlb_20261012.json");
  scan("scan_nhl_day", "day_nhl_20261009.json");
  scan("scan_soccer_day", "day_bra_20261012.json");

  // A college football game poll reads the whole day's scoreboard.
  {
    std::string json = slurp("fixtures/scoreboard_ncaa_live.json");
    measure("poll_ncaa_scoreboard", [&] {
      GameSnapshot g;
      return parse_scoreboard(json, std::string("401858433"), 68, g);
    });
  }
  event("poll_nfl_event", "event_nfl_pre.json", Sport::FOOTBALL);
  event("poll_mlb_event", "event_mlb_top.json", Sport::BASEBALL);
  event("poll_nba_event", "event_nba_live.json", Sport::BASKETBALL);
  event("poll_nhl_event", "event_nhl_live.json", Sport::HOCKEY);
  event("poll_soccer_event", "event_soccer_second_half.json", Sport::SOCCER);

  {
    std::string json = slurp("fixtures/team_nfl_dal.json");
    measure("team_nfl", [&] {
      Schedule s;
      return parse_team(json, s);
    });
  }
  {
    std::string json = slurp("fixtures/day_ncaa_20261017.json");
    measure("team_day_ncaa", [&] {
      Schedule s;
      return parse_team_game(json, 251, std::string(), s) == 1;
    });
  }
  {
    std::string json = slurp("fixtures/schedule_nfl_dal.json");
    measure("schedule_nfl", [&] {
      std::vector<Upcoming> up;
      return parse_upcoming(json, 6, 4, up);
    });
  }

  printf("\n}\n");
  set_json_allocator(nullptr);
  return failures == 0 ? 0 : 1;
}
