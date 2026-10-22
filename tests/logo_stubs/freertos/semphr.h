#pragma once

#include "FreeRTOS.h"

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  static int mutex;
  return &mutex;
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return pdPASS; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdPASS; }
