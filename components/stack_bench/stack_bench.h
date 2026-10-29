// Bench only: stack watermarks per task and per suspect. Never in a release.
//
// Two readings. The sampler asks FreeRTOS once a second for every task's
// high-water mark (the bytes of its stack never touched) and keeps the lowest
// per task. A Meter paints the free stack below its caller with the kernel's
// fill byte, lets the code in its scope run, then finds the deepest byte the
// scope dirtied: how much stack that one call tree needed, and how much was
// left at its deepest point. Both tables live in RTC memory, so a Reset
// Wi-Fi, an OTA or a crash restart keeps them; GET /bench/stack prints them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"

namespace esphome::stack_bench {

// A scoped meter. Only the running task's own stack; nothing to clean up if
// the scope throws, since nothing is allocated.
class Meter {
 public:
  explicit Meter(const char *name);
  ~Meter();
  Meter(const Meter &) = delete;
  Meter &operator=(const Meter &) = delete;

 private:
  const char *name_;
  uint8_t *start_;      // lowest address of this task's stack
  uint8_t *sp0_;        // the caller's stack pointer at construction
  uint8_t *paint_top_;  // painted [start_ + 64, paint_top_)
};

// Records the running task's free stack at this point under a name (the
// lowest wins). For places a Meter cannot bracket, like an OTA progress call.
void probe(const char *name);
// At a task's last line: its high-water mark, before FreeRTOS reclaims it.
void note_exit();
// Folds one task's free bytes into the table (the lowest wins).
void fold_task(const char *name, uint32_t free_bytes);
// The situation label written into the tables with each new low.
void set_label(const char *label);
// Bench: seconds of uptime after which a panel in setup mode restarts onto its
// saved networks (0 = the firmware's own 15 minutes). Kept through the restart.
uint32_t setup_cut_s();
void set_setup_cut(uint32_t s);

// Registers the first log callback (before the web server's and the API's),
// which starts a meter around theirs on the main task.
class StackBenchEarly : public Component {
 public:
  void setup() override;
  float get_setup_priority() const override { return 249.5f; }
};

class StackBench : public Component, public AsyncWebHandler {
 public:
  explicit StackBench(web_server_base::WebServerBase *base) : base_(base) {}
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return 50.0f; }
  void dump_config() override;

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  void sample_();
  void log_line_();
  web_server_base::WebServerBase *base_;
  uint32_t last_sample_ms_{0};
  uint32_t last_log_ms_{0};
  bool booted_logged_{false};
};

}  // namespace esphome::stack_bench
