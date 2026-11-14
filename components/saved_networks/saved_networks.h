// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

// Keeps more than one Wi-Fi network for the panel. ESPHome itself remembers
// one: the hotspot page and the app's Bluetooth setup each write one slot
// (SavedWifiSettings), and at boot WiFiComponent::start() hands that slot to
// set_sta(), which replaces the whole list. So a panel set up on a phone
// hotspot at a bar forgot its home network.
//
// This component keeps its own list in flash, up to MAX_NETWORKS name and
// password pairs with the one that connected most recently first, and hands
// the whole list to the Wi-Fi component right after it starts, before its
// first scan finishes. ESPHome then tries them like a networks: list from the
// YAML: the first one gets priority 1 and the rest 0, so the last network
// that worked is tried first and signal strength decides among the others.
//
// ESPHome's own slot is still what setup writes. It is read once at boot,
// and the list keeps a hash of what it last took from it: a slot that
// changed since goes to the front (a panel updating from firmware that kept
// one network, or a setup that restarted before it connected), and a blank
// slot, which "Reset Wi-Fi" wrote in older firmware, empties the list too.
// Forgetting a network rewrites the slot to the new front one, so nothing
// forgotten comes back from it and firmware rolled back to one network still
// has one. Forgetting the last one leaves the slot alone (wifi_list::forget()).
//
// "Reset Wi-Fi" keeps the list and the slot. It saves a flag instead, and the
// next boot reads and clears it and starts in setup mode with a blank network
// in memory, the state the rest of the firmware already reads as "reset". So
// that one boot is in setup and any later one, after an unplug or after the
// 15 minutes in gameday-common.yaml run out, is back on the saved networks.
//
// A new password for a saved network, from a setup that restarted before it
// connected, is tried first that boot but not saved over the one that
// worked: after PENDING_MS with no connection the panel goes back to the
// saved password (wifi_list::merge_slot()).
//
// A setup from the app narrows the live list to the new network the way
// ESPHome always did; a failed one hands the whole list back (reapply(),
// from improv_ble). Forgetting a network takes it out of flash and out of
// the live list at once. Forgetting the one the panel is on, with another
// saved network left, also takes it off that one a second later (time for
// the HTTP reply to go out), and ESPHome scans and joins the rest the way it
// does after any drop. ESP-IDF reports that as ASSOC_LEAVE, which the Wi-Fi
// health doesn't count as a drop (wifi_history.h).

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include "network_list.h"
#include "setup_window.h"

namespace esphome {
namespace saved_networks {

using wifi_list::MAX_NETWORKS;
using wifi_list::NetworkList;
using wifi_list::SetupWindow;

// One row of the list as the API and the device page see it: never a password.
struct Entry {
  std::string ssid;
  bool connected;  // the network the panel is on right now
};

// What forget() did.
enum class Forgot : uint8_t {
  NOT_SAVED,  // the network was not in the list; nothing changed
  STAYS,      // taken off the list; the panel stays where it is
  LEAVES,     // taken off the list, and the panel leaves it for the rest
};

class SavedNetworks : public Component {
 public:
  void setup() override;
  void dump_config() override;
  // Right after the Wi-Fi component (setup_priority::WIFI), which loads
  // ESPHome's one slot and starts its first scan in start(). Replacing the
  // list here, before that scan finishes, is what makes every saved network
  // a candidate on the first scan.
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

  // Called from wifi: on_connect with the network the panel just joined. It
  // moves to the front and the list is saved when anything changed.
  void note_connected(const wifi::WiFiAP &ap);
  // Takes one network out of the saved list, and the panel off it when it
  // is on it and another is left (wifi_list::leaves_now()). Safe from the
  // HTTP task; the flash write and the leaving run on the main loop.
  Forgot forget(const std::string &ssid);
  // "Reset Wi-Fi": the next boot starts in setup mode. Call it right before
  // restarting; the flag is in flash when this returns.
  void start_setup();
  // This boot started in setup mode after "Reset Wi-Fi" and has networks to
  // go back to. Only false for a panel that never had one.
  bool in_setup() const { return this->in_setup_; }
  // The saved networks in the order they are tried. Safe from the HTTP task.
  std::vector<Entry> list();
  size_t count();
  static constexpr size_t max() { return MAX_NETWORKS; }
  // Hands the whole saved list to the Wi-Fi component again, after something
  // narrowed it to one network. Main loop only.
  void reapply();

  // How long a new password for a saved network gets before the panel goes
  // back to the one that worked.
  static constexpr uint32_t PENDING_MS = 60000;

 protected:
  void apply_();  // hands the list to the Wi-Fi component; main loop only
  void save_();   // main loop only
  void pending_timeout_();
  void leave_(const std::string &ssid);  // main loop only
  // The network the panel is on, read from the driver; blank when on none.
  // Safe from the HTTP task.
  std::array<char, wifi::SSID_BUFFER_SIZE> connected_ssid_() const;

  NetworkList list_{};
  // A new password for a saved network, tried first this boot and stored
  // only once it connects. ssid[0] is 0 when there is none.
  wifi_list::Network pending_{};
  std::mutex mutex_;
  ESPPreferenceObject pref_;
  ESPPreferenceObject setup_pref_;
  bool in_setup_{false};
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern SavedNetworks *global_saved_networks;

}  // namespace saved_networks
}  // namespace esphome
