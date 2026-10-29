// "Update ready" on the ticker: a panel that doesn't install updates on its
// own says on its ticker, for an hour a day, that a new release is waiting.
// Pure, with no ESPHome includes, so the host tests compile it directly.
//
// The rules: only while a newer release is out and Install updates
// automatically is off, never while a game the panel follows is live, and
// never with the night schedule's screen off. The day's hour starts the
// first time all of that holds at or after kUpdateNudgeFromHour; a live game
// in that hour hides the line, and once the hour is up it waits for the next
// day. Installing the release ends it, since nothing newer is waiting.
//
// A release that fixes something broken (scores gone after an ESPN change)
// is marked at release time: its CHANGELOG.md summary line starts with
// "Important:". That line is the manifest's update summary, the one field
// besides the version the panel's updater reads, so the ticker and
// /gameday/state (update_important, for the app) can say so plainly.
#pragma once

#include <cstdint>
#include <string>

namespace espn {

// Owners are about at the end of the day, before the evening's games.
static const int kUpdateNudgeFromHour = 17;
static const int64_t kUpdateNudgeS = 60 * 60;

static const char kImportantTag[] = "Important:";

// The summary starts with "Important:", after any spaces.
inline bool update_important(const std::string &summary) {
  size_t i = summary.find_first_not_of(' ');
  return i != std::string::npos && summary.compare(i, sizeof(kImportantTag) - 1, kImportantTag) == 0;
}

inline const char *update_nudge_text(bool important) {
  return important ? "Important update ready: open the Game Day app" : "Update ready: open the Game Day app";
}

struct UpdateNudgeInputs {
  bool waiting{false};  // a newer release is out, and it hasn't rolled back here
  bool auto_on{false};  // Install updates automatically
  bool clock_valid{false};
  int hour{0};         // local
  int32_t today{0};    // auto_update_day() for the local date
  int64_t now{0};      // seconds
  bool live{false};    // a game the panel follows is in progress
  bool dark{false};    // the night schedule has the screen off
};

// The day's hour, kept in RAM: a restart can show it once more that day.
struct UpdateNudgeDay {
  int32_t day{0};     // the day its hour started, 0 = none yet
  int64_t since{0};   // when, in seconds
};

// True when the line belongs on the ticker now. Starts the day's hour in
// `d` the first time it may show.
inline bool update_nudge_on(const UpdateNudgeInputs &in, UpdateNudgeDay &d) {
  if (!in.waiting || in.auto_on || !in.clock_valid)
    return false;
  bool hidden = in.live || in.dark;
  if (d.day != in.today) {
    if (in.hour < kUpdateNudgeFromHour || hidden)
      return false;
    d.day = in.today;
    d.since = in.now;
  }
  if (in.now < d.since || in.now - d.since >= kUpdateNudgeS)
    return false;
  return !hidden;
}

}  // namespace espn
