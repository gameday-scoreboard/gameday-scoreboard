#pragma once

// When the boot after "Reset Wi-Fi" gives up on setup and restarts onto the
// saved networks (gd_setup_window in gameday-common.yaml). Plain C++ with no
// ESPHome in it, so tests/test_networks.cpp can build it on a PC.
//
// Fifteen minutes is the window. Someone still on Bluetooth or on the hotspot
// then gets up to ten more. A handover from the app that failed (a wrong name
// or password) leaves the panel in setup so the app can try again, and once
// nobody has been on Bluetooth or the hotspot for a minute after that, the
// owner has gone and the panel goes back to the networks it knows.

#include <cstdint>

namespace wifi_list {

struct SetupWindow {
  static constexpr uint32_t WINDOW_S = 15 * 60;
  static constexpr uint32_t MAX_S = 25 * 60;
  static constexpr uint32_t FAILED_IDLE_S = 60;

  // now_s: seconds since boot. busy: the app is connected over Bluetooth, a
  // handover is under way, or someone is on the hotspot. handover_failed: a
  // handover from the app has failed this boot.
  bool restart_due(uint32_t now_s, bool busy, bool handover_failed) {
    if (now_s >= MAX_S)
      return true;
    if (busy) {
      this->idle_ = false;
      return false;
    }
    if (!this->idle_) {
      this->idle_ = true;
      this->idle_since_s_ = now_s;
    }
    if (now_s >= WINDOW_S)
      return true;
    return handover_failed && now_s - this->idle_since_s_ >= FAILED_IDLE_S;
  }

 private:
  bool idle_{false};
  uint32_t idle_since_s_{0};
};

}  // namespace wifi_list
