// When a settings change reaches flash. ESPHome's ESP32 preferences keep every
// save() in memory and write flash from a 60 s timer or a clean shutdown, so a
// panel unplugged within the minute of a change comes back without it. The
// firmware asks for the write itself after an owner's change (sync_prefs_ in
// gameday.cpp); this gate folds the several changes of one /gameday/set request
// into a single write at its end. Pure, so the host tests compile it directly.
#pragma once

namespace espn {

class PrefsSyncGate {
 public:
  // A setting was saved. True: write flash now. False: a request is still
  // applying its keys, and the write is owed when it ends.
  bool request() {
    if (!this->batching_)
      return true;
    this->owed_ = true;
    return false;
  }
  // A request that can change several settings starts applying them.
  void begin() { this->batching_ = true; }
  // The request is done. True when a write is owed for it.
  bool end() {
    this->batching_ = false;
    bool owed = this->owed_;
    this->owed_ = false;
    return owed;
  }
  bool batching() const { return this->batching_; }

 private:
  bool batching_ = false;
  bool owed_ = false;
};

}  // namespace espn
