// Host stand-in for ESPHome's StaticTask, for test_logo_cache.cpp: a start
// records the task as created and woken (fake_rtos in freertos/FreeRTOS.h),
// or fails while the test says so.
#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"

namespace esphome {

class StaticTask {
 public:
  bool is_created() const { return this->created_; }
  TaskHandle_t get_handle() const { return this->created_ ? (TaskHandle_t) this : nullptr; }
  bool create(TaskFunction_t, const char *, uint32_t, void *, unsigned, bool use_psram) {
    if (fake_rtos::fail_starts > 0) {
      fake_rtos::fail_starts--;
      return false;
    }
    fake_rtos::starts++;
    fake_rtos::woken = true;
    fake_rtos::psram_stack = use_psram;
    this->created_ = true;
    return true;
  }

 protected:
  bool created_{false};
};

}  // namespace esphome
