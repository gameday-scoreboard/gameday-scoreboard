// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Golf: the PGA Tour, the LPGA and the DP World Tour (ESPN's golf/eur) as
// leaderboard boards, read from the tour's scoreboard document (about
// 90-600 KB, a tenth of that gzipped) and shown between games, a page of rows at a time, like the
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
#include <utility>

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
// ten-second pace of a game. ESPN calls the event in progress from the first
// tee time to the last putt, overnight and through a weather stop too, so
// between rounds the board is read every half hour. On the last day, with
// play complete, it stays at the live pace: a playoff starts within minutes
// and the final lands soon after. Between tournaments the board is read
// every few hours, and every half hour from kGolfLeadSecs before the next
// event's date. ESPN dates an event at midnight Eastern, and a first round
// in Australia tees off about nine hours before that.
constexpr uint32_t kGolfLiveReadMs = 5u * 60 * 1000;
constexpr uint32_t kGolfBreakReadMs = 30u * 60 * 1000;
constexpr uint32_t kGolfIdleReadMs = 6u * 60 * 60 * 1000;
constexpr uint32_t kGolfRetryMs = 5u * 60 * 1000;  // after a failed read
constexpr int64_t kGolfLeadSecs = 12LL * 60 * 60;
// A board is left off once its event ended more than this long ago: the
// last round of one week to the first of the next is four days. An event
// that hasn't started this long after its date is looked for at the slow
// pace again.
constexpr int64_t kGolfStaleSecs = 4LL * 24 * 60 * 60;
// A tournament in progress is left off when its last good read is older than
// this (a game was on, the screen was dark, the reads fail): the scores have
// moved on. Twice the slowest read pace of an event in progress.
constexpr int64_t kGolfOldSecs = 60LL * 60;

inline bool golf_league(League l) { return l == League::PGA || l == League::LPGA || l == League::EUR; }

// The board's slot on the panel: 0 PGA, 1 LPGA, 2 DP World Tour.
inline int golf_slot(League l) { return l == League::PGA ? 0 : l == League::LPGA ? 1 : l == League::EUR ? 2 : -1; }
inline League golf_slot_league(int slot) { return slot == 0 ? League::PGA : slot == 1 ? League::LPGA : League::EUR; }
constexpr int kGolfSlots = 3;

// A golfer no longer in the tournament (espn_parse_impl.h gone_). LEFT is a
// withdrawal or a disqualification; ESPN doesn't say which.
enum class GolfGone : uint8_t { NO, CUT, LEFT };

struct GolfRow {
  uint16_t pos{0};       // the document's order: 1 is the leader
  uint16_t place{0};     // the place shown: golfers on the same score share one; 0 before a score
  bool tied{false};      // another golfer has the same place
  GolfGone gone{GolfGone::NO};
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
  GolfState state{GolfState::PRE};  // the tournament's: IN from the first round's start to the final
  bool round_live{false};           // a round is being played now (not overnight, not stopped for weather)
  bool round_done{false};           // the round is complete (not stopped part way through)
  bool playoff{false};              // ESPN counts a playoff as one more round
  char event[48]{};    // "World Wide Technology Championship"
  char detail[32]{};   // ESPN's word for where the event is: "Round 2 - In Progress", "Final"
  uint8_t round{0};    // the round in progress or last played, 0 when ESPN gives none
  int64_t start_epoch{0};
  int64_t end_epoch{0};  // the event's last day, for golf_fresh
  int64_t next_epoch{0};  // on a final kept between events: the next event's start
  int64_t read_epoch{0};  // the clock at the last good read
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

// Whether the board has anything to show: before an event starts ESPN lists
// the field in no order, level par or no score for everyone.
inline bool golf_scored(const GolfBoard &b) {
  return b.valid && b.state != GolfState::PRE && !b.rows.empty() && b.rows[0].score[0] != '\0';
}

// Up, its event not long over, and the scores of one in progress not old.
// With no clock nothing is fresh.
inline bool golf_fresh(const GolfBoard &b, int64_t now) {
  if (!golf_scored(b) || now <= 0 || (b.end_epoch != 0 && now - b.end_epoch > kGolfStaleSecs))
    return false;
  return b.state == GolfState::POST || now - b.read_epoch <= kGolfOldSecs;
}

// A good read at now replaces the board kept, except that a final stays up
// until it is stale or the next event has scores: ESPN drops a final for the
// next event, listed with no field or no scores, a day or two later.
inline void golf_take(GolfBoard &kept, GolfBoard &&read, int64_t now) {
  if (!golf_scored(read) && kept.state == GolfState::POST && golf_scored(kept))
    kept.next_epoch = read.start_epoch;
  else
    kept = std::move(read);
  kept.read_epoch = now;
}

// When a tour's board is read again, from its last read.
inline uint32_t golf_read_ms(const GolfBoard &b) {
  const int64_t now = b.read_epoch;
  if (!b.valid || now <= 0)
    return kGolfIdleReadMs;
  if (b.state == GolfState::IN) {
    const int64_t over = now - b.end_epoch;
    const bool last_day = b.end_epoch != 0 && over >= 0 && over < 24 * 60 * 60;
    return b.round_live || (b.round_done && last_day) ? kGolfLiveReadMs : kGolfBreakReadMs;
  }
  const int64_t start = b.state == GolfState::PRE ? b.start_epoch : b.next_epoch;
  if (start == 0 || now > start + kGolfStaleSecs)
    return kGolfIdleReadMs;
  const int64_t until = start - kGolfLeadSecs - now;
  if (until >= (int64_t) (kGolfIdleReadMs / 1000))
    return kGolfIdleReadMs;
  return until * 1000 <= (int64_t) kGolfBreakReadMs ? kGolfBreakReadMs : (uint32_t) (until * 1000);
}

// "PGA R2", over the rows; a playoff reads "PGA PLAYOFF", a final "PGA
// FINAL", and before the first round just the tour.
constexpr size_t kGolfHead = 24;
inline void golf_header(const GolfBoard &b, char (&out)[kGolfHead]) {
  const LeagueInfo *l = league_info(b.league);
  const char *tour = l != nullptr ? l->prefix : "GOLF";
  if (b.state == GolfState::POST)
    snprintf(out, sizeof(out), "%s FINAL", tour);
  else if (b.playoff)
    snprintf(out, sizeof(out), "%s PLAYOFF", tour);
  else if (b.round > 0)
    snprintf(out, sizeof(out), "%s R%u", tour, (unsigned) b.round);
  else
    snprintf(out, sizeof(out), "%s", tour);
}

// Columns of the 5x8 font a row fills: 13 on a single panel (the last
// column of the last digit is blank, so 65 px fit in 64), 25 on a wide one.
constexpr int kGolfNarrow = 13;
constexpr int kGolfWide = 25;

// One row, in fixed columns so the page lines up. A single panel: the place,
// the surname and the total, " 1 Greyse -16". A wide display: "T10", more of
// the name, the total, and while a round is being played today's score and
// the hole the golfer is through ("F" finished), where the document has them:
// " T1 M. Penge   -16  -5  F".
inline void golf_row(const GolfBoard &b, const GolfRow &r, bool wide, char (&out)[kGolfWide + 1]) {
  char place[8], thru[4] = "", tail[9] = "";
  if (r.gone != GolfGone::NO)
    snprintf(place, sizeof(place), "%s", r.gone == GolfGone::CUT ? "CUT" : "OUT");
  else if (r.place == 0)
    snprintf(place, sizeof(place), "-");
  else
    snprintf(place, sizeof(place), "%s%u", wide && r.tied ? "T" : "", (unsigned) r.place);
  bool today = false, holes = false;
  if (wide && b.state == GolfState::IN && !b.playoff) {
    for (const GolfRow &g : b.rows) {
      today = today || g.today[0] != '\0';
      holes = holes || g.thru != 0;
    }
  }
  if (r.thru == 18)
    snprintf(thru, sizeof(thru), "F");
  else if (r.thru != 0)
    snprintf(thru, sizeof(thru), "%u", (unsigned) r.thru);
  if (holes)
    snprintf(tail, sizeof(tail), " %3.3s %2s", r.today, thru);
  else if (today)
    snprintf(tail, sizeof(tail), " %3.3s", r.today);
  const int pw = wide ? 3 : 2;
  const int len = (int) strlen(place);
  const int nw = (wide ? kGolfWide : kGolfNarrow) - (len > pw ? len : pw) - 5 - (int) strlen(tail);
  const char *name = r.name[0] != '\0' && (r.last[0] == '\0' || (wide && (int) strlen(r.name) <= nw)) ? r.name : r.last;
  snprintf(out, sizeof(out), "%*s %-*.*s %3.3s%s", pw, place, nw, nw, name, r.score[0] != '\0' ? r.score : "-", tail);
}

// Pages a turn takes.
inline uint32_t golf_pages(const GolfBoard &b) { return ((uint32_t) b.rows.size() + kGolfRows - 1) / kGolfRows; }

// Page page, one row per line.
constexpr size_t kGolfText = kGolfRows * (kGolfWide + 1) + 1;
inline void golf_page(const GolfBoard &b, uint32_t page, bool wide, char (&out)[kGolfText]) {
  size_t at = 0;
  out[0] = '\0';
  for (size_t i = (size_t) page * kGolfRows; i < ((size_t) page + 1) * kGolfRows && i < b.rows.size(); i++) {
    char row[kGolfWide + 1];
    golf_row(b, b.rows[i], wide, row);
    at += (size_t) snprintf(out + at, sizeof(out) - at, "%s%s", at != 0 ? "\n" : "", row);
  }
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
