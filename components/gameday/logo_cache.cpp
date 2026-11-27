// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#include "logo_cache.h"

#include <cstring>

#include "esphome/components/runtime_image/image_decoder.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "logo_lru.h"
#include "logo_shrink.h"

namespace esphome::gameday {

static const char *const TAG = "gameday.logo";
static const char *const USER_AGENT = "curl/8.0";
// About four logos per team on screen at once in the busiest playlist, times
// a full favorites list plus live games, with room to spare. Each is ~3 KB.
static const size_t MAX_LOGOS = 48;
// ESPN's PNGs are a few KB; anything far bigger is not a logo.
static const size_t MAX_BODY = 128 * 1024;
static const int LOGO_SIZE = 32;

class Locked {
 public:
  explicit Locked(SemaphoreHandle_t lock) : lock_(lock) { xSemaphoreTake(lock_, portMAX_DELAY); }
  ~Locked() { xSemaphoreGive(lock_); }

 private:
  SemaphoreHandle_t lock_;
};

void LogoCache::setup() {
  this->lock_ = xSemaphoreCreateMutex();
  this->entries_.reserve(MAX_LOGOS);
}

image::Image *LogoCache::get(const std::string &url) {
  if (url.empty() || this->lock_ == nullptr)
    return nullptr;
  Locked guard(this->lock_);
  uint32_t now = millis();
  for (auto &e : this->entries_) {
    if (std::string_view(e->url) != url)
      continue;
    e->used_ms = now;
    if (e->state == State::READY)
      return &*e->image;
    if (e->state == State::FAILED && ::espn::logo_retry_due(e->failed_ms, e->wait_ms, now)) {
      e->state = State::QUEUED;
      this->start_task_();
    }
    return nullptr;
  }
  this->make_room_();
  auto e = std::make_unique<Entry>();
  e->url.assign(url.data(), url.size());
  e->used_ms = now;
  this->entries_.push_back(std::move(e));
  this->start_task_();
  return nullptr;
}

bool LogoCache::waiting(const std::string &url) {
  if (url.empty() || this->lock_ == nullptr || this->get(url) != nullptr)
    return false;
  Locked guard(this->lock_);
  for (auto &e : this->entries_) {
    if (std::string_view(e->url) == url)
      return e->state != State::FAILED;
  }
  return false;
}

// Never drops a logo to make room, since only the main loop knows what LVGL
// is drawing; a full cache skips the head start and get() fetches it later.
void LogoCache::prefetch(const std::string &url) {
  if (url.empty() || this->lock_ == nullptr)
    return;
  Locked guard(this->lock_);
  for (auto &e : this->entries_) {
    if (std::string_view(e->url) == url)
      return;
  }
  if (this->entries_.size() >= MAX_LOGOS)
    return;
  auto e = std::make_unique<Entry>();
  e->url.assign(url.data(), url.size());
  e->used_ms = millis();
  this->entries_.push_back(std::move(e));
  this->start_task_();
}

void LogoCache::pin(const std::string &team, const std::string &opponent) {
  if (this->lock_ == nullptr || (team == this->pinned_[0] && opponent == this->pinned_[1]))
    return;
  Locked guard(this->lock_);
  this->pinned_[2] = std::move(this->pinned_[0]);
  this->pinned_[3] = std::move(this->pinned_[1]);
  this->pinned_[0] = team;
  this->pinned_[1] = opponent;
}

bool LogoCache::pinned_url_(std::string_view url) const {
  for (const auto &p : this->pinned_) {
    if (!p.empty() && p == url)
      return true;
  }
  return false;
}

// Main loop, once a second. Asks again for a failed logo that is still on
// screen, and starts the logo task again for anything still queued after it
// could not start, so neither waits for the next board update, which can be
// hours away.
void LogoCache::retry_(uint32_t now) {
  if (now - this->retry_check_ms_ < 1000)
    return;
  this->retry_check_ms_ = now;
  Locked guard(this->lock_);
  bool queued = false;
  for (auto &e : this->entries_) {
    if (e->state == State::FAILED && this->pinned_url_(e->url) &&
        ::espn::logo_retry_due(e->failed_ms, e->wait_ms, now))
      e->state = State::QUEUED;
    queued |= e->state == State::QUEUED;
  }
  if (queued)
    this->start_task_();
}

// Caller holds the lock. Drops the least recently used logo that is idle and
// off screen, so the image it frees is not one LVGL is drawing.
void LogoCache::make_room_() {
  if (this->entries_.size() < MAX_LOGOS)
    return;
  std::vector<::espn::LogoSlot> slots;
  slots.reserve(this->entries_.size());
  for (auto &e : this->entries_) {
    bool busy = e->state == State::QUEUED || e->state == State::LOADING || e->state == State::DOWNLOADED;
    bool pinned = this->pinned_url_(e->url);
    slots.push_back({e->used_ms, busy, pinned});
  }
  int i = ::espn::pick_evict(slots, millis());
  if (i >= 0)
    this->entries_.erase(this->entries_.begin() + i);
}

// Caller holds the lock. Wakes the logo task, starting it the first time.
// When it can't start (no PSRAM left for its stack), retry_() tries again a
// second later.
void LogoCache::start_task_() {
  if (this->task_.is_created()) {
    xTaskNotifyGive(this->task_.get_handle());
    return;
  }
  // Same stack as the ESPN worker: the TLS handshake needs it.
  if (!this->task_.create(&LogoCache::task_fn_, "gameday_logo", 16384, this, 1, true)) {
    if (this->start_fails_ == 0)
      ESP_LOGW(TAG, "Could not start the logo task (largest free PSRAM block %u bytes)",
               (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    if (this->start_fails_ < UINT16_MAX)
      this->start_fails_++;
    return;
  }
  if (this->start_fails_ > 0)
    ESP_LOGI(TAG, "Started the logo task after %u failed tries", (unsigned) this->start_fails_);
  this->start_fails_ = 0;
}

void LogoCache::task_fn_(void *arg) {
  auto *self = static_cast<LogoCache *>(arg);
  while (true) {
    self->run_();
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}

// Logo task: works through queued logos one at a time, then goes back to
// waiting. It only touches an entry while it is LOADING, and nothing drops a
// busy entry. A logo queued while it works wakes it again, so none waits.
void LogoCache::run_() {
  while (true) {
    Entry *job = nullptr;
    std::string url;
    {
      Locked guard(this->lock_);
      for (auto &e : this->entries_) {
        if (e->state == State::QUEUED) {
          job = e.get();
          break;
        }
      }
      if (job == nullptr)
        return;
      job->state = State::LOADING;
      url.assign(job->url.data(), job->url.size());
    }
    Bytes body;
    bool throttled = false;
    uint32_t retry_after_ms = 0;
    uint32_t start = millis();
    bool ok = this->download_(url, body, throttled, retry_after_ms);
    size_t bytes = body.size();
    {
      Locked guard(this->lock_);
      if (ok) {
        job->body = std::move(body);
        job->state = State::DOWNLOADED;
      } else {
        job->state = State::FAILED;
        job->failed_ms = millis();
        ::espn::logo_failed(job->fails, job->wait_ms, throttled, retry_after_ms, random_uint32());
      }
    }
    if (ok) {
      ESP_LOGD(TAG, "Downloaded %s (%u bytes, %u ms)", url.c_str(), (unsigned) bytes, (unsigned) (millis() - start));
    } else {
      ESP_LOGW(TAG, "Logo download failed after %u ms: %s", (unsigned) (millis() - start), url.c_str());
    }
    this->downloaded_ = true;
  }
}

// On a refusal (403 or 429) sets throttled, and retry_after_ms when the reply
// carried a plain Retry-After.
bool LogoCache::download_(const std::string &url, Bytes &body, bool &throttled, uint32_t &retry_after_ms) {
  std::vector<http_request::Header> headers = {
      {"User-Agent", USER_AGENT},
      {"Accept", "image/png,*/*;q=0.8"},
  };
  auto container = this->http_->get(url, headers, {"retry-after"});
  if (container == nullptr)
    return false;
  if (container->status_code != 200) {
    int code = container->status_code;
    throttled = ::espn::is_throttle_status(code);
    if (throttled)
      retry_after_ms = ::espn::parse_retry_after_ms(container->get_response_header("retry-after"));
    container->end();
    // Kept under the dark URL, so the next lookup finds it and nothing asks
    // for the missing one again and again.
    std::string plain = ::espn::plain_logo_url(url);
    if (code == 404 && !plain.empty()) {
      ESP_LOGD(TAG, "No dark logo, using %s", plain.c_str());
      return this->download_(plain, body, throttled, retry_after_ms);
    }
    ESP_LOGW(TAG, "HTTP %d for %s", code, url.c_str());
    return false;
  }
  if (container->content_length > 0 && container->content_length <= MAX_BODY)
    body.reserve(container->content_length);
  uint8_t buf[1024];
  while (body.size() < MAX_BODY) {
    int n = container->read(buf, sizeof(buf));
    if (n <= 0)
      break;
    body.insert(body.end(), buf, buf + n);
  }
  container->end();
  this->bytes_downloaded_ += body.size();
  if (body.empty() || (container->content_length > 0 && body.size() < container->content_length))
    return false;
  return true;
}

// Feeds the whole PNG to a decoder already begun and ends the decode. False
// on a decode error.
template<class Body> static bool feed(runtime_image::RuntimeImage &image, const Body &body) {
  size_t offset = 0;
  while (offset < body.size()) {
    // feed_data takes a non-const pointer but only reads it.
    int used = image.feed_data(const_cast<uint8_t *>(body.data()) + offset, body.size() - offset);
    if (used < 0) {
      ESP_LOGW(TAG, "Logo decode error: %s", runtime_image::decode_error_to_string(used));
      return false;
    }
    if (used == 0)
      break;
    offset += (size_t) used;
  }
  return image.end_decode();
}

bool LogoCache::Logo::paint(const uint32_t *px) {
  if (this->resize(LOGO_SIZE, LOGO_SIZE) <= 0 || this->get_buffer_width() != LOGO_SIZE ||
      this->get_buffer_height() != LOGO_SIZE)
    return false;
  for (int y = 0; y < LOGO_SIZE; y++) {
    for (int x = 0; x < LOGO_SIZE; x++) {
      uint32_t p = px[y * LOGO_SIZE + x];
      this->draw_pixel(x, y, Color((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF, p >> 24));
    }
  }
  this->width_ = this->buffer_width_;
  this->height_ = this->buffer_height_;
  this->data_start_ = this->buffer_;
  this->refresh_lvgl_();
  return true;
}

// Decodes a size x size logo at full size into PSRAM and shrinks it with
// logo_shrink.h, so thin lines survive. One PSRAM block holds the decoded
// picture (64 KB for 128 px), the shrink's scratch (64 KB) and its result, and
// is freed before this returns, as is the decoder. False when PSRAM has no
// block that big or the PNG does not decode at the size its header gave.
bool LogoCache::shrink_(Logo &logo, const Bytes &body, int size) {
  runtime_image::RuntimeImage src(runtime_image::PNG, image::IMAGE_TYPE_RGB, image::TRANSPARENCY_ALPHA_CHANNEL);
  const size_t pixels = src.get_buffer_size(size, size);
  const size_t words = ::espn::shrink::scratch_words(size) + (size_t) LOGO_SIZE * LOGO_SIZE;
  if (pixels != (size_t) size * size * 4)
    return false;
  auto *mem = static_cast<uint8_t *>(heap_caps_malloc(pixels + words * 4, MALLOC_CAP_SPIRAM));
  if (mem == nullptr) {
    ESP_LOGW(TAG, "No PSRAM to shrink a %d px logo (largest free block %u bytes)", size,
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    return false;
  }
  const uint32_t start = millis();
  memset(mem, 0, pixels);
  auto *scratch = reinterpret_cast<uint32_t *>(mem + pixels);
  uint32_t *out = scratch + ::espn::shrink::scratch_words(size);
  bool ok = src.set_external_buffer(mem, size, size) && src.begin_decode(body.size()) && feed(src, body) &&
            src.get_width() == size && src.get_height() == size;
  if (ok) {
    ::espn::shrink::rare_vote(src.get_data_start(), size, scratch, out);
    ok = logo.paint(out);
  }
  // Lets go of the buffer and ends any decode before the buffer is freed.
  src.release();
  heap_caps_free(mem);
  if (ok)
    ESP_LOGD(TAG, "Shrank a %d px logo to %d px in %u ms", size, LOGO_SIZE, (unsigned) (millis() - start));
  return ok;
}

// Decodes into the entry itself, so the image object lives in PSRAM with it.
// Safe without the lock: the entry is DOWNLOADED until the caller marks it
// READY, so nothing draws it and nothing drops it.
//
// A logo the shrink takes (logo_shrink.h, fits()) is shrunk by it. Any other,
// or one it could not shrink, is decoded straight to 32 px, which keeps one
// source pixel per LED.
bool LogoCache::decode_(std::optional<Logo> &image, const Bytes &body) {
  image.emplace(runtime_image::PNG, image::IMAGE_TYPE_RGB565, image::TRANSPARENCY_ALPHA_CHANNEL, nullptr, false,
                LOGO_SIZE, LOGO_SIZE);
  int w = 0, h = 0;
  if (::espn::shrink::png_size(body.data(), body.size(), w, h) && ::espn::shrink::fits(w, h)) {
    if (this->shrink_(*image, body, w))
      return true;
    ESP_LOGW(TAG, "Could not shrink a %dx%d logo; decoding it straight to %d px", w, h, LOGO_SIZE);
    image.reset();
    image.emplace(runtime_image::PNG, image::IMAGE_TYPE_RGB565, image::TRANSPARENCY_ALPHA_CHANNEL, nullptr, false,
                  LOGO_SIZE, LOGO_SIZE);
  }
  bool ok = image->begin_decode(body.size()) && feed(*image, body);
  if (ok) {
    image->drop_decoder();
  } else {
    image.reset();
  }
  return ok;
}

bool LogoCache::loop() {
  if (this->lock_ == nullptr)
    return false;
  this->retry_(millis());
  if (!this->downloaded_.exchange(false))
    return false;
  bool ready = false;
  while (true) {
    Entry *job = nullptr;
    Bytes body;
    {
      Locked guard(this->lock_);
      for (auto &e : this->entries_) {
        if (e->state == State::DOWNLOADED) {
          job = e.get();
          body = std::move(e->body);
          e->body = {};
          break;
        }
      }
    }
    if (job == nullptr)
      return ready;
    // Only the main loop drops entries, so job stays valid without the lock.
    bool ok = this->decode_(job->image, body);
    Locked guard(this->lock_);
    if (ok) {
      job->state = State::READY;
      ::espn::logo_loaded(job->fails, job->wait_ms);
      ready = true;
    } else {
      job->state = State::FAILED;
      job->failed_ms = millis();
      ::espn::logo_failed(job->fails, job->wait_ms, false, 0, random_uint32());
    }
  }
}

}  // namespace esphome::gameday
