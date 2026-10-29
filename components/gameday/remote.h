// The WizMote's buttons 1 to 4: what each one plays. A button starts on its
// own favorite (button 2 on favorite 2) and stays on what the owner sets:
// a favorite, which it follows through a reorder; a playlist, which it
// follows through a rename; every favorite; or nothing. Nothing else moves
// a button. When the team leaves the favorites, or the playlist is removed,
// the button goes back to its own favorite and keeps what it lost (gone) so
// the page can say so, until the owner sets that button again.
// In set requests, the state and the settings backup a button reads fav,
// team:nfl:6, list:<name>, all or none.
// Pure, no ESPHome includes, so the host tests cover it.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "playlists.h"
#include "set_request.h"

namespace espn {

constexpr int kRemoteButtons = 4;

// Saved as it is (Prefs blob), so a field never changes size. A saved zero
// is the button's own favorite with nothing lost.
struct RemoteButton {
  uint8_t kind;  // RemoteKind
  uint8_t gone;  // RemoteKind of what was lost (TEAM or LIST), FAV for nothing
  uint8_t league;
  uint32_t id;
  char name[kListNameMax];  // no terminator when full
} __attribute__((packed));

inline std::string remote_name(const RemoteButton &b) { return std::string(b.name, strnlen(b.name, kListNameMax)); }

inline void remote_put_name(RemoteButton &b, const std::string &name) {
  memset(b.name, 0, sizeof(b.name));
  strncpy(b.name, name.c_str(), kListNameMax);
}

inline std::string remote_text(RemoteKind kind, const RemoteButton &b) {
  switch (kind) {
    case RemoteKind::TEAM:
      return "team:" + std::string(league_key((League) b.league)) + ":" + std::to_string(b.id);
    case RemoteKind::LIST:
      return "list:" + remote_name(b);
    case RemoteKind::ALL:
      return "all";
    case RemoteKind::NONE:
      return "none";
    case RemoteKind::FAV:
      break;
  }
  return "fav";
}

// A remoteN op (set_request.h) onto its button. False, the button as it was,
// for a team that isn't a favorite or a playlist name that isn't one of names.
inline bool remote_set(RemoteButton &b, const SetOp &op, const std::vector<TeamRef> &favs,
                       const std::vector<std::string> &names) {
  RemoteKind kind = (RemoteKind) op.value;
  RemoteButton n{};
  n.kind = (uint8_t) kind;
  if (kind == RemoteKind::TEAM) {
    bool fav = false;
    for (const auto &f : favs)
      fav = fav || (f.id != 0 && f == op.team);
    if (!fav)
      return false;
    n.league = op.team.league;
    n.id = op.team.id;
  } else if (kind == RemoteKind::LIST) {
    bool found = false;
    for (const auto &name : names)
      found = found || name == op.text;
    if (!found)
      return false;
    remote_put_name(n, op.text);
  }
  b = n;
  return true;
}

// After the favorites changed: a button on a team that left them goes back
// to its own favorite. True when it did.
inline bool remote_after_favs(RemoteButton &b, const std::vector<TeamRef> &favs) {
  if (b.kind != (uint8_t) RemoteKind::TEAM)
    return false;
  TeamRef t{b.league, b.id};
  for (const auto &f : favs)
    if (f.id != 0 && f == t)
      return false;
  b.kind = (uint8_t) RemoteKind::FAV;
  b.gone = (uint8_t) RemoteKind::TEAM;
  return true;
}

// After the playlists changed from before to after (names in order): a
// button on a playlist keeps it by name. A rename, the same number of
// playlists with a new name where the old one was, carries the button to the
// new name; a playlist that is gone puts the button back on its own favorite.
// True when the button changed.
inline bool remote_after_lists(RemoteButton &b, const std::vector<std::string> &before,
                               const std::vector<std::string> &after) {
  if (b.kind != (uint8_t) RemoteKind::LIST)
    return false;
  std::string name = remote_name(b);
  for (const auto &a : after)
    if (a == name)
      return false;
  if (before.size() == after.size()) {
    for (size_t i = 0; i < before.size(); i++) {
      if (before[i] != name)
        continue;
      bool fresh = true;
      for (const auto &o : before)
        fresh = fresh && o != after[i];
      if (fresh) {
        remote_put_name(b, after[i]);
        return true;
      }
    }
  }
  b.kind = (uint8_t) RemoteKind::FAV;
  b.gone = (uint8_t) RemoteKind::LIST;
  return true;
}

}  // namespace espn
