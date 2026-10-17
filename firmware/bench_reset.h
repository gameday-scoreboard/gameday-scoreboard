// BENCH BUILD ONLY (silent reboot hunt). Never merges.
// Keeps what the panel was doing just before a restart where the restart keeps
// it (RTC memory), so a reset with no crash report still leaves a trail:
//  - the last log lines, which a silent reset never gets onto the network
//  - a heartbeat every 5 s with uptime and internal heap
//  - the name of a task that overflowed its stack
#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esphome/core/log.h"

#define BENCH_MAGIC 0x0200A5C3u
#define BENCH_LINES 24
#define BENCH_LINE_LEN 120

struct BenchCrumbs {
  uint32_t magic;
  uint32_t boots;
  uint32_t head;  // next slot to write
  uint32_t ovf_count;
  char ovf_name[24];
  uint32_t hb_uptime_s;
  uint32_t hb_heap;
  uint32_t hb_block;
  char lines[BENCH_LINES][BENCH_LINE_LEN];
};
RTC_NOINIT_ATTR BenchCrumbs bench_rtc;

// What the previous boot left, copied out at setup before this boot writes over it.
static std::string bench_prev;

// Replaces IDF's weak hook: save the task name where the reboot keeps it,
// then abort exactly as IDF does.
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  if (bench_rtc.magic == BENCH_MAGIC) {
    bench_rtc.ovf_count++;
    size_t i = 0;
    for (; pcTaskName != nullptr && pcTaskName[i] != 0 && i < sizeof(bench_rtc.ovf_name) - 1; i++)
      bench_rtc.ovf_name[i] = pcTaskName[i];
    bench_rtc.ovf_name[i] = 0;
  }
  char buf[96] = "***ERROR*** A stack overflow in task ";
  strncat(buf, pcTaskName != nullptr ? pcTaskName : "?", sizeof(buf) - strlen(buf) - 1);
  esp_system_abort(buf);
}

static void bench_put_(const char *text, size_t len) {
  char *slot = bench_rtc.lines[bench_rtc.head % BENCH_LINES];
  uint32_t ms = (uint32_t) (esp_timer_get_time() / 1000);
  int n = snprintf(slot, BENCH_LINE_LEN, "%7u.%03u ", (unsigned) (ms / 1000), (unsigned) (ms % 1000));
  if (n < 0)
    n = 0;
  // Copy the message without ANSI colour codes or the trailing newline.
  size_t o = (size_t) n;
  for (size_t i = 0; i < len && o < BENCH_LINE_LEN - 1; i++) {
    char c = text[i];
    if (c == '\033') {
      while (i < len && text[i] != 'm')
        i++;
      continue;
    }
    if (c == '\n' || c == '\r' || c == 0)
      break;
    slot[o++] = c;
  }
  slot[o] = 0;
  bench_rtc.head++;
}

// Called from logger on_message, which reserves its own listener slot. The
// logger sizes its callback list at build time, so add_log_callback() from a
// lambda takes the API's slot and the API log goes silent.
static void bench_log(uint8_t level, const char *tag, const char *message) {
  if (level > ESPHOME_LOG_LEVEL_DEBUG || message == nullptr || (tag != nullptr && strcmp(tag, "bench") == 0))
    return;
  bench_put_(message, strlen(message));
}

// Called once, early in boot (on_boot priority 800).
static void bench_setup() {
  if (bench_rtc.magic != BENCH_MAGIC) {
    memset(&bench_rtc, 0, sizeof(bench_rtc));
    bench_rtc.magic = BENCH_MAGIC;
    bench_prev = "no crumbs (power-on, or RTC memory lost)";
  } else {
    char hdr[160];
    snprintf(hdr, sizeof(hdr),
             "boot %u; last heartbeat at uptime %u s, internal free %u, largest block %u; stack overflows %u%s%s",
             (unsigned) bench_rtc.boots, (unsigned) bench_rtc.hb_uptime_s, (unsigned) bench_rtc.hb_heap,
             (unsigned) bench_rtc.hb_block, (unsigned) bench_rtc.ovf_count, bench_rtc.ovf_count ? ", last in " : "",
             bench_rtc.ovf_count ? bench_rtc.ovf_name : "");
    bench_prev = hdr;
    uint32_t count = bench_rtc.head < BENCH_LINES ? bench_rtc.head : BENCH_LINES;
    for (uint32_t k = bench_rtc.head - count; k != bench_rtc.head; k++) {
      char *line = bench_rtc.lines[k % BENCH_LINES];
      line[BENCH_LINE_LEN - 1] = 0;
      bench_prev += "\n  ";
      bench_prev += line;
    }
  }
  bench_rtc.boots++;
  bench_rtc.head = 0;
  bench_rtc.hb_uptime_s = 0;
}

static void bench_heartbeat() {
  bench_rtc.hb_uptime_s = (uint32_t) (esp_timer_get_time() / 1000000);
  bench_rtc.hb_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  bench_rtc.hb_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
}

static void bench_report(const char *why) {
  ESP_LOGW("bench", "Reset reason: %s (%s)", esphome::to_string((int) esp_reset_reason()).c_str(), why);
  // One log call per line so no line runs past the logger's buffer.
  size_t start = 0;
  while (start < bench_prev.size()) {
    size_t end = bench_prev.find('\n', start);
    if (end == std::string::npos)
      end = bench_prev.size();
    ESP_LOGW("bench", "prev: %.*s", (int) (end - start), bench_prev.c_str() + start);
    start = end + 1;
  }
}
