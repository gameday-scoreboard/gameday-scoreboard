// Host stand-in for FreeRTOS, for test_logo_cache.cpp. Single threaded: a
// mutex is a no-op, and starting the logo task (esphome/core/static_task.h)
// or waking it only records that it has work, so the test runs it when it
// chooses (or refuses the start, as a full heap does).
#pragma once

#include <cstdint>

using BaseType_t = int;
using TaskFunction_t = void (*)(void *);
using TaskHandle_t = void *;
using SemaphoreHandle_t = void *;

#define pdTRUE 1
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY 0xffffffffu

namespace fake_rtos {
// Set by the test: how many of the next task starts fail.
inline int fail_starts = 0;
inline int starts = 0;  // successful starts
inline int wakes = 0;     // wake-ups sent to a running task
inline bool woken = false;  // the task has been started or woken since it last ran
inline bool psram_stack = false;  // the last start asked for its stack in PSRAM
}  // namespace fake_rtos

inline void xTaskNotifyGive(TaskHandle_t) {
  fake_rtos::wakes++;
  fake_rtos::woken = true;
}
// The task's wait. The tests never run the task's loop, only its work.
inline uint32_t ulTaskNotifyTake(BaseType_t, uint32_t) { return 1; }
