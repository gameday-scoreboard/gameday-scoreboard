// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

#include <cstdint>

namespace esphome {
namespace gameday {

// The web server task's stack, in bytes (web_stack.cpp). ESPHome's own is
// 4,512; the deepest request measured used 4,092 of it.
static constexpr uint32_t WEB_STACK_SIZE = 5632;

}  // namespace gameday
}  // namespace esphome
