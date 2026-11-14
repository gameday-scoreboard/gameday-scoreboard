// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// "Install updates automatically": when an opted-in panel may install a new
// release on its own. Pure, with no ESPHome includes, so the host tests
// compile it directly.
//
// The rules: only between 3 and 5 AM panel time, never while a game the
// panel follows is live, and one try a night. A failed or interrupted
// install leaves the old firmware running (the new image only boots once it
// is fully written and checked), and the next try is the next night. A
// release that rolled back on the panel is not installed on its own again
// (UpdateMark below).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
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

// The beta channel's versions are the next release's number plus
// "-beta.<build>" ("2026.11.1-beta.812", scripts/beta_version.py), so a
// beta sits just below the release of the same number: 2026.11.1 is newer
// than every 2026.11.1 beta, and each beta than the one before it.
static const char kBetaTag[] = "-beta.";

struct VersionParts {
  long n[4];
  bool beta;   // a "-beta.<build>" version
  long build;  // its build number
};

// The dotted numbers (up to four) and the beta build of a version that
// starts with a digit; false otherwise. Anything else after the numbers
// ("-dev", "-beta" with no build) is ignored.
inline bool parse_version_parts(const char *p, VersionParts &v) {
  v = VersionParts{};
  if (*p < '0' || *p > '9')
    return false;
  for (int part = 0; part < 4; part++) {
    char *e;
    long x = strtol(p, &e, 10);
    if (e == p)
      break;
    v.n[part] = x;
    p = e;
    if (*p != '.')
      break;
    p++;
  }
  if (strncmp(p, kBetaTag, sizeof(kBetaTag) - 1) == 0) {
    p += sizeof(kBetaTag) - 1;
    char *e;
    long b = strtol(p, &e, 10);
    if (e != p) {
      v.beta = true;
      v.build = b;
    }
  }
  return true;
}

// True when `latest` is a higher version than `current`. A panel on a test
// build newer than the release never "updates" down to it, and a malformed
// version never installs.
inline bool version_newer(const std::string &latest, const std::string &current) {
  VersionParts a, b;
  if (!parse_version_parts(latest.c_str(), a) || !parse_version_parts(current.c_str(), b))
    return false;
  for (int part = 0; part < 4; part++)
    if (a.n[part] != b.n[part])
      return a.n[part] > b.n[part];
  if (a.beta != b.beta)
    return !a.beta;  // the release is newer than its own betas
  return a.beta && a.build > b.build;
}

// An update that rolled back. The new image only counts once it has run a
// minute (safe_mode); a crash before that and the bootloader puts the old
// firmware back, which still sees the same release in the manifest. So the
// panel saves the version before any install starts, manual or automatic,
// and the next boot settles it: running that version means it took, running
// another one after a rollback means it failed here, and automatic installs
// skip it until the panel runs it or something newer.
static const size_t kUpdateVersionLen = 24;  // "2026.10.0-beta.812" with room to spare

struct UpdateMark {
  char installing[kUpdateVersionLen];  // the install under way, "" = none
  char failed[kUpdateVersionLen];      // the version that rolled back here, "" = none
  // The new image was written: the panel shut down cleanly into it. Only
  // then can a rollback be this install's. The bootloader's invalid-image
  // flag outlives the rollback that set it, so a crash partway through a
  // later download would otherwise blame that release.
  uint8_t written;
} __attribute__((packed));

inline void update_mark_set(char *dst, const std::string &version) {
  strncpy(dst, version.c_str(), kUpdateVersionLen - 1);
  dst[kUpdateVersionLen - 1] = '\0';
}

enum class UpdateOutcome : uint8_t {
  NONE,         // nothing was being installed
  PENDING,      // running the version being installed, not yet a good boot
  ROLLED_BACK,  // the bootloader put the old firmware back
  NOT_INSTALLED,  // still on the old firmware with no rollback (power cut mid-download)
};

// At boot: what became of the install in the mark. `rolled_back` is the
// bootloader's word that an app image was rolled back. Clears `installing`
// unless it is PENDING (the caller clears that once the boot is good), and
// drops `failed` once the panel runs that version or a newer one.
inline UpdateOutcome update_mark_boot(UpdateMark &m, const std::string &running, bool rolled_back) {
  UpdateOutcome out = UpdateOutcome::NONE;
  if (m.installing[0] != '\0') {
    if (running == m.installing) {
      out = UpdateOutcome::PENDING;
    } else {
      rolled_back = rolled_back && m.written != 0;
      out = rolled_back ? UpdateOutcome::ROLLED_BACK : UpdateOutcome::NOT_INSTALLED;
      if (rolled_back)
        update_mark_set(m.failed, m.installing);
      m.installing[0] = '\0';
      m.written = 0;
    }
  }
  m.failed[kUpdateVersionLen - 1] = '\0';
  if (m.failed[0] != '\0' && !version_newer(m.failed, running))
    m.failed[0] = '\0';
  return out;
}

// The release on offer is the one that rolled back on this panel.
inline bool update_failed_here(const UpdateMark &m, const std::string &latest) {
  return m.failed[0] != '\0' && !latest.empty() && strncmp(latest.c_str(), m.failed, kUpdateVersionLen) == 0;
}

enum class AutoUpdateStep : uint8_t {
  IDLE,       // off, outside the window, before the panel's minute, or done for tonight
  CHECK,      // read the manifest now, then come back
  WAIT_GAME,  // an update is waiting but a followed game is live
  INSTALL,    // record tonight's try, then install
  SKIP_FAILED,  // the release on offer rolled back here; leave it to a manual install
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
  bool failed{false};      // update_failed_here() for the release on offer
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
  if (in.failed)
    return AutoUpdateStep::SKIP_FAILED;
  if (in.live)
    return AutoUpdateStep::WAIT_GAME;
  return AutoUpdateStep::INSTALL;
}

// Whether a manifest result came from the manifest at `source`. Our
// manifests give the ota path relative to their folder, so the updater's
// firmware URL starts with that folder; one read from the other channel
// (a check still running when Follow beta switched) starts with the other
// folder. A result with no firmware URL counts as matching.
inline bool firmware_from_source(const std::string &firmware_url, const char *source) {
  if (firmware_url.empty() || source == nullptr)
    return true;
  const char *slash = strrchr(source, '/');
  size_t n = slash != nullptr ? (size_t) (slash - source) + 1 : 0;
  return firmware_url.compare(0, n, source, n) == 0;
}

}  // namespace espn
