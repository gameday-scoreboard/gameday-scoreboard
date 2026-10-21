// The night schedule: between two times the owner sets, the panel runs at
// its own night brightness or turns the screen off. A live game of the
// owner's team or one of their favorites lights it at the normal brightness
// until the game ends. Off out of the box. Pure, with no ESPHome includes,
// so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace espn {

// Minutes after local midnight
static const uint16_t kNightStartDefault = 23 * 60;
static const uint16_t kNightEndDefault = 7 * 60;
// Out of 255, like the Brightness setting
static const uint8_t kNightLevelDefault = 16;
static const uint16_t kMinutesPerDay = 24 * 60;

// Kept in flash. Its size is fixed: a blob that changes size fails to load
// and resets every setting in it, so later settings take the spare bytes.
struct NightPrefs {
  uint8_t on;           // 1 = the schedule runs
  uint16_t start;       // minutes after midnight
  uint16_t end;         // minutes after midnight, may be before start (past midnight)
  uint8_t screen_off;   // 1 = dark at night, 0 = at level
  uint8_t level;        // 1 to 255
  uint8_t spare[3];
} __attribute__((packed));

inline NightPrefs night_defaults() {
  NightPrefs p{};
  p.start = kNightStartDefault;
  p.end = kNightEndDefault;
  p.level = kNightLevelDefault;
  return p;
}

// A blob from flash that can't be right (a later firmware's, or damage)
// goes back to the defaults field by field.
inline void night_sanitize(NightPrefs &p) {
  const NightPrefs d = night_defaults();
  if (p.start >= kMinutesPerDay)
    p.start = d.start;
  if (p.end >= kMinutesPerDay)
    p.end = d.end;
  if (p.level == 0)
    p.level = d.level;
  p.on = p.on ? 1 : 0;
  p.screen_off = p.screen_off ? 1 : 0;
}

// "23:00" or "7:05" -> minutes after midnight, -1 for anything else
inline int night_parse_time(const std::string &v) {
  size_t colon = v.find(':');
  if (colon == std::string::npos || colon == 0 || colon > 2 || v.size() != colon + 3)
    return -1;
  int h = 0, m = 0;
  for (size_t i = 0; i < v.size(); i++) {
    if (i == colon)
      continue;
    if (v[i] < '0' || v[i] > '9')
      return -1;
    if (i < colon)
      h = h * 10 + (v[i] - '0');
    else
      m = m * 10 + (v[i] - '0');
  }
  if (h > 23 || m > 59)
    return -1;
  return h * 60 + m;
}

// Minutes after midnight -> "07:00", the form the page's time fields use
inline std::string night_time_text(uint16_t minutes) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned) (minutes / 60 % 24), (unsigned) (minutes % 60));
  return buf;
}

// True when the minute is inside [start, end), across midnight when end is
// earlier than start. The same start and end is an empty window.
inline bool night_window(uint16_t start, uint16_t end, int minute) {
  if (start == end)
    return false;
  if (start < end)
    return minute >= start && minute < end;
  return minute >= start || minute < end;
}

// What the schedule is doing right now
enum class Night : uint8_t {
  DAY = 0,   // off, outside its hours, or no clock yet
  DIM,       // at the night brightness
  DARK,      // screen off
  GAME,      // its hours, but a live game of the owner's has the panel lit
};

// clock_valid false (no time yet) never darkens the panel: a dark panel
// that can't say why looks broken.
inline Night night_state(const NightPrefs &p, bool clock_valid, int minute, bool owner_live) {
  if (!p.on || !clock_valid || !night_window(p.start, p.end, minute))
    return Night::DAY;
  if (owner_live)
    return Night::GAME;
  return p.screen_off ? Night::DARK : Night::DIM;
}

// The brightness the owner wants right now, before heat and power have their
// say (guard.h): the night level, 0 for screen off, or the day setting.
inline uint8_t night_brightness(uint8_t day, Night state, const NightPrefs &p) {
  switch (state) {
    case Night::DIM:
      return p.level;
    case Night::DARK:
      return 0;
    default:
      return day;
  }
}

// For the state document's night_now and the log
inline const char *night_word(Night state) {
  switch (state) {
    case Night::DIM:
      return "dim";
    case Night::DARK:
      return "off";
    case Night::GAME:
      return "game";
    default:
      return "";
  }
}

}  // namespace espn
