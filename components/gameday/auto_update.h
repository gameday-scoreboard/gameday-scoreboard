// "Install updates automatically": when an opted-in panel may install a new
// release on its own. Pure, with no ESPHome includes, so the host tests
// compile it directly.
//
// The rules: only between 3 and 5 AM panel time, never while a game the
// panel follows is live, and one try a night. A failed or interrupted
// install leaves the old firmware running (the new image only boots once it
// is fully written and checked), and the next try is the next night.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

namespace espn {

// The quiet window, local hours [start, end).
static const int kAutoUpdateStartHour = 3;
static const int kAutoUpdateEndHour = 5;
// Each panel starts at its own minute in the first hour so a house full of
// panels, or the whole fleet, doesn't download at once. The second hour is
// left for a panel held off by a late game.
static const int kAutoUpdateSpreadMinutes = 60;

// The panel's own start minute, 0 to kAutoUpdateSpreadMinutes - 1, from the
// last two bytes of its MAC.
inline int auto_update_slot(uint8_t mac4, uint8_t mac5) {
  return (int) (((uint32_t) mac4 << 8 | mac5) % kAutoUpdateSpreadMinutes);
}

// A number that changes once per local calendar day, for "tried tonight".
inline int32_t auto_update_day(int year, int day_of_year) { return (int32_t) year * 1000 + day_of_year; }

// True when `latest` is a higher version than `current`. Both are dotted
// numbers ("1.5.5", "2026.10.0"); anything after the numbers ("-beta") is
// ignored. A panel on a test build newer than the release never "updates"
// down to it, and a malformed version never installs.
inline bool version_newer(const std::string &latest, const std::string &current) {
  const char *a = latest.c_str();
  const char *b = current.c_str();
  if (*a < '0' || *a > '9' || *b < '0' || *b > '9')
    return false;
  for (int part = 0; part < 4; part++) {
    char *ea;
    char *eb;
    long x = strtol(a, &ea, 10);
    long y = strtol(b, &eb, 10);
    if (ea == a)
      x = 0;
    if (eb == b)
      y = 0;
    if (x != y)
      return x > y;
    a = (*ea == '.') ? ea + 1 : ea;
    b = (*eb == '.') ? eb + 1 : eb;
  }
  return false;
}

enum class AutoUpdateStep : uint8_t {
  IDLE,       // off, outside the window, before the panel's minute, or done for tonight
  CHECK,      // read the manifest now, then come back
  WAIT_GAME,  // an update is waiting but a followed game is live
  INSTALL,    // record tonight's try, then install
};

struct AutoUpdateInputs {
  bool enabled{false};
  bool clock_valid{false};
  int hour{0};  // local
  int minute{0};
  int32_t today{0};      // auto_update_day() for the local date
  int32_t tried_day{0};  // the night of the last install try, saved in flash
  int32_t checked_day{0};  // the night the manifest was last read for this, not saved
  int slot{0};             // auto_update_slot()
  bool check_done{false};  // tonight's read has had time to come back
  bool available{false};   // the update entity says a different version is out
  bool newer{false};       // and version_newer() agrees it is newer
  bool live{false};        // a game the panel follows is in progress
};

inline AutoUpdateStep auto_update_step(const AutoUpdateInputs &in) {
  if (!in.enabled || !in.clock_valid || in.tried_day == in.today)
    return AutoUpdateStep::IDLE;
  if (in.hour < kAutoUpdateStartHour || in.hour >= kAutoUpdateEndHour)
    return AutoUpdateStep::IDLE;
  int minutes = (in.hour - kAutoUpdateStartHour) * 60 + in.minute;
  if (minutes < in.slot)
    return AutoUpdateStep::IDLE;
  if (in.checked_day != in.today)
    return AutoUpdateStep::CHECK;
  if (!in.check_done || !in.available || !in.newer)
    return AutoUpdateStep::IDLE;
  if (in.live)
    return AutoUpdateStep::WAIT_GAME;
  return AutoUpdateStep::INSTALL;
}

}  // namespace espn
