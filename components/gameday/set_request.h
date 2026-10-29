// The device page's POST /gameday/set: which keys it takes, what each value
// means and where it is clamped or refused, and the leagues each live mode
// scans.
// Pure, no ESPHome includes, so the host tests cover it and check the same
// table as tests/web/mock_device.py (tests/fixtures/set_cases.json).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "cards.h"
#include "countdown.h"
#include "hook.h"
#include "leagues.h"
#include "message.h"
#include "night.h"
#include "startup.h"
#include "teams.h"
#include "timezones.h"

namespace espn {

// In the order the firmware applies them, whatever order the request uses.
constexpr const char *kSetKeys[] = {"mode",   "rotate",   "fav1",   "fav2",    "fav3",
                                    "fav4",   "tz",       "tzauto", "down",    "play",    "odds",
                                    "opp",    "panels",   "bootaddr", "lockon", "release", "collide",
                                    "favs",   "today",    "live",   "autoupd", "count",   "match",
                                    "beta",   "countdown", "top25", "cdname", "cddate", "gameleaders", "msg",
                                    "msgurl",
                                    "msgevery", "best",   "jump",   "banner",  "leaders", "night",
                                    "nightstart", "nightend", "nightscreen", "nightlevel", "favtake",
                                    "nick",   "lists",    "list",   "sport",   "hook",    "pregame",
                                    "news",   "cards"};

// FAVORITES (4) follows the favorites list; LIVE (5) follows live games in
// the leagues of the live mask. Modes 1-3 are the football-only live modes
// from before, kept for panels that saved them. 0 was My team, which
// Favorites replaced: a saved or sent 0 reads as Favorites (read_mode).
enum class Mode : uint8_t { LIVE_NFL = 1, LIVE_NCAA = 2, LIVE_ANY = 3, FAVORITES = 4, LIVE = 5 };

// The modes parse_set and a saved setting take, 0 to kModeCount - 1.
constexpr int kModeCount = 6;
static_assert((int) Mode::LIVE + 1 == kModeCount, "parse_set takes modes 0 to kModeCount - 1");

// A mode number from the page, an app, a backup or flash, as the panel runs
// it: My team's 0 is Favorites. -1 when it is out of range.
inline int read_mode(int m) {
  if (m < 0 || m >= kModeCount)
    return -1;
  return m == 0 ? (int) Mode::FAVORITES : m;
}

constexpr int kLockonMinMinutes = 5;
constexpr int kLockonMaxMinutes = 120;
constexpr int kReleaseMinSeconds = 30;
constexpr int kReleaseMaxSeconds = 3600;

inline int clamp_lockon_minutes(int minutes) {
  return minutes < kLockonMinMinutes ? kLockonMinMinutes : minutes > kLockonMaxMinutes ? kLockonMaxMinutes : minutes;
}

inline int clamp_release_seconds(int seconds) {
  return seconds < kReleaseMinSeconds ? kReleaseMinSeconds : seconds > kReleaseMaxSeconds ? kReleaseMaxSeconds : seconds;
}

// "nfl:6" -> league + id. False for "none"/"" (id 0) or garbage.
inline bool parse_team_ref(const std::string &v, uint8_t &league, uint32_t &id) {
  league = 0;
  id = 0;
  size_t colon = v.find(':');
  if (colon == std::string::npos)
    return false;
  std::string lg = v.substr(0, colon);
  id = (uint32_t) strtoul(v.c_str() + colon + 1, nullptr, 10);
  const LeagueInfo *info = league_by_key(lg.c_str());
  if (info == nullptr)
    return false;
  league = (uint8_t) info->league;
  return id != 0;
}

inline bool known_team(uint8_t league, uint32_t id) {
  for (size_t t = 0; t < kTeamCount; t++)
    if ((uint8_t) kTeams[t].league == league && kTeams[t].espn_id == id)
      return true;
  return false;
}

// "mlb,nhl" -> a bit per league. Unknown keys are skipped, so a list of only
// unknown keys, like "", is 0: back to auto.
inline uint16_t live_mask_from_keys(const std::string &keys) {
  uint16_t mask = 0;
  size_t start = 0;
  while (start < keys.size()) {
    size_t comma = keys.find(',', start);
    std::string key = keys.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    start = comma == std::string::npos ? keys.size() : comma + 1;
    const LeagueInfo *info = league_by_key(key.c_str());
    if (info != nullptr)
      mask |= (uint16_t) (1u << (uint8_t) info->league);
  }
  return mask;
}

// The leagues Live mode scans, in kLeagues order. A mask of 0 is auto: each
// favorite's league, NFL when there are none.
inline std::vector<League> live_leagues(uint16_t mask, const std::vector<uint8_t> &fav_leagues) {
  if (mask == 0) {
    for (uint8_t l : fav_leagues)
      if (l < 16)
        mask |= (uint16_t) (1u << l);
  }
  std::vector<League> out;
  for (const auto &l : kLeagues)
    if (mask & (1u << (uint8_t) l.league))
      out.push_back(l.league);
  if (out.empty())
    out.push_back(League::NFL);
  return out;
}

// No team picked: Favorites with no favorite set. Any Live mode counts as a
// choice. The panel then runs Live games over no_team_leagues() and the
// state document's live list is that set.
inline bool no_team_default(uint8_t mode, bool have_favorite) {
  return mode == (uint8_t) Mode::FAVORITES && !have_favorite;
}

// The leagues a panel with no team picked scans: every league, in kLeagues
// order. Men's college basketball is read as ESPN's Top 25 slate there
// (scan_url's top25), not all of Division I.
inline std::vector<League> no_team_leagues() {
  std::vector<League> out;
  for (const auto &l : kLeagues)
    out.push_back(l.league);
  return out;
}

// The leagues a mode's scan reads, in the order it reads them, and so the
// leagues its finals rotation covers. live is Live games' own list
// (live_leagues above, or no_team_leagues for the no-team default). A mode
// that follows teams (Favorites) scans none.
inline std::vector<League> scan_leagues(uint8_t mode, const std::vector<League> &live) {
  switch ((Mode) mode) {
    case Mode::LIVE_NFL:
      return {League::NFL};
    case Mode::LIVE_NCAA:
      return {League::NCAA};
    case Mode::LIVE_ANY:
      return {League::NFL, League::NCAA};
    case Mode::LIVE:
      return live;
    default:
      return {};
  }
}

// Live games' leagues went from before to after (its list, or with the list
// on auto the favorites it follows). True when the scan now reads
// other leagues, so the board has to drop what the old ones put up and scan
// again; a mode that doesn't read the list never does.
inline bool live_scan_changed(uint8_t mode, const std::vector<League> &before, const std::vector<League> &after) {
  return scan_leagues(mode, before) != scan_leagues(mode, after);
}

enum class SetKind : uint8_t {
  MODE,      // value, read_mode's
  ROTATE,    // value, minutes
  FAV_SLOT,  // slot (1-4), team (id 0 clears the slot)
  TZ,        // value, index into kTimezones
  TZ_AUTO,   // on
  DOWN,      // on, football
  PLAY,      // on
  ODDS,      // on
  OPP,       // on
  PANELS,    // value; PanelLayout refuses a count it can't drive
  BOOTADDR,  // on
  LOCKON,    // value, minutes
  RELEASE,   // value, seconds
  COLLIDE,   // on
  FAVS,      // teams, in priority order
  TODAY,     // on
  LIVE,      // mask, 0 = auto
  AUTOUPD,   // on
  COUNT,     // on, baseball
  MATCH,     // on, soccer
  BETA,      // on, release builds only
  COUNTDOWN,  // on, the next game's countdown card
  TOP25,      // on, the college Top 25 card (rankings.h)
  CD_NAME,    // text, the custom countdown's name
  CD_DATE,    // ymd, the custom countdown's day (0 clears it)
  GAMELEADERS,  // on, this game's leaders card at halftime and the final (game_leaders.h)
  MSG,       // text, the message card's own entries (message.h); "" clears it
  MSGURL,    // text, the list's address; "" clears it
  MSGEVERY,  // value, minutes between the card's turns, 0 = holds the board between games
  BEST,      // on, Live games picks the best game (best_game.h)
  JUMP,      // on, Favorites jumps to the action (favorites.h)
  BANNER,    // on, Favorites shows a banner when another favorite scores
  FAVTAKE,   // on, Live games locks onto a favorite's game while it is live (favorites.h)
  LEADERS,   // on, the season leaders in the pre-game ticker
  NIGHT,        // on, the night schedule (night.h)
  NIGHT_START,  // value, minutes after midnight
  NIGHT_END,    // value, minutes after midnight
  NIGHT_SCREEN, // on: lit at the night brightness, off: dark
  NIGHT_LEVEL,  // value, 1 to 255
  NICK,         // text, the panel's nickname (kNickMax); "" clears it
  LISTS,        // text, every playlist (playlists.h parse_lists); "" clears them
  LIST,         // value, the playlist Favorite teams plays, 0 = every favorite
  SPORT,        // value, the sport row Favorite teams plays, 1 + Sport; 0 = none (every favorite)
  HOOK,         // text, the game events' webhook address (hook.h); "" turns it off
  PREGAME,      // value, minutes before the start for the reminder event, 0 = none
  NEWS,         // on, the news card between games (news.h)
  CARDS,        // mask: the between-games cards named, value: those turned on (cards.h); refused: ids left out
};

struct TeamRef {
  uint8_t league{0};
  uint32_t id{0};
  bool operator==(const TeamRef &o) const { return league == o.league && id == o.id; }
};

struct SetOp {
  SetKind kind{SetKind::MODE};
  int value{0};
  bool on{false};
  uint8_t slot{0};
  uint16_t mask{0};
  TeamRef team;
  std::vector<TeamRef> teams;
  std::vector<std::string> refused;  // favs entries left out, for the log
  std::string text;
  uint32_t ymd{0};
};

// A whole number, the whole value: an optional minus and up to six digits.
// "abc", "", "5x" and "1.5" are refused rather than read as 0 or 5.
inline bool parse_whole_int(const std::string &v, int &out) {
  size_t i = !v.empty() && v[0] == '-' ? 1 : 0;
  if (v.size() == i || v.size() - i > 6)
    return false;
  int n = 0;
  for (size_t k = i; k < v.size(); k++) {
    if (v[k] < '0' || v[k] > '9')
      return false;
    n = n * 10 + (v[k] - '0');
  }
  out = i ? -n : n;
  return true;
}

// Decodes one key=value from the request. False when the key is unknown or
// the value is refused: the firmware logs it and changes nothing. Switches
// are on for "1" only. The mode and the minute and second settings take a
// whole number only (parse_whole_int); the other numbers read like atoi.
inline bool parse_set(const std::string &k, const std::string &v, SetOp &op, size_t fav_max) {
  op = SetOp{};
  auto on = [&](SetKind kind) {
    op.kind = kind;
    op.on = v == "1";
    return true;
  };
  if (k == "mode") {
    op.kind = SetKind::MODE;
    if (!parse_whole_int(v, op.value))
      return false;
    op.value = read_mode(op.value);
    return op.value >= 0;
  }
  if (k == "rotate") {
    op.kind = SetKind::ROTATE;
    if (!parse_whole_int(v, op.value))
      return false;
    op.value = clamp_rotate_minutes(op.value);
    return true;
  }
  if (k == "lockon") {
    op.kind = SetKind::LOCKON;
    if (!parse_whole_int(v, op.value))
      return false;
    op.value = clamp_lockon_minutes(op.value);
    return true;
  }
  if (k == "release") {
    op.kind = SetKind::RELEASE;
    if (!parse_whole_int(v, op.value))
      return false;
    op.value = clamp_release_seconds(op.value);
    return true;
  }
  if (k == "panels") {
    op.kind = SetKind::PANELS;
    op.value = atoi(v.c_str());
    return true;
  }
  if (k == "tz") {
    op.kind = SetKind::TZ;
    op.value = timezone_index(v.c_str());
    return op.value >= 0;
  }
  if (k == "live") {
    op.kind = SetKind::LIVE;
    op.mask = live_mask_from_keys(v);
    return true;
  }
  if (k == "favs") {
    // "mlb:15,nfl:6,..." in priority order; "" clears the list. Unknown teams
    // and repeats drop out; the rest keep their order up to fav_max.
    op.kind = SetKind::FAVS;
    size_t start = 0;
    while (start < v.size() && op.teams.size() < fav_max) {
      size_t comma = v.find(',', start);
      std::string ref = v.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      start = comma == std::string::npos ? v.size() : comma + 1;
      TeamRef t;
      if (!parse_team_ref(ref, t.league, t.id) || !known_team(t.league, t.id)) {
        op.refused.push_back(ref);
        continue;
      }
      bool dup = false;
      for (const auto &f : op.teams)
        dup = dup || f == t;
      if (!dup)
        op.teams.push_back(t);
    }
    return true;
  }
  if (k.size() == 4 && k.compare(0, 3, "fav") == 0 && k[3] >= '1' && k[3] <= '4') {
    // One of the remote's four slots; "none" or "" clears it.
    op.kind = SetKind::FAV_SLOT;
    op.slot = (uint8_t) (k[3] - '0');
    if (parse_team_ref(v, op.team.league, op.team.id) && known_team(op.team.league, op.team.id))
      return true;
    op.team = TeamRef{};
    return v == "none" || v.empty();
  }
  if (k == "msg") {
    op.kind = SetKind::MSG;
    op.text = message_clean(v, kMsgTextMax, true);
    return true;
  }
  if (k == "nick") {
    op.kind = SetKind::NICK;
    op.text = message_clean(utf8_only(v), kNickMax, false);
    return true;
  }
  if (k == "msgurl") {
    op.kind = SetKind::MSGURL;
    op.text = v;
    return msg_url_ok(v);
  }
  if (k == "msgevery") {
    // 0 means something here, so text isn't read as 0 like the other numbers.
    op.kind = SetKind::MSGEVERY;
    op.value = clamp_msg_every(atoi(v.c_str()));
    return !v.empty() && v[0] >= '0' && v[0] <= '9';
  }
  if (k == "nightstart" || k == "nightend") {
    // "23:00", the form a time field sends; anything else is refused.
    op.kind = k == "nightstart" ? SetKind::NIGHT_START : SetKind::NIGHT_END;
    op.value = night_parse_time(v);
    return op.value >= 0;
  }
  if (k == "nightlevel") {
    op.kind = SetKind::NIGHT_LEVEL;
    int level = atoi(v.c_str());
    op.value = level < 1 ? 1 : level > 255 ? 255 : level;
    return true;
  }
  if (k == "lists") {
    // Parsed by playlists.h once the request's favorites are in, since a
    // playlist can only hold favorites.
    op.kind = SetKind::LISTS;
    op.text = v;
    return true;
  }
  if (k == "list") {
    // 0 means something here, so text isn't read as 0 like the other numbers.
    // A number past the last playlist is refused where the playlists are known.
    // strtol, not atoi: a number too long for an int saturates, and is refused.
    op.kind = SetKind::LIST;
    long n = strtol(v.c_str(), nullptr, 10);
    op.value = n >= 0 && n <= 16 ? (int) n : -1;
    return !v.empty() && v[0] >= '0' && v[0] <= '9' && op.value >= 0;
  }
  if (k == "sport") {
    // A sport row (playlists.h) by its kSportKeys word; "none" stops the one
    // playing. A sport with no row is refused where the favorites are known.
    op.kind = SetKind::SPORT;
    op.value = v == "none" ? 0 : sport_row_from_key(v.c_str());
    return v == "none" || op.value != 0;
  }
  if (k == "hook") {
    op.kind = SetKind::HOOK;
    op.text = v;
    return hook_url_ok(v);
  }
  if (k == "pregame") {
    op.kind = SetKind::PREGAME;
    if (!parse_whole_int(v, op.value))
      return false;
    op.value = clamp_pregame_minutes(op.value);
    return true;
  }
  if (k == "night")
    return on(SetKind::NIGHT);
  if (k == "nightscreen")
    return on(SetKind::NIGHT_SCREEN);
  if (k == "tzauto")
    return on(SetKind::TZ_AUTO);
  if (k == "down")
    return on(SetKind::DOWN);
  if (k == "count")
    return on(SetKind::COUNT);
  if (k == "match")
    return on(SetKind::MATCH);
  if (k == "play")
    return on(SetKind::PLAY);
  if (k == "odds")
    return on(SetKind::ODDS);
  if (k == "opp")
    return on(SetKind::OPP);
  if (k == "bootaddr")
    return on(SetKind::BOOTADDR);
  if (k == "collide")
    return on(SetKind::COLLIDE);
  if (k == "today")
    return on(SetKind::TODAY);
  if (k == "autoupd")
    return on(SetKind::AUTOUPD);
  if (k == "beta")
    return on(SetKind::BETA);
  if (k == "countdown")
    return on(SetKind::COUNTDOWN);
  if (k == "top25")
    return on(SetKind::TOP25);
  if (k == "news")
    return on(SetKind::NEWS);
  if (k == "cards") {
    op.kind = SetKind::CARDS;
    uint16_t named = 0, cards_on = 0;
    if (!parse_cards(v, named, cards_on, op.refused))
      return false;
    op.mask = named;
    op.value = cards_on;
    return true;
  }
  if (k == "cdname") {
    op.kind = SetKind::CD_NAME;
    op.text = countdown_name(v);
    return true;
  }
  if (k == "cddate") {
    // "2026-12-25"; "" or "none" clears it. Anything else is refused.
    op.kind = SetKind::CD_DATE;
    return parse_countdown_date(v, op.ymd);
  }
  if (k == "gameleaders")
    return on(SetKind::GAMELEADERS);
  if (k == "best")
    return on(SetKind::BEST);
  if (k == "jump")
    return on(SetKind::JUMP);
  if (k == "banner")
    return on(SetKind::BANNER);
  if (k == "favtake")
    return on(SetKind::FAVTAKE);
  if (k == "leaders")
    return on(SetKind::LEADERS);
  return false;
}

}  // namespace espn
