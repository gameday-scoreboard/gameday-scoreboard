#pragma once

#include <cstdint>

namespace esphome {
namespace fake_clock {
inline uint32_t now_ms = 0;
}
inline uint32_t millis() { return fake_clock::now_ms; }
}  // namespace esphome
