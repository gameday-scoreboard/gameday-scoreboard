#pragma once

// The game events' settings: the owner's webhook address and how long
// before the start the reminder goes out (ha_events.h). Pure, so the host
// tests and set_request.h use it.

#include <cctype>
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

// Where the host sits in an http:// address: [begin, end) is the name or
// IP without the port. False when there is none.
inline bool hook_host_span(const std::string &url, size_t &begin, size_t &end) {
  if (url.compare(0, 7, "http://") != 0)
    return false;
  begin = 7;
  end = url.find_first_of(":/?#", begin);
  if (end == std::string::npos)
    end = url.size();
  return end > begin;
}

// The host without its ".local" when the address names one (homeassistant.local),
// "" for an IP or any other name. The panel's resolver hands .local names to
// the router's DNS, which doesn't know them, so the post looks them up over
// mDNS itself.
inline std::string hook_mdns_name(const std::string &url) {
  size_t b, e;
  if (!hook_host_span(url, b, e) || e - b <= 6)
    return "";
  std::string host = url.substr(b, e - b);
  std::string tail = host.substr(host.size() - 6);
  for (auto &c : tail)
    c = (char) tolower((unsigned char) c);
  if (tail != ".local")
    return "";
  return host.substr(0, host.size() - 6);
}

constexpr int kPregameMax = 120;
constexpr int kPregameDefault = 15;

// Minutes before the start, 0 to kPregameMax; 0 sends no reminder.
inline int clamp_pregame_minutes(int m) { return m < 0 ? 0 : m > kPregameMax ? kPregameMax : m; }

}  // namespace espn
