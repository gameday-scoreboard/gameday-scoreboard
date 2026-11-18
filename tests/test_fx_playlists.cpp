// Effect playlists (components/gameday/fx_playlists.h): the set text, the
// wait steps, the player's order and shuffle, and the guard flag that keeps
// the player's own steps from counting as a hand pick.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fx_playlists.h"

using namespace espn;

namespace {

int checks = 0;
int failures = 0;

#define FCHECK(cond, ...)                         \
  do {                                            \
    checks++;                                     \
    if (!(cond)) {                                \
      failures++;                                 \
      printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                        \
      printf("\n");                               \
    }                                             \
  } while (0)

// A panel offering a few effects and palettes, in the select's spelling.
const std::vector<std::string> kEffects = {"Matrix", "Plasma Ball", "Drift", "DNA", "Ripple Rainbow"};
const std::vector<std::string> kPalettes = {"Default", "* Random Cycle", "* Color 1", "* Colors 1&2",
                                            "* Color Gradient", "* Colors Only", "Party", "Cloud", "Lava",
                                            "Ocean"};

FxNames names() {
  FxNames n;
  n.effect = [](const std::string &s) {
    for (const auto &e : kEffects)
      if (e == s)
        return e;
    return std::string();
  };
  n.palette_index = [](const std::string &s) {
    for (size_t i = 0; i < kPalettes.size(); i++)
      if (kPalettes[i] == s)
        return (int) i;
    return -1;
  };
  n.palette_name = [](uint8_t i) { return i < kPalettes.size() ? kPalettes[i] : std::string(); };
  return n;
}

void test_wait_steps() {
  FCHECK(kFxWaitSeconds[kFxWaitDefault] == 15, "a new effect waits 15 s");
  FCHECK(kFxWaitSeconds[0] == 1 && kFxWaitSeconds[kFxWaitCount - 1] == 3600, "1 s to an hour");
  for (uint8_t i = 0; i < kFxWaitCount; i++)
    FCHECK(fx_wait_step(kFxWaitSeconds[i]) == i, "step %u reads back", (unsigned) i);
  FCHECK(fx_wait_step(0) == 0 && fx_wait_step(-5) == 0, "below 1 s is 1 s");
  FCHECK(fx_wait_step(99999) == kFxWaitCount - 1, "past an hour is an hour");
  FCHECK(kFxWaitSeconds[fx_wait_step(4)] == 3, "4 s is nearer 3 or 5: ties go shorter");
  FCHECK(kFxWaitSeconds[fx_wait_step(50)] == 45, "50 s is 45 s");
  FCHECK(kFxWaitSeconds[fx_wait_step(100)] == 120, "100 s is 2 min");
}

void test_parse() {
  FxNames n = names();
  FxStore st = parse_fx_lists("Party=Matrix|Party|200|100|30,Plasma Ball|Lava|1|2|3600,@shuffle;Calm=Drift", n);
  FCHECK(st.version == kFxStoreVersion, "version");
  FCHECK(st.count == 2, "two playlists, got %u", (unsigned) st.count);
  FCHECK(strcmp(st.list[0].name, "Party") == 0, "name");
  FCHECK(st.list[0].count == 2, "two effects");
  FCHECK(st.list[0].flags & FXLIST_SHUFFLE, "shuffle");
  const FxEntry &a = st.list[0].fx[0];
  FCHECK(strcmp(a.effect, "Matrix") == 0 && a.palette == 6 && a.speed == 200 && a.intensity == 100 &&
             kFxWaitSeconds[a.wait] == 30,
         "first effect");
  const FxEntry &b = st.list[0].fx[1];
  FCHECK(b.palette == 8 && b.speed == 1 && b.intensity == 2 && kFxWaitSeconds[b.wait] == 3600, "second effect");
  const FxEntry &c = st.list[1].fx[0];
  FCHECK(strcmp(c.effect, "Drift") == 0 && c.palette == 0 && c.speed == kFxSpeedDefault &&
             c.intensity == kFxIntensityDefault && c.wait == kFxWaitDefault,
         "an effect alone takes the defaults");
  FCHECK(!(st.list[1].flags & FXLIST_SHUFFLE), "in order");

  // Unknown effects drop out; an unknown palette is the default; bad numbers
  // are the defaults.
  st = parse_fx_lists("A=Nope|Party|1|1|1,matrix|Party,DNA|Bogus|x|999|soon", n);
  FCHECK(st.count == 1 && st.list[0].count == 1, "only DNA is offered (names match exactly)");
  FCHECK(strcmp(st.list[0].fx[0].effect, "DNA") == 0 && st.list[0].fx[0].palette == 0 &&
             st.list[0].fx[0].speed == kFxSpeedDefault && st.list[0].fx[0].intensity == kFxIntensityDefault &&
             st.list[0].fx[0].wait == kFxWaitDefault,
         "DNA with defaults");

  // At most kFxListMax playlists and kFxEntryMax effects; names cleaned,
  // empty and repeated names dropped; an empty playlist is kept.
  std::string many = "One=;=Matrix;One=Drift;Two=;Three=;Four=;Five=";
  st = parse_fx_lists(many, n);
  FCHECK(st.count == 4, "four playlists, got %u", (unsigned) st.count);
  FCHECK(strcmp(st.list[1].name, "Two") == 0 && strcmp(st.list[3].name, "Four") == 0, "repeats and blanks gone");
  FCHECK(st.list[0].count == 0, "the first One kept, empty");
  std::string nine = "Nine=";
  for (int i = 0; i < 9; i++)
    nine += std::string(i ? "," : "") + "Matrix";
  st = parse_fx_lists(nine, n);
  FCHECK(st.list[0].count == kFxEntryMax, "eight effects at most");
  st = parse_fx_lists("A very long playlist name here=Matrix", n);
  FCHECK(strlen(st.list[0].name) <= kListNameMax, "name cut to 16 bytes");
  FCHECK(parse_fx_lists("", n).count == 0, "empty clears");

  // Round trip through the text.
  std::string text = "Party=Matrix|Party|200|100|30,Plasma Ball|Lava|1|2|3600,@shuffle;Calm=Drift|Default|128|128|15;Empty=";
  st = parse_fx_lists(text, n);
  FCHECK(fx_lists_text(st, n) == text, "round trip: %s", fx_lists_text(st, n).c_str());
  st = parse_fx_lists("S=@shuffle", n);
  FCHECK(fx_lists_text(st, n) == "S=@shuffle", "empty shuffled: %s", fx_lists_text(st, n).c_str());
  FCHECK(sizeof(FxPlayer) <= 8, "the player is 8 bytes of internal RAM, got %zu", sizeof(FxPlayer));
}

// Plays a playlist with the guard flag the way gameday.cpp does: each step's
// writes fire the effect controls' callbacks, which call fx_hand_pick.
void test_player() {
  FxNames n = names();
  FxStore st = parse_fx_lists("Go=Matrix|Party|1|1|1,Drift|Lava|2|2|5,DNA|Ocean|3|3|10", n);
  FxPlayer p;
  FCHECK(!fx_start(st, p, 2, 0) && p.list == 0, "no playlist 2");
  FCHECK(fx_start(st, p, 1, 7) && p.list == 1 && p.pos == 0, "in order starts on the first");
  std::vector<std::string> shown;
  auto write = [&](const FxEntry &e) {
    shown.push_back(e.effect);
    // Four controls change under the step; none of them is a hand pick.
    for (int i = 0; i < 4; i++)
      FCHECK(!fx_hand_pick(p), "a step's own write doesn't stop it");
  };
  uint32_t now = 1000;
  fx_show(st, p, now, write);
  FCHECK(p.list == 1, "a step keeps the playlist playing");
  FCHECK(p.next_ms == now + 1000, "1 s wait");
  FCHECK(!fx_due(p, now + 999) && fx_due(p, now + 1000), "due at its wait");
  for (int i = 0; i < 5; i++) {
    now = p.next_ms;
    FCHECK(fx_due(p, now), "due");
    fx_advance(st, p, 0);
    fx_show(st, p, now, write);
  }
  std::vector<std::string> want = {"Matrix", "Drift", "DNA", "Matrix", "Drift", "DNA"};
  FCHECK(shown == want, "in order, round again");
  FCHECK(p.next_ms == now + 10000, "DNA waits 10 s");

  // A hand pick (outside a step) stops it, and the panel keeps the pick.
  FCHECK(fx_hand_pick(p), "a hand pick stops the playlist");
  FCHECK(p.list == 0 && !fx_due(p, now + 99999), "stopped, nothing due");
  FCHECK(!fx_hand_pick(p), "a pick with nothing playing stops nothing");

  // The millis() wrap.
  fx_start(st, p, 1, 0);
  fx_show(st, p, 0xFFFFFF00u, [](const FxEntry &) {});
  FCHECK(!fx_due(p, 0xFFFFFFF0u) && fx_due(p, 0x000002F0u), "due across the wrap");

  // An empty playlist doesn't start.
  FxStore e = parse_fx_lists("E=", n);
  FCHECK(!fx_start(e, p, 1, 0) && p.list == 0, "an empty playlist doesn't start");
}

void test_shuffle() {
  FxNames n = names();
  FxStore st = parse_fx_lists("S=Matrix,Drift,DNA,Plasma Ball,Ripple Rainbow,@shuffle", n);
  uint32_t seed = 12345;
  auto rnd = [&]() {
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
  };
  for (int run = 0; run < 50; run++) {
    FxPlayer p;
    fx_start(st, p, 1, rnd());
    std::vector<int> order = {p.pos};
    for (int i = 0; i < 5 * 6 - 1; i++) {
      fx_advance(st, p, rnd());
      order.push_back(p.pos);
    }
    for (int round = 0; round < 6; round++) {
      uint8_t seen = 0;
      for (int i = 0; i < 5; i++)
        seen |= (uint8_t) (1u << order[round * 5 + i]);
      FCHECK(seen == 0x1F, "run %d round %d shows every effect once", run, round);
    }
    for (size_t i = 1; i < order.size(); i++)
      FCHECK(order[i] != order[i - 1], "run %d: never the same effect twice running", run);
  }
  // Two effects shuffled alternate; one stays.
  FxStore two = parse_fx_lists("T=Matrix,Drift,@shuffle", n);
  FxPlayer p;
  fx_start(two, p, 1, 1);
  for (int i = 0; i < 6; i++) {
    uint8_t before = p.pos;
    fx_advance(two, p, rnd());
    FCHECK(p.pos != before, "two shuffled alternate");
  }
  FxStore one = parse_fx_lists("O=Matrix,@shuffle", n);
  fx_start(one, p, 1, 3);
  fx_advance(one, p, rnd());
  FCHECK(p.pos == 0, "one stays");
}

void test_edit() {
  FxNames n = names();
  FxStore st = parse_fx_lists("A=Matrix;B=Drift,DNA", n);
  FxPlayer p;
  fx_start(st, p, 2, 0);
  FxList before = st.list[1];
  // B moves first, unchanged: it keeps playing at its new place.
  FxStore moved = parse_fx_lists("B=Drift,DNA;A=Matrix", n);
  FCHECK(fx_after_edit(moved, p, before) == FxEdit::KEEP && p.list == 1, "moved, kept");
  // B changed: it starts over.
  before = moved.list[0];
  FxStore changed = parse_fx_lists("B=Drift|Lava,DNA;A=Matrix", n);
  FCHECK(fx_after_edit(changed, p, before) == FxEdit::RESTART && p.list == 1, "changed, restarts");
  // B renamed in its place, effects the same: it keeps playing.
  before = changed.list[0];
  FCHECK(fx_after_edit(parse_fx_lists("Bee=Drift|Lava,DNA;A=Matrix", n), p, before) == FxEdit::KEEP && p.list == 1,
         "renamed, kept");
  // B gone (deleted, or renamed and changed), or emptied: it stops.
  FCHECK(fx_after_edit(parse_fx_lists("A=Matrix", n), p, before) == FxEdit::STOP && p.list == 0, "deleted, stops");
  fx_start(changed, p, 1, 0);
  FCHECK(fx_after_edit(parse_fx_lists("C=Drift,DNA", n), p, before) == FxEdit::STOP && p.list == 0, "gone, stops");
  fx_start(changed, p, 1, 0);
  FCHECK(fx_after_edit(parse_fx_lists("B=", n), p, before) == FxEdit::STOP && p.list == 0, "emptied, stops");
  FCHECK(fx_after_edit(changed, p, before) == FxEdit::NONE, "nothing playing");
}

}  // namespace

int run_fx_tests(int &total_checks) {
  test_wait_steps();
  test_parse();
  test_player();
  test_shuffle();
  test_edit();
  total_checks += checks;
  return failures;
}
