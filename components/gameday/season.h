// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The schedule and season recap cards: one read of a favorite's season
// schedule (the same document as the Up next list, schedule_url) gives both.
// The schedule card is the team's next three games; the recap card, only
// while the team is between seasons, is its regular-season record and how
// its playoffs ended ("11-6, lost NFC Wild Card"). They share one turn,
// over the board between games like the news card (news.h), and each turn
// is the next favorite's. Every league but MLB has them (season_cards).
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <cstring>
#include <vector>

#include "leagues.h"
#include "psram.h"

namespace espn {

constexpr size_t kSeasonGames = 4;  // games kept: three to show, and one to spare once the first is played
constexpr size_t kSeasonShown = 3;
constexpr uint32_t kSeasonReadMs = 6u * 60 * 60 * 1000;  // a read is good this long
constexpr uint32_t kSeasonRetryMs = 15u * 60 * 1000;     // after a failed read of that team
// A big schedule's (season_big): once a day, and an hour after a failure.
constexpr uint32_t kSeasonBigReadMs = 24u * 60 * 60 * 1000;
constexpr uint32_t kSeasonBigRetryMs = 60u * 60 * 1000;
constexpr int kSeasonEveryMin = 5;                       // minutes between the cards' turns
constexpr size_t kSeasonRoundMax = 28;                   // "NFC Wild Card", in bytes

// The leagues with the cards. Football's and soccer's season schedule is
// 200 to 400 KB (LeagueInfo::season_list). Basketball's and hockey's is
// bigger (on 11/3: NBA 873 KB, NHL 1.3 MB with 70 games to go and 1.9 MB
// for a whole season, college 280 to 545 KB, WNBA 809 KB after its
// season), so those are read once a day a team. Not MLB: its regular
// season is 2.8 MB.
inline bool season_big(const LeagueInfo &l) { return !l.season_list; }
inline bool season_cards(const LeagueInfo &l) {
  return l.season_list || l.sport == Sport::BASKETBALL || l.sport == Sport::HOCKEY;
}

// One game still ahead.
struct SeasonGame {
  int64_t kickoff_epoch{0};
  bool time_tbd{false};
  bool home{false};
  std::string opp;  // abbreviation
  std::string tv;
};

// What one read of a team's season schedule leaves, kept per team in PSRAM
// (internal heap is the panel's scarcest): the round inline, the games in
// a PSRAM vector, their short strings inline too.
struct SeasonRead {
  uint16_t year{0};           // the season's, from requestedSeason
  uint8_t wins{0}, losses{0}, ties{0};  // regular-season finals; ties are a soccer draw
  // Of the losses, those that went past a third period: in hockey an
  // overtime or shootout loss, its record's third number.
  uint8_t late_losses{0};
  bool post{false};           // a playoff game was played to the end
  bool post_won{false};       // and the team won the last one
  char round[kSeasonRoundMax + 1]{};  // that game's round, "NFC Wild Card", or ""
  std::vector<SeasonGame, PsramAllocator<SeasonGame>> next;  // up to kSeasonGames not started, soonest first
  uint8_t finals() const { return (uint8_t) (wins + losses + ties); }
  void set_round(const std::string &r) {
    strncpy(round, r.c_str(), kSeasonRoundMax);
    round[kSeasonRoundMax] = '\0';
  }
};

// A playoff round from what ESPN says about a game: the note's headline,
// else the season type's name. "ALDS - Game 3" is "ALDS", "East 1st Round
// - Game 7" is "East 1st Round", "NFC Wild Card Playoffs" is "NFC Wild
// Card", "College Football Playoff Quarterfinal at the Rose Bowl Presented
// by Prudential" is "CFP Quarterfinal", "Eastern Conference Playoffs -
// Semifinals" is "East Semifinals", "NCAA Men's Basketball Championship -
// Midwest Region - Sweet 16" is "NCAA Sweet 16". "" when neither says more
// than "Postseason".
inline std::string season_round(const std::string &headline, const std::string &type_name) {
  std::string s = headline.empty() ? type_name : headline;
  // The tournament's name and region left off: its round alone.
  static const char kNcaa[] = "NCAA Men's Basketball Championship - ";
  if (s.compare(0, sizeof(kNcaa) - 1, kNcaa) == 0)
    s = "NCAA " + s.substr(s.rfind(" - ") + 3);
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
  swap("College Basketball Crown", "Crown");
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
// losses, the way soccer writes it), a hockey one "43-33-6" (wins, losses,
// overtime and shootout losses, always all three). record is the team
// read's own, for a schedule that holds only the playoffs (an MLB team's
// in the fall, or any team's while the playoffs are on): used when the
// read counted no regular-season finals.
inline std::string season_record(const SeasonRead &r, Sport sport, const std::string &record) {
  if (r.finals() == 0)
    return record;
  char buf[24];
  if (sport == Sport::SOCCER)
    snprintf(buf, sizeof(buf), "%u-%u-%u", (unsigned) r.wins, (unsigned) r.ties, (unsigned) r.losses);
  else if (sport == Sport::HOCKEY)
    snprintf(buf, sizeof(buf), "%u-%u-%u", (unsigned) r.wins, (unsigned) (r.losses - r.late_losses),
             (unsigned) r.late_losses);
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
  return out + (r.round[0] == '\0' ? std::string(" in the playoffs") : " " + std::string(r.round));
}

// Whether the recap card has something: a record either way. A schedule
// with neither finals nor a playoff game (a team that missed the playoffs,
// read while ESPN's list holds only the playoffs) still has the team read's.
inline bool season_recap_ready(const SeasonRead &r, const std::string &record) {
  return r.year != 0 && (r.finals() != 0 || !record.empty());
}

// The recap card's top line and body: "DAL 2025" and "11-6\nlost NFC Wild Card".
inline std::string recap_header(const std::string &abbr, uint16_t year) {
  return abbr + " " + std::to_string((unsigned) year);
}
inline std::string recap_body(const SeasonRead &r, Sport sport, const std::string &record) {
  std::string out = season_record(r, sport, record);
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
// vs ARI 12P Fox", "Sun 11/8 @ IND 7:15P CBS", the network left off when it
// doesn't fit. Narrow (12 a line): "Sun 11/1" over "ARI 12P" at home,
// "@IND 7:15P" away.
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
// than kSeasonReadMs, or read in the other half of the season (before a
// team's last game the recap could miss it; a soccer team's fixture list
// has no results for the recap, its results no fixtures). That last one
// waits kSeasonRetryMs after the read, so a team flipping between the two
// halves can't be read every turn. After a failed read, not before
// kSeasonRetryMs. big (season_big) is once a day and kSeasonBigRetryMs.
// Times are millis(), 0 = never.
inline bool season_read_due(uint32_t now, uint32_t read_ms, uint32_t fail_ms, bool same_kind, bool big) {
  const uint32_t retry = big ? kSeasonBigRetryMs : kSeasonRetryMs;
  if (fail_ms != 0 && now - fail_ms < retry)
    return false;
  if (read_ms == 0)
    return true;
  return now - read_ms >= (same_kind ? (big ? kSeasonBigReadMs : kSeasonReadMs) : retry);
}

}  // namespace espn
