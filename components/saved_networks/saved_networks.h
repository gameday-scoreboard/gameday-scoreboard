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
// Within a boot the list is only saved, never re-applied: a setup from the
// app narrows the live list to the new network the way ESPHome always did,
// and the one-minute offline restart in gameday-common.yaml brings the full
// list back. Forgetting a network takes it out of flash at once; the live
// session keeps it until the next restart.

#include <mutex>
#include <string>
#include <vector>

#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include "network_list.h"

namespace esphome {
namespace saved_networks {

using wifi_list::MAX_NETWORKS;
using wifi_list::NetworkList;

// One row of the list as the API and the device page see it: never a password.
struct Entry {
  std::string ssid;
  bool connected;  // the network the panel is on right now
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
  // Takes one network out of the saved list. Returns false when it was not
  // there. Safe from the HTTP task; the flash write runs on the main loop.
  bool forget(const std::string &ssid);
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

 protected:
  void apply_();  // hands the list to the Wi-Fi component (setup only)
  void save_();   // main loop only

  NetworkList list_{};
  std::mutex mutex_;
  ESPPreferenceObject pref_;
  ESPPreferenceObject setup_pref_;
  bool in_setup_{false};
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern SavedNetworks *global_saved_networks;

}  // namespace saved_networks
}  // namespace esphome
