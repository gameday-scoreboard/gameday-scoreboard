// Names wall (backlog #152 spike): a list of names the panel pages through,
// for a bar that puts a plate on the wall for every customer who earns one
// and is running out of wall. Throwaway: it proves the display and the two
// ways names get in, and measures what they cost.
//
// Names come in two ways:
//   POST /names  with a raw body (Content-Type text/plain, text/csv or
//                application/json): one name a line, a CSV's first column
//                (a "name" header row is skipped), or a JSON array of
//                strings. Replaces the whole list.
//   POST /names?url=<csv or text url>   a list the panel fetches itself,
//                at boot and every poll minutes (a Google Sheet published
//                as CSV is the realistic case). Replaces any POSTed list.
// Settings, all on the query string or as form fields, saved in flash:
//   on=0/1 hold=2..60 (s a page stays up) font=8/12 cols=0 auto|1|2
//   title=<text> poll=1..1440 (min) do=fetch|clear
//   GET /names        state as JSON      GET /names/list   the names, one a line
//
// The list lives in PSRAM as one packed buffer plus an offset table, so a
// few thousand names cost internal RAM nothing. The page
// (firmware/pages/names-wall.yaml) reads it from the main loop under the
// mutex; the web server task and the fetch task write it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace names_wall {

// What the page needs to draw the names on screen now.
struct WallView {
  bool on;
  uint8_t font;  // 8 or 12
  uint8_t cols;  // 0 auto (64 px a column at font 8, one column at 12)
  uint16_t hold_s;
  uint32_t generation;  // bumped on every list or settings change
  size_t count;
  std::string title;
};

class NamesWall : public Component, public AsyncWebHandler {
 public:
  void set_http(http_request::HttpRequestComponent *http) { this->http_ = http; }
  void set_web_server_base(web_server_base::WebServerBase *base) { this->base_ = base; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Main loop side (the page). names_for_page fills up to per_page names
  // starting at first, and returns how many it wrote.
  WallView view();
  size_t names_for_page(size_t first, size_t per_page, std::vector<std::string> &out);
  // The page reports where it is so GET /names can say so (bench timing).
  void report_page(size_t page, size_t pages) {
    this->page_ = page;
    this->pages_ = pages;
    this->page_ms_ = millis();
  }

  // Web handler (/names*), on the HTTP task.
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override;
  bool isRequestHandlerTrivial() const override { return false; }

 protected:
  struct Prefs {
    uint8_t on;
    uint8_t hold_s;
    uint8_t font;
    uint8_t cols;
    uint16_t poll_min;
    char title[24];
    char url[200];
  };

  // Replaces the list from text (lines, CSV first column or a JSON array).
  // Returns the number of names kept. Called on the HTTP or fetch task.
  size_t load_text_(const char *text, size_t len, const char *source);
  bool parse_json_array_(const char *text, size_t len, std::vector<std::string> &names);
  void swap_in_(std::vector<std::string> &names, const char *source);
  void clear_();

  void apply_settings_(const std::vector<std::pair<std::string, std::string>> &kv);
  std::string state_json_();
  void save_prefs_() { this->pref_.save(&this->prefs_); }

  void start_fetch_();
  static void fetch_task_(void *arg);
  void fetch_();

  http_request::HttpRequestComponent *http_{nullptr};
  web_server_base::WebServerBase *base_{nullptr};
  ESPPreferenceObject pref_;
  Prefs prefs_{};

  std::mutex mutex_;
  // PSRAM: every name NUL-terminated back to back, and where each starts.
  char *blob_{nullptr};
  size_t blob_len_{0};
  uint32_t *offsets_{nullptr};
  size_t count_{0};
  uint32_t generation_{1};
  std::string source_;     // "post", "url" or ""
  uint32_t loaded_ms_{0};  // millis() of the last successful load
  size_t loaded_bytes_{0};
  std::string last_error_;

  // A POST body on its way in (handleBody chunks), PSRAM.
  char *body_{nullptr};
  size_t body_cap_{0};
  size_t body_len_{0};
  bool body_too_big_{false};

  // Fetch task
  volatile bool fetching_{false};
  bool fetch_wanted_{false};
  uint32_t next_fetch_ms_{0};
  bool booted_{false};

  // Page position, from the page, for GET /names
  volatile size_t page_{0};
  volatile size_t pages_{0};
  volatile uint32_t page_ms_{0};
};

}  // namespace names_wall
}  // namespace esphome
