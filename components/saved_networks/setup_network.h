// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

// The network a panel in setup keeps in memory: no name, so the on_boot
// checks in gameday-common.yaml, the setup screen and improv_ble read it as
// "reset", and it still counts as has_sta(), so ESPHome doesn't drop to its
// no-network path.
//
// A plain blank WiFiAP is not enough. ESPHome's WiFiScanResult::matches()
// skips the name check for a network with no name, and with no password it
// matches every open network in range, so the panel joined a neighbor's
// guest Wi-Fi a few seconds into setup. The broadcast address as the BSSID
// matches no scan result, since no access point sends from it. ESPHome's
// hidden-network retry still tries it without the BSSID, and ESP-IDF refuses
// a connect with no name (esp_wifi_connect returns ESP_ERR_WIFI_SSID).

#include "esphome/components/wifi/wifi_component.h"

namespace esphome {
namespace saved_networks {

inline wifi::WiFiAP setup_network() {
  wifi::WiFiAP ap{};
  ap.set_bssid(wifi::bssid_t{0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
  return ap;
}

}  // namespace saved_networks
}  // namespace esphome
