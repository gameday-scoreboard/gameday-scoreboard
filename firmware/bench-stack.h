#pragma once

// Bench only, not for merging: measures stack use on the panel.
//  - log_tasks(): every task's high-water mark (bytes never touched).
//  - check_loop(): logs each new low of the loop task, with how deep it got.
//  - arm_watch(depth): a store watchpoint at that many bytes into the loop
//    task's stack. The first write that deep panics, and the crash report
//    keeps the backtrace of the deepest path (decode with the build's ELF).
// Included into every component file by `includes:`, so everything is inline.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "esp_cpu.h"
#include "esp_ipc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esphome/core/log.h"

namespace bench_stack {

static constexpr uint32_t LOOP_STACK = 16384;

inline uint32_t &loop_low() {
  static uint32_t v = UINT32_MAX;
  return v;
}

inline void log_tasks() {
  UBaseType_t n = uxTaskGetNumberOfTasks() + 2;
  auto *tasks = (TaskStatus_t *) malloc(n * sizeof(TaskStatus_t));
  if (tasks == nullptr) {
    ESP_LOGW("stack", "no memory for %u task records", (unsigned) n);
    return;
  }
  UBaseType_t got = uxTaskGetSystemState(tasks, n, nullptr);
  std::string line;
  for (UBaseType_t i = 0; i < got; i++) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s=%u ", tasks[i].pcTaskName, (unsigned) tasks[i].usStackHighWaterMark);
    line += buf;
    if (line.size() > 160) {
      ESP_LOGI("stack", "hwm bytes: %s", line.c_str());
      line.clear();
    }
  }
  if (!line.empty())
    ESP_LOGI("stack", "hwm bytes: %s", line.c_str());
  free(tasks);
}

// Call from the loop task.
inline void check_loop() {
  uint32_t hwm = uxTaskGetStackHighWaterMark(nullptr);
  if (hwm < loop_low()) {
    loop_low() = hwm;
    ESP_LOGI("stack", "loopTask new low: %u never touched, %u used", (unsigned) hwm, (unsigned) (LOOP_STACK - hwm));
  }
  static uint32_t httpd_low = UINT32_MAX;
  TaskHandle_t h = xTaskGetHandle("httpd");
  if (h != nullptr) {
    uint32_t v = uxTaskGetStackHighWaterMark(h);
    if (v < httpd_low) {
      httpd_low = v;
      ESP_LOGI("stack", "httpd new low: %u never touched, %u used", (unsigned) v, (unsigned) (4512 - v));
    }
  }
}

// Call from the loop task (a button's on_press runs there). The loop task is
// pinned to core 1 and the watchpoint is per core, which is the core it runs on.
inline void arm_watch(uint32_t depth) {
  uint8_t *start = pxTaskGetStackStart(xTaskGetCurrentTaskHandle());
  uintptr_t addr = ((uintptr_t) start + LOOP_STACK - depth) & ~(uintptr_t) 31;
  esp_err_t err = esp_cpu_set_watchpoint(0, (const void *) addr, 32, ESP_CPU_WATCHPOINT_STORE);
  ESP_LOGW("stack", "watchpoint at %p, %u bytes into the loop stack, core %d: %s", (void *) addr,
           (unsigned) ((uintptr_t) start + LOOP_STACK - addr), xPortGetCoreID(), esp_err_to_name(err));
}

inline void disarm_watch() {
  esp_cpu_clear_watchpoint(0);
  ESP_LOGW("stack", "watchpoint cleared");
}

// The same for another task's stack (httpd: ESPHome gives it 4512 bytes). That
// task isn't pinned, so watchpoint 1 goes on both cores through esp_ipc.
inline uintptr_t &task_watch_addr() {
  static uintptr_t a = 0;
  return a;
}

inline void arm_task_watch(const char *name, uint32_t size, uint32_t depth) {
  TaskHandle_t h = xTaskGetHandle(name);
  if (h == nullptr) {
    ESP_LOGW("stack", "no task named %s", name);
    return;
  }
  uint8_t *start = pxTaskGetStackStart(h);
  task_watch_addr() = ((uintptr_t) start + size - depth) & ~(uintptr_t) 31;
  esp_ipc_func_t set = [](void *) {
    esp_cpu_set_watchpoint(1, (const void *) task_watch_addr(), 32, ESP_CPU_WATCHPOINT_STORE);
  };
  esp_ipc_call_blocking(0, set, nullptr);
  esp_ipc_call_blocking(1, set, nullptr);
  ESP_LOGW("stack", "watchpoint 1 at %p, %u bytes into %s's stack, both cores", (void *) task_watch_addr(),
           (unsigned) ((uintptr_t) start + size - task_watch_addr()), name);
}

}  // namespace bench_stack
