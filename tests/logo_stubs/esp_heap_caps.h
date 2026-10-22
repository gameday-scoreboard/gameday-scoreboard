// Host stand-in for ESP-IDF's heap_caps, for test_logo_cache.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

inline void *heap_caps_malloc_prefer(size_t size, size_t, ...) { return malloc(size); }
inline void heap_caps_free(void *p) { free(p); }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 0; }
