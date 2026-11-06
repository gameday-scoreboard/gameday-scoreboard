// Golf: the PGA Tour, the LPGA and the DP World Tour (ESPN's golf/eur) as
// leaderboard boards, read from the tour's scoreboard document (about
// 90-350 KB) and shown between games, a page of rows at a time, like the
// Top 25 card. A tournament is not a game between two teams: the board is
// the top of the leaderboard, with the golfers the owner follows pinned
// into it wherever they stand. A favorite in a golf league is a golfer,
// named by ESPN athlete id (favorites carry league + id, and a tour's field
// changes every week, so golfers are not a table in teams.h). Pure, with
// no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "leagues.h"
#include "psram.h"

namespace espn {

enum class GolfState : uint8_t { PRE, IN, POST };

constexpr uint8_t kGolfTopRows = 10;   // leaders kept from every read
constexpr uint8_t kGolfFavRows = 6;    // followed golfers kept past the top, at most
constexpr uint8_t kGolfRowsMax = kGolfTopRows + kGolfFavRows;
constexpr uint32_t kGolfRows = 5;  // rows in a column, as the Top 25 card
constexpr uint32_t kGolfPageMs = 5u * 1000;
constexpr uint32_t kGolfEveryMs = 10u * 60 * 1000;  // from the end of one turn to the next
// While a round is being played the board is read again this often; a
// scoreboard read is the tour's whole document, so golf never polls at the
// ten-second pace of a game. Between tournaments the next read is a day
// away: the scoreboard names the next event as soon as ESPN posts it.
constexpr uint32_t kGolfLiveReadMs = 5u * 60 * 1000;
constexpr uint32_t kGolfIdleReadMs = 24u * 60 * 60 * 1000;
constexpr uint32_t kGolfRetryMs = 5u * 60 * 1000;  // after a failed read
// A board is left off once its event ended more than this long ago: the
// last round of one week to the first of the next is four days.
constexpr int64_t kGolfStaleSecs = 4LL * 24 * 60 * 60;

inline bool golf_league(League l) { return l == League::PGA || l == League::LPGA || l == League::EUR; }

// The board's slot on the panel: 0 PGA, 1 LPGA, 2 DP World Tour.
inline int golf_slot(League l) { return l == League::PGA ? 0 : l == League::LPGA ? 1 : l == League::EUR ? 2 : -1; }
inline League golf_slot_league(int slot) { return slot == 0 ? League::PGA : slot == 1 ? League::LPGA : League::EUR; }
constexpr int kGolfSlots = 3;

struct GolfRow {
  uint16_t pos{0};       // the document's order: 1 is the leader
  uint32_t id{0};        // ESPN athlete id
  char name[24]{};       // ESPN's short name, "B. James"
  char last[16]{};       // the surname alone, for a narrow panel: "James"
  char score[6]{};       // the total as ESPN writes it: "-11", "E", "+4"; "" before a score
  char today[6]{};       // the current round's, the same way; "" when there is none yet
  uint8_t thru{0};       // holes finished in the current round, 0 when the document has none
  bool favorite{false};  // the owner follows this golfer
};

struct GolfBoard {
  bool valid{false};
  League league{League::PGA};
  GolfState state{GolfState::PRE};
  char event[48]{};    // "World Wide Technology Championship"
  char detail[32]{};   // ESPN's word for where the event is: "Round 2 - In Progress", "Final"
  uint8_t round{0};    // the round in progress or last played, 0 when ESPN gives none
  int64_t start_epoch{0};
  int64_t end_epoch{0};  // the event's last day, for golf_fresh
  uint16_t field{0};     // competitors in the document
  PsVector<GolfRow> rows;  // the top of the board, then followed golfers, in position order
};

// A golf score as ESPN writes it: "-11", "E" for level par, "+4", "-" or ""
// before a golfer has one. False for the last two.
inline bool golf_score(const char *text, int &out) {
  out = 0;
  if (text == nullptr || text[0] == '\0' || (text[0] == '-' && text[1] == '\0'))
    return false;
  if (text[0] == 'E' && text[1] == '\0')
    return true;
  char *end = nullptr;
  long n = strtol(text, &end, 10);
  if (end == text || *end != '\0' || n < -99 || n > 99)
    return false;
  out = (int) n;
  return true;
}

// Up, and its event not long over. With no clock nothing is fresh.
inline bool golf_fresh(const GolfBoard &b, int64_t now) {
  return b.valid && !b.rows.empty() && now > 0 && (b.end_epoch == 0 || now - b.end_epoch <= kGolfStaleSecs);
}

// When a tour's board is read again: soon while its event is being played,
// a day away when it is over or still to come.
inline uint32_t golf_read_ms(const GolfBoard &b) { return b.valid && b.state == GolfState::IN ? kGolfLiveReadMs : kGolfIdleReadMs; }

// "PGA R2", over the rows; a final reads "PGA FINAL", and before the first
// round just the tour.
inline std::string golf_header(const GolfBoard &b) {
  const LeagueInfo *l = league_info(b.league);
  std::string out = l != nullptr ? l->prefix : "GOLF";
  if (b.state == GolfState::POST)
    out += " FINAL";
  else if (b.round > 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), " R%u", (unsigned) b.round);
    out += buf;
  }
  return out;
}

// One row for the wide board: " 1 B. James -11 -4 9" (position, name, total,
// today's score and the hole the golfer is through, the last two only while
// a round is being played and the document carries them).
inline std::string golf_row_wide(const GolfBoard &b, const GolfRow &r) {
  char buf[48];
  std::string tail;
  if (b.state == GolfState::IN) {
    if (r.today[0] != '\0' && strcmp(r.today, "-") != 0) {
      tail += ' ';
      tail += r.today;
    }
    if (r.thru > 0) {
      char tbuf[8];
      snprintf(tbuf, sizeof(tbuf), " %u", (unsigned) r.thru);
      tail += tbuf;
    }
  }
  snprintf(buf, sizeof(buf), "%2u %-14s %4s%s", (unsigned) r.pos, r.name, r.score[0] != '\0' ? r.score : "-", tail.c_str());
  std::string out = buf;
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

// One row for a single panel: " 1 James -11".
inline std::string golf_row(const GolfRow &r) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%2u %-9s %4s", (unsigned) r.pos, r.last[0] != '\0' ? r.last : r.name,
           r.score[0] != '\0' ? r.score : "-");
  std::string out = buf;
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

// Pages a turn takes: rows per column times columns on each.
inline uint32_t golf_pages(const GolfBoard &b, uint32_t cols) {
  uint32_t per = kGolfRows * (cols == 0 ? 1 : cols);
  return ((uint32_t) b.rows.size() + per - 1) / per;
}

// Column col of page page, one row per line. The wide board's rows carry
// the round score and thru; a single panel's only the total.
inline std::string golf_column(const GolfBoard &b, uint32_t page, uint32_t cols, uint32_t col, bool wide) {
  if (cols == 0)
    cols = 1;
  std::string out;
  size_t first = (size_t) (page * cols + col) * kGolfRows;
  for (size_t i = first; i < first + kGolfRows && i < b.rows.size(); i++) {
    if (!out.empty())
      out += '\n';
    out += wide ? golf_row_wide(b, b.rows[i]) : golf_row(b.rows[i]);
  }
  return out;
}

// The tour the next turn shows: of the fresh ones the panel follows (bit
// per slot), the one after last, so the tours take turns. -1 for none.
inline int golf_next(uint8_t ready, int last) {
  for (int k = 1; k <= kGolfSlots; k++) {
    int s = (last + k + kGolfSlots) % kGolfSlots;
    if (ready & (1u << s))
      return s;
  }
  return -1;
}

}  // namespace espn
