// Host tests for the logo cache's state machine (components/gameday/logo_cache.cpp),
// built against the stand-ins in logo_stubs/: FreeRTOS, http_request and
// ESPHome 2026.10's RuntimeImage. The logo task runs only when a test says so,
// and a task start can be made to fail, as it does on a full heap.
// Build with `make` in tests/.
#include <cstdio>
#include <string>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "logo_cache.h"

using namespace esphome;
using namespace esphome::gameday;

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

static void reset_fakes() {
  fake_rtos::fail_starts = 0;
  fake_rtos::starts = 0;
  fake_rtos::wakes = 0;
  fake_rtos::woken = false;
  fake_rtos::psram_stack = false;
  fake_clock::now_ms = 0;
  fake_log::warnings = 0;
}

// The logo task's work, which the task does each time it is started or woken.
struct TestLogoCache : LogoCache {
  using LogoCache::run_;
  size_t entries() const { return this->entries_.size(); }
};

// Runs the logo task, if it was started or woken, until it has nothing queued.
static void run_task(TestLogoCache &c) {
  if (!fake_rtos::woken)
    return;
  fake_rtos::woken = false;
  c.run_();
}

static void tick(LogoCache &c, uint32_t ms) {
  fake_clock::now_ms += ms;
  c.loop();
}

static std::string url(int i) { return "http://logos/" + std::to_string(i) + ".png"; }

static void test_download_decode_drops_the_decoder() {
  reset_fakes();
  http_request::HttpRequestComponent http;
  http.replies[url(1)] = {200, "PNG one"};
  TestLogoCache c;
  c.set_http(&http);
  c.setup();
  CHECK(c.get(url(1)) == nullptr);
  CHECK(fake_rtos::starts == 1);
  CHECK(c.waiting(url(1)));
  run_task(c);
  CHECK(c.loop());
  CHECK(c.get(url(1)) != nullptr);
  CHECK(!c.waiting(url(1)));
  // The picture is in; nothing holds a decoder for it.
  CHECK(runtime_image::fake_decoder::live == 0);
  CHECK(c.bytes_downloaded() == 7);
}

static void test_full_cache_holds_no_decoders() {
  reset_fakes();
  http_request::HttpRequestComponent http;
  for (int i = 0; i < 48; i++)
    http.replies[url(i)] = {200, "PNG logo"};
  TestLogoCache c;
  c.set_http(&http);
  c.setup();
  for (int i = 0; i < 48; i++)
    c.get(url(i));
  run_task(c);
  c.loop();
  int ready = 0;
  for (int i = 0; i < 48; i++)
    ready += c.get(url(i)) != nullptr;
  CHECK(ready == 48);
  CHECK(runtime_image::fake_decoder::live == 0);
}

static void test_bad_png_fails_and_frees() {
  reset_fakes();
  http_request::HttpRequestComponent http;
  http.replies[url(1)] = {200, "not a png"};
  TestLogoCache c;
  c.set_http(&http);
  c.setup();
  c.get(url(1));
  run_task(c);
  CHECK(!c.loop());
  CHECK(c.get(url(1)) == nullptr);
  CHECK(!c.waiting(url(1)));  // failed is not on its way
  CHECK(runtime_image::fake_decoder::live == 0);
}

// The task can't start (no room for its stack). The logo stays queued and
// the next second's loop() starts the task again, for a logo asked for by the
// board and for one only prefetched. Once started, the task stays and a new
// logo wakes it.
static void test_failed_task_start_is_retried() {
  reset_fakes();
  http_request::HttpRequestComponent http;
  http.replies[url(1)] = {200, "PNG one"};
  http.replies[url(2)] = {200, "PNG two"};
  http.replies[url(3)] = {200, "PNG three"};
  TestLogoCache c;
  c.set_http(&http);
  c.setup();
  fake_rtos::fail_starts = 1;
  CHECK(c.get(url(1)) == nullptr);
  CHECK(fake_rtos::starts == 0);
  CHECK(fake_log::warnings == 1);
  CHECK(c.waiting(url(1)));
  // Asking again for a queued logo doesn't start anything by itself...
  CHECK(c.get(url(1)) == nullptr);
  CHECK(fake_rtos::starts == 0);
  // ...but the once-a-second check does.
  tick(c, 1000);
  CHECK(fake_rtos::starts == 1);
  CHECK(fake_rtos::psram_stack);
  run_task(c);
  tick(c, 10);
  CHECK(c.get(url(1)) != nullptr);

  c.prefetch(url(2));
  CHECK(fake_rtos::starts == 1);
  CHECK(fake_rtos::wakes == 1);
  run_task(c);
  tick(c, 10);
  CHECK(c.get(url(2)) != nullptr);

  CHECK(c.get(url(3)) == nullptr);
  CHECK(fake_rtos::starts == 1);
  CHECK(fake_rtos::wakes == 2);
  run_task(c);
  tick(c, 10);
  CHECK(c.get(url(3)) != nullptr);
}

// A heap that stays full for a while: one warning, a try every second,
// and the logo lands on the first start that works.
static void test_failed_starts_warn_once() {
  reset_fakes();
  http_request::HttpRequestComponent http;
  http.replies[url(1)] = {200, "PNG one"};
  TestLogoCache c;
  c.set_http(&http);
  c.setup();
  fake_rtos::fail_starts = 5;
  c.get(url(1));
  for (int i = 0; i < 4; i++)
    tick(c, 1000);
  CHECK(fake_rtos::starts == 0);
  CHECK(fake_log::warnings == 1);
  tick(c, 1000);
  CHECK(fake_rtos::starts == 1);
  run_task(c);
  tick(c, 10);
  CHECK(c.get(url(1)) != nullptr);
  // The task stays: the next logo wakes it, nothing starts or warns.
  c.get(url(2));
  CHECK(fake_rtos::starts == 1);
  CHECK(fake_log::warnings == 1);
}

// A failed logo on screen is asked for again once its wait is over; one off
// screen waits for the board to ask.
static void test_failed_logo_on_screen_retries() {
  reset_fakes();
  http_request::HttpRequestComponent http;
  http.replies[url(1)] = {503, ""};
  http.replies[url(2)] = {503, ""};
  TestLogoCache c;
  c.set_http(&http);
  c.setup();
  c.get(url(1));
  c.get(url(2));
  run_task(c);
  CHECK(!c.loop());
  CHECK(!c.waiting(url(1)));
  int requests = http.requests;
  c.pin(url(1), "");
  http.replies[url(1)] = {200, "PNG one"};
  http.replies[url(2)] = {200, "PNG two"};
  tick(c, 11 * 60 * 1000);  // past the longest first wait
  run_task(c);
  tick(c, 10);
  // Only the logo on screen was asked for again.
  CHECK(http.requests == requests + 1);
  CHECK(c.get(url(1)) != nullptr);
}

// A full cache drops the least recently used logo that is idle and off
// screen; a pinned one stays, and a dropped one downloads again when asked.
static void test_full_cache_evicts_lru_not_pinned() {
  reset_fakes();
  http_request::HttpRequestComponent http;
  for (int i = 0; i < 50; i++)
    http.replies[url(i)] = {200, "PNG logo"};
  TestLogoCache c;
  c.set_http(&http);
  c.setup();
  for (int i = 0; i < 48; i++) {
    fake_clock::now_ms = 1000 + i;
    c.get(url(i));
  }
  run_task(c);
  c.loop();
  c.pin(url(0), url(1));
  fake_clock::now_ms = 5000;
  c.get(url(48));  // drops url(2), the oldest not on screen
  run_task(c);
  c.loop();
  int requests = http.requests;
  CHECK(c.get(url(0)) != nullptr);
  CHECK(c.get(url(1)) != nullptr);
  CHECK(c.get(url(48)) != nullptr);
  CHECK(c.get(url(2)) == nullptr);
  run_task(c);
  CHECK(http.requests == requests + 1);
}

// The board asks for logos first up to last - 1; the task doesn't run yet.
static void ask(TestLogoCache &c, int first, int last) {
  for (int i = first; i < last; i++)
    c.get(url(i));
}

static int ready_count(TestLogoCache &c, int n) {
  int ready = 0;
  for (int i = 0; i < n; i++)
    ready += c.get(url(i)) != nullptr;
  return ready;
}

// A full cache never drops a logo that is queued, downloading or waiting to
// be decoded: the logo task holds a pointer to it, and freeing it would have
// the task write into freed memory (the ASan build, test_logo_cache_asan,
// stops there). Each time the 49th logo goes in over the limit instead.
static void test_full_cache_keeps_busy_logos() {
  // Queued: the task hasn't started on any of them.
  {
    reset_fakes();
    http_request::HttpRequestComponent http;
    for (int i = 0; i < 49; i++)
      http.replies[url(i)] = {200, "PNG logo"};
    TestLogoCache c;
    c.set_http(&http);
    c.setup();
    ask(c, 0, 49);
    CHECK(c.entries() == 49);
    run_task(c);
    c.loop();
    CHECK(ready_count(c, 49) == 49);
    CHECK(http.requests == 49);
  }
  // Downloading: the board asks for a 49th logo while the oldest is on its way.
  {
    reset_fakes();
    http_request::HttpRequestComponent http;
    for (int i = 0; i < 49; i++)
      http.replies[url(i)] = {200, "PNG logo"};
    TestLogoCache c;
    c.set_http(&http);
    c.setup();
    ask(c, 0, 48);
    bool asked = false;
    http.on_get = [&](const std::string &u) {
      if (u == url(0) && !asked) {
        asked = true;
        c.get(url(48));
      }
    };
    run_task(c);
    CHECK(asked);
    CHECK(c.entries() == 49);
    c.loop();
    CHECK(ready_count(c, 49) == 49);
    CHECK(http.requests == 49);
  }
  // Downloaded, not decoded yet.
  {
    reset_fakes();
    http_request::HttpRequestComponent http;
    for (int i = 0; i < 49; i++)
      http.replies[url(i)] = {200, "PNG logo"};
    TestLogoCache c;
    c.set_http(&http);
    c.setup();
    ask(c, 0, 48);
    run_task(c);
    c.get(url(48));
    CHECK(c.entries() == 49);
    run_task(c);
    c.loop();
    CHECK(ready_count(c, 49) == 49);
    CHECK(http.requests == 49);
  }
}

// A reply that ends before the length it claims is a cut-off download: the
// logo fails, even though the part that came would decode.
static void test_cut_off_download_fails() {
  reset_fakes();
  http_request::HttpRequestComponent http;
  http.replies[url(1)] = {200, "PNG half", 4000};
  TestLogoCache c;
  c.set_http(&http);
  c.setup();
  c.get(url(1));
  run_task(c);
  CHECK(!c.loop());
  CHECK(c.get(url(1)) == nullptr);
  CHECK(!c.waiting(url(1)));
  CHECK(c.bytes_downloaded() == 8);
}

// The board asking again for a failed logo off screen waits out the logo's
// own wait (15 s after the first failure here) before it goes out again.
static void test_failed_logo_waits_before_asking_again() {
  reset_fakes();
  http_request::HttpRequestComponent http;
  http.replies[url(1)] = {503, ""};
  TestLogoCache c;
  c.set_http(&http);
  c.setup();
  c.get(url(1));
  run_task(c);
  c.loop();
  CHECK(http.requests == 1);
  http.replies[url(1)] = {200, "PNG one"};
  tick(c, 14999);
  CHECK(c.get(url(1)) == nullptr);
  CHECK(!c.waiting(url(1)));
  run_task(c);
  CHECK(http.requests == 1);
  tick(c, 1);
  CHECK(c.get(url(1)) == nullptr);
  run_task(c);
  CHECK(http.requests == 2);
  tick(c, 10);
  CHECK(c.get(url(1)) != nullptr);
}

// A prefetch never makes room: with 48 logos in, it does nothing, and every
// logo stays.
static void test_prefetch_stops_at_a_full_cache() {
  reset_fakes();
  http_request::HttpRequestComponent http;
  for (int i = 0; i < 49; i++)
    http.replies[url(i)] = {200, "PNG logo"};
  TestLogoCache c;
  c.set_http(&http);
  c.setup();
  ask(c, 0, 47);
  c.prefetch(url(47));  // the 48th still fits
  run_task(c);
  c.loop();
  CHECK(c.entries() == 48);
  CHECK(http.requests == 48);
  c.prefetch(url(48));
  run_task(c);
  c.loop();
  CHECK(c.entries() == 48);
  CHECK(http.requests == 48);
  CHECK(ready_count(c, 48) == 48);
  CHECK(http.requests == 48);
}

// A dark logo ESPN doesn't have (404) is the plain one, kept under the dark
// URL so nothing asks for the missing one again. Only a 404 falls back.
static void test_missing_dark_logo_uses_the_plain_one() {
  const std::string dark = "https://a.espncdn.com/i/teamlogos/soccer/500-dark/436.png";
  const std::string plain = "https://a.espncdn.com/i/teamlogos/soccer/500/436.png";
  {
    reset_fakes();
    http_request::HttpRequestComponent http;
    http.replies[dark] = {404, ""};
    http.replies[plain] = {200, "PNG plain"};
    TestLogoCache c;
    c.set_http(&http);
    c.setup();
    c.get(dark);
    run_task(c);
    CHECK(c.loop());
    CHECK(c.get(dark) != nullptr);
    CHECK(http.asked.size() == 2 && http.asked[0] == dark && http.asked[1] == plain);
    run_task(c);
    CHECK(http.requests == 2);
    CHECK(c.entries() == 1);
  }
  // Neither there: one try each, then it fails.
  {
    reset_fakes();
    http_request::HttpRequestComponent http;
    http.replies[dark] = {404, ""};
    http.replies[plain] = {404, ""};
    TestLogoCache c;
    c.set_http(&http);
    c.setup();
    c.get(dark);
    run_task(c);
    CHECK(!c.loop());
    CHECK(!c.waiting(dark));
    CHECK(http.requests == 2);
  }
  // A server error is not a missing logo.
  {
    reset_fakes();
    http_request::HttpRequestComponent http;
    http.replies[dark] = {503, ""};
    http.replies[plain] = {200, "PNG plain"};
    TestLogoCache c;
    c.set_http(&http);
    c.setup();
    c.get(dark);
    run_task(c);
    CHECK(!c.loop());
    CHECK(http.asked.size() == 1 && http.asked[0] == dark);
  }
}

int main() {
  test_download_decode_drops_the_decoder();
  test_full_cache_holds_no_decoders();
  test_bad_png_fails_and_frees();
  test_failed_task_start_is_retried();
  test_failed_starts_warn_once();
  test_failed_logo_on_screen_retries();
  test_full_cache_evicts_lru_not_pinned();
  test_full_cache_keeps_busy_logos();
  test_cut_off_download_fails();
  test_failed_logo_waits_before_asking_again();
  test_prefetch_stops_at_a_full_cache();
  test_missing_dark_logo_uses_the_plain_one();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
