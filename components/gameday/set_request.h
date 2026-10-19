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

#include "leagues.h"
#include "message.h"
#include "startup.h"
#include "teams.h"
#include "timezones.h"

namespace espn {

// In the order the firmware applies them, whatever order the request uses.
constexpr const char *kSetKeys[] = {"team",   "mode",     "rotate", "fav1",    "fav2",    "fav3",
                                    "fav4",   "tz",       "tzauto", "down",    "play",    "odds",
                                    "opp",    "panels",   "bootaddr", "lockon", "release", "collide",
                                    "favs",   "today",    "live",   "autoupd", "count",   "match",
                                    "beta",   "msg",      "msgurl", "msgevery"};

// LIVE (5) follows live games in the leagues of the live mask; modes 1-3 are
// the football-only live modes from before, kept for panels that saved them.
enum class Mode : uint8_t { MY_TEAM = 0, LIVE_NFL = 1, LIVE_NCAA = 2, LIVE_ANY = 3, FAVORITES = 4, LIVE = 5 };

// Mode 0 (My team) to 5 (Live games).
constexpr int kModeCount = 6;
static_assert((int) Mode::LIVE + 1 == kModeCount, "parse_set takes modes 0 to kModeCount - 1");

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

// The leagues Live mode scans, in kLeagues order. A mask of 0 is auto: the
// saved team's league and each favorite's. team_league is the league byte
// the panel keeps, which stays after the team is cleared. NFL when nothing
// is left.
inline std::vector<League> live_leagues(uint16_t mask, uint8_t team_league, const std::vector<uint8_t> &fav_leagues) {
  if (mask == 0) {
    if (team_league < 16)
      mask |= (uint16_t) (1u << team_league);
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

// The leagues a mode's scan reads, in the order it reads them, and so the
// leagues its finals rotation covers. live is Live games' own list
// (live_leagues above). Live anything is also the no-team default. A mode
// that follows a team scans none.
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
// on auto the team or favorites it follows). True when the scan now reads
// other leagues, so the board has to drop what the old ones put up and scan
// again; a mode that doesn't read the list never does.
inline bool live_scan_changed(uint8_t mode, const std::vector<League> &before, const std::vector<League> &after) {
  return scan_leagues(mode, before) != scan_leagues(mode, after);
}

enum class SetKind : uint8_t {
  TEAM,      // team, id 0 clears it
  MODE,      // value
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
  MSG,       // text, the message card's own entries (message.h); "" clears it
  MSGURL,    // text, the list's address; "" clears it
  MSGEVERY,  // value, minutes between the card's turns, 0 = holds the board between games
};

struct TeamRef {
  uint8_t league{0};
  uint32_t id{0};
  bool operator==(const TeamRef &o) const { return league == o.league && id == o.id; }
};

struct SetOp {
  SetKind kind{SetKind::TEAM};
  int value{0};
  bool on{false};
  uint8_t slot{0};
  uint16_t mask{0};
  TeamRef team;
  std::vector<TeamRef> teams;
  std::vector<std::string> refused;  // favs entries left out, for the log
  std::string text;
};

// Decodes one key=value from the request. False when the key is unknown or
// the value is refused: the firmware logs it and changes nothing. Switches
// are on for "1" only; numbers read like atoi, so "abc" is 0.
inline bool parse_set(const std::string &k, const std::string &v, SetOp &op, size_t fav_max) {
  op = SetOp{};
  auto on = [&](SetKind kind) {
    op.kind = kind;
    op.on = v == "1";
    return true;
  };
  if (k == "team") {
    op.kind = SetKind::TEAM;
    if (v == "none")
      return true;
    return parse_team_ref(v, op.team.league, op.team.id) && known_team(op.team.league, op.team.id);
  }
  if (k == "mode") {
    op.kind = SetKind::MODE;
    op.value = atoi(v.c_str());
    return op.value >= 0 && op.value < kModeCount;
  }
  if (k == "rotate") {
    op.kind = SetKind::ROTATE;
    op.value = clamp_rotate_minutes(atoi(v.c_str()));
    return true;
  }
  if (k == "lockon") {
    op.kind = SetKind::LOCKON;
    op.value = clamp_lockon_minutes(atoi(v.c_str()));
    return true;
  }
  if (k == "release") {
    op.kind = SetKind::RELEASE;
    op.value = clamp_release_seconds(atoi(v.c_str()));
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
  return false;
}

}  // namespace espn
