// Host tests for the saved Wi-Fi network list (components/saved_networks/network_list.h)
// and the setup window after "Reset Wi-Fi" (setup_window.h).
// Build with `make` in tests/.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "network_list.h"
#include "setup_window.h"

using namespace wifi_list;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
    }                                                                          \
  } while (0)

// The names in order, "a,b,c".
static std::string names(const NetworkList &l) {
  std::string out;
  for (size_t i = 0; i < l.count; i++)
    out += std::string(i ? "," : "") + l.nets[i].ssid;
  return out;
}

static void test_put_front_and_order() {
  NetworkList l;
  clear(l);
  CHECK(l.count == 0);
  CHECK(valid(l));
  CHECK(find(l, "Home") == -1);

  CHECK(put_front(l, "Home", "pw-home"));
  CHECK(names(l) == "Home");
  CHECK(strcmp(l.nets[0].password, "pw-home") == 0);
  // Connecting to the same network again with the same password changes nothing.
  CHECK(!put_front(l, "Home", "pw-home"));
  // A new network goes first; the old one is still there.
  CHECK(put_front(l, "iPhone", "pw-phone"));
  CHECK(names(l) == "iPhone,Home");
  CHECK(find(l, "Home") == 1);
  // Back home: Home moves up, iPhone stays known.
  CHECK(put_front(l, "Home", "pw-home"));
  CHECK(names(l) == "Home,iPhone");
  CHECK(l.count == 2);
  // A changed password for a known network replaces the old one and moves it up.
  CHECK(put_front(l, "iPhone", "pw-new"));
  CHECK(names(l) == "iPhone,Home");
  CHECK(strcmp(l.nets[0].password, "pw-new") == 0);
  // Same network at the front with a new password is a change too.
  CHECK(put_front(l, "iPhone", "pw-newer"));
  CHECK(strcmp(l.nets[0].password, "pw-newer") == 0);
  CHECK(valid(l));
}

static void test_cap_at_max() {
  NetworkList l;
  clear(l);
  CHECK(put_front(l, "n1", ""));
  CHECK(put_front(l, "n2", ""));
  CHECK(put_front(l, "n3", ""));
  CHECK(put_front(l, "n4", ""));
  CHECK(put_front(l, "n5", ""));
  CHECK(l.count == MAX_NETWORKS);
  CHECK(names(l) == "n5,n4,n3,n2,n1");
  // The sixth pushes the oldest off the end.
  CHECK(put_front(l, "n6", ""));
  CHECK(l.count == MAX_NETWORKS);
  CHECK(names(l) == "n6,n5,n4,n3,n2");
  CHECK(find(l, "n1") == -1);
  // Moving a known one up on a full list drops nothing.
  CHECK(put_front(l, "n3", ""));
  CHECK(names(l) == "n3,n6,n5,n4,n2");
  CHECK(l.count == MAX_NETWORKS);
  CHECK(valid(l));
}

static void test_remove() {
  NetworkList l;
  clear(l);
  put_front(l, "c", "");
  put_front(l, "b", "");
  put_front(l, "a", "");
  CHECK(!remove(l, "zzz"));
  CHECK(!remove(l, ""));
  CHECK(!remove(l, nullptr));
  CHECK(remove(l, "b"));
  CHECK(names(l) == "a,c");
  CHECK(l.count == 2);
  // The freed slot is blank, so a saved list carries no stale password.
  CHECK(l.nets[2].ssid[0] == '\0');
  CHECK(l.nets[2].password[0] == '\0');
  CHECK(remove(l, "a"));
  CHECK(names(l) == "c");
  CHECK(remove(l, "c"));
  CHECK(l.count == 0);
  CHECK(valid(l));
  CHECK(!remove(l, "c"));
}

static void test_blank_and_long_names() {
  NetworkList l;
  clear(l);
  CHECK(!put_front(l, "", "pw"));
  CHECK(!put_front(l, nullptr, "pw"));
  CHECK(l.count == 0);
  // 32 characters is the longest a network name can be; 33 is refused.
  std::string max(32, 'x'), over(33, 'x');
  CHECK(put_front(l, max.c_str(), ""));
  CHECK(l.count == 1);
  CHECK(strlen(l.nets[0].ssid) == 32);
  CHECK(!put_front(l, over.c_str(), ""));
  CHECK(l.count == 1);
  // Likewise a 64 character password fits and 65 does not.
  std::string pmax(64, 'p'), pover(65, 'p');
  CHECK(put_front(l, "n", pmax.c_str()));
  CHECK(strlen(l.nets[0].password) == 64);
  CHECK(!put_front(l, "m", pover.c_str()));
  CHECK(find(l, "m") == -1);
  // A null password is an open network.
  CHECK(put_front(l, "open", nullptr));
  CHECK(l.nets[0].password[0] == '\0');
  CHECK(valid(l));
}

static void test_valid_rejects_bad_flash() {
  NetworkList l;
  clear(l);
  CHECK(valid(l));
  l.count = MAX_NETWORKS + 1;
  CHECK(!valid(l));
  clear(l);
  put_front(l, "ok", "");
  // A blank name inside the count is corrupt.
  l.count = 2;
  CHECK(!valid(l));
  l.count = 1;
  CHECK(valid(l));
  // An unterminated name or password is corrupt.
  memset(l.nets[0].ssid, 'x', SSID_SIZE);
  CHECK(!valid(l));
  clear(l);
  put_front(l, "ok", "");
  memset(l.nets[0].password, 'x', PASSWORD_SIZE);
  CHECK(!valid(l));
}

// ESPHome's one slot, as the app's setup, the hotspot page and Reset Wi-Fi
// in older firmware write it and as the component rewrites it after a forget.
struct Slot {
  std::string ssid, password;
};

// A boot as SavedNetworks::setup() does it: the list from flash meets the slot.
static bool boot(NetworkList &l, const Slot &slot, Network *pending = nullptr) {
  return merge_slot(l, slot.ssid.c_str(), slot.password.c_str(), pending);
}

// POST /gameday/wifi?forget= as SavedNetworks::forget() does it.
static bool forget_net(NetworkList &l, Slot &slot, const char *ssid) {
  bool removed = false;
  const Network *front = forget(l, ssid, &removed);
  if (front != nullptr)
    slot = Slot{front->ssid, front->password};
  return removed;
}

static void test_slot_hash() {
  CHECK(slot_hash("Home", "pw") != 0);
  CHECK(slot_hash("", "") != 0);
  CHECK(slot_hash("Home", "pw") == slot_hash("Home", "pw"));
  CHECK(slot_hash("Home", "pw") != slot_hash("Home", "pw2"));
  CHECK(slot_hash("Home", "pw") != slot_hash("Home2", "pw"));
  // The separator keeps the name and password apart.
  CHECK(slot_hash("ab", "c") != slot_hash("a", "bc"));
  CHECK(slot_hash("Home", nullptr) == slot_hash("Home", ""));
}

static void test_migration_from_one_network() {
  // A panel updating from 1.5.5: no list in flash, home in ESPHome's slot.
  NetworkList l;
  clear(l);
  Slot slot{"Home", "pw-home"};
  CHECK(boot(l, slot));
  CHECK(names(l) == "Home");
  CHECK(strcmp(l.nets[0].password, "pw-home") == 0);
  CHECK(l.slot_hash == slot_hash("Home", "pw-home"));
  // The next boot has nothing new to take.
  CHECK(!boot(l, slot));
  CHECK(names(l) == "Home");
  // An open network in the slot comes over too.
  clear(l);
  CHECK(boot(l, Slot{"Cafe", ""}));
  CHECK(names(l) == "Cafe");
}

static void test_changed_slot_is_added() {
  NetworkList l;
  clear(l);
  Slot slot{"Home", "pw-home"};
  boot(l, slot);
  // The app sets the panel up on a phone hotspot, and the panel restarts
  // before it connects: the slot is new, so it goes first.
  slot = Slot{"iPhone", "pw-phone"};
  CHECK(boot(l, slot));
  CHECK(names(l) == "iPhone,Home");
  CHECK(!boot(l, slot));

  // Set up on the hotspot without a restart: it connects and goes first,
  // then the panel gets home and Home goes first again. At the next boot
  // the slot still holds the hotspot, which the list already has with that
  // password: the order the panel learned since stays, only the hash is
  // recorded.
  clear(l);
  slot = Slot{"Home", "pw-home"};
  boot(l, slot);
  slot = Slot{"iPhone", "pw-phone"};    // the app's setup writes the slot
  put_front(l, "iPhone", "pw-phone");  // wifi: on_connect
  put_front(l, "Home", "pw-home");     // back home
  CHECK(boot(l, slot));
  CHECK(names(l) == "Home,iPhone");
  CHECK(l.slot_hash == slot_hash("iPhone", "pw-phone"));
  CHECK(!boot(l, slot));
  // A new password for a known network may be mistyped: the list keeps the
  // one that worked, and the new one comes back as pending, to try first
  // this boot. Connecting with it (wifi: on_connect) is what stores it.
  Network pending{};
  slot = Slot{"Home", "pw-new"};
  CHECK(boot(l, slot, &pending));
  CHECK(names(l) == "Home,iPhone");
  CHECK(strcmp(l.nets[0].password, "pw-home") == 0);
  CHECK(strcmp(pending.ssid, "Home") == 0);
  CHECK(strcmp(pending.password, "pw-new") == 0);
  CHECK(l.slot_hash == slot_hash("Home", "pw-new"));
  // It did not connect: the next boot has nothing pending and the saved
  // password is still there.
  CHECK(!boot(l, slot, &pending));
  CHECK(pending.ssid[0] == '\0');
  CHECK(strcmp(l.nets[0].password, "pw-home") == 0);
  // A later network, iPhone, with a new password that does connect.
  slot = Slot{"iPhone", "pw-phone2"};
  CHECK(boot(l, slot, &pending));
  CHECK(names(l) == "Home,iPhone");
  CHECK(strcmp(l.nets[1].password, "pw-phone") == 0);
  CHECK(strcmp(pending.ssid, "iPhone") == 0);
  CHECK(put_front(l, pending.ssid, pending.password));  // wifi: on_connect
  CHECK(names(l) == "iPhone,Home");
  CHECK(strcmp(l.nets[0].password, "pw-phone2") == 0);
  CHECK(!boot(l, slot, &pending));
  CHECK(pending.ssid[0] == '\0');
  // Without somewhere to put it the new password is dropped, never saved.
  slot = Slot{"Home", "pw-other"};
  CHECK(boot(l, slot));
  CHECK(strcmp(l.nets[1].password, "pw-home") == 0);
  // A network the list does not have still goes first at once.
  slot = Slot{"Cabin", "pw-cabin"};
  CHECK(boot(l, slot, &pending));
  CHECK(names(l) == "Cabin,iPhone,Home");
  CHECK(pending.ssid[0] == '\0');
}

static void test_blank_slot_keeps_the_list() {
  NetworkList l;
  clear(l);
  Slot slot{"Home", "pw-home"};
  boot(l, slot);
  put_front(l, "iPhone", "pw-phone");
  const uint32_t hash = l.slot_hash;
  // Save on the hotspot page with no network picked blanks ESPHome's slot.
  // The saved networks stay, and so does what the list last saw in the slot.
  slot = Slot{"", ""};
  CHECK(!boot(l, slot));
  CHECK(names(l) == "iPhone,Home");
  CHECK(l.slot_hash == hash);
  CHECK(!merge_slot(l, nullptr, nullptr));
  CHECK(l.count == 2);
  // A real setup afterwards still goes to the front.
  slot = Slot{"Cabin", "pw-cabin"};
  CHECK(boot(l, slot));
  CHECK(names(l) == "Cabin,iPhone,Home");
}

static void test_forget_survives_a_restart() {
  // Home first, then set up on a phone hotspot at a bar: the slot holds the
  // hotspot. Back home the panel connects to Home again.
  NetworkList l;
  clear(l);
  Slot slot{"Home", "pw-home"};
  boot(l, slot);
  slot = Slot{"iPhone", "pw-phone"};
  boot(l, slot);
  put_front(l, "Home", "pw-home");
  CHECK(names(l) == "Home,iPhone");
  // Forget the hotspot, the slot's network, and restart: it stays forgotten,
  // and the slot now holds Home for firmware rolled back to one network.
  CHECK(forget_net(l, slot, "iPhone"));
  CHECK(names(l) == "Home");
  CHECK(slot.ssid == "Home");
  CHECK(slot.password == "pw-home");
  CHECK(!boot(l, slot));
  CHECK(names(l) == "Home");
  CHECK(find(l, "iPhone") == -1);

  // A setup made since boot is in the slot but not yet hashed: the panel
  // was set up on a friend's network and connected, then it is forgotten
  // before any restart. It must not come back from the slot either.
  slot = Slot{"Friend", "pw-friend"};
  put_front(l, "Friend", "pw-friend");  // wifi: on_connect
  CHECK(names(l) == "Friend,Home");
  CHECK(forget_net(l, slot, "Friend"));
  CHECK(slot.ssid == "Home");
  CHECK(!boot(l, slot));
  CHECK(names(l) == "Home");

  // Forgetting one that is not the slot's leaves the slot on the front one.
  put_front(l, "Office", "pw-office");
  put_front(l, "Home", "pw-home");
  CHECK(forget_net(l, slot, "Office"));
  CHECK(slot.ssid == "Home");
  CHECK(!boot(l, slot));
  CHECK(names(l) == "Home");

  // Not saved: nothing changes.
  Slot before = slot;
  CHECK(!forget_net(l, slot, "nope"));
  CHECK(slot.ssid == before.ssid);
}

static void test_forget_the_last_network() {
  NetworkList l;
  clear(l);
  Slot slot{"Home", "pw-home"};
  boot(l, slot);
  // The list empties but the slot keeps Home, so the panel is never left
  // with nothing to join. After a restart the slot is not read back into
  // the list (it has not changed); ESPHome joins it on its own and
  // wifi: on_connect puts it back.
  CHECK(forget_net(l, slot, "Home"));
  CHECK(l.count == 0);
  CHECK(slot.ssid == "Home");
  CHECK(!boot(l, slot));
  CHECK(l.count == 0);
  CHECK(put_front(l, "Home", "pw-home"));
  CHECK(names(l) == "Home");
}

// Forget on the device page or in the app: the panel leaves the network it
// is on only when that is the one forgotten and another is left to join.
static void test_forget_leaves_now() {
  NetworkList l;
  clear(l);
  put_front(l, "iPhone", "pw-phone");
  put_front(l, "Home", "pw-home");
  bool removed = false;
  // On Home, Home forgotten, iPhone left: it moves now.
  forget(l, "Home", &removed);
  CHECK(removed && leaves_now(l, "Home", "Home"));
  // On iPhone, a network it is not on forgotten: it stays.
  clear(l);
  put_front(l, "iPhone", "pw-phone");
  put_front(l, "Home", "pw-home");
  forget(l, "Home", &removed);
  CHECK(!leaves_now(l, "Home", "iPhone"));
  // On nothing (setup mode, or between networks): nothing to leave.
  CHECK(!leaves_now(l, "Home", ""));
  CHECK(!leaves_now(l, "Home", nullptr));
  // On Home, the only one, forgotten: the slot keeps it, so it stays.
  clear(l);
  put_front(l, "Home", "pw-home");
  forget(l, "Home", &removed);
  CHECK(removed && l.count == 0 && !leaves_now(l, "Home", "Home"));
  // A name that only starts like the one it is on is another network.
  clear(l);
  put_front(l, "Home", "pw-home");
  put_front(l, "Home-5G", "pw-5g");
  forget(l, "Home", &removed);
  CHECK(!leaves_now(l, "Home", "Home-5G"));
  // Three saved, on the front one: it leaves, and the next one is tried first.
  clear(l);
  put_front(l, "Friend", "pw-friend");
  put_front(l, "iPhone", "pw-phone");
  put_front(l, "Home", "pw-home");
  const Network *next = forget(l, "Home", &removed);
  CHECK(leaves_now(l, "Home", "Home"));
  CHECK(next != nullptr && strcmp(next->ssid, "iPhone") == 0);
  // On the second one and it is forgotten: it leaves too.
  clear(l);
  put_front(l, "iPhone", "pw-phone");
  put_front(l, "Home", "pw-home");
  forget(l, "iPhone", &removed);
  CHECK(leaves_now(l, "iPhone", "iPhone"));
}

// Ticks every 10 s like gd_setup_window; the first second it restarts at, or 0.
static uint32_t restart_at(bool (*busy)(uint32_t), uint32_t failed_at) {
  SetupWindow w;
  for (uint32_t t = 10; t <= 30 * 60; t += 10)
    if (w.restart_due(t, busy(t), t >= failed_at))
      return t;
  return 0;
}

static void test_setup_window() {
  const uint32_t never = UINT32_MAX;
  // Nobody comes: the 15 minutes run out.
  CHECK(restart_at([](uint32_t) { return false; }, never) == 15 * 60);
  // Someone on Bluetooth or the hotspot the whole time: 10 more minutes at most.
  CHECK(restart_at([](uint32_t) { return true; }, never) == 25 * 60);
  // Still there at 15 minutes, gone at 18: restart at once.
  CHECK(restart_at([](uint32_t t) { return t >= 14 * 60 && t < 18 * 60; }, never) == 18 * 60);
  // Comes and goes without a failed handover: the window stays 15 minutes.
  CHECK(restart_at([](uint32_t t) { return t >= 60 && t < 120; }, never) == 15 * 60);

  // The app connects at 1 min, the handover fails at 2, the owner retries
  // over Bluetooth until 5 and closes the app: a minute later, back on the
  // saved networks.
  CHECK(restart_at([](uint32_t t) { return t >= 60 && t < 300; }, 120) == 360);
  // Back in the app 30 s after leaving: the minute starts over when it leaves again.
  CHECK(restart_at([](uint32_t t) { return (t >= 60 && t < 300) || (t >= 330 && t < 400); }, 120) == 460);
  // The app drops during the handover; the failure 30 s later starts the minute.
  // (A handover under way counts as busy, so idle starts at the failure.)
  CHECK(restart_at([](uint32_t t) { return t >= 60 && t < 200; }, 200) == 260);
  // Never more than 25 minutes, failure or not.
  CHECK(restart_at([](uint32_t) { return true; }, 120) == 25 * 60);
}

// A name that starts another one, or differs from it only in case, is a
// network of its own: a router's 2.4 GHz and 5 GHz names often share a start.
static void test_names_that_share_a_start() {
  NetworkList l;
  clear(l);
  CHECK(put_front(l, "Home-5G", "pw-5g"));
  CHECK(find(l, "Home") == -1);
  CHECK(put_front(l, "Home", "pw-home"));
  CHECK(names(l) == "Home,Home-5G");
  CHECK(find(l, "Home-5G") == 1);
  CHECK(put_front(l, "home", "pw-lower"));
  CHECK(names(l) == "home,Home,Home-5G");
  CHECK(find(l, "Home-5G-2") == -1);
  CHECK(strcmp(l.nets[2].password, "pw-5g") == 0);
  // Forgetting the short name keeps the longer one, and the other way round.
  bool removed = false;
  const Network *slot = forget(l, "Home", &removed);
  CHECK(removed && names(l) == "home,Home-5G");
  CHECK(slot != nullptr && strcmp(slot->ssid, "home") == 0);
  CHECK(remove(l, "Home-5G"));
  CHECK(names(l) == "home");
  CHECK(!remove(l, "hom"));
  // A changed slot under the longer name is that network, not the shorter one.
  clear(l);
  put_front(l, "Home", "pw-home");
  Network pending;
  CHECK(merge_slot(l, "Home-5G", "pw-5g", &pending));
  CHECK(names(l) == "Home-5G,Home");
  CHECK(pending.ssid[0] == '\0');
  CHECK(strcmp(l.nets[1].password, "pw-home") == 0);
}

int main() {
  test_put_front_and_order();
  test_names_that_share_a_start();
  test_cap_at_max();
  test_remove();
  test_blank_and_long_names();
  test_valid_rejects_bad_flash();
  test_slot_hash();
  test_migration_from_one_network();
  test_changed_slot_is_added();
  test_blank_slot_keeps_the_list();
  test_forget_survives_a_restart();
  test_forget_the_last_network();
  test_forget_leaves_now();
  test_setup_window();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
