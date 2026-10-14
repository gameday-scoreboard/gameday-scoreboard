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

int main() {
  test_put_front_and_order();
  test_cap_at_max();
  test_remove();
  test_blank_and_long_names();
  test_valid_rejects_bad_flash();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
