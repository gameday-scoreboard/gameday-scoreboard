// Sign-in (captive portal) Wi-Fi: bar, hotel and dorm networks that let the
// panel join and then hold everything behind a sign-in page. Every ESPN read
// is HTTPS, and a portal shows up there only as a failed handshake, the same
// as no internet at all. So after joining, the panel asks ESPN for one tiny
// document over plain HTTP, where a portal has to answer with its redirect or
// its page. Pure, with no ESPHome includes, so the host tests compile it
// directly.
#pragma once

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

namespace espn {

// ESPN serves the same JSON over plain HTTP. This one is two bytes ("{}").
static const char *const kProbeUrl = "http://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/6/record";

// While the panel is on a sign-in network it asks again this often, so it
// notices when someone registers it or the network opens up.
static const uint32_t kProbeCaptiveRecheckMs = 60 * 1000;
// While ESPN reads fail on a network that looked open, this often: a hotel's
// sign-in can run out a day after the panel joined.
static const uint32_t kProbeFailingRecheckMs = 2 * 60 * 1000;

enum class Probe : uint8_t {
  OPEN,     // ESPN answered
  CAPTIVE,  // something else answered in ESPN's place
  UNKNOWN,  // no answer, or one that proves nothing either way
};

inline const char *probe_name(Probe p) {
  return p == Probe::OPEN ? "open" : p == Probe::CAPTIVE ? "sign-in page" : "no answer";
}

namespace detail {

inline bool has_ci(const std::string &s, const char *needle) {
  const size_t n = strlen(needle);
  for (size_t i = 0; i + n <= s.size(); i++) {
    size_t k = 0;
    while (k < n && tolower((unsigned char) s[i + k]) == needle[k])
      k++;
    if (k == n)
      return true;
  }
  return false;
}

// The first character that isn't white space or a UTF-8 byte order mark.
inline char first_char(const std::string &body) {
  for (size_t i = 0; i < body.size(); i++) {
    unsigned char c = (unsigned char) body[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xEF || c == 0xBB || c == 0xBF)
      continue;
    return (char) c;
  }
  return '\0';
}

// The host of an absolute URL, lower case; empty for a relative one.
inline std::string url_host(const std::string &url) {
  size_t p = url.find("://");
  if (p == std::string::npos)
    return "";
  p += 3;
  size_t at = url.find('@', p);
  size_t end = url.find_first_of("/?#", p);
  if (at != std::string::npos && (end == std::string::npos || at < end))
    p = at + 1;
  end = url.find_first_of(":/?#", p);
  std::string host = url.substr(p, end == std::string::npos ? std::string::npos : end - p);
  for (auto &c : host)
    c = (char) tolower((unsigned char) c);
  return host;
}

inline bool espn_host(const std::string &host) {
  static const char kEspn[] = "espn.com";
  const size_t n = sizeof(kEspn) - 1;
  if (host == kEspn)
    return true;
  return host.size() > n && host.compare(host.size() - n, n, kEspn) == 0 && host[host.size() - n - 1] == '.';
}

}  // namespace detail

// What the plain HTTP read of kProbeUrl says about the network. status is the
// HTTP status (0 or less for no answer at all), location the Location header,
// body the first few hundred bytes. Only a clear portal answer is CAPTIVE:
// 511 (the status made for this), a redirect to some host other than ESPN, or
// a page where JSON was asked for. An error page (ESPN's own "Access Denied"
// is HTML from Akamai) proves nothing, so it is UNKNOWN, never CAPTIVE.
inline Probe classify_probe(int status, const std::string &location, const std::string &content_type,
                            const std::string &body) {
  if (status <= 0)
    return Probe::UNKNOWN;
  if (status == 511)
    return Probe::CAPTIVE;
  if (status >= 300 && status < 400) {
    const std::string host = detail::url_host(location);
    if (host.empty())
      return Probe::UNKNOWN;  // none, or relative: the same host, whoever that is
    return detail::espn_host(host) ? Probe::OPEN : Probe::CAPTIVE;
  }
  const char c = detail::first_char(body);
  if (c == '{' || c == '[' || (c == '\0' && detail::has_ci(content_type, "json")))
    return Probe::OPEN;
  const bool page = c == '<' || (c == '\0' && detail::has_ci(content_type, "html"));
  if (page && status >= 200 && status < 300)
    return Probe::CAPTIVE;
  return Probe::UNKNOWN;
}

// Whether to read kProbeUrl now: once per join, then again while the panel is
// on a sign-in network, or while ESPN reads fail. since_ms is the time since
// the last probe this join.
inline bool probe_due(bool probed_this_join, bool captive, bool reads_failing, uint32_t since_ms) {
  if (!probed_this_join)
    return true;
  if (captive)
    return since_ms >= kProbeCaptiveRecheckMs;
  return reads_failing && since_ms >= kProbeFailingRecheckMs;
}

}  // namespace espn
