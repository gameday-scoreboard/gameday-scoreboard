// What the panel says about its heat and its power, and the one thing it does
// about them: a dim start for a few seconds after a brownout restart. Pure,
// with no ESPHome includes, so the host tests compile it directly.
//
// Heat: the ESP32-S3's own temperature sensor. It reads the chip, which runs
// warmer than the air in the case, so the limits below are chip readings. At
// kHotC the panel shows a thermometer in its corner, until the chip reads
// under kCoolC. The brightness stays the owner's.
//
// Power: a brownout restart (the supply dipped below what the chip needs) is
// counted in flash, and the start after one lights the display at half the
// owner's brightness for kDipMs, so a brick that only just carries the panel
// gets it past the surge. A plug shows in the corner until the panel has run
// kPowerSettledMs without another brownout.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace espn {

// Chip temperatures, degrees C. A panel indoors at full brightness reads
// about 46 C, 20 to 25 C over the room. So 75 C is about 50 C around the
// panel, short of the 65 C the module is rated for and the 70 C the parts and
// the PETG case are held to.
static const float kHotC = 75.0f;
static const float kCoolC = 70.0f;

struct HeatGuard {
  bool hot = false;    // the thermometer shows
  float last_c = NAN;  // the last reading, NAN before the first

  // A new reading. True when hot changed.
  bool feed(float c) {
    if (std::isnan(c))
      return false;
    this->last_c = c;
    const bool before = this->hot;
    if (c >= kHotC)
      this->hot = true;
    else if (c < kCoolC)
      this->hot = false;
    return this->hot != before;
  }
};

// How long the display runs at half brightness after a brownout restart,
// from the moment it first lights
static const uint32_t kDipMs = 3000;
// A start that runs this long without a brownout clears the plug
static const uint32_t kPowerSettledMs = 10 * 60 * 1000;

// Kept in flash. Eight bytes, as it was when it also held a streak and a cap,
// so a panel's count survives the update.
struct PowerLog {
  uint32_t brownouts;  // every brownout restart this panel has had
  uint8_t unused[4];
} __attribute__((packed));

// At boot, with whether this start was a brownout. True when the log changed.
inline bool power_log_start(PowerLog &log, bool brownout) {
  if (!brownout)
    return false;
  log.brownouts++;
  return true;
}

// The dim start after a brownout restart
struct PowerDip {
  bool pending = false;  // a brownout start whose display hasn't lit yet
  bool on = false;       // the display is lit and inside kDipMs
  uint32_t lit_ms = 0;

  // The display is about to be driven at a brightness above 0. True while
  // the dip applies; the first call after a brownout start opens the window.
  bool light(uint32_t now_ms) {
    if (this->pending) {
      this->pending = false;
      this->on = true;
      this->lit_ms = now_ms;
    }
    if (this->on && now_ms - this->lit_ms >= kDipMs)
      this->on = false;
    return this->on;
  }
  // From the main loop. True once, when the window has just closed and the
  // owner's brightness needs applying.
  bool tick(uint32_t now_ms) {
    if (!this->on || now_ms - this->lit_ms < kDipMs)
      return false;
    this->on = false;
    return true;
  }
};

// The brightness to drive for the owner's setting (1 to 255)
inline uint8_t dipped_brightness(uint8_t want, bool dip) {
  if (!dip)
    return want;
  unsigned b = want / 2;
  if (want > 0 && b == 0)
    b = 1;
  return (uint8_t) b;
}

// What the panel shows in its corner and on its bottom line, and the page's
// guard state
enum class GuardMark : uint8_t { NONE = 0, HEAT, POWER };

// Power wins when both show: a weak brick is the likelier cause of trouble.
inline GuardMark guard_mark(bool hot, bool dipped) {
  if (dipped)
    return GuardMark::POWER;
  return hot ? GuardMark::HEAT : GuardMark::NONE;
}

// The keys added to the health object: ,"temp":41.5,"heat":0,"brownouts":0,"power":"ok"
// temp is left out before the first reading. heat is 1 while the thermometer
// shows, power "dipped" while the plug does.
inline int guard_json_keys(char *buf, size_t size, float temp_c, bool hot, uint32_t brownouts, bool dipped) {
  char temp[24] = "";
  if (!std::isnan(temp_c))
    snprintf(temp, sizeof(temp), ",\"temp\":%.1f", temp_c);
  return snprintf(buf, size, "%s,\"heat\":%u,\"brownouts\":%u,\"power\":\"%s\"", temp, hot ? 1u : 0u,
                  (unsigned) brownouts, dipped ? "dipped" : "ok");
}

}  // namespace espn
