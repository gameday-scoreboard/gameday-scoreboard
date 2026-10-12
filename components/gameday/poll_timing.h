// How often the panel reads ESPN and how it backs off. Pure, with no ESPHome
// includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "espn_parse.h"

namespace espn {

// ---- Backoff ------------------------------------------------------------------

// After an ordinary failure (timeout, 5xx, a parse that came up empty) the
// wait doubles from 30 s up to 10 min. After ESPN refuses the panel (403 or
// 429) it doubles from 5 min up to an hour, so a block or a rate limit never
// turns into every panel in the fleet hammering it.
static const uint32_t kBackoffBaseMs = 30 * 1000;
static const uint32_t kBackoffCapMs = 10 * 60 * 1000;
static const uint32_t kThrottleBaseMs = 5 * 60 * 1000;
static const uint32_t kThrottleCapMs = 60 * 60 * 1000;

inline bool is_throttle_status(int status) { return status == 403 || status == 429; }

// Retry-After in seconds, as milliseconds. 0 when it is missing, not a plain
// number of seconds (the HTTP-date form is ignored) or absurd.
inline uint32_t parse_retry_after_ms(const std::string &value) {
  if (value.empty() || value.size() > 6)
    return 0;
  uint32_t s = 0;
  for (char c : value) {
    if (c < '0' || c > '9')
      return 0;
    s = s * 10 + (uint32_t) (c - '0');
  }
  return s * 1000;
}

// The wait before the next read after `fails` failures in a row (1 for the
// first). `rnd` is any random number. The result lands somewhere in the upper
// half of the doubled delay, so panels that failed together (an ESPN outage,
// a router reboot) spread out instead of coming back in one wave. A
// Retry-After longer than that is honored up to the cap, with up to a quarter
// on top for the same reason.
inline uint32_t backoff_ms(uint32_t fails, bool throttled, uint32_t retry_after_ms, uint32_t rnd) {
  const uint32_t base = throttled ? kThrottleBaseMs : kBackoffBaseMs;
  const uint32_t cap = throttled ? kThrottleCapMs : kBackoffCapMs;
  uint32_t d = base;
  for (uint32_t i = 1; i < fails && d < cap; i++)
    d *= 2;
  if (d > cap)
    d = cap;
  uint32_t wait = d / 2 + rnd % (d / 2 + 1);
  if (throttled && retry_after_ms > wait) {
    uint32_t ra = retry_after_ms > cap ? cap : retry_after_ms;
    wait = ra + rnd % (ra / 4 + 1);
  }
  return wait;
}

// ---- Local game clock -----------------------------------------------------------

// "12:34" -> 754. -1 for anything that is not M:SS or MM:SS (an empty clock,
// "45.2" tenths, "1:02:03").
inline int clock_seconds(const std::string &clock) {
  size_t colon = clock.find(':');
  if (colon == std::string::npos || colon == 0 || colon > 2 || clock.size() != colon + 3)
    return -1;
  int m = 0;
  for (size_t i = 0; i < colon; i++) {
    if (clock[i] < '0' || clock[i] > '9')
      return -1;
    m = m * 10 + (clock[i] - '0');
  }
  char a = clock[colon + 1], b = clock[colon + 2];
  if (a < '0' || a > '5' || b < '0' || b > '9')
    return -1;
  return m * 60 + (a - '0') * 10 + (b - '0');
}

inline std::string format_clock(int seconds) {
  if (seconds < 0)
    seconds = 0;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
  return buf;
}

// The live poll runs every 15 s. Between polls the panel counts the game
// clock down itself, so the board never sits on one time and then jumps.
// ESPN has no "clock running" field, so the clock counts only when the last
// two polls of the same period show it ran for at least half the time
// between them. It never counts more than kMaxRunMs past the last poll: a
// missed poll stops it instead of running it to 0:00. The next poll always
// wins, even when that moves the clock back up (it stopped for an incomplete
// pass a few seconds after the last poll).
class LiveClock {
 public:
  static constexpr uint32_t kMaxRunMs = 20 * 1000;

  // A game poll landed at now_ms.
  void on_poll(const GameSnapshot &g, uint32_t now_ms) {
    int secs = g.valid && g.state == GameState::IN && clock_shows_time(g) ? clock_seconds(g.display_clock) : -1;
    bool same = secs >= 0 && this->secs_ >= 0 && g.event_id == this->event_id_ && g.period == this->period_;
    uint32_t gap = now_ms - this->at_ms_;
    int ran = same ? this->secs_ - secs : 0;
    this->running_ = same && ran > 0 && (uint64_t) ran * 2000 >= gap;
    this->event_id_ = g.event_id;
    this->period_ = g.period;
    this->secs_ = secs;
    this->at_ms_ = now_ms;
  }

  void reset() { *this = LiveClock{}; }

  bool running() const { return this->running_; }

  // clock_text(g), counted down from the last poll while the clock runs. Only
  // for the exact snapshot that poll put on the board; anything else (a
  // different game, a redraw after a team change) gets the feed's text.
  std::string text(const GameSnapshot &g, uint32_t now_ms) const {
    if (!this->running_ || g.event_id != this->event_id_ || g.period != this->period_ ||
        clock_seconds(g.display_clock) != this->secs_ || g.state != GameState::IN || !clock_shows_time(g))
      return clock_text(g);
    uint32_t elapsed = now_ms - this->at_ms_;
    if (elapsed > kMaxRunMs)
      elapsed = kMaxRunMs;
    return clock_text(g, format_clock(this->secs_ - (int) (elapsed / 1000)));
  }

 private:
  std::string event_id_;
  int period_{0};
  int secs_{-1};  // the clock in the last poll, -1 when it had none
  uint32_t at_ms_{0};
  bool running_{false};
};

}  // namespace espn
