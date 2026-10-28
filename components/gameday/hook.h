#pragma once

// The game events' settings: the owner's webhook address and how long
// before the start the reminder goes out (ha_events.h). Pure, so the host
// tests and set_request.h use it.

#include <string>

namespace espn {

constexpr size_t kHookUrlMax = 160;

// The webhook takes plain http only: it is meant for something on the home
// network (Node-RED, Home Assistant's own webhooks, a script), and a second
// TLS session beside ESPN's would cost the panel memory it doesn't have.
// "" turns it off.
inline bool hook_url_ok(const std::string &url) {
  if (url.empty())
    return true;
  if (url.size() > kHookUrlMax || url.compare(0, 7, "http://") != 0)
    return false;
  size_t host_end = url.find_first_of("/?#", 7);
  if (host_end == 7 || url.size() == 7)
    return false;  // no host
  for (unsigned char c : url)
    if (c <= ' ' || c >= 0x7F)
      return false;
  return true;
}

constexpr int kPregameMax = 120;
constexpr int kPregameDefault = 15;

// Minutes before the start, 0 to kPregameMax; 0 sends no reminder.
inline int clamp_pregame_minutes(int m) { return m < 0 ? 0 : m > kPregameMax ? kPregameMax : m; }

}  // namespace espn
