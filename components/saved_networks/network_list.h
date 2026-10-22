#pragma once

// The panel's list of saved Wi-Fi networks, as it is kept in flash: up to
// MAX_NETWORKS name and password pairs, the one that connected most recently
// first. Plain C++ with no ESPHome in it, so tests/test_networks.cpp can
// build it on a PC.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wifi_list {

static constexpr size_t MAX_NETWORKS = 5;
// IEEE 802.11 limits, plus the terminator; the same sizes ESPHome's own
// SavedWifiSettings uses.
static constexpr size_t SSID_SIZE = 33;
static constexpr size_t PASSWORD_SIZE = 65;

struct Network {
  char ssid[SSID_SIZE];
  char password[PASSWORD_SIZE];
};

struct NetworkList {
  // slot_hash() of what ESPHome's own slot held when the list last took a
  // network from it or wrote one to it; 0 for none. A slot that still
  // hashes the same at boot has nothing new in it.
  uint32_t slot_hash;
  uint8_t count;
  Network nets[MAX_NETWORKS];
};

// FNV-1a over the name, a zero byte and the password. Never 0, which
// stands for "nothing taken from the slot yet".
inline uint32_t slot_hash(const char *ssid, const char *password) {
  uint32_t h = 2166136261UL;
  auto mix = [&h](const char *s) {
    for (; s != nullptr && *s != '\0'; s++) {
      h ^= (uint8_t) *s;
      h *= 16777619UL;
    }
  };
  mix(ssid);
  h *= 16777619UL;  // the zero byte between the two
  mix(password);
  return h == 0 ? 1 : h;
}

// Index of the network named ssid, or -1.
inline int find(const NetworkList &list, const char *ssid) {
  if (ssid == nullptr || ssid[0] == '\0')
    return -1;
  for (size_t i = 0; i < list.count && i < MAX_NETWORKS; i++)
    if (strncmp(list.nets[i].ssid, ssid, SSID_SIZE) == 0)
      return (int) i;
  return -1;
}

// A list read back from flash: the count fits and every name is terminated
// and not blank.
inline bool valid(const NetworkList &list) {
  if (list.count > MAX_NETWORKS)
    return false;
  for (size_t i = 0; i < list.count; i++) {
    const Network &n = list.nets[i];
    if (n.ssid[0] == '\0' || n.ssid[SSID_SIZE - 1] != '\0' || n.password[PASSWORD_SIZE - 1] != '\0')
      return false;
  }
  return true;
}

inline void clear(NetworkList &list) { memset(&list, 0, sizeof(list)); }

// Puts a network at the front, where it is tried first. A network already in
// the list moves up and takes the password given here (a router's password
// may have changed since). A new one pushes the rest down, and when the list
// is full the last one drops off. A blank name is ignored. Returns true when
// the list changed.
inline bool put_front(NetworkList &list, const char *ssid, const char *password) {
  if (ssid == nullptr || ssid[0] == '\0' || strlen(ssid) >= SSID_SIZE)
    return false;
  if (password == nullptr)
    password = "";
  if (strlen(password) >= PASSWORD_SIZE)
    return false;
  const int at = find(list, ssid);
  if (at == 0 && strncmp(list.nets[0].password, password, PASSWORD_SIZE) == 0)
    return false;
  Network entry{};
  strncpy(entry.ssid, ssid, SSID_SIZE - 1);
  strncpy(entry.password, password, PASSWORD_SIZE - 1);
  // Close the gap the moving entry leaves, or make room at the front.
  size_t last = at >= 0 ? (size_t) at : (list.count < MAX_NETWORKS ? list.count : MAX_NETWORKS - 1);
  for (size_t i = last; i > 0; i--)
    list.nets[i] = list.nets[i - 1];
  list.nets[0] = entry;
  if (at < 0 && list.count < MAX_NETWORKS)
    list.count++;
  return true;
}

// Takes a network out. Returns true when it was there.
inline bool remove(NetworkList &list, const char *ssid) {
  const int at = find(list, ssid);
  if (at < 0)
    return false;
  for (size_t i = (size_t) at; i + 1 < list.count; i++)
    list.nets[i] = list.nets[i + 1];
  list.count--;
  memset(&list.nets[list.count], 0, sizeof(Network));
  return true;
}

// What the list does at boot with ESPHome's one slot, which holds the last
// network set up through the app or the hotspot page. A blank name is what
// "Reset Wi-Fi" wrote in older firmware, and empties the list. A slot that
// hashes as it did when the list last looked is left alone, so a network
// forgotten since stays forgotten. A changed slot is a new setup, or a
// panel updating from firmware that kept one network: that network goes to
// the front, unless the list already has it with the same password, which
// keeps the order.
// Returns true when the list changed and needs saving.
inline bool merge_slot(NetworkList &list, const char *ssid, const char *password) {
  if (ssid == nullptr || ssid[0] == '\0') {
    if (list.count == 0 && list.slot_hash == 0)
      return false;
    clear(list);
    return true;
  }
  if (password == nullptr)
    password = "";
  const uint32_t h = slot_hash(ssid, password);
  if (h == list.slot_hash)
    return false;
  list.slot_hash = h;
  const int at = find(list, ssid);
  if (at < 0 || strncmp(list.nets[at].password, password, PASSWORD_SIZE) != 0)
    put_front(list, ssid, password);
  return true;
}

// Forgets a network for good. Returns the network ESPHome's slot should hold
// now, the new front one, with slot_hash already matching it; the caller
// writes it to the slot. Rewriting the slot after every forget, not only
// when the forgotten network looks like the slot's, also covers a setup made
// since boot that the list has not seen in the slot yet, and leaves firmware
// rolled back to one network with one that was not forgotten.
// Returns nullptr when nothing was removed, and when the list is now empty:
// the slot then keeps its network, so the panel always has one to join and
// adds it back once it connects (Factory Reset is what clears everything).
// *removed says whether the network was there.
inline const Network *forget(NetworkList &list, const char *ssid, bool *removed) {
  *removed = remove(list, ssid);
  if (!*removed || list.count == 0)
    return nullptr;
  list.slot_hash = slot_hash(list.nets[0].ssid, list.nets[0].password);
  return &list.nets[0];
}

}  // namespace wifi_list
