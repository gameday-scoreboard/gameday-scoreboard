// How often the panel reads ESPN and how it backs off. Pure, with no ESPHome
// includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <string>


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

// The run of failed reads since ESPN last answered. Every fetch path shares
// the component's one run, so a failure anywhere lengthens the next wait.
struct Backoff {
  uint8_t streak{0};
  bool throttled{false};  // ESPN said 403 or 429 since the last good read

  // A read failed: start the next, longer wait and return it. A refusal keeps
  // the longer schedule until a good read clears it.
  uint32_t fail(bool refused, uint32_t retry_after_ms, uint32_t rnd) {
    if (refused)
      this->throttled = true;
    if (this->streak < 255)
      this->streak++;
    return backoff_ms(this->streak, this->throttled, retry_after_ms, rnd);
  }
  void clear() {
    this->streak = 0;
    this->throttled = false;
  }
};

// The count of failed reads in a row after one more. It stops at 255 instead
// of wrapping to 0, which would read as "ESPN answered" after about 42 hours
// of outage.
inline uint8_t miss_count_after(uint8_t misses) { return misses < 255 ? (uint8_t) (misses + 1) : misses; }

// Favorites: the wait between polls of a live favorite while another
// favorite's game has the board (for Jump to the action and the score
// banner). `base` while they work; after a failure 5 minutes, doubling up to
// an hour, so one favorite whose game can't be read stops costing a read
// every 30 seconds.
inline uint32_t side_poll_wait_ms(uint8_t fails, uint32_t base) {
  if (fails == 0)
    return base;
  uint32_t d = 5 * 60 * 1000;
  for (uint8_t i = 1; i < fails && d < 60 * 60 * 1000; i++)
    d *= 2;
  return d > 60 * 60 * 1000 ? 60 * 60 * 1000 : d;
}

// What a Favorites read that came back does to the board's shared state: the
// miss count and freshness the ticker and page report, and the back-off every
// read waits out. Only a read of the board's own favorite (`board`, or
// nothing is on the board) counts a miss, backs off or marks the board fresh.
// A read about another favorite, its side poll (side_poll_wait_ms) or its
// refresh, touches none of it, except a refusal (403 or 429), which every
// read has to respect. Any read that worked other than a side poll clears
// the back-off: ESPN answered.
struct FavPollEffect {
  bool count_miss{false};
  bool back_off{false};
  bool mark_good{false};
  bool clear_backoff{false};
};
inline FavPollEffect fav_poll_effect(bool ok, bool side, bool refused, bool board) {
  FavPollEffect fx;
  bool ours = board && !side;
  fx.count_miss = !ok && ours;
  fx.back_off = !ok && (ours || refused);
  fx.mark_good = ok && ours;
  fx.clear_backoff = ok && !side;
  return fx;
}

// After a My team game poll failed: read the team endpoint again now instead
// of backing off. Yes when the game came from the boot cache and no poll has
// confirmed it, or when ESPN answered but didn't serve the game (gone: a 404,
// another game's document, one the parser rejects), once per game (again:
// the team was already read again for this one and named it anyway). Never
// on a refusal (403 or 429), which every read waits out.
inline bool reread_team_after_poll(bool cache_unconfirmed, bool gone, bool throttled, bool again) {
  if (throttled)
    return false;
  return cache_unconfirmed || (gone && !again);
}

// Whether a soccer cup job (one cup's team read, then the game poll when the
// pick moved) must back off. A poll is the job's last read, so its result
// decides. Without one, a failed or refused cup read backs off.
inline bool cup_read_backs_off(bool cup_ok, bool polled, bool game_ok, bool throttled) {
  return polled ? !game_ok : (!cup_ok || throttled);
}

// ---- Live modes' scan clock ---------------------------------------------------

// While nothing is live (the saved team's card, or the finals rotation with no
// team) the board waits `rescan_ms` from `started_ms` before it scans for a
// live game again. 0 means no wait is running: scan now. A favorite's game
// due to start (take_due) cuts the wait short. Times are millis(), so the
// subtraction is safe across its wrap.
inline bool live_rescan_due(uint32_t now, uint32_t started_ms, uint32_t rescan_ms, bool take_due) {
  return started_ms == 0 || now - started_ms >= rescan_ms || take_due;
}

// The scan clock after a scan failed, or its worker was given up on. While
// the board waits on a scan, the wait starts over from nothing, so the next
// try is the back-off's retry and not the end of a wait that can run three
// hours. While a game is followed the clock times its rotate, and the next
// cycle's own checks decide whether to scan again.
inline uint32_t live_clock_after_failed_scan(uint32_t started_ms, bool fallback, bool recap_on) {
  return fallback || recap_on ? 0 : started_ms;
}

}  // namespace espn
