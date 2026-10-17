// Host tests for the saved Wi-Fi network list (components/saved_networks/network_list.h).
// Build with `make` in tests/.
#include <cstdio>
#include <cstring>
#include <string>

#include "network_list.h"

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
static bool boot(NetworkList &l, const Slot &slot) { return merge_slot(l, slot.ssid.c_str(), slot.password.c_str()); }

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
  // A new password for a known network is a fresh setup: it goes first with it.
  slot = Slot{"Home", "pw-new"};
  CHECK(boot(l, slot));
  CHECK(names(l) == "Home,iPhone");
  CHECK(strcmp(l.nets[0].password, "pw-new") == 0);
  slot = Slot{"iPhone", "pw-phone2"};
  CHECK(boot(l, slot));
  CHECK(names(l) == "iPhone,Home");
  CHECK(strcmp(l.nets[0].password, "pw-phone2") == 0);
}

static void test_reset_empties_the_list() {
  NetworkList l;
  clear(l);
  Slot slot{"Home", "pw-home"};
  boot(l, slot);
  put_front(l, "iPhone", "pw-phone");
  // Reset Wi-Fi in older firmware blanked the slot. Even a list that firmware
  // did not clear itself goes, so a reset stays a reset.
  slot = Slot{"", ""};
  CHECK(boot(l, slot));
  CHECK(l.count == 0);
  CHECK(l.slot_hash == 0);
  CHECK(!boot(l, slot));
  CHECK(!merge_slot(l, nullptr, nullptr));
  // Set up again after the reset.
  slot = Slot{"Home", "pw-home"};
  CHECK(boot(l, slot));
  CHECK(names(l) == "Home");
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

int main() {
  test_put_front_and_order();
  test_cap_at_max();
  test_remove();
  test_blank_and_long_names();
  test_valid_rejects_bad_flash();
  test_slot_hash();
  test_migration_from_one_network();
  test_changed_slot_is_added();
  test_reset_empties_the_list();
  test_forget_survives_a_restart();
  test_forget_the_last_network();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
