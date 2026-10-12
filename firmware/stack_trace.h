// TRACE BUILD ONLY (backlog #84). Never merges.
// Names the task behind a FreeRTOS stack overflow and logs stack high water
// marks so the shrinking task shows up before the crash.
#pragma once
#include <cstring>
#include "esp_attr.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esphome/core/log.h"

#define TRACE_OVF_MAGIC 0x0F0F84A5u
RTC_NOINIT_ATTR uint32_t trace_ovf_magic;
RTC_NOINIT_ATTR uint32_t trace_ovf_count;
RTC_NOINIT_ATTR char trace_ovf_name[24];

// Replaces IDF's weak hook: save the task name where the reboot keeps it,
// then abort exactly as IDF does.
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  if (trace_ovf_magic != TRACE_OVF_MAGIC) {
    trace_ovf_magic = TRACE_OVF_MAGIC;
    trace_ovf_count = 0;
  }
  trace_ovf_count++;
  size_t i = 0;
  for (; pcTaskName != nullptr && pcTaskName[i] != 0 && i < sizeof(trace_ovf_name) - 1; i++)
    trace_ovf_name[i] = pcTaskName[i];
  trace_ovf_name[i] = 0;
  char buf[96] = "***ERROR*** A stack overflow in task ";
  strncat(buf, trace_ovf_name, sizeof(buf) - strlen(buf) - 1);
  esp_system_abort(buf);
}

static void trace_report_overflow() {
  if (trace_ovf_magic == TRACE_OVF_MAGIC && trace_ovf_count > 0) {
    ESP_LOGE("trace", "STACK OVERFLOW recorded: task '%s' (%u so far since power-on)", trace_ovf_name,
             (unsigned) trace_ovf_count);
  } else {
    ESP_LOGI("trace", "no stack overflow recorded since power-on");
  }
}

// Logs a task when its free stack drops to a new low, plus a full table every
// `full_every` calls.
static void trace_stack_marks(uint32_t full_every) {
  static TaskStatus_t tasks[48];
  static char names[48][16];
  static uint32_t lows[48];
  static size_t known = 0;
  static uint32_t calls = 0;
  UBaseType_t n = uxTaskGetSystemState(tasks, 48, nullptr);
  bool full = (calls++ % full_every) == 0;
  for (UBaseType_t t = 0; t < n; t++) {
    const char *name = tasks[t].pcTaskName;
    uint32_t free_now = tasks[t].usStackHighWaterMark;
    size_t k = 0;
    while (k < known && strncmp(names[k], name, 15) != 0)
      k++;
    bool is_new = k == known;
    if (is_new && known < 48) {
      strncpy(names[k], name, 15);
      names[k][15] = 0;
      lows[k] = free_now;
      known++;
    }
    bool dropped = !is_new && k < 48 && free_now < lows[k];
    if (dropped)
      lows[k] = free_now;
    if (full || dropped || (is_new && calls > 1)) {
      ESP_LOGW("trace", "stack %-16s free low %5u B%s%s prio %u", name, (unsigned) free_now,
               dropped ? " (NEW LOW)" : "", is_new ? " (new task)" : "",
               (unsigned) tasks[t].uxCurrentPriority);
    }
  }
}
