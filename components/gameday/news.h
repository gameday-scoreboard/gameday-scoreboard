// The news card: ESPN headlines between games, one a turn, over the board
// like the message card (message.h) and on the same kind of clock. The
// owner turns it on; its source follows the panel. Favorite teams reads its
// teams' news, one favorite per read in turn, and Live games its leagues'.
// A read keeps kNewsMax headlines and is good for kNewsReadMs, so a panel
// reads ESPN's news about once an hour, and only while the card has turns.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "leagues.h"

namespace espn {

constexpr uint32_t kNewsMax = 6;         // headlines one read keeps
constexpr size_t kNewsLineMax = 120;     // one headline, in bytes; ESPN's run to about 90
constexpr uint32_t kNewsReadMs = 60 * 60 * 1000;   // a read is good this long
constexpr uint32_t kNewsRetryMs = 10 * 60 * 1000;  // after a failed read
constexpr int kNewsEveryMin = 3;         // minutes between the card's turns

// Where a read's headlines come from: a team (id != 0) or a whole league.
// abbr is the team's, for the card's top line; it isn't part of the match.
struct NewsSource {
  League league{League::NFL};
  uint32_t id{0};
  const char *abbr{""};
  bool operator==(const NewsSource &o) const { return league == o.league && id == o.id; }
};

// The source of read number `turn`: the candidates in turn. False with none.
inline bool news_source(const std::vector<NewsSource> &candidates, uint32_t turn, NewsSource &out) {
  if (candidates.empty())
    return false;
  out = candidates[turn % candidates.size()];
  return true;
}

// The card's top line: "DAL NEWS", "NFL NEWS". abbr is the team's (empty
// for a league), prefix the league's ("NFL", "NCAAF").
inline std::string news_header(const char *abbr, const char *prefix) {
  return std::string(abbr != nullptr && *abbr != '\0' ? abbr : prefix != nullptr ? prefix : "") + " NEWS";
}

// Whether the headlines on hand need a read before the card's next turn:
// none yet, older than kNewsReadMs, or from a source the panel no longer
// follows (followed false). After a failed read, not before kNewsRetryMs.
// Times are millis(), 0 = never.
inline bool news_read_due(uint32_t now, uint32_t list_ms, bool followed, uint32_t fail_ms) {
  if (fail_ms != 0 && now - fail_ms < kNewsRetryMs)
    return false;
  return list_ms == 0 || !followed || now - list_ms >= kNewsReadMs;
}

// Whether the headlines on hand may still show: some, and from a source the
// panel follows. A failed read leaves them up until the next one lands.
inline bool news_showable(uint32_t n, bool followed) { return n > 0 && followed; }

}  // namespace espn
