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
  uint8_t count;
  Network nets[MAX_NETWORKS];
};

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

}  // namespace wifi_list
