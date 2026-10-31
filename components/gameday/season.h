// The schedule and season recap cards: one read of a favorite's season
// schedule (the same document as the Up next list, schedule_url) gives both.
// The schedule card is the team's next three games; the recap card, only
// while the team is between seasons, is its regular-season record and how
// its playoffs ended ("11-6, lost NFC Wild Card"). They share one turn,
// over the board between games like the news card (news.h), and each turn
// is the next favorite's. Only the leagues whose season schedule is small
// enough to read (LeagueInfo::season_list) have them.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "leagues.h"

namespace espn {

constexpr size_t kSeasonGames = 4;  // games kept: three to show, and one to spare once the first is played
constexpr size_t kSeasonShown = 3;
constexpr uint32_t kSeasonReadMs = 6u * 60 * 60 * 1000;  // a read is good this long
constexpr uint32_t kSeasonRetryMs = 15u * 60 * 1000;     // after a failed read of that team
constexpr int kSeasonEveryMin = 5;                       // minutes between the cards' turns
constexpr size_t kSeasonRoundMax = 28;                   // "NFC Wild Card", in bytes

// One game still ahead.
struct SeasonGame {
  int64_t kickoff_epoch{0};
  bool time_tbd{false};
  bool home{false};
  std::string opp;  // abbreviation
  std::string tv;
};

// What one read of a team's season schedule leaves.
struct SeasonRead {
  uint16_t year{0};           // the season's, from requestedSeason
  uint8_t wins{0}, losses{0}, ties{0};  // regular-season finals; ties are a soccer draw
  bool post{false};           // a playoff game was played to the end
  bool post_won{false};       // and the team won the last one
  std::string round;          // that game's round, "NFC Wild Card", or ""
  std::vector<SeasonGame> next;  // up to kSeasonGames not started, soonest first
  uint8_t finals() const { return (uint8_t) (wins + losses + ties); }
};

// A playoff round from what ESPN says about a game: the note's headline,
// else the season type's name. "ALDS - Game 3" is "ALDS", "East 1st Round
// - Game 7" is "East 1st Round", "NFC Wild Card Playoffs" is "NFC Wild
// Card", "College Football Playoff Quarterfinal at the Rose Bowl Presented
// by Prudential" is "CFP Quarterfinal", "Eastern Conference Playoffs -
// Semifinals" is "East Semifinals". "" when neither says more than
// "Postseason".
inline std::string season_round(const std::string &headline, const std::string &type_name) {
  std::string s = headline.empty() ? type_name : headline;
  auto cut = [&s](const char *at) {
    size_t p = s.find(at);
    if (p != std::string::npos)
      s.erase(p);
  };
  auto swap = [&s](const char *from, const char *to) {
    size_t p = s.find(from);
    if (p != std::string::npos)
      s.replace(p, std::char_traits<char>::length(from), to);
  };
  cut(" - Game");
  cut(" Presented by");
  cut(" at the ");
  cut(" at ");
  swap("College Football Playoff", "CFP");
  swap("Eastern Conference", "East");
  swap("Western Conference", "West");
  swap(" Playoffs - ", " ");
  swap(" Playoffs", "");
  swap(" - ", " ");
  if (s == "Postseason" || s == "Playoffs")
    s.clear();
  if (s.size() > kSeasonRoundMax) {
    s.erase(kSeasonRoundMax);
    size_t sp = s.rfind(' ');
    if (sp != std::string::npos && sp > 0)
      s.erase(sp);
  }
  return s;
}

// "11-6", "10-6-1" with a tie, a soccer record "20-5-9" (wins, draws,
// losses, the way soccer writes it). record is the team read's own, for a
// schedule that holds only the playoffs (an MLB team's in the fall): used
// when the read counted no regular-season finals.
inline std::string season_record(const SeasonRead &r, bool soccer, const std::string &record) {
  if (r.finals() == 0)
    return record;
  char buf[24];
  if (soccer)
    snprintf(buf, sizeof(buf), "%u-%u-%u", (unsigned) r.wins, (unsigned) r.ties, (unsigned) r.losses);
  else if (r.ties != 0)
    snprintf(buf, sizeof(buf), "%u-%u-%u", (unsigned) r.wins, (unsigned) r.losses, (unsigned) r.ties);
  else
    snprintf(buf, sizeof(buf), "%u-%u", (unsigned) r.wins, (unsigned) r.losses);
  return buf;
}

// The playoffs' end: "lost NFC Wild Card", "won Super Bowl LX", "" with no
// playoff game.
inline std::string season_post_line(const SeasonRead &r) {
  if (!r.post)
    return "";
  std::string out = r.post_won ? "won" : "lost";
  return out + (r.round.empty() ? std::string(" in the playoffs") : " " + r.round);
}

// Whether the recap card has something: a record either way.
inline bool season_recap_ready(const SeasonRead &r, const std::string &record) {
  return r.year != 0 && (r.finals() != 0 || (r.post && !record.empty()));
}

// The recap card's top line and body: "DAL 2025" and "11-6\nlost NFC Wild Card".
inline std::string recap_header(const std::string &abbr, uint16_t year) {
  return abbr + " " + std::to_string((unsigned) year);
}
inline std::string recap_body(const SeasonRead &r, bool soccer, const std::string &record) {
  std::string out = season_record(r, soccer, record);
  std::string post = season_post_line(r);
  if (!post.empty())
    out += (out.empty() ? "" : "\n") + post;
  return out;
}

// The games still ahead at now, up to kSeasonShown.
inline std::vector<SeasonGame> season_upcoming(const SeasonRead &r, int64_t now) {
  std::vector<SeasonGame> out;
  for (const auto &g : r.next) {
    if (out.size() >= kSeasonShown)
      break;
    // A game with no time yet sits at midnight Eastern: it stays its whole day.
    if (g.kickoff_epoch + (g.time_tbd ? 36 * 3600 : 0) > now)
      out.push_back(g);
  }
  return out;
}

inline std::string schedule_header(const std::string &abbr) { return abbr + " SCHEDULE"; }

// "7:15P", "12P" on the hour, "TBD".
inline std::string season_time(const struct tm &kick, bool tbd) {
  if (tbd)
    return "TBD";
  int h = kick.tm_hour % 12;
  if (h == 0)
    h = 12;
  const char ap = kick.tm_hour < 12 ? 'A' : 'P';
  char buf[12];
  if (kick.tm_min == 0)
    snprintf(buf, sizeof(buf), "%d%c", h, ap);
  else
    snprintf(buf, sizeof(buf), "%d:%02d%c", h, kick.tm_min, ap);
  return buf;
}

// One game on the schedule card. kick is local (from tbd_day_epoch with no
// time yet). Wide (two panels, 25 characters of spleen_8 a line): "Sun 11/1
// vs ARI 12:00P Fox", "Sun 11/8 @ IND 12:00P Fox", the network left off
// when it doesn't fit. Narrow (12 a line): "Sun 11/1" over "ARI 12:00P" at
// home, "@IND 12:00P" away.
inline std::string schedule_line(const struct tm &kick, const SeasonGame &g, bool wide) {
  static const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char day[16];
  snprintf(day, sizeof(day), "%s %d/%d", kDays[(kick.tm_wday % 7 + 7) % 7], kick.tm_mon + 1, kick.tm_mday);
  const std::string when = season_time(kick, g.time_tbd);
  if (!wide)
    return std::string(day) + "\n" + (g.home ? "" : "@") + g.opp + " " + when;
  std::string out = std::string(day) + (g.home ? " vs " : " @ ") + g.opp + " " + when;
  if (!g.tv.empty() && out.size() + 1 + g.tv.size() <= 25)
    out += " " + g.tv;
  return out;
}

// Which of the two cards a team's turn shows: the recap between seasons,
// the schedule otherwise, each only when it is on and has something.
enum class SeasonCard : uint8_t { NONE, SCHEDULE, RECAP };
inline SeasonCard season_card(bool off_season, bool schedule_on, bool recap_on, bool has_games, bool recap_ready) {
  if (off_season)
    return recap_on && recap_ready ? SeasonCard::RECAP : SeasonCard::NONE;
  return schedule_on && has_games ? SeasonCard::SCHEDULE : SeasonCard::NONE;
}

// Whether a team's season read is due before its turn: never read, older
// than kSeasonReadMs, or read for the other half of the season (a soccer
// team's fixture list has no results for the recap, its results no
// fixtures). After a failed read, not before kSeasonRetryMs. Times are
// millis(), 0 = never.
inline bool season_read_due(uint32_t now, uint32_t read_ms, uint32_t fail_ms, bool same_kind) {
  if (fail_ms != 0 && now - fail_ms < kSeasonRetryMs)
    return false;
  return read_ms == 0 || !same_kind || now - read_ms >= kSeasonReadMs;
}

}  // namespace espn
