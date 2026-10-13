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

}  // namespace espn
