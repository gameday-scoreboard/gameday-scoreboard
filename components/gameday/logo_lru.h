#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "poll_timing.h"

namespace espn {

struct LogoSlot {
  uint32_t used;  // millis() of the last lookup
  bool busy;      // queued or downloading
  bool pinned;    // on screen right now
};

// Index of the least recently used slot that can be dropped, or -1 when every
// slot is busy or on screen.
inline int pick_evict(const std::vector<LogoSlot> &slots) {
  int best = -1;
  for (size_t i = 0; i < slots.size(); i++) {
    if (slots[i].busy || slots[i].pinned)
      continue;
    if (best < 0 || slots[i].used < slots[(size_t) best].used)
      best = (int) i;
  }
  return best;
}

// A logo that fails waits longer after each failure in a row, counted per
// logo so one missing logo never holds back the others or the score reads:
// the same 30 s doubling to 10 min the ESPN reads use, and the longer 5 min
// to an hour after a 403 or 429. A logo that loads starts over.
inline void logo_failed(uint8_t &fails, uint32_t &wait_ms, bool throttled, uint32_t retry_after_ms, uint32_t rnd) {
  if (fails < 255)
    fails++;
  wait_ms = backoff_ms(fails, throttled, retry_after_ms, rnd);
}

inline void logo_loaded(uint8_t &fails, uint32_t &wait_ms) {
  fails = 0;
  wait_ms = 0;
}

inline bool logo_retry_due(uint32_t failed_ms, uint32_t wait_ms, uint32_t now) { return now - failed_ms >= wait_ms; }

// How long a board with new logos waits for them before it goes up anyway,
// with a blank where a logo is still missing. ESPN's logos usually take well
// under a second each, one at a time.
static const uint32_t LOGO_HOLD_MS = 3500;

// True once a held board can go up: neither logo is still on its way, or it
// has waited LOGO_HOLD_MS. A failed or refused logo is not on its way, so it
// never holds a board.
inline bool logo_hold_over(bool team_waiting, bool opp_waiting, uint32_t since_ms, uint32_t now) {
  return (!team_waiting && !opp_waiting) || now - since_ms >= LOGO_HOLD_MS;
}

// The plain logo for a dark one (espn::dark_logo), or "" for any other URL.
// A few teams have no dark variant: Fenerbahce's answers 404 (2026-10-14).
inline std::string plain_logo_url(const std::string &url) {
  size_t pos = url.find("/500-dark/");
  if (pos == std::string::npos)
    return "";
  std::string out = url;
  out.replace(pos, 10, "/500/");
  return out;
}

}  // namespace espn
