// Fuzz target: the device page's POST /gameday/set (set_request.h). The
// input is a request body as the web server hands it over, already decoded:
// key=value pairs split on '&'. Checks what apply_set_() relies on: a value
// it indexes a table with is in range whenever parse_set accepts it.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "panel_text.h"
#include "set_request.h"

using namespace espn;

namespace {

constexpr size_t kFavMax = 16;  // FAV_MAX in gameday.h

void require(bool ok, const char *what, const std::string &k, const std::string &v) {
  if (ok)
    return;
  fprintf(stderr, "parse_set accepted %s=%s but %s\n", k.c_str(), v.c_str(), what);
  abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::string body((const char *) data, size);
  std::vector<uint8_t> fav_leagues;
  uint16_t mask = 0;
  uint8_t team_league = 0;
  size_t start = 0;
  while (start <= body.size()) {
    size_t amp = body.find('&', start);
    std::string pair = body.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
    start = amp == std::string::npos ? body.size() + 1 : amp + 1;
    size_t eq = pair.find('=');
    std::string k = pair.substr(0, eq);
    std::string v = eq == std::string::npos ? std::string() : pair.substr(eq + 1);
    // The log line a refused value gets.
    esphome::gameday::panel_text(k + "=" + v);
    SetOp op;
    if (!parse_set(k, v, op, kFavMax))
      continue;
    switch (op.kind) {
      case SetKind::MODE:
        require(op.value >= 0 && op.value < kModeCount, "the mode is past MODE_OPTIONS", k, v);
        break;
      case SetKind::TZ:
        require(op.value >= 0 && (size_t) op.value < kTimezoneCount, "the zone is past kTimezones", k, v);
        break;
      case SetKind::ROTATE:
        require(rotate_minutes_valid(op.value), "the rotation is out of range", k, v);
        break;
      case SetKind::LOCKON:
        require(op.value >= kLockonMinMinutes && op.value <= kLockonMaxMinutes, "lock-on is out of range", k, v);
        break;
      case SetKind::RELEASE:
        require(op.value >= kReleaseMinSeconds && op.value <= kReleaseMaxSeconds, "release is out of range", k, v);
        break;
      case SetKind::FAV_SLOT:
        require(op.slot >= 1 && op.slot <= 4, "the slot is not 1 to 4", k, v);
        require(op.team.id == 0 || known_team(op.team.league, op.team.id), "the team is unknown", k, v);
        if (op.team.id != 0)
          fav_leagues.push_back(op.team.league);
        break;
      case SetKind::TEAM:
        require(op.team.id == 0 || known_team(op.team.league, op.team.id), "the team is unknown", k, v);
        if (op.team.id != 0)
          team_league = op.team.league;
        break;
      case SetKind::FAVS:
        require(op.teams.size() <= kFavMax, "the list is longer than FAV_MAX", k, v);
        for (const auto &t : op.teams) {
          require(known_team(t.league, t.id), "a favorite is unknown", k, v);
          fav_leagues.push_back(t.league);
        }
        break;
      case SetKind::LIVE:
        require((op.mask >> kLeagueCount) == 0, "the mask names a league past kLeagues", k, v);
        mask = op.mask;
        break;
      case SetKind::MSG:
        require(op.text.size() <= kMsgTextMax, "the message is longer than kMsgTextMax", k, v);
        // What the board gets of each page, as msg_tick_ hands it to board_lines().
        for (uint32_t i = 0; i < msg_count(op.text); i += kMsgPageLines)
          esphome::gameday::panel_text(msg_entries(op.text, i, kMsgPageLines));
        break;
      case SetKind::MSGURL:
        require(msg_url_ok(op.text) && op.text.size() <= kMsgUrlMax, "the address is not one the panel reads", k, v);
        break;
      case SetKind::MSGEVERY:
        require(op.value >= 0 && op.value <= kMsgEveryMax, "msgevery is out of range", k, v);
        break;
      default:
        break;
    }
  }
  std::vector<League> live = live_leagues(mask, team_league, fav_leagues);
  for (uint8_t mode = 0; mode < kModeCount; mode++) {
    live_scan_changed(mode, live, live_leagues(0, team_league, fav_leagues));
    for (League l : scan_leagues(mode, live))
      if (league_info(l) == nullptr)
        abort();
  }
  return 0;
}
