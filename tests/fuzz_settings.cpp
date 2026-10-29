// Fuzz target: POST /gameday/settings (settings_doc.h). The input is a request
// body; it is also read as a state document for the export. Checks what
// apply_ops_() relies on: a refused document has nothing to apply, and every
// op of an accepted one is in range, as parse_set leaves them.
#include <ArduinoJson.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "settings_doc.h"

using namespace espn;

namespace {

void require(bool ok, const char *what) {
  if (ok)
    return;
  fprintf(stderr, "parse_settings: %s\n", what);
  abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size > kSettingsMaxBytes)
    return 0;  // the panel answers 413 before it reads one
  JsonDocument doc;
  if (deserializeJson(doc, (const char *) data, size))
    return 0;
  SettingsTarget target;
  target.panels = 1;
  target.max_panels = 2;
  target.autoupd = target.beta = target.brightness = target.scroll = true;
  SettingsImport in;
  bool ok = parse_settings(doc.as<JsonVariantConst>(), target, in);
  require(ok == in.error.empty(), "the result and the error disagree");
  settings_reply(in);
  if (!ok) {
    require(in.ops.empty() && in.brightness < 0 && in.scroll < 0, "a refused document has something to apply");
  } else {
    require(in.brightness < 0 || (in.brightness >= 1 && in.brightness <= 255), "brightness is out of range");
    require(in.scroll < 0 || (in.scroll >= kScrollMin && in.scroll <= kScrollMax), "scroll is out of range");
    for (const auto &op : in.ops) {
      switch (op.kind) {
        case SetKind::MODE:
          require(op.value > 0 && op.value < kModeCount, "the mode is out of range or My team's 0");
          break;
        case SetKind::TZ:
          require(op.value >= 0 && (size_t) op.value < kTimezoneCount, "the zone is past kTimezones");
          break;
        case SetKind::PANELS:
          require(op.value >= 1 && op.value <= target.max_panels, "the panel count is out of range");
          break;
        case SetKind::FAV_SLOT:
          require(op.team.id == 0 || known_team(op.team.league, op.team.id), "the team is unknown");
          break;
        case SetKind::FAVS:
          require(op.teams.size() <= target.fav_max, "the list is longer than FAV_MAX");
          for (const auto &t : op.teams)
            require(t.id == 0 || known_team(t.league, t.id), "a favorite is unknown");
          break;
        case SetKind::LIVE:
          require((op.mask >> kLeagueCount) == 0, "the mask names a league past kLeagues");
          break;
        case SetKind::MSG:
          require(op.text.size() <= kMsgTextMax, "the message is longer than kMsgTextMax");
          break;
        case SetKind::MSGURL:
          require(msg_url_ok(op.text), "the address is not one the panel reads");
          break;
        case SetKind::NIGHT_LEVEL:
          require(op.value >= 1 && op.value <= 255, "the night level is out of range");
          break;
        default:
          break;
      }
    }
  }
  if (doc.is<JsonObjectConst>()) {
    JsonDocument out;
    settings_from_state(doc.as<JsonObjectConst>(), 0x1ff, 128, 5, out);
  }
  return 0;
}
