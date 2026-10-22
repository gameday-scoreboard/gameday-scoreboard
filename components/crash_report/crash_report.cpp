#include "crash_report.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/logger/logger.h"
#include "esphome/core/build_info_data.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32_CRASH_HANDLER
#include "esphome/components/esp32/crash_handler.h"
#endif

#include <esp_system.h>

#include <cinttypes>
#include <cstring>
#include <ctime>

namespace esphome {
namespace crash_report {

static const char *const TAG = "crash_report";
// The crash handler's log tag; only its lines go into the report.
static const char *const HANDLER_TAG = "esp32.crash";
// Anything earlier is a clock that was never set.
static constexpr time_t CLOCK_VALID = 1700000000;

CrashReport *global_crash_report = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static bool crashed(esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

static const char *reason_name(uint8_t r) {
  switch (r) {
    case ESP_RST_PANIC:
      return "crash";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_BROWNOUT:
      return "brownout";
    default:
      return "other";
  }
}

void CrashReport::setup() {
  global_crash_report = this;
  if (logger::global_logger != nullptr) {
    logger::global_logger->add_log_callback(
        this, [](void *self, uint8_t level, const char *tag, const char *message, size_t len) {
          static_cast<CrashReport *>(self)->on_log_(tag, message, len);
        });
  }
  this->pref_ = global_preferences->make_preference<Record>(fnv1_hash("gameday_crash_report_v1"));
  RAMAllocator<Record> alloc;
  this->record_ = alloc.allocate(1);
  if (this->record_ == nullptr)
    return;
  if (!this->pref_.load(this->record_))
    memset(this->record_, 0, sizeof(Record));
  this->record_->report[REPORT_SIZE - 1] = '\0';
  this->record_->version[VERSION_SIZE - 1] = '\0';

  const esp_reset_reason_t reason = esp_reset_reason();
  if (crashed(reason) || reason == ESP_RST_BROWNOUT) {
    Record &r = *this->record_;
    r.count++;
    r.reason = (uint8_t) reason;
    r.build_time = (uint32_t) ESPHOME_BUILD_TIME;
#ifdef ESPHOME_PROJECT_VERSION
    strncpy(r.version, ESPHOME_PROJECT_VERSION, VERSION_SIZE - 1);
#endif
    r.version[VERSION_SIZE - 1] = '\0';
    // The clock survives a soft reset, so it is often already set.
    const time_t now = ::time(nullptr);
    r.time = now >= CLOCK_VALID ? (uint32_t) now : 0;
    this->need_time_ = r.time == 0;
    r.report[0] = '\0';
    this->report_len_ = 0;
#ifdef USE_ESP32_CRASH_HANDLER
    if (crashed(reason) && esp32::crash_handler_has_data()) {
      this->capturing_ = true;
      esp32::crash_handler_log();
      this->capturing_ = false;
    }
#endif
    this->save_();
    // Saved now, not at the next flash write a minute away: a panel in a
    // crash loop might not live that long.
    global_preferences->sync();
    ESP_LOGW(TAG, "Restarted after a %s (%" PRIu32 " since cleared); saved for GET /gameday/crash",
             reason_name(reason), r.count);
  }

  if (this->record_->count == 0) {
    alloc.deallocate(this->record_, 1);
    this->record_ = nullptr;
  }
  if (!this->need_time_)
    this->disable_loop();
}

// Waits for the clock on a boot that saved a record, and runs a clear().
void CrashReport::loop() {
  if (this->clear_pending_.exchange(false)) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->record_ != nullptr) {
      RAMAllocator<Record> alloc;
      alloc.deallocate(this->record_, 1);
      this->record_ = nullptr;
    }
    this->need_time_ = false;
    Record empty{};
    this->pref_.save(&empty);
    global_preferences->sync();
    ESP_LOGI(TAG, "Crash report cleared");
  }
  if (this->need_time_) {
    const time_t now = ::time(nullptr);
    if (now >= CLOCK_VALID) {
      std::lock_guard<std::mutex> lock(this->mutex_);
      if (this->record_ != nullptr) {
        this->record_->time = (uint32_t) now;
        this->save_();
      }
      this->need_time_ = false;
    }
  }
  if (!this->need_time_ && !this->clear_pending_)
    this->disable_loop();
}

void CrashReport::dump_config() {
  ESP_LOGCONFIG(TAG, "Crash report:");
  std::lock_guard<std::mutex> lock(this->mutex_);
  if (this->record_ == nullptr) {
    ESP_LOGCONFIG(TAG, "  None saved");
    return;
  }
  ESP_LOGCONFIG(TAG,
                "  Restarts since cleared: %" PRIu32 "\n"
                "  Last: %s on %s",
                this->record_->count, reason_name(this->record_->reason), this->record_->version);
}

std::string CrashReport::json() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  return json::build_json([this](JsonObject root) {
    root["variant"] = this->variant_;
    if (this->record_ == nullptr) {
      root["count"] = 0;
      return;
    }
    const Record &r = *this->record_;
    root["count"] = r.count;
    root["reason"] = reason_name(r.reason);
    root["time"] = r.time;
    root["version"] = r.version;
    root["build_time"] = r.build_time;
    root["report"] = r.report;
  });
}

void CrashReport::clear() {
  this->clear_pending_ = true;
  this->enable_loop_soon_any_context();
}

// The crash handler logs one line per call from setup(), on this task, so
// no lock: the HTTP task can't read the record before setup() ends.
void CrashReport::on_log_(const char *tag, const char *message, size_t len) {
  if (!this->capturing_ || this->record_ == nullptr || strcmp(tag, HANDLER_TAG) != 0)
    return;
  // message is the whole console line: a color code, "[E][esp32.crash:123]",
  // ": ", the text and a color reset. Keep the text.
  const char *end = message + len;
  const char *start = strstr(message, ": ");
  if (start == nullptr)
    return;
  start += 2;
  if (end - start >= 4 && memcmp(end - 4, "\033[0m", 4) == 0)
    end -= 4;
  // The addr2line hints repeat the addresses above them; the script builds
  // its own command.
  if (strncmp(start, "Use: ", 5) == 0 || strncmp(start, "Other core: addr2line", 21) == 0)
    return;
  const size_t n = end - start;
  if (this->report_len_ + n + 2 > REPORT_SIZE)
    return;
  memcpy(this->record_->report + this->report_len_, start, n);
  this->report_len_ += n;
  this->record_->report[this->report_len_++] = '\n';
  this->record_->report[this->report_len_] = '\0';
}

void CrashReport::save_() { this->pref_.save(this->record_); }

}  // namespace crash_report
}  // namespace esphome
