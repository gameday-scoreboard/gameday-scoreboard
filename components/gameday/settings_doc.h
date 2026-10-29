// The settings backup: GET /gameday/settings writes the panel's settings as
// one JSON document and POST /gameday/settings takes one back, for the device
// page's Export and Import and the apps' back up and restore. The document's
// keys are POST /gameday/set's, with JSON types instead of text:
//   {"schema": 1, "version": "v2026.10.1", "name": "gameday-2f6a70",
//    "mode": 4, "favs": ["nfl:6", "", "mlb:15"],
//    "live": [], "tz": "US Central", "down": true, ..., "brightness": 128}
// "" in favs is an empty slot (the remote's buttons keep their
// teams), [] in live the leagues of the teams it follows. lists are the
// playlists by name and team, [{"n": "Football", "t": ["nfl:6", "ncaa:251"]}]. Brightness and
// scroll are the Home Assistant numbers of the same names.
//
// An import is checked whole before anything changes: a value of the wrong
// type, or text the panel can't read (a team, a time, an address), refuses
// the document. What this firmware doesn't know or can't take (a key, a team,
// a league or a time zone from newer firmware, a number out of its range, a
// favorite past its 16, a message list address it can't read) is left out
// and listed, so a backup still restores across versions. The nickname and
// the panel layout describe the panel, not the owner's choices: they come
// back only onto the panel the file was saved from (its "name", the hostname,
// which a factory reset keeps), and a file from another panel lists them as
// kept. Wi-Fi networks and the WizMote pairing are not in it.
// Pure, no ESPHome includes, so the host tests cover it.
#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "espn_parse.h"
#include "playlists.h"
#include "set_request.h"

namespace espn {

// Goes up only when a key changes meaning; new keys don't need it, since an
// older firmware lists what it doesn't know and restores the rest.
constexpr int kSettingsSchema = 1;
// The largest import the panel reads. A typical document is about 1 KB; one
// with every favorite in all 16 playlists is about 6 KB (test_settings.cpp
// checks the fullest export fits). The buffer is PSRAM, held only during an
// import.
constexpr size_t kSettingsMaxBytes = 8192;
constexpr int kScrollMin = 1;
constexpr int kScrollMax = 10;

enum class SettingType : uint8_t { NONE, BOOL, INT, TEXT, LIST, PLAYLISTS };

// The JSON type of each set key in a settings document.
inline SettingType setting_type(const char *k) {
  static const char *const kBool[] = {"tzauto", "down",   "play",  "odds",  "opp",    "bootaddr",   "today",
                                      "autoupd", "count", "match", "beta",  "best",   "jump",       "banner",
                                      "leaders", "night", "nightscreen", "favtake", "countdown", "top25",
                                      "news"};
  static const char *const kInt[] = {"mode",  "rotate",   "panels",    "lockon",
                                     "release", "collide", "msgevery", "nightlevel", "list", "pregame"};
  static const char *const kText[] = {"fav1", "fav2", "fav3",       "fav4",     "tz",   "msg",
                                      "msgurl", "nightstart", "nightend", "nick", "cdname", "cddate", "hook",
                                      "sport"};
  static const char *const kList[] = {"favs", "live"};
  for (const char *b : kBool)
    if (strcmp(k, b) == 0)
      return SettingType::BOOL;
  for (const char *i : kInt)
    if (strcmp(k, i) == 0)
      return SettingType::INT;
  for (const char *t : kText)
    if (strcmp(k, t) == 0)
      return SettingType::TEXT;
  for (const char *l : kList)
    if (strcmp(k, l) == 0)
      return SettingType::LIST;
  if (strcmp(k, "lists") == 0)
    return SettingType::PLAYLISTS;
  return SettingType::NONE;
}

// "nfl:6", or "" for no team or one this firmware doesn't know.
inline std::string team_ref_text(JsonObjectConst t) {
  const char *l = t["l"] | "";
  uint32_t id = t["id"] | 0u;
  if (*l == '\0' || id == 0)
    return "";
  return std::string(l) + ":" + std::to_string(id);
}

// The settings document from the state document (GET /gameday/state), which
// the panel keeps built on its main loop, so the web server never reads a
// setting while the loop changes it. live_mask is Live games' own list (0 for
// automatic): the state's live list is what the panel scans now, every league
// while no favorite is picked. brightness and scroll are -1 in a build without
// those numbers. Keys the state leaves out (autoupd and beta in some builds)
// are left out here too.
inline void settings_from_state(JsonObjectConst s, uint16_t live_mask, int brightness, int scroll, JsonDocument &out) {
  out.clear();
  out["schema"] = kSettingsSchema;
  if (s["version"].is<const char *>())
    out["version"] = s["version"];
  if (s["name"].is<const char *>())
    out["name"] = s["name"];
  // In the device page's order. Ints, switches and text have the state's
  // key and type; the rest are converted.
  static const char *const kKeys[] = {"nick",     "mode",     "favs",     "rotate",      "lockon",     "release",
                                      "collide",  "today",    "jump",     "banner",      "favtake",    "live",
                                      "tz",       "tzauto",   "down",     "count",       "match",      "play",
                                      "odds",     "leaders",  "opp",      "best",        "bootaddr",   "night",
                                      "nightstart", "nightend", "nightscreen", "nightlevel", "countdown", "top25",
                                      "news",     "cdname",   "cddate",   "msg",      "msgurl",      "msgevery",
                                      "autoupd",  "beta",
                                      "lists",    "list",     "sport",    "hook",       "pregame"};
  for (const char *k : kKeys) {
    if (strcmp(k, "favs") == 0) {
      // An empty slot is "", kept so the teams after it keep their buttons;
      // the empty slots at the end are left off.
      if (!s["favs"].is<JsonArrayConst>())
        continue;
      std::vector<std::string> favs;
      for (JsonVariantConst f : s["favs"].as<JsonArrayConst>())
        favs.push_back(f.is<JsonObjectConst>() ? team_ref_text(f) : std::string());
      while (!favs.empty() && favs.back().empty())
        favs.pop_back();
      JsonArray list = out["favs"].to<JsonArray>();
      for (const auto &f : favs)
        list.add(f);
    } else if (strcmp(k, "lists") == 0) {
      // The state's teams are places in favs; the backup names them.
      if (!s["lists"].is<JsonArrayConst>())
        continue;
      JsonArrayConst favs = s["favs"].as<JsonArrayConst>();
      JsonArray lists = out["lists"].to<JsonArray>();
      for (JsonObjectConst l : s["lists"].as<JsonArrayConst>()) {
        JsonObject o = lists.add<JsonObject>();
        o["n"] = l["n"];
        JsonArray t = o["t"].to<JsonArray>();
        for (JsonVariantConst f : l["f"].as<JsonArrayConst>()) {
          std::string ref = f.is<int>() ? team_ref_text(favs[f.as<int>()].as<JsonObjectConst>()) : std::string();
          if (!ref.empty())
            t.add(ref);
        }
      }
    } else if (strcmp(k, "live") == 0) {
      JsonArray live = out["live"].to<JsonArray>();
      for (const auto &l : kLeagues)
        if (live_mask & (1u << (uint8_t) l.league))
          live.add(l.key);
    } else if (strcmp(k, "tz") == 0) {
      if (s["tz_name"].is<const char *>())
        out["tz"] = s["tz_name"];
    } else if (strcmp(k, "tzauto") == 0) {
      if (s["tz_auto"].is<bool>())
        out["tzauto"] = s["tz_auto"];
    } else if (strcmp(k, "sport") == 0) {
      // The sport row playing, or none: list then says what plays.
      if (s["sport"].is<const char *>())
        out["sport"] = *s["sport"].as<const char *>() != '\0' ? s["sport"].as<const char *>() : "none";
    } else if (!s[k].isNull()) {
      out[k] = s[k];
    }
  }
  int panels = s["panels"] | 0;
  if (panels > 0)
    out["panels"] = panels;
  if (brightness >= 0)
    out["brightness"] = brightness;
  if (scroll >= 0)
    out["scroll"] = scroll;
}

// What the panel can take, from the build and the running firmware.
struct SettingsTarget {
  std::string name;  // this panel's hostname, as the export writes it
  std::string nick;  // its nickname now
  size_t fav_max{16};
  int panels{0};      // panels wide now; 0 when the build has no panel layout
  int max_panels{0};  // the most it drives
  bool autoupd{false};
  bool beta{false};
  bool brightness{false};
  bool scroll{false};
};

struct SettingsImport {
  std::vector<SetOp> ops;  // in kSetKeys order, as apply_ops_ applies them
  int brightness{-1};      // -1: leave it
  int scroll{-1};
  bool restart{false};  // the panel count changes, so the panel restarts
  std::vector<std::string> skipped;  // "key" or "key=value" this firmware left out
  std::vector<std::string> kept;     // "nick", "panels": another panel's, this one keeps its own
  std::string error;  // why the document was refused; nothing was applied
};

namespace detail {

// Refused: nothing to apply, only the reason.
inline bool settings_fail(SettingsImport &out, const std::string &why) {
  out = SettingsImport{};
  out.error = why;
  return false;
}

// "nfl:6" a known team, "none"/"" no team. 1 known, 0 empty, -1 unknown to
// this firmware (newer, or a club that left its league), -2 not a team.
inline int settings_team(const std::string &v, TeamRef &t) {
  t = TeamRef{};
  if (v.empty() || v == "none")
    return 0;
  if (!parse_team_ref(v, t.league, t.id)) {
    // A league this firmware doesn't have reads like an unknown team.
    size_t colon = v.find(':');
    return colon != std::string::npos && colon > 0 && atoi(v.c_str() + colon + 1) > 0 ? -1 : -2;
  }
  return known_team(t.league, t.id) ? 1 : -1;
}

}  // namespace detail

// Checks a settings document and turns it into the ops POST /gameday/set
// would apply, in kSetKeys order. False with out.error when it is refused.
inline bool parse_settings(JsonVariantConst root, const SettingsTarget &target, SettingsImport &out) {
  out = SettingsImport{};
  if (!root.is<JsonObjectConst>())
    return detail::settings_fail(out, "Not a settings backup.");
  JsonObjectConst doc = root.as<JsonObjectConst>();
  if (!doc["schema"].is<int>())
    return detail::settings_fail(out, "Not a settings backup.");
  int schema = doc["schema"].as<int>();
  if (schema < 1)
    return detail::settings_fail(out, "Not a settings backup.");
  if (schema > kSettingsSchema)
    return detail::settings_fail(out, "This backup is from newer firmware. Update the panel, then restore it.");
  // Saved from this panel (after a factory reset, say), not from another one.
  bool same_panel = !target.name.empty() && doc["name"].is<const char *>() && target.name == doc["name"].as<const char *>();

  for (const char *key : kSetKeys) {
    JsonVariantConst v = doc[key];
    if (v.isNull())
      continue;
    std::string k = key;
    SettingType type = setting_type(key);
    SetOp op;
    switch (type) {
      case SettingType::BOOL: {
        if (!v.is<bool>())
          return detail::settings_fail(out, k + " should be true or false.");
        if ((k == "autoupd" && !target.autoupd) || (k == "beta" && !target.beta)) {
          out.skipped.push_back(k);
          continue;
        }
        parse_set(k, v.as<bool>() ? "1" : "0", op, target.fav_max);
        break;
      }
      case SettingType::INT: {
        if (!v.is<int>())
          return detail::settings_fail(out, k + " should be a whole number.");
        int n = v.as<int>();
        if (k == "panels" && target.max_panels == 0) {
          out.skipped.push_back(k);
          continue;
        }
        // Out of range is left out and listed, not clamped as a single set
        // would: newer firmware may take more (another mode, a third panel).
        bool in_range = k == "panels"    ? n >= 1 && n <= target.max_panels
                        : k == "collide" ? n == 0 || n == 1
                                         : true;
        in_range = in_range && parse_set(k, std::to_string(n), op, target.fav_max);
        bool clamped = op.kind == SetKind::ROTATE || op.kind == SetKind::LOCKON || op.kind == SetKind::RELEASE ||
                       op.kind == SetKind::MSGEVERY || op.kind == SetKind::NIGHT_LEVEL ||
                       op.kind == SetKind::PREGAME;
        if (!in_range || (clamped && op.value != n)) {
          out.skipped.push_back(k + "=" + std::to_string(n));
          continue;
        }
        if (k == "panels" && !same_panel) {
          if (n != target.panels)
            out.kept.push_back(k);
          continue;
        }
        if (k == "panels")
          out.restart = n != target.panels;
        break;
      }
      case SettingType::TEXT: {
        if (!v.is<const char *>())
          return detail::settings_fail(out, k + " should be text.");
        std::string text = v.as<const char *>();
        if (k.size() == 4 && k.compare(0, 3, "fav") == 0) {
          TeamRef t;
          int r = detail::settings_team(text, t);
          if (r == -2)
            return detail::settings_fail(out, k + " should be a team like nfl:6, or none.");
          if (r == -1) {
            out.skipped.push_back(k + "=" + text);
            continue;
          }
          parse_set(k, r == 0 ? "none" : text, op, target.fav_max);
          break;
        }
        // A sport this firmware has no row for (newer firmware may have more)
        // is left out and listed, like a team it doesn't know.
        if ((k == "tz" && timezone_index(text.c_str()) < 0) || (k == "msgurl" && !msg_url_ok(text)) ||
            (k == "hook" && !hook_url_ok(text)) ||
            (k == "sport" && text != "none" && sport_row_from_key(text.c_str()) == 0)) {
          out.skipped.push_back(k + "=" + text);
          continue;
        }
        if (k == "nick" && !same_panel) {
          parse_set(k, text, op, target.fav_max);
          if (op.text != target.nick)
            out.kept.push_back(k);
          continue;
        }
        if (!parse_set(k, text, op, target.fav_max))
          return detail::settings_fail(out, k + " isn't a value the panel takes.");
        break;
      }
      case SettingType::LIST: {
        if (!v.is<JsonArrayConst>())
          return detail::settings_fail(out, k + " should be a list.");
        JsonArrayConst list = v.as<JsonArrayConst>();
        for (JsonVariantConst e : list)
          if (!e.is<const char *>())
            return detail::settings_fail(out, k + " should be a list of text.");
        if (k == "live") {
          std::string keys;
          for (JsonVariantConst e : list) {
            const char *lk = e.as<const char *>();
            if (league_by_key(lk) == nullptr) {
              out.skipped.push_back("live=" + std::string(lk));
              continue;
            }
            keys += (keys.empty() ? "" : ",") + std::string(lk);
          }
          parse_set(k, keys, op, target.fav_max);
          break;
        }
        // favs, slot by slot: an empty or unknown slot stays empty. Past
        // this firmware's most (newer firmware may hold more), the rest are
        // left out and listed.
        op.kind = SetKind::FAVS;
        for (JsonVariantConst e : list) {
          std::string text = e.as<const char *>();
          if (op.teams.size() == target.fav_max) {
            if (!text.empty() && text != "none")
              out.skipped.push_back("favs=" + text);
            continue;
          }
          TeamRef t;
          int r = detail::settings_team(text, t);
          if (r == -2)
            return detail::settings_fail(out, "favs should list teams like nfl:6.");
          if (r == -1) {
            out.skipped.push_back("favs=" + text);
            t = TeamRef{};
          }
          if (t.id != 0)
            for (const auto &f : op.teams)
              if (f == t)
                return detail::settings_fail(out, "favs lists " + text + " twice.");
          op.teams.push_back(t);
        }
        break;
      }
      case SettingType::PLAYLISTS: {
        // Into lists= text: "Name=nfl:6,ncaa:251;Name2=...". A team this
        // firmware doesn't know, a nameless playlist and any past the most
        // are left out and listed.
        const char *bad = "lists should be playlists like {\"n\": \"Football\", \"t\": [\"nfl:6\"]}.";
        if (!v.is<JsonArrayConst>())
          return detail::settings_fail(out, bad);
        std::string text;
        size_t kept = 0;
        for (JsonVariantConst e : v.as<JsonArrayConst>()) {
          if (!e.is<JsonObjectConst>() || !e["n"].is<const char *>() || !e["t"].is<JsonArrayConst>())
            return detail::settings_fail(out, bad);
          std::string name = list_name_clean(e["n"].as<const char *>());
          std::string refs;
          for (JsonVariantConst t : e["t"].as<JsonArrayConst>()) {
            if (!t.is<const char *>())
              return detail::settings_fail(out, bad);
            TeamRef ref;
            std::string r = t.as<const char *>();
            int known = detail::settings_team(r, ref);
            if (known == -2 || known == 0)
              return detail::settings_fail(out, "lists should list teams like nfl:6.");
            if (known == -1) {
              out.skipped.push_back("lists=" + name + ":" + r);
              continue;
            }
            refs += (refs.empty() ? "" : ",") + r;
          }
          if (name.empty() || kept == (size_t) kListMax) {
            out.skipped.push_back("lists=" + name);
            continue;
          }
          kept++;
          text += (text.empty() ? "" : ";") + name + "=" + refs;
        }
        parse_set(k, text, op, target.fav_max);
        break;
      }
      case SettingType::NONE:
        out.skipped.push_back(k);
        continue;
    }
    out.ops.push_back(op);
  }

  // The same start and end would turn the schedule on and never run it: the
  // end is left out, and the panel keeps the end it has.
  const SetOp *night_start = nullptr;
  for (const auto &op : out.ops)
    if (op.kind == SetKind::NIGHT_START)
      night_start = &op;
  for (size_t i = 0; night_start != nullptr && i < out.ops.size(); i++) {
    if (out.ops[i].kind == SetKind::NIGHT_END && out.ops[i].value == night_start->value) {
      out.skipped.push_back("nightend=" + night_time_text((uint16_t) out.ops[i].value));
      out.ops.erase(out.ops.begin() + i);
      break;
    }
  }

  for (const char *n : {"brightness", "scroll"}) {
    JsonVariantConst v = doc[n];
    if (v.isNull())
      continue;
    bool bright = n[0] == 'b';
    int lo = bright ? 1 : kScrollMin, hi = bright ? 255 : kScrollMax;
    if (!v.is<int>())
      return detail::settings_fail(out, std::string(n) + " should be a whole number.");
    if (v.as<int>() < lo || v.as<int>() > hi || !(bright ? target.brightness : target.scroll)) {
      out.skipped.push_back(std::string(n) + "=" + std::to_string(v.as<int>()));
      continue;
    }
    (bright ? out.brightness : out.scroll) = v.as<int>();
  }

  // Keys no part of this firmware reads: from newer firmware, or not settings.
  for (JsonPairConst p : doc) {
    const char *k = p.key().c_str();
    bool known = strcmp(k, "schema") == 0 || strcmp(k, "version") == 0 || strcmp(k, "name") == 0 ||
                 strcmp(k, "brightness") == 0 || strcmp(k, "scroll") == 0;
    for (const char *s : kSetKeys)
      known = known || strcmp(k, s) == 0;
    if (!known)
      out.skipped.push_back(k);
  }
  return true;
}

// The reply to an import: {"ok":true,"restart":false,"skipped":[...]}.
inline std::string settings_reply(const SettingsImport &in) {
  JsonDocument doc(detail::json_allocator());
  if (!in.error.empty()) {
    doc["error"] = in.error;
  } else {
    doc["ok"] = true;
    doc["restart"] = in.restart;
    JsonArray skipped = doc["skipped"].to<JsonArray>();
    for (const auto &s : in.skipped)
      skipped.add(s);
    JsonArray kept = doc["kept"].to<JsonArray>();
    for (const auto &k : in.kept)
      kept.add(k);
  }
  std::string body;
  serializeJson(doc, body);
  return body;
}

}  // namespace espn
