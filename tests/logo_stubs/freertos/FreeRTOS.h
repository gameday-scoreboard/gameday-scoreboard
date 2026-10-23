// Host stand-in for FreeRTOS, for test_logo_cache.cpp. Single threaded: a
// mutex is a no-op, and xTaskCreate only records the task so the test runs it
// when it chooses (or refuses it, as a fragmented heap does).
#pragma once

#include <cstdint>

using BaseType_t = int;
using TaskFunction_t = void (*)(void *);
using TaskHandle_t = void *;
using SemaphoreHandle_t = void *;

#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xffffffffu

namespace fake_rtos {
// Set by the test: how many of the next task starts fail.
inline int fail_starts = 0;
inline int starts = 0;  // successful starts
inline TaskFunction_t task = nullptr;
inline void *task_arg = nullptr;
}  // namespace fake_rtos

inline BaseType_t xTaskCreate(TaskFunction_t fn, const char *, uint32_t, void *arg, unsigned, TaskHandle_t *) {
  if (fake_rtos::fail_starts > 0) {
    fake_rtos::fail_starts--;
    return pdFAIL;
  }
  fake_rtos::starts++;
  fake_rtos::task = fn;
  fake_rtos::task_arg = arg;
  return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {}
