// PSRAM for the component's own containers: logos, the favorites list, the
// finals rotation. Plain new and malloc only use internal RAM in this build.
// Pure, with no ESPHome includes, so the host tests compile it; there it is
// the plain heap. JSON documents use ESPHome's json::SpiRamAllocator instead
// (espn_parse_impl.h's json_allocator()).
#pragma once

#include <cstddef>
#include <cstdlib>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace espn {

// PSRAM first, internal RAM only if PSRAM is full. Aborts on failure, as
// operator new does with exceptions off.
inline void *psram_alloc(size_t size) {
#ifdef ESP_PLATFORM
  void *p = heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
#else
  void *p = malloc(size);
#endif
  if (p == nullptr)
    abort();
  return p;
}

// ESP-IDF's free takes memory from either heap.
inline void psram_free(void *p) { free(p); }

template<class T> struct PsramAllocator {
  using value_type = T;
  PsramAllocator() = default;
  template<class U> PsramAllocator(const PsramAllocator<U> &) {}
  T *allocate(size_t n) { return static_cast<T *>(psram_alloc(n * sizeof(T))); }
  void deallocate(T *p, size_t) { psram_free(p); }
  template<class U> bool operator==(const PsramAllocator<U> &) const { return true; }
  template<class U> bool operator!=(const PsramAllocator<U> &) const { return false; }
};

}  // namespace espn
