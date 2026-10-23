#pragma once

#include <cstdio>

namespace fake_log {
inline int warnings = 0;
}

#define ESP_LOGW(tag, ...)        \
  do {                            \
    fake_log::warnings++;         \
    if (0)                        \
      printf(__VA_ARGS__);        \
  } while (0)
#define ESP_LOGI(tag, ...)        \
  do {                            \
    if (0)                        \
      printf(__VA_ARGS__);        \
  } while (0)
#define ESP_LOGD ESP_LOGI
