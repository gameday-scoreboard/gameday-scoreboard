// Host stand-in for ESP-IDF's heap_caps, for test_logo_cache.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

namespace fake_heap {
inline bool psram_full = false;  // heap_caps_malloc from PSRAM fails
inline int psram_blocks = 0;     // heap_caps_malloc blocks not freed yet
}  // namespace fake_heap

inline void *heap_caps_malloc_prefer(size_t size, size_t, ...) { return malloc(size); }
inline void *heap_caps_malloc(size_t size, uint32_t) {
  if (fake_heap::psram_full)
    return nullptr;
  fake_heap::psram_blocks++;
  return malloc(size);
}
inline void heap_caps_free(void *p) {
  if (p != nullptr)
    fake_heap::psram_blocks--;
  free(p);
}
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 0; }
