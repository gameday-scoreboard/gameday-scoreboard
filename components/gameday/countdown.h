// The countdown card: big digits to the next kickoff ("2d 14h"), or to a day
// the owner named ("Christmas"), on the no-game screen in the team's colors.
// Between games the next game's card and its countdown take turns, and a
// custom countdown takes the last turn of every minute. The page counts down
// itself once a second (gameday-live.yaml), with no fetch. Pure, with no
// ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace espn {

// Seconds each card keeps the board: the next game, its countdown, the
// custom countdown. The Favorites playlist gives each card the same.
static const int64_t kCountdownTurnS = 10;
static const int64_t kCountdownCycleS = 60;
static const int64_t kDayS = 24 * 3600;
// The custom countdown's name, in bytes: one line on a wide board, two on a
// single panel.
static const size_t kCountdownNameMax = 20;

// "2d 14h" a day or more out, "14h 05m" under a day, "45m" under an hour.
// Minutes round up, so the last minute before kickoff reads "1m", not "0m".
// Empty once the time has come. Past 999 days, the days alone.
inline std::string countdown_text(int64_t seconds) {
  if (seconds <= 0)
    return "";
  long long mins = (long long) ((seconds + 59) / 60);
  long long d = mins / 1440, h = mins % 1440 / 60, m = mins % 60;
  char buf[24];
  if (d > 999)
    snprintf(buf, sizeof(buf), "%lldd", d);
  else if (d > 0)
    snprintf(buf, sizeof(buf), "%lldd %lldh", d, h);
  else if (h > 0)
    snprintf(buf, sizeof(buf), "%lldh %02lldm", h, m);
  else
    snprintf(buf, sizeof(buf), "%lldm", m);
  return buf;
}

// What the card's big digits say at now. at is the kickoff, or the start of
// the custom countdown's day; end is when the card stops (the kickoff itself,
// or the end of that day, which reads "Today"). Empty when it is over.
inline std::string countdown_card_text(int64_t at, int64_t end, int64_t now) {
  if (now < at)
    return countdown_text(at - now);
  return now < end ? "Today" : "";
}

// The line under the digits: "DAL vs PHI" at home, "DAL at PHI" away.
inline std::string matchup_line(const std::string &ours, const std::string &opp, bool home) {
  if (opp.empty())
    return "Next game";
  if (ours.empty())
    return (home ? "vs " : "at ") + opp;
  return ours + (home ? " vs " : " at ") + opp;
}

// The custom countdown's day as YYYYMMDD, from "2026-12-25". "" and "none"
// clear it (0). False for anything else, or a day that doesn't exist.
inline bool parse_countdown_date(const std::string &v, uint32_t &ymd) {
  ymd = 0;
  if (v.empty() || v == "none")
    return true;
  if (v.size() != 10 || v[4] != '-' || v[7] != '-')
    return false;
  auto num = [&v](size_t at, size_t len) {
    int n = 0;
    for (size_t i = at; i < at + len; i++) {
      if (v[i] < '0' || v[i] > '9')
        return -1;
      n = n * 10 + (v[i] - '0');
    }
    return n;
  };
  int y = num(0, 4), m = num(5, 2), d = num(8, 2);
  static const int kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (y < 2020 || y > 2099 || m < 1 || m > 12 || d < 1 || d > kDays[m - 1])
    return false;
  if (m == 2 && d == 29 && y % 4 != 0)
    return false;
  ymd = (uint32_t) (y * 10000 + m * 100 + d);
  return true;
}

// "2026-12-25" for the device page and the app; "" for none.
inline std::string countdown_date_text(uint32_t ymd) {
  if (ymd == 0)
    return "";
  char buf[16];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", (unsigned) (ymd / 10000 % 10000), (unsigned) (ymd / 100 % 100),
           (unsigned) (ymd % 100));
  return buf;
}

// "Dec 25": under the digits when the countdown has no name.
inline std::string countdown_day_label(uint32_t ymd) {
  static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  unsigned m = ymd / 100 % 100;
  if (m < 1 || m > 12)
    return "";
  char buf[16];
  snprintf(buf, sizeof(buf), "%s %u", kMonths[m - 1], (unsigned) (ymd % 100));
  return buf;
}

// The name as the panel keeps it: spaces trimmed, line breaks and other
// control characters dropped, cut to kCountdownNameMax bytes without
// splitting a UTF-8 character.
inline std::string countdown_name(const std::string &v) {
  std::string out;
  for (unsigned char c : v)
    if (c >= 0x20 && c != 0x7F)
      out += (char) c;
  size_t a = out.find_first_not_of(' ');
  if (a == std::string::npos)
    return "";
  out = out.substr(a, out.find_last_not_of(' ') - a + 1);
  if (out.size() > kCountdownNameMax) {
    size_t n = kCountdownNameMax;
    while (n > 0 && ((unsigned char) out[n] & 0xC0) == 0x80)
      n--;
    out.resize(n);
    while (!out.empty() && out.back() == ' ')
      out.pop_back();
  }
  return out;
}

enum class CountdownCard : uint8_t { NONE, NEXT, CUSTOM };

// The custom countdown takes the last turn of each minute, on the wall clock,
// so it comes up at the same moment on every panel in the house.
inline bool custom_turn(int64_t now) { return now % kCountdownCycleS >= kCountdownCycleS - kCountdownTurnS; }

// My team between games: which card has the board at now. The next game's
// card and its countdown take turns; the custom countdown the last turn of
// each minute. next and custom say whether each has something to show.
inline CountdownCard countdown_turn(int64_t now, bool next, bool custom) {
  if (custom && custom_turn(now))
    return CountdownCard::CUSTOM;
  if (next && (now % kCountdownCycleS / kCountdownTurnS) % 2 == 1)
    return CountdownCard::NEXT;
  return CountdownCard::NONE;
}

}  // namespace espn
