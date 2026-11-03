// The college Top 25 card: the AP poll for college football and men's
// college basketball, read from ESPN's core API (about 28 KB a poll) once a
// day and shown between games, a page of rows at a time, while the panel
// follows a college team. The poll names each team only by its ESPN id;
// the abbreviation comes from teams.h. Pure, with no ESPHome includes, so
// the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "leagues.h"
#include "psram.h"
#include "teams.h"

namespace espn {

constexpr uint8_t kTop25Max = 25;
constexpr uint32_t kTop25ReadMs = 24u * 60 * 60 * 1000;  // a good poll is read again after a day
constexpr uint32_t kTop25RetryMs = 30u * 60 * 1000;      // after a failed read
constexpr uint32_t kTop25EveryMs = 10u * 60 * 1000;      // from the end of one turn to the next
constexpr uint32_t kTop25PageMs = 5u * 1000;
constexpr uint32_t kTop25Rows = 5;  // rows in a column; a wide display shows two columns
// A poll older than this is left off. The longest gap between two polls
// is football's, from the last regular-season poll in early December to
// the final one after the title game in late January, about 45 days.
constexpr int64_t kTop25StaleSecs = 50LL * 24 * 60 * 60;

struct RankRow {
  uint8_t rank{0};
  uint8_t prev{0};  // last poll's rank, 0 = unranked
  uint32_t id{0};   // ESPN team id
  char rec[8]{};    // "7-0", "" when ESPN gave none
};

struct Poll {
  int64_t date{0};  // when ESPN published it, UTC epoch
  char when[8]{};   // "WK 9", "PRE", "FINAL"
  PsVector<RankRow> rows;
};

inline bool top25_league(League l) { return l == League::NCAA || l == League::MCBB; }

// The poll's slot on the panel: 0 football, 1 basketball.
inline int top25_slot(League l) { return l == League::NCAA ? 0 : l == League::MCBB ? 1 : -1; }
inline League top25_slot_league(int slot) { return slot == 0 ? League::NCAA : League::MCBB; }

// The poll under ESPN's core root ("https://sports.core.api.espn.com/v2/sports/").
inline const char *top25_path(League l) {
  return l == League::MCBB ? "basketball/leagues/mens-college-basketball/rankings/1"
                           : "football/leagues/college-football/rankings/1";
}

// The team id in a core API reference, ".../seasons/2026/teams/2390?lang=en".
inline uint32_t top25_ref_id(const char *ref) {
  if (ref == nullptr)
    return 0;
  const char *p = strstr(ref, "/teams/");
  if (p == nullptr)
    return 0;
  p += 7;
  uint32_t id = 0;
  bool any = false;
  for (; *p >= '0' && *p <= '9'; p++) {
    if (id > 100000000u)
      return 0;
    id = id * 10 + (uint32_t) (*p - '0');
    any = true;
  }
  return any ? id : 0;
}

// The header's short name for the poll: ESPN's "Week 9" is "WK 9",
// "Preseason" "PRE", and the last poll of a season "FINAL".
inline void top25_when(const char *display, bool last, char (&out)[8]) {
  memset(out, 0, sizeof(out));
  const char *d = display != nullptr ? display : "";
  if (last || strstr(d, "Final") != nullptr) {
    strcpy(out, "FINAL");
  } else if (strstr(d, "Preseason") != nullptr) {
    strcpy(out, "PRE");
  } else if (strncmp(d, "Week ", 5) == 0) {
    int n = atoi(d + 5);
    if (n > 0 && n < 100)
      snprintf(out, sizeof(out), "WK %d", n);
  }
}

// Up, and not older than kTop25StaleSecs. With no clock nothing is fresh.
inline bool top25_fresh(const Poll &p, int64_t now) {
  return !p.rows.empty() && p.date > 0 && now > 0 && now - p.date <= kTop25StaleSecs;
}

inline const char *top25_abbr(League l, uint32_t id) {
  for (size_t i = 0; i < kTeamCount; i++)
    if (kTeams[i].league == l && kTeams[i].espn_id == id)
      return kTeams[i].abbr;
  // A school's id is the same in every sport ESPN covers.
  League other = l == League::NCAA ? League::MCBB : League::NCAA;
  for (size_t i = 0; i < kTeamCount; i++)
    if (kTeams[i].league == other && kTeams[i].espn_id == id)
      return kTeams[i].abbr;
  return "?";
}

// "CFB AP WK 9", centered over the rows.
inline std::string top25_header(League l, const Poll &p) {
  std::string out = l == League::MCBB ? "CBB AP" : "CFB AP";
  if (p.when[0] != '\0') {
    out += ' ';
    out += p.when;
  } else {
    out += " TOP 25";
  }
  return out;
}

// " 1 IND 7-0", in a monospaced font so the columns line up. A record of
// 0-0 (a preseason poll) is left off.
inline std::string top25_row(League l, const RankRow &r) {
  char buf[24];
  const char *rec = strcmp(r.rec, "0-0") == 0 ? "" : r.rec;
  snprintf(buf, sizeof(buf), "%2u %-4s %s", (unsigned) r.rank, top25_abbr(l, r.id), rec);
  std::string out = buf;
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

// Pages a turn takes: rows per column times columns on each.
inline uint32_t top25_pages(const Poll &p, uint32_t cols) {
  uint32_t per = kTop25Rows * (cols == 0 ? 1 : cols);
  return ((uint32_t) p.rows.size() + per - 1) / per;
}

// Column col of page page, one row per line.
inline std::string top25_column(League l, const Poll &p, uint32_t page, uint32_t cols, uint32_t col) {
  if (cols == 0)
    cols = 1;
  std::string out;
  size_t first = (size_t) (page * cols + col) * kTop25Rows;
  for (size_t i = first; i < first + kTop25Rows && i < p.rows.size(); i++) {
    if (!out.empty())
      out += '\n';
    out += top25_row(l, p.rows[i]);
  }
  return out;
}

// The poll the next turn shows: of the fresh ones the panel follows (bit
// slot), the one after last, so football and basketball take turns.
// -1 for none.
inline int top25_next(uint8_t ready, int last) {
  for (int k = 1; k <= 2; k++) {
    int s = (last + k + 2) % 2;
    if (ready & (1u << s))
      return s;
  }
  return -1;
}

}  // namespace espn
