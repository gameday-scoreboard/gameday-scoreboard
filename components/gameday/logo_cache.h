// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/runtime_image/runtime_image.h"
#include "esphome/core/static_task.h"
#include "logo_shrink.h"
#include "psram.h"

namespace esphome::gameday {

using ::espn::psram_alloc;
using ::espn::psram_free;
using ::espn::PsramAllocator;

// Team logos, downloaded and decoded once, then kept in PSRAM until the
// panel restarts. Downloads run on their own task so a slow logo server can
// only delay a logo, never the main loop. Decoding stays on the main loop
// because ESPHome's PNG decoder feeds the main loop's watchdog.
//
// The logo task starts with the first logo and then waits for the next one,
// its stack in PSRAM. A list of 16 favorites queues 30 or more logos while
// the ESPN worker reads one favorite after another, so with its stack in
// internal RAM the two 16 KB stacks were up at once for the whole rescan and
// left too small a block for the next worker. The task only downloads into
// PSRAM, and a task that never writes flash may keep its stack there.
//
// Everything a logo allocates goes to PSRAM, not just its pixels. Plain new
// and malloc only use internal RAM in this build, so the PNG download and
// each entry's small objects used to land there, and a full cache broke
// internal RAM into pieces too small for the 16 KB fetch task stack it then
// held.
class LogoCache {
 public:
  void set_http(http_request::HttpRequestComponent *http) { this->http_ = http; }
  void setup();
  // Any task. Starts downloading a logo the board is about to ask for.
  void prefetch(const std::string &url);

  // The rest is main loop only.

  // The decoded logo, or nullptr while it is still missing; a miss starts a download.
  image::Image *get(const std::string &url);
  // True while the logo is still on its way. A miss starts it, as get() does;
  // a failed logo is not on its way.
  bool waiting(const std::string &url);
  // The two URLs about to be shown. They and the two before them are never
  // dropped, so nothing LVGL may still point at is freed.
  void pin(const std::string &team, const std::string &opponent);
  // Decodes finished downloads, retries failed logos that are on screen and
  // starts the logo task again if it could not start.
  // True when at least one logo became ready.
  bool loop();
  // Logo bytes downloaded since boot, any task.
  uint32_t bytes_downloaded() const { return this->bytes_downloaded_.load(); }

 protected:
  using Bytes = std::vector<uint8_t, PsramAllocator<uint8_t>>;
  using Url = std::basic_string<char, std::char_traits<char>, PsramAllocator<char>>;

  // ESPHome's RuntimeImage (2026.10) keeps its decoder after a decode so the
  // next one can reuse it. A cached logo is decoded once, and a PNG decoder
  // holds about 44 KB of PSRAM (pngle's inflate dictionary), so it goes with
  // the decode.
  struct Logo : runtime_image::RuntimeImage {
    using RuntimeImage::RuntimeImage;
    void drop_decoder() { this->decoder_.reset(); }
    // Shows 32x32 pixels the cache shrank itself (0xAARRGGBB, row by row),
    // the way end_decode() shows a decoded picture. False when the buffer
    // could not be allocated.
    bool paint(const uint32_t *px);
  };

  // DECODING: the main loop has the body and is decoding or shrinking it.
  enum class State : uint8_t { QUEUED, LOADING, DOWNLOADED, DECODING, READY, FAILED };
  struct Entry {
    static void *operator new(size_t size) { return psram_alloc(size); }
    static void operator delete(void *p) { psram_free(p); }

    Url url;
    Bytes body;  // the PNG, from the task to the main loop
    std::optional<Logo> image;
    State state{State::QUEUED};
    uint8_t fails{0};  // failures in a row, for the wait
    uint32_t used_ms{0};
    uint32_t failed_ms{0};
    uint32_t wait_ms{0};  // how long after failed_ms to ask again
  };

  // A logo being shrunk from its full size (logo_shrink.h), one step each
  // loop() pass: the decode, which stops itself after 15 ms a call, then the
  // three steps of the vote. Done at once, a 128 px logo held the main loop
  // for about 95 ms. One PSRAM block holds the full-size picture, the vote's
  // scratch and the 32 px result; it and the decoder go when this does.
  // Main loop only.
  struct Shrink {
    static void *operator new(size_t size) { return psram_alloc(size); }
    static void operator delete(void *p) { psram_free(p); }
    Shrink() : src(runtime_image::PNG, image::IMAGE_TYPE_RGB, image::TRANSPARENCY_ALPHA_CHANNEL) {}
    ~Shrink();
    uint32_t *scratch() const { return reinterpret_cast<uint32_t *>(this->mem + (size_t) this->size * this->size * 4); }
    uint32_t *out() const { return this->scratch() + ::espn::shrink::scratch_words(this->size); }

    Entry *job{nullptr};  // DECODING while this lives, so nothing drops it
    Bytes body;
    runtime_image::RuntimeImage src;
    uint8_t *mem{nullptr};
    int size{0};
    size_t offset{0};  // bytes of body the decoder has taken
    std::optional<::espn::shrink::RareVote> vote;
    uint32_t started_ms{0};
    uint16_t passes{0};
  };

  static void task_fn_(void *arg);
  void run_();
  bool download_(const std::string &url, Bytes &body, bool &throttled, uint32_t &retry_after_ms);
  bool decode_(std::optional<Logo> &image, const Bytes &body);
  bool start_shrink_(Entry *job, Bytes &body);
  bool shrink_step_();
  bool finish_(Entry *job, bool ok);
  void make_room_();
  void start_task_();
  bool pinned_url_(std::string_view url) const;
  void retry_(uint32_t now);

  http_request::HttpRequestComponent *http_{nullptr};
  // Guards entries_ and every Entry's state and body.
  SemaphoreHandle_t lock_{nullptr};
  std::vector<std::unique_ptr<Entry>> entries_;
  std::string pinned_[4];  // current pair, then the pair before
  uint32_t retry_check_ms_{0};
  StaticTask task_;
  uint16_t start_fails_{0};  // failed task starts in a row
  std::atomic<bool> downloaded_{false};
  std::atomic<uint32_t> bytes_downloaded_{0};
  std::unique_ptr<Shrink> shrink_;  // main loop only
};

}  // namespace esphome::gameday
