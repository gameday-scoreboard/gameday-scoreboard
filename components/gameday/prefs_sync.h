// When a settings change reaches flash. ESPHome's ESP32 preferences keep every
// save() in memory and write flash from a 60 s timer or a clean shutdown, so a
// panel unplugged within the minute of a change comes back without it. The
// firmware asks for the write itself after an owner's change (sync_prefs_ in
// gameday.cpp); this gate folds the several changes of one /gameday/set request
// or one settings restore into a single write at its end. Pure, so the host
// tests compile it directly.
#pragma once

#include <cstdint>

namespace espn {

class PrefsSyncGate {
 public:
  // A setting was saved. True: write flash now. False: a batch is still
  // applying its keys, and the write is owed when it ends.
  bool request() {
    if (this->depth_ == 0)
      return true;
    this->owed_ = true;
    return false;
  }
  // A batch that can change several settings starts applying them. Batches
  // nest: a restore applies its /gameday/set keys inside its own batch.
  void begin() { this->depth_++; }
  // A batch is done. True when it was the outermost one and a write is owed.
  bool end() {
    if (this->depth_ == 0 || --this->depth_ != 0)
      return false;
    bool owed = this->owed_;
    this->owed_ = false;
    return owed;
  }
  bool batching() const { return this->depth_ != 0; }

 private:
  uint8_t depth_ = 0;
  bool owed_ = false;
};

}  // namespace espn
