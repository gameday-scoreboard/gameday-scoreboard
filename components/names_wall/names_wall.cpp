// Names wall (backlog #152 spike). See names_wall.h.
#include "names_wall.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <strings.h>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace names_wall {

static const char *const TAG = "names_wall";

// Caps, so a wrong URL (a whole web page) can't eat the PSRAM.
static constexpr size_t MAX_BODY = 256 * 1024;
static constexpr size_t MAX_NAMES = 8000;
static constexpr size_t MAX_NAME = 48;
static constexpr uint32_t BOOT_FETCH_DELAY_MS = 8000;

static void *psram_alloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p == nullptr)
    p = malloc(n);  // a board with no PSRAM: internal, and the caps above keep it small
  return p;
}

// ---- setup and loop ---------------------------------------------------------

void NamesWall::setup() {
  this->pref_ = global_preferences->make_preference<Prefs>(fnv1_hash("names_wall_v1"));
  if (!this->pref_.load(&this->prefs_) || this->prefs_.hold_s < 2 || this->prefs_.hold_s > 60 ||
      (this->prefs_.font != 8 && this->prefs_.font != 12) || this->prefs_.cols > 2) {
    this->prefs_ = Prefs{};
    this->prefs_.hold_s = 4;
    this->prefs_.font = 8;
    this->prefs_.poll_min = 15;
    strlcpy(this->prefs_.title, "200 CLUB", sizeof(this->prefs_.title));
  }
  this->prefs_.title[sizeof(this->prefs_.title) - 1] = 0;
  this->prefs_.url[sizeof(this->prefs_.url) - 1] = 0;
  if (this->prefs_.poll_min < 1)
    this->prefs_.poll_min = 15;
  if (this->base_ != nullptr)
    this->base_->add_handler(this);
  ESP_LOGI(TAG, "Names wall %s, hold %u s, font %u, title '%s', url '%s', poll %u min", this->prefs_.on ? "on" : "off",
           this->prefs_.hold_s, this->prefs_.font, this->prefs_.title, this->prefs_.url, this->prefs_.poll_min);
}

void NamesWall::loop() {
  const uint32_t now = millis();
  if (!this->booted_) {
    if (!network::is_connected())
      return;
    this->booted_ = true;
    // The list isn't kept in flash: a panel with a URL reads it again after
    // Wi-Fi is up, a panel that was POSTed a list starts empty.
    this->next_fetch_ms_ = now + BOOT_FETCH_DELAY_MS;
    return;
  }
  if (this->fetching_)
    return;
  const bool due = this->prefs_.url[0] != 0 && (int32_t) (now - this->next_fetch_ms_) >= 0;
  if (this->fetch_wanted_ || due) {
    this->fetch_wanted_ = false;
    this->next_fetch_ms_ = now + (uint32_t) this->prefs_.poll_min * 60000UL;
    if (this->prefs_.url[0] != 0 && network::is_connected())
      this->start_fetch_();
  }
}

// ---- the page's side ----------------------------------------------------------

WallView NamesWall::view() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  WallView v;
  v.on = this->prefs_.on != 0;
  v.font = this->prefs_.font;
  v.cols = this->prefs_.cols;
  v.hold_s = this->prefs_.hold_s;
  v.generation = this->generation_;
  v.count = this->count_;
  v.title = this->prefs_.title;
  return v;
}

size_t NamesWall::names_for_page(size_t first, size_t per_page, std::vector<std::string> &out) {
  std::lock_guard<std::mutex> lock(this->mutex_);
  out.clear();
  for (size_t i = first; i < this->count_ && out.size() < per_page; i++)
    out.emplace_back(this->blob_ + this->offsets_[i]);
  return out.size();
}

// ---- the list ---------------------------------------------------------------

void NamesWall::clear_() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  free(this->blob_);
  free(this->offsets_);
  this->blob_ = nullptr;
  this->offsets_ = nullptr;
  this->blob_len_ = 0;
  this->count_ = 0;
  this->source_.clear();
  this->generation_++;
}

// Trims spaces and quotes, caps the length, and hands back what to keep.
static size_t clean_name(const char *s, size_t len, char *out, size_t out_cap) {
  while (len > 0 && (s[0] == ' ' || s[0] == '\t' || s[0] == '\r' || s[0] == '"'))
    s++, len--;
  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '"'))
    len--;
  if (len == 0)
    return 0;
  size_t n = std::min(len, out_cap - 1);
  // Never cut a UTF-8 sequence in half.
  while (n > 0 && n < len && ((unsigned char) s[n] & 0xC0) == 0x80)
    n--;
  memcpy(out, s, n);
  out[n] = 0;
  return n;
}

// One name per line, or a CSV's first column. A quoted first field may hold
// a comma ("Smith, John"); an unquoted one ends at the first comma.
static void scan_lines(const char *text, size_t len, const std::function<void(const char *, size_t)> &fn) {
  size_t i = 0;
  bool first_line = true;
  while (i < len) {
    size_t end = i;
    while (end < len && text[end] != '\n')
      end++;
    const char *line = text + i;
    size_t n = end - i;
    i = end + 1;
    if (n > 0 && line[n - 1] == '\r')
      n--;
    const char *field = line;
    size_t flen = n;
    if (n > 0 && line[0] == '"') {
      // Quoted field: up to the closing quote (doubled quotes stay; rare on a name)
      size_t q = 1;
      while (q < n && !(line[q] == '"' && (q + 1 >= n || line[q + 1] == ',')))
        q++;
      field = line + 1;
      flen = q - 1;
    } else {
      const char *comma = (const char *) memchr(line, ',', n);
      if (comma != nullptr)
        flen = (size_t) (comma - line);
    }
    char buf[MAX_NAME];
    size_t clen = clean_name(field, flen, buf, sizeof(buf));
    if (clen == 0)
      continue;
    // A header row the export tool wrote
    if (first_line && (strcasecmp(buf, "name") == 0 || strcasecmp(buf, "names") == 0)) {
      first_line = false;
      continue;
    }
    first_line = false;
    fn(buf, clen);
  }
}

// ["Ann", "Bob"]: every string in the array, with the common escapes.
static bool scan_json_array(const char *text, size_t len, const std::function<void(const char *, size_t)> &fn) {
  size_t i = 0;
  while (i < len && text[i] != '[')
    i++;
  if (i >= len)
    return false;
  i++;
  char buf[MAX_NAME];
  while (i < len) {
    char c = text[i];
    if (c == ']')
      return true;
    if (c != '"') {
      i++;
      continue;
    }
    i++;
    size_t n = 0;
    bool closed = false;
    while (i < len) {
      c = text[i++];
      if (c == '"') {
        closed = true;
        break;
      }
      if (c == '\\' && i < len) {
        char e = text[i++];
        if (e == 'n' || e == 't' || e == 'r')
          c = ' ';
        else if (e == 'u') {
          c = '?';
          i = std::min(len, i + 4);
        } else
          c = e;
      }
      if (n < sizeof(buf) - 1)
        buf[n++] = c;
    }
    if (!closed)
      return false;
    buf[n] = 0;
    char cleaned[MAX_NAME];
    size_t clen = clean_name(buf, n, cleaned, sizeof(cleaned));
    if (clen > 0)
      fn(cleaned, clen);
  }
  return false;
}

size_t NamesWall::load_text_(const char *text, size_t len, const char *source) {
  size_t i = 0;
  while (i < len && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t' ||
                     (unsigned char) text[i] == 0xEF || (unsigned char) text[i] == 0xBB ||
                     (unsigned char) text[i] == 0xBF))
    i++;  // whitespace and a UTF-8 byte order mark
  const bool json = i < len && text[i] == '[';
  // Pass 1: how much room the list needs. Pass 2: fill it. No temporary
  // list of std::string, which for 2,000 names would be 60 KB of internal RAM.
  size_t count = 0, bytes = 0;
  auto measure = [&](const char *, size_t n) {
    if (count < MAX_NAMES) {
      count++;
      bytes += n + 1;
    }
  };
  if (json)
    scan_json_array(text + i, len - i, measure);
  else
    scan_lines(text + i, len - i, measure);
  if (count == 0) {
    ESP_LOGW(TAG, "No names in %u bytes from %s", (unsigned) len, source);
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->last_error_ = "no names found";
    return 0;
  }
  char *blob = (char *) psram_alloc(bytes);
  uint32_t *offsets = (uint32_t *) psram_alloc(count * sizeof(uint32_t));
  if (blob == nullptr || offsets == nullptr) {
    free(blob);
    free(offsets);
    ESP_LOGE(TAG, "Out of memory for %u names (%u bytes)", (unsigned) count, (unsigned) bytes);
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->last_error_ = "out of memory";
    return 0;
  }
  size_t k = 0, pos = 0;
  auto fill = [&](const char *s, size_t n) {
    if (k >= count)
      return;
    offsets[k++] = (uint32_t) pos;
    memcpy(blob + pos, s, n);
    pos += n;
    blob[pos++] = 0;
  };
  if (json)
    scan_json_array(text + i, len - i, fill);
  else
    scan_lines(text + i, len - i, fill);
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    free(this->blob_);
    free(this->offsets_);
    this->blob_ = blob;
    this->offsets_ = offsets;
    this->blob_len_ = pos;
    this->count_ = k;
    this->source_ = source;
    this->loaded_ms_ = millis();
    this->loaded_bytes_ = len;
    this->last_error_.clear();
    this->generation_++;
  }
  ESP_LOGI(TAG, "%u names (%u bytes packed) from %s; internal free %u, PSRAM free %u", (unsigned) k, (unsigned) pos,
           source, (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  return k;
}

// ---- fetching a URL -------------------------------------------------------------

void NamesWall::start_fetch_() {
  if (this->fetching_)
    return;
  this->fetching_ = true;
  // 12 KB: TLS and the chunked read; the body itself lands in PSRAM.
  if (xTaskCreate(&NamesWall::fetch_task_, "names_fetch", 12288, this, 1, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Could not start the fetch task");
    this->fetching_ = false;
  }
}

void NamesWall::fetch_task_(void *arg) {
  auto *self = static_cast<NamesWall *>(arg);
  self->fetch_();
  self->fetching_ = false;
  vTaskDelete(nullptr);
}

void NamesWall::fetch_() {
  std::string url = this->prefs_.url;
  ESP_LOGI(TAG, "Fetching %s", url.c_str());
  std::vector<http_request::Header> headers = {{"User-Agent", "curl/8.0"}, {"Accept", "text/csv, text/plain, */*"}};
  auto container = this->http_->get(url, headers);
  if (container == nullptr) {
    ESP_LOGW(TAG, "Fetch failed: %s", url.c_str());
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->last_error_ = "fetch failed";
    return;
  }
  if (container->status_code != 200) {
    ESP_LOGW(TAG, "HTTP %d for %s", container->status_code, url.c_str());
    container->end();
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->last_error_ = "HTTP " + std::to_string(container->status_code);
    return;
  }
  // Grows in PSRAM as the body streams in; most exports don't say their length.
  size_t cap = container->content_length > 0 ? std::min((size_t) container->content_length + 1, MAX_BODY) : 16384;
  char *body = (char *) psram_alloc(cap);
  size_t len = 0;
  bool too_big = false;
  while (body != nullptr) {
    if (len + 1024 > cap) {
      if (cap >= MAX_BODY) {
        too_big = true;
        break;
      }
      size_t ncap = std::min(cap * 2, MAX_BODY);
      char *nb = (char *) psram_alloc(ncap);
      if (nb == nullptr) {
        too_big = true;
        break;
      }
      memcpy(nb, body, len);
      free(body);
      body = nb;
      cap = ncap;
    }
    int n = container->read((uint8_t *) body + len, 1024);
    if (n <= 0)
      break;
    len += (size_t) n;
    App.feed_wdt();
  }
  container->end();
  if (body == nullptr || too_big) {
    free(body);
    ESP_LOGW(TAG, "Body from %s is over %u bytes or out of memory, dropped", url.c_str(), (unsigned) MAX_BODY);
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->last_error_ = "body too big";
    return;
  }
  this->load_text_(body, len, "url");
  free(body);
}

// ---- /names ----------------------------------------------------------------------

bool NamesWall::canHandle(AsyncWebServerRequest *request) const {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(buf);
  return url == StringRef("/names") || url.starts_with("/names/");
}

void NamesWall::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (index == 0) {
    free(this->body_);
    this->body_ = nullptr;
    this->body_len_ = 0;
    this->body_too_big_ = total > MAX_BODY;
    if (!this->body_too_big_) {
      this->body_cap_ = total + 1;
      this->body_ = (char *) psram_alloc(this->body_cap_);
      if (this->body_ == nullptr)
        this->body_too_big_ = true;
    }
  }
  if (this->body_too_big_ || this->body_ == nullptr || this->body_len_ + len > this->body_cap_ - 1)
    return;
  memcpy(this->body_ + this->body_len_, data, len);
  this->body_len_ += len;
}

static void json_escape(std::string &out, const std::string &s) {
  for (char c : s) {
    if (c == '"' || c == '\\')
      out += '\\';
    if ((unsigned char) c < 0x20)
      out += ' ';
    else
      out += c;
  }
}

std::string NamesWall::state_json_() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  const uint32_t now = millis();
  std::string s = "{\"on\":";
  s += this->prefs_.on ? "true" : "false";
  s += ",\"count\":" + std::to_string(this->count_);
  s += ",\"bytes\":" + std::to_string(this->blob_len_);
  s += ",\"hold\":" + std::to_string(this->prefs_.hold_s);
  s += ",\"font\":" + std::to_string(this->prefs_.font);
  s += ",\"cols\":" + std::to_string(this->prefs_.cols);
  s += ",\"poll\":" + std::to_string(this->prefs_.poll_min);
  s += ",\"title\":\"";
  json_escape(s, this->prefs_.title);
  s += "\",\"url\":\"";
  json_escape(s, this->prefs_.url);
  s += "\",\"source\":\"" + this->source_ + "\"";
  s += ",\"loaded_s_ago\":" + std::to_string(this->loaded_ms_ == 0 ? -1 : (int) ((now - this->loaded_ms_) / 1000));
  s += ",\"loaded_bytes\":" + std::to_string(this->loaded_bytes_);
  s += ",\"fetching\":" + std::string(this->fetching_ ? "true" : "false");
  s += ",\"page\":" + std::to_string((unsigned) this->page_);
  s += ",\"pages\":" + std::to_string((unsigned) this->pages_);
  s += ",\"page_ms\":" + std::to_string((unsigned) this->page_ms_);
  s += ",\"uptime_ms\":" + std::to_string(now);
  s += ",\"error\":\"";
  json_escape(s, this->last_error_);
  s += "\",\"heap\":{\"internal\":" + std::to_string(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
  s += ",\"block\":" + std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
  s += ",\"psram\":" + std::to_string(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024) + "}}";
  return s;
}

static const char *const SETTING_KEYS[] = {"on", "hold", "font", "cols", "title", "url", "poll", "do"};

void NamesWall::handleRequest(AsyncWebServerRequest *request) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(buf);
  if (url == StringRef("/names/list") && request->method() == HTTP_GET) {
    std::string out;
    {
      std::lock_guard<std::mutex> lock(this->mutex_);
      out.reserve(this->blob_len_ + this->count_);
      for (size_t i = 0; i < this->count_; i++) {
        out += this->blob_ + this->offsets_[i];
        out += '\n';
      }
    }
    request->send(200, "text/plain; charset=utf-8", out.c_str());
    return;
  }
  if (url != StringRef("/names")) {
    request->send(404, "application/json", "{\"error\":\"unknown route\"}");
    return;
  }
  if (request->method() == HTTP_GET) {
    std::string body = this->state_json_();
    request->send(200, "application/json", body.c_str());
    return;
  }
  if (request->method() != HTTP_POST) {
    request->send(405, "application/json", "{\"error\":\"GET or POST\"}");
    return;
  }
  // A raw body is a new list.
  if (this->body_too_big_) {
    this->body_too_big_ = false;
    request->send(413, "application/json", "{\"error\":\"list too big\"}");
    return;
  }
  size_t loaded = 0;
  bool had_body = false;
  if (this->body_ != nullptr && this->body_len_ > 0) {
    had_body = true;
    loaded = this->load_text_(this->body_, this->body_len_, "post");
    free(this->body_);
    this->body_ = nullptr;
    this->body_len_ = 0;
  }
  std::vector<std::pair<std::string, std::string>> kv;
  // A POSTed list replaces the URL as the source until a URL is set again
  // (applied on the main loop with the rest, so the save happens there).
  if (loaded > 0 && this->prefs_.url[0] != 0 && !request->hasParam("url"))
    kv.emplace_back("url", "");
  for (const char *key : SETTING_KEYS)
    if (request->hasParam(key))
      kv.emplace_back(key, request->arg(key));
  if (!kv.empty())
    this->defer([this, kv]() { this->apply_settings_(kv); });
  if (!had_body && kv.empty()) {
    request->send(400, "application/json", "{\"error\":\"send a list as the body, or a setting\"}");
    return;
  }
  std::string out = "{\"ok\":true,\"loaded\":" + std::to_string(loaded) + "}";
  request->send(200, "application/json", out.c_str());
}

void NamesWall::apply_settings_(const std::vector<std::pair<std::string, std::string>> &kv) {
  bool changed = false;
  for (const auto &p : kv) {
    const std::string &k = p.first;
    const std::string &v = p.second;
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (k == "on") {
      this->prefs_.on = v == "1" ? 1 : 0;
      changed = true;
    } else if (k == "hold") {
      int n = atoi(v.c_str());
      if (n >= 2 && n <= 60) {
        this->prefs_.hold_s = (uint8_t) n;
        changed = true;
      }
    } else if (k == "font") {
      int n = atoi(v.c_str());
      if (n == 8 || n == 12) {
        this->prefs_.font = (uint8_t) n;
        changed = true;
      }
    } else if (k == "cols") {
      int n = atoi(v.c_str());
      if (n >= 0 && n <= 2) {
        this->prefs_.cols = (uint8_t) n;
        changed = true;
      }
    } else if (k == "title") {
      strlcpy(this->prefs_.title, v.c_str(), sizeof(this->prefs_.title));
      changed = true;
    } else if (k == "poll") {
      int n = atoi(v.c_str());
      if (n >= 1 && n <= 1440) {
        this->prefs_.poll_min = (uint16_t) n;
        changed = true;
      }
    } else if (k == "url") {
      strlcpy(this->prefs_.url, v.c_str(), sizeof(this->prefs_.url));
      changed = true;
      if (this->prefs_.url[0] != 0)
        this->fetch_wanted_ = true;
    } else if (k == "do") {
      if (v == "fetch")
        this->fetch_wanted_ = true;
      else if (v == "clear") {
        this->prefs_.url[0] = 0;
        changed = true;
      }
    }
  }
  for (const auto &p : kv)
    if (p.first == "do" && p.second == "clear")
      this->clear_();
  if (changed) {
    this->save_prefs_();
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->generation_++;
    ESP_LOGI(TAG, "Settings: %s, hold %u s, font %u, cols %u, title '%s', url '%s', poll %u min",
             this->prefs_.on ? "on" : "off", this->prefs_.hold_s, this->prefs_.font, this->prefs_.cols,
             this->prefs_.title, this->prefs_.url, this->prefs_.poll_min);
  }
}

}  // namespace names_wall
}  // namespace esphome
