// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Effect playlists: named lists of effect setups (an effect, a palette, a
// speed, an intensity and how long it stays up) that the panel steps through
// on its Effects page, in order or shuffled, like a WLED playlist. Team
// playlists (playlists.h) are the other kind; every place an owner sees
// these says "Effect playlist" in full so the two never share a label.
//
// The playlists are kept in one fixed-size blob, saved to flash as it is and
// held in PSRAM (gameday.cpp); only the player below sits in internal RAM.
// An effect is saved by its name, since its place in the effect list moves
// when a firmware update adds or drops effects; a palette by its number,
// which is WLED's own and fixed.
//
// In a set request and the settings backup they are one line of text:
//   Party=Matrix|Party|128|128|15,Plasma Ball|Lava|200|100|60,@shuffle;Calm=Drift|Ocean|64|128|300
// A playlist's name, then its effects, each effect|palette|speed|intensity|
// seconds, and @shuffle for a shuffled one. Pure, no ESPHome includes, so the
// host tests cover it.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "playlists.h"

namespace espn {

constexpr int kFxListMax = 4;         // effect playlists on a panel
constexpr int kFxEntryMax = 8;        // effects in one
constexpr size_t kFxEffectNameMax = 19;  // bytes; the longest WLED 2D name is 16

// How long an effect stays up: a dropdown of these, 1 second to an hour.
constexpr int kFxWaitCount = 16;
constexpr uint16_t kFxWaitSeconds[kFxWaitCount] = {1,   2,   3,   5,   10,  15,   20,   30,
                                                   45,  60,  120, 300, 600, 900,  1800, 3600};
static_assert(kFxListMax == 4, "set_request.h takes fxlist=0 to 4");
constexpr uint8_t kFxWaitDefault = 5;  // 15 seconds for a new effect
constexpr uint8_t kFxSpeedDefault = 128;
constexpr uint8_t kFxIntensityDefault = 128;

constexpr uint8_t FXLIST_SHUFFLE = 1;

struct FxEntry {
  char effect[kFxEffectNameMax + 1];  // terminated; "" is an empty slot
  uint8_t palette;                    // WLED's palette number
  uint8_t speed;
  uint8_t intensity;
  uint8_t wait;  // index into kFxWaitSeconds
} __attribute__((packed));

struct FxList {
  char name[kListNameMax + 1];  // terminated, so Home Assistant's select can point at it
  uint8_t count;                // effects in it, 0 to kFxEntryMax
  uint8_t flags;                // FXLIST_*
  FxEntry fx[kFxEntryMax];
} __attribute__((packed));

// The saved blob. version is 1; a blob of another size or version is
// dropped, leaving no effect playlists.
struct FxStore {
  uint8_t version;
  uint8_t count;  // playlists, 0 to kFxListMax
  FxList list[kFxListMax];
  uint8_t spare[4];
} __attribute__((packed));
static_assert(sizeof(FxEntry) == 24, "FxEntry is part of a saved blob");
static_assert(sizeof(FxStore) == 2 + kFxListMax * (kListNameMax + 3 + kFxEntryMax * 24) + 4,
              "FxStore is a saved blob");
constexpr uint8_t kFxStoreVersion = 1;

// The step nearest a number of seconds, ties to the shorter one; past the
// ends, the first or the last step.
inline uint8_t fx_wait_step(long seconds) {
  uint8_t best = 0;
  for (uint8_t i = 1; i < kFxWaitCount; i++) {
    long d = labs(seconds - (long) kFxWaitSeconds[i]), b = labs(seconds - (long) kFxWaitSeconds[best]);
    if (d < b)
      best = i;
  }
  return best;
}

inline uint32_t fx_wait_ms(const FxEntry &e) {
  return (uint32_t) kFxWaitSeconds[e.wait < kFxWaitCount ? e.wait : kFxWaitDefault] * 1000u;
}

// The panel's effects and palettes are set_request.h's FxNames, which the
// effects package fills at boot: an effect's own spelling ("" for one this
// panel doesn't offer), a palette's number by name and its name by number.

namespace detail {
inline void fx_copy(char *dst, size_t cap, const std::string &s) {
  size_t n = s.size() < cap ? s.size() : cap;
  memcpy(dst, s.data(), n);
  dst[n] = '\0';
}
// 0 to 255 from text, or the default when it isn't a whole number there.
inline uint8_t fx_byte(const std::string &v, uint8_t dflt) {
  if (v.empty() || v.size() > 3)
    return dflt;
  int n = 0;
  for (char c : v) {
    if (c < '0' || c > '9')
      return dflt;
    n = n * 10 + (c - '0');
  }
  return n > 255 ? dflt : (uint8_t) n;
}
inline std::string fx_field(const std::string &s, size_t &p, char sep) {
  size_t end = s.find(sep, p);
  std::string out = s.substr(p, end == std::string::npos ? std::string::npos : end - p);
  p = end == std::string::npos ? s.size() : end + 1;
  return out;
}
}  // namespace detail

// One effect from "effect|palette|speed|intensity|seconds". False for an
// effect the panel doesn't offer, which is left out. A palette it doesn't
// know is the effect's default (0), and a speed, intensity or time it can't
// read is the default for a new effect.
inline bool parse_fx_entry(const std::string &text, const FxNames &names, FxEntry &e) {
  e = FxEntry{};
  size_t p = 0;
  if (names.effect == nullptr)
    return false;  // the effects package hasn't said which effects there are
  std::string effect = names.effect(detail::fx_field(text, p, '|'));
  if (effect.empty() || effect.size() > kFxEffectNameMax)
    return false;
  detail::fx_copy(e.effect, kFxEffectNameMax, effect);
  std::string pal = detail::fx_field(text, p, '|');
  int pi = pal.empty() || names.palette_id == nullptr ? 0 : names.palette_id(pal);
  e.palette = (uint8_t) (pi >= 0 && pi <= 255 ? pi : 0);
  e.speed = detail::fx_byte(detail::fx_field(text, p, '|'), kFxSpeedDefault);
  e.intensity = detail::fx_byte(detail::fx_field(text, p, '|'), kFxIntensityDefault);
  std::string secs = detail::fx_field(text, p, '|');
  int s = 0;
  e.wait = parse_whole_int(secs, s) ? fx_wait_step(s) : kFxWaitDefault;
  return true;
}

// The set text into a store. At most kFxListMax playlists of kFxEntryMax
// effects; a name left empty by list_name_clean, or one a playlist before it
// already has, drops that playlist, so Home Assistant never lists two the
// same. "" clears them all. A playlist with no effects is kept, so one can be
// made and then filled. Written straight into st, so the panel parses into
// its PSRAM copy without a second one on the loop task's stack.
inline void parse_fx_lists(const std::string &v, const FxNames &names, FxStore &st) {
  memset(&st, 0, sizeof(st));
  st.version = kFxStoreVersion;
  size_t start = 0;
  while (start < v.size() && st.count < kFxListMax) {
    std::string part = detail::fx_field(v, start, ';');
    size_t eq = part.find('=');
    std::string name = list_name_clean(part.substr(0, eq));
    if (name.empty())
      continue;
    bool dup = false;
    for (uint8_t n = 0; n < st.count; n++)
      dup = dup || name == st.list[n].name;
    if (dup)
      continue;
    FxList &l = st.list[st.count++];
    detail::fx_copy(l.name, kListNameMax, name);
    std::string body = eq == std::string::npos ? "" : part.substr(eq + 1);
    size_t p = 0;
    while (p < body.size()) {
      std::string tok = detail::fx_field(body, p, ',');
      if (tok == "@shuffle") {
        l.flags |= FXLIST_SHUFFLE;
        continue;
      }
      if (tok.empty() || tok[0] == '@' || l.count >= kFxEntryMax)
        continue;
      FxEntry e;
      if (parse_fx_entry(tok, names, e))
        l.fx[l.count++] = e;
    }
  }
}
inline FxStore parse_fx_lists(const std::string &v, const FxNames &names) {
  FxStore st;
  parse_fx_lists(v, names, st);
  return st;
}

// The store back as set text, for the settings backup.
inline std::string fx_lists_text(const FxStore &st, const FxNames &names) {
  std::string out;
  for (uint8_t n = 0; n < st.count && n < kFxListMax; n++) {
    const FxList &l = st.list[n];
    if (n > 0)
      out += ';';
    out += l.name;
    out += '=';
    for (uint8_t i = 0; i < l.count && i < kFxEntryMax; i++) {
      const FxEntry &e = l.fx[i];
      if (i > 0)
        out += ',';
      out += e.effect;
      out += '|' + (names.palette_name != nullptr ? names.palette_name(e.palette) : std::string()) + '|' + std::to_string(e.speed) + '|' +
             std::to_string(e.intensity) + '|' + std::to_string(kFxWaitSeconds[e.wait < kFxWaitCount ? e.wait : 0]);
    }
    if (l.flags & FXLIST_SHUFFLE)
      out += l.count > 0 ? ",@shuffle" : "@shuffle";
  }
  return out;
}

// The playlist playing, in internal RAM: 8 bytes.
struct FxPlayer {
  uint8_t list{0};        // 1 to the playlist count; 0 none
  uint8_t pos{0};         // the effect up now
  uint8_t played{0};      // shuffle: a bit per effect shown this time round
  bool stepping{false};   // the guard flag: the player is writing the effect controls
  uint32_t next_ms{0};    // millis() when the next effect is due
};

// The effect after the one up now. In order it is the next one, round to the
// first. Shuffled it is one not shown yet this time round, picked by rnd;
// once all have been, a new round starts, and never with the one just shown.
inline uint8_t fx_next_pos(const FxList &l, FxPlayer &p, uint32_t rnd) {
  uint8_t count = l.count < kFxEntryMax ? l.count : kFxEntryMax;
  if (count <= 1)
    return 0;
  if (!(l.flags & FXLIST_SHUFFLE))
    return (uint8_t) ((p.pos + 1) % count);
  uint8_t all = (uint8_t) ((1u << count) - 1);
  uint8_t left = (uint8_t) (all & ~p.played);
  if (left == 0) {
    p.played = 0;
    left = (uint8_t) (all & ~(1u << p.pos));
  }
  int n = 0;
  for (uint8_t i = 0; i < count; i++)
    n += (left >> i) & 1u;
  int pick = (int) (rnd % (uint32_t) n);
  for (uint8_t i = 0; i < count; i++)
    if ((left >> i) & 1u) {
      if (pick == 0)
        return i;
      pick--;
    }
  return 0;
}

// Starts playlist n (1-based) on its first effect, or a random one when it
// is shuffled. False, and nothing plays, for a playlist that isn't there or
// has no effects.
inline bool fx_start(const FxStore &st, FxPlayer &p, uint8_t n, uint32_t rnd) {
  p = FxPlayer{};
  if (n < 1 || n > st.count || st.list[n - 1].count == 0)
    return false;
  const FxList &l = st.list[n - 1];
  p.list = n;
  p.pos = (l.flags & FXLIST_SHUFFLE) ? (uint8_t) (rnd % l.count) : 0;
  p.played = (uint8_t) (1u << p.pos);
  return true;
}

// Moves to the next effect; the caller then shows it (fx_show).
inline void fx_advance(const FxStore &st, FxPlayer &p, uint32_t rnd) {
  if (p.list < 1 || p.list > st.count)
    return;
  p.pos = fx_next_pos(st.list[p.list - 1], p, rnd);
  p.played |= (uint8_t) (1u << p.pos);
}

// Is the next effect due? Signed, so it holds across the millis() wrap.
inline bool fx_due(const FxPlayer &p, uint32_t now) { return p.list != 0 && (int32_t) (now - p.next_ms) >= 0; }

// Shows the effect up now: write puts its effect, palette, speed and
// intensity on the panel. The guard flag is up while it runs, so the effect
// controls changing under it don't count as a hand pick (fx_hand_pick).
template<class Write> void fx_show(const FxStore &st, FxPlayer &p, uint32_t now, Write write) {
  if (p.list < 1 || p.list > st.count)
    return;
  const FxEntry &e = st.list[p.list - 1].fx[p.pos];
  p.stepping = true;
  write(e);
  p.stepping = false;
  p.next_ms = now + fx_wait_ms(e);
}

// One of the effect controls (effect, palette, speed, intensity) changed.
// Outside the player's own writes that is somebody picking by hand on the
// device page, the app or Home Assistant: the playlist stops and the panel
// stays on what they picked. True when that stopped a playlist.
inline bool fx_hand_pick(FxPlayer &p) {
  if (p.stepping || p.list == 0)
    return false;
  p = FxPlayer{};
  return true;
}

// The same effects, Shuffle and all, whatever the names.
inline bool fx_same_effects(const FxList &a, const FxList &b) {
  return a.count == b.count && a.flags == b.flags && memcmp(a.fx, b.fx, sizeof(a.fx)) == 0;
}

// What the player does after the playlists were edited, given the one
// playing before the edit (a copy): KEEP when a playlist of that name is
// still there exactly as it was (it may have moved: p.list follows it), or
// the one in its place has a new name and the same effects (a rename);
// RESTART when it is there but changed (play it from the top); STOP when
// it is gone or empty now.
enum class FxEdit : uint8_t { NONE, KEEP, RESTART, STOP };
inline FxEdit fx_after_edit(const FxStore &st, FxPlayer &p, const FxList &before) {
  if (p.list == 0)
    return FxEdit::NONE;
  for (uint8_t n = 1; n <= st.count; n++) {
    const FxList &l = st.list[n - 1];
    if (strcmp(before.name, l.name) != 0)
      continue;
    if (l.count == 0)
      break;
    p.list = n;
    return fx_same_effects(before, l) ? FxEdit::KEEP : FxEdit::RESTART;
  }
  // Its name is gone: renamed in its place, with the same effects?
  if (p.list <= st.count && st.list[p.list - 1].count > 0 && fx_same_effects(before, st.list[p.list - 1]))
    return FxEdit::KEEP;
  p = FxPlayer{};
  return FxEdit::STOP;
}

}  // namespace espn
