// Keeps the panel from cooking itself or its power brick. Pure, with no
// ESPHome includes, so the host tests compile it directly.
//
// Heat: the ESP32-S3's own temperature sensor. It reads the chip, which runs
// warmer than the air in the case, so the limits below are chip readings.
// Past kHotC the panel steps its brightness down, one step every
// kHotStepMs while it stays hot, and comes back one step every kCoolStepMs
// once it reads under kCoolC. kHotFloorC goes straight to the dimmest step.
//
// Power: a brownout restart (the supply dipped below what the chip needs) is
// counted in flash. Two in a row, with no settled run in between, means the
// brick can't carry the panel, so the brightness stays capped at kPowerCap
// until the owner clears it from the device page or the app.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace espn {

// Chip temperatures, degrees C. A panel indoors at full brightness reads
// about 46 C, 20 to 25 C over the room. So 75 C is about 50 C around the
// panel and 85 C about 60 C, both short of the 65 C the module is rated for
// and the 70 C the parts and the PETG case are held to.
static const float kHotC = 75.0f;
static const float kHotFloorC = 85.0f;
static const float kCoolC = 70.0f;
static const uint32_t kHotStepMs = 2 * 60 * 1000;
static const uint32_t kCoolStepMs = 5 * 60 * 1000;

// The owner's brightness at each heat step, in percent
static const uint8_t kHeatSteps = 3;
static const uint8_t kHeatPercent[kHeatSteps + 1] = {100, 70, 45, 25};

struct HeatGuard {
  uint8_t step = 0;            // 0 is full brightness
  uint32_t changed_ms = 0;     // when step last changed
  uint32_t cool_since_ms = 0;  // when it last went under kCoolC, 0 while it isn't
  float last_c = NAN;          // the last reading, NAN before the first

  // A new reading. True when the step changed.
  bool feed(float c, uint32_t now_ms) {
    if (std::isnan(c))
      return false;
    this->last_c = c;
    const uint8_t before = this->step;
    if (c >= kHotFloorC) {
      this->step = kHeatSteps;
    } else if (c >= kHotC) {
      if (this->step == 0 || (this->step < kHeatSteps && now_ms - this->changed_ms >= kHotStepMs))
        this->step++;
    }
    if (c < kCoolC) {
      if (this->cool_since_ms == 0)
        this->cool_since_ms = now_ms == 0 ? 1 : now_ms;
      // Cool for a whole kCoolStepMs, and that long since the last step
      const uint32_t cool_for = now_ms - this->cool_since_ms;
      const uint32_t stepped_for = now_ms - this->changed_ms;
      if (this->step > 0 && cool_for >= kCoolStepMs && stepped_for >= kCoolStepMs)
        this->step--;
    } else {
      this->cool_since_ms = 0;
    }
    if (this->step != before) {
      this->changed_ms = now_ms;
      return true;
    }
    return false;
  }
};

// The cap after repeated brownouts, out of 255
static const uint8_t kPowerCap = 64;
// Brownouts in a row that set the cap
static const uint8_t kBrownoutsToCap = 2;
// A start that runs this long without a brownout ends the run of them
static const uint32_t kPowerSettledMs = 10 * 60 * 1000;

// Kept in flash
struct PowerLog {
  uint32_t brownouts;  // every brownout restart this panel has had
  uint8_t streak;      // brownouts since the last settled run
  uint8_t capped;      // 1 until the owner clears it
  uint8_t pad[2];
} __attribute__((packed));

// At boot, with whether this start was a brownout. True when the log changed.
inline bool power_log_start(PowerLog &log, bool brownout) {
  if (!brownout)
    return false;
  log.brownouts++;
  if (log.streak < 255)
    log.streak++;
  if (log.streak >= kBrownoutsToCap)
    log.capped = 1;
  return true;
}

// After kPowerSettledMs up. True when the log changed.
inline bool power_log_settled(PowerLog &log) {
  if (log.streak == 0)
    return false;
  log.streak = 0;
  return true;
}

// The owner's clear. True when the log changed.
inline bool power_log_clear(PowerLog &log) {
  if (!log.capped && log.streak == 0)
    return false;
  log.capped = 0;
  log.streak = 0;
  return true;
}

// The brightness to drive for the owner's setting (1 to 255).
inline uint8_t guarded_brightness(uint8_t want, uint8_t heat_step, bool power_capped) {
  if (heat_step > kHeatSteps)
    heat_step = kHeatSteps;
  unsigned b = ((unsigned) want * kHeatPercent[heat_step] + 50) / 100;
  if (power_capped && b > kPowerCap)
    b = kPowerCap;
  if (want > 0 && b == 0)
    b = 1;
  return (uint8_t) b;
}

// What the panel shows in its corner and on its bottom line, and the page's
// guard state
enum class GuardMark : uint8_t { NONE = 0, HEAT, POWER };

// Power wins: it lasts until cleared, and a weak brick is the likelier cause
// of trouble than the heat.
inline GuardMark guard_mark(uint8_t heat_step, bool power_capped) {
  if (power_capped)
    return GuardMark::POWER;
  return heat_step > 0 ? GuardMark::HEAT : GuardMark::NONE;
}

// The keys added to the health object: ,"temp":41.5,"heat":0,"brownouts":0,"power":"ok"
// temp is left out before the first reading.
inline int guard_json_keys(char *buf, size_t size, float temp_c, uint8_t heat_step, const PowerLog &log) {
  char temp[24] = "";
  if (!std::isnan(temp_c))
    snprintf(temp, sizeof(temp), ",\"temp\":%.1f", temp_c);
  return snprintf(buf, size, "%s,\"heat\":%u,\"brownouts\":%u,\"power\":\"%s\"", temp, (unsigned) heat_step,
                  (unsigned) log.brownouts, log.capped ? "capped" : "ok");
}

}  // namespace espn
