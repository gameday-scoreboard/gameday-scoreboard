// Why the panel last started, for the health block in /gameday/state. Pure,
// with no ESPHome includes, so the host tests compile it directly.
//
// The chip only knows why this start happened. A crash followed by a restart
// (the owner pressing Reboot, the panel's own Wi-Fi restart, an update) would
// hide the crash, so a small BootLog in RTC memory keeps the last crash,
// watchdog or brownout until the power goes off. RTC memory survives a
// restart, a crash and a watchdog, not a power cut; a log that doesn't check
// out is started over.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace espn {

// esp_reset_reason_t from esp_system.h, by value (gameday.cpp checks them).
static const int kResetUnknown = 0;
static const int kResetPowerOn = 1;
static const int kResetExternal = 2;
static const int kResetSoftware = 3;
static const int kResetPanic = 4;
static const int kResetIntWdt = 5;
static const int kResetTaskWdt = 6;
static const int kResetWdt = 7;
static const int kResetDeepSleep = 8;
static const int kResetBrownout = 9;
static const int kResetSdio = 10;
static const int kResetUsb = 11;
static const int kResetJtag = 12;
static const int kResetEfuse = 13;
static const int kResetPowerGlitch = 14;
static const int kResetCpuLockup = 15;

enum class Restart : uint8_t {
  NONE = 0,
  POWER,     // the power came on (plugged in, or the reset button)
  RESTART,   // the firmware asked: Reboot, a layout change, the Wi-Fi restart
  UPDATE,    // the firmware asked, and a different build came up
  CRASH,     // a panic or a locked-up CPU
  WATCHDOG,  // a task or an interrupt hung
  BROWNOUT,  // the supply dipped too low, a weak or overloaded power brick
  USB,       // reset over USB, by a flasher
  OTHER,
};

// The key the page and the app read.
inline const char *restart_key(Restart r) {
  switch (r) {
    case Restart::NONE:
      return "none";
    case Restart::POWER:
      return "power";
    case Restart::RESTART:
      return "restart";
    case Restart::UPDATE:
      return "update";
    case Restart::CRASH:
      return "crash";
    case Restart::WATCHDOG:
      return "watchdog";
    case Restart::BROWNOUT:
      return "brownout";
    case Restart::USB:
      return "usb";
    case Restart::OTHER:
      break;
  }
  return "other";
}

// The starts that mean something went wrong, kept in BootLog::fault.
inline bool restart_is_fault(Restart r) {
  return r == Restart::CRASH || r == Restart::WATCHDOG || r == Restart::BROWNOUT;
}

// planned: the firmware marked the restart on its way down (on_shutdown).
// new_build: the build that came up isn't the one that marked it.
inline Restart classify_restart(int reason, bool planned, bool new_build) {
  switch (reason) {
    case kResetPowerOn:
    case kResetExternal:
      return Restart::POWER;
    case kResetSoftware:
      return planned && new_build ? Restart::UPDATE : Restart::RESTART;
    case kResetWdt:
      // On the S3 with PSRAM, esp_restart() sometimes comes back as this
      // watchdog instead of a software reset; the mark tells them apart.
      if (planned)
        return new_build ? Restart::UPDATE : Restart::RESTART;
      return Restart::WATCHDOG;
    case kResetIntWdt:
    case kResetTaskWdt:
      return Restart::WATCHDOG;
    case kResetPanic:
    case kResetCpuLockup:
      return Restart::CRASH;
    case kResetBrownout:
    case kResetPowerGlitch:
      return Restart::BROWNOUT;
    case kResetUsb:
    case kResetJtag:
      return Restart::USB;
    default:
      return Restart::OTHER;
  }
}

static const uint32_t kBootLogMagic = 0x47444231;  // "GDB1"

// Laid out with no padding, so the check covers every byte before it.
struct BootLog {
  uint32_t magic;
  uint32_t boots;       // starts since the power came on, this one included
  int64_t fault_at;     // wall time of the last fault, 0 until the clock is known
  uint32_t build;       // the running build, to tell an update from a restart
  uint32_t fault_boot;  // the boots count of the last fault's start
  uint8_t planned;      // 1 from on_shutdown until the next start
  uint8_t fault;        // Restart of the last crash, watchdog or brownout
  uint8_t pad[2];
  uint32_t check;
};
static_assert(sizeof(BootLog) == 32, "BootLog has padding");

inline uint32_t boot_log_check(const BootLog &log) {
  // FNV-1a over everything before check
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&log);
  uint32_t h = 2166136261u;
  for (unsigned i = 0; i < (unsigned) offsetof(BootLog, check); i++)
    h = (h ^ p[i]) * 16777619u;
  return h;
}

inline void boot_log_seal(BootLog &log) { log.check = boot_log_check(log); }

inline bool boot_log_valid(const BootLog &log) {
  return log.magic == kBootLogMagic && log.check == boot_log_check(log) && log.fault <= (uint8_t) Restart::OTHER;
}

// A wall clock reads past this once it has been set (2024-01-01).
static const int64_t kClockValidAfter = 1704067200;

// At boot: records this start and returns why it happened. now is the wall
// clock, which a restart keeps; anything before kClockValidAfter is unknown.
inline Restart boot_log_start(BootLog &log, int reason, uint32_t build, int64_t now) {
  if (!boot_log_valid(log)) {
    log = BootLog{};
    log.magic = kBootLogMagic;
  }
  const bool planned = log.planned != 0;
  const bool new_build = log.boots != 0 && log.build != build;
  Restart r = classify_restart(reason, planned, new_build);
  if (r == Restart::POWER) {
    // A power cut normally scrambles RTC memory, but not always.
    log = BootLog{};
    log.magic = kBootLogMagic;
  }
  log.boots++;
  log.build = build;
  log.planned = 0;
  if (restart_is_fault(r)) {
    log.fault = (uint8_t) r;
    log.fault_boot = log.boots;
    log.fault_at = now >= kClockValidAfter ? now : 0;
  }
  boot_log_seal(log);
  return r;
}

// On the way down from a restart the firmware asked for.
inline void boot_log_planned(BootLog &log) {
  if (!boot_log_valid(log))
    return;
  log.planned = 1;
  boot_log_seal(log);
}

// Once the clock is set: the time a fault at this start happened.
inline void boot_log_clock(BootLog &log, int64_t boot_epoch) {
  if (!boot_log_valid(log) || log.fault == 0 || log.fault_boot != log.boots || log.fault_at != 0 ||
      boot_epoch < kClockValidAfter)
    return;
  log.fault_at = boot_epoch;
  boot_log_seal(log);
}

// Seconds since the last fault, or -1 when there is none or its time isn't
// known. A fault at this start is as old as the uptime.
inline int64_t boot_log_fault_ago(const BootLog &log, int64_t now, int64_t uptime_s) {
  if (!boot_log_valid(log) || log.fault == 0)
    return -1;
  if (log.fault_boot == log.boots)
    return uptime_s;
  if (log.fault_at == 0 || now < kClockValidAfter || now < log.fault_at)
    return -1;
  return now - log.fault_at;
}

// Everything the health block says, gathered by the caller.
struct Health {
  int64_t uptime_s;
  Restart start;        // why this start happened
  uint32_t boots;       // starts since the power came on
  Restart fault;        // the last crash, watchdog or brownout, NONE for none
  int64_t fault_ago;    // seconds, -1 when not known
  float heap_kb;        // free internal RAM
  float block_kb;       // its largest free block
  float psram_kb;       // free PSRAM
  bool wifi;            // connected, so rssi means something
  int rssi;
  int espn_status;      // HTTP status of the last ESPN read, 0 for no answer
  int64_t espn_ago;     // seconds since it, -1 before the first read
  const char *version;  // the release, "2026.10.0"
  int64_t built;        // the build's compile time, Unix seconds
  const char *channel;  // stable, beta, bar or none
};

// The "health" object in /gameday/state. Optional keys are left out rather
// than sent empty: fault and fault_ago, rssi off Wi-Fi, espn before a read.
// Returns what snprintf does; the strings are the firmware's own, no quotes.
inline int health_json(char *buf, size_t size, const Health &h) {
  char fault[64] = "";
  if (h.fault != Restart::NONE) {
    if (h.fault_ago >= 0)
      snprintf(fault, sizeof(fault), ",\"fault\":\"%s\",\"fault_ago\":%lld", restart_key(h.fault),
               (long long) h.fault_ago);
    else
      snprintf(fault, sizeof(fault), ",\"fault\":\"%s\"", restart_key(h.fault));
  }
  char rssi[24] = "";
  if (h.wifi)
    snprintf(rssi, sizeof(rssi), ",\"rssi\":%d", h.rssi);
  char espn[48] = "";
  if (h.espn_ago >= 0)
    snprintf(espn, sizeof(espn), ",\"espn\":%d,\"espn_ago\":%lld", h.espn_status, (long long) h.espn_ago);
  return snprintf(buf, size,
                  "{\"up\":%lld,\"start\":\"%s\",\"boots\":%u%s,\"heap_kb\":%.1f,\"block_kb\":%.1f,"
                  "\"psram_kb\":%.1f%s%s,\"version\":\"%s\",\"built\":%lld,\"channel\":\"%s\"}",
                  (long long) h.uptime_s, restart_key(h.start), (unsigned) h.boots, fault, h.heap_kb, h.block_kb,
                  h.psram_kb, rssi, espn, h.version, (long long) h.built, h.channel);
}

}  // namespace espn
