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
//
// The bar build (firmware/pages/bar.yaml) runs the same rules against the
// beta channel with the setting always on, plus "Install now"
// (install_now_listed) for pushing a fix to one bar without waiting.
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

// The beta channel's versions are the release number plus "-beta.<build>"
// ("2026.10.0-beta.812"), and the build number counts as the next part, so
// each beta is newer than the one before and than the plain number.
static const char kBetaTag[] = "-beta.";

// Past the separator after a version part: a dot or the beta tag.
inline const char *next_version_part(const char *p) {
  if (*p == '.')
    return p + 1;
  if (strncmp(p, kBetaTag, sizeof(kBetaTag) - 1) == 0)
    return p + sizeof(kBetaTag) - 1;
  return p;
}

// True when `latest` is a higher version than `current`. Both are dotted
// numbers ("1.5.5", "2026.10.0"); anything after the numbers other than the
// beta tag ("-dev") is ignored. A panel on a test build newer than the
// release never "updates" down to it, and a malformed version never installs.
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
    a = next_version_part(ea);
    b = next_version_part(eb);
  }
  return false;
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
      out = rolled_back ? UpdateOutcome::ROLLED_BACK : UpdateOutcome::NOT_INSTALLED;
      if (rolled_back)
        update_mark_set(m.failed, m.installing);
      m.installing[0] = '\0';
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
  INSTALL_NOW,  // record the try for this version, then install (install_now_listed)
  SKIP_FAILED,  // the release on offer rolled back here; leave it to a manual install
};

// "Install now" for the beta channel: the publisher names panels in the
// release summary ("... Install now: gameday-aba970 gameday-500ebc", or
// "all"), and a listed bar panel installs at its next manifest read, any hour,
// instead of waiting for the night. Names are separated by spaces or commas.
static const char kInstallNowTag[] = "Install now:";

inline bool install_now_listed(const std::string &summary, const std::string &name) {
  size_t at = summary.find(kInstallNowTag);
  if (at == std::string::npos || name.empty())
    return false;
  size_t i = at + sizeof(kInstallNowTag) - 1;
  while (i < summary.size()) {
    while (i < summary.size() && (summary[i] == ' ' || summary[i] == ','))
      i++;
    size_t start = i;
    while (i < summary.size() && summary[i] != ' ' && summary[i] != ',')
      i++;
    std::string word = summary.substr(start, i - start);
    if (word == name || word == "all")
      return true;
  }
  return false;
}

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
  bool now{false};         // install_now_listed() and not tried for this version yet
  bool failed{false};      // update_failed_here() for the release on offer
};

inline AutoUpdateStep auto_update_step(const AutoUpdateInputs &in) {
  if (in.enabled && in.now && in.available && in.newer && !in.failed)
    return in.live ? AutoUpdateStep::WAIT_GAME : AutoUpdateStep::INSTALL_NOW;
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

}  // namespace espn
