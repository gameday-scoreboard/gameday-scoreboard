// Readouts for the QC screen (firmware/pages/qc.yaml), and the factory flag
// that starts it.
#pragma once

#include <esp_psram.h>
#include <esp_wifi.h>
#include <nvs.h>

#include "esphome/components/wifi/wifi_component.h"

namespace esphome::gameday {

// Physical PSRAM in MB, 0 when none was found. Not the free heap: with
// execute_from_psram the firmware itself takes part of it.
inline uint32_t qc_psram_mb() { return esp_psram_get_size() / (1024 * 1024); }

// The strongest network the last Wi-Fi scan found, in dBm, or 0 while there
// is nothing to show. A connected panel has usually dropped its scan list, so
// it reports the network it is on instead. One with
// no saved network gets a scan started here, the way improv_ble starts one.
// One with a saved network it can't reach is left alone: the Wi-Fi component
// scans for it on every retry, and a scan of our own could cut into a join.
inline int qc_strongest_rssi() {
  auto *w = wifi::global_wifi_component;
  if (w == nullptr || w->is_disabled())
    return 0;
  int best = 0;
  for (const auto &res : w->get_scan_result()) {
    if (best == 0 || res.get_rssi() > best)
      best = res.get_rssi();
  }
  if (best != 0)
    return best;
  if (w->is_connected())
    return w->wifi_rssi();
  if (w->has_sta())
    return 0;
  wifi_scan_config_t config{};
  config.show_hidden = false;
  config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = 100;
  config.scan_time.active.max = 300;
  esp_wifi_scan_start(&config, false);  // fails harmlessly while another scan runs
  return 0;
}

// The factory flag: scripts/factory_flash.py writes it into NVS right after
// the full flash, so the panel's first boot goes straight into QC. Only a
// press during QC clears it. ESPHome keeps its own settings in the
// "esphome" namespace; a panel flashed any other way has no "gameday"
// namespace at all and never sees the flag. Change these names in both
// places or the factory flag stops working.
static const char *const QC_FLAG_NAMESPACE = "gameday";
static const char *const QC_FLAG_KEY = "qc_pending";

inline bool qc_flag_get() {
  nvs_handle_t h;
  if (nvs_open(QC_FLAG_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    return false;
  uint8_t v = 0;
  const bool set = nvs_get_u8(h, QC_FLAG_KEY, &v) == ESP_OK && v != 0;
  nvs_close(h);
  return set;
}

inline void qc_flag_clear() {
  nvs_handle_t h;
  if (nvs_open(QC_FLAG_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
    return;
  if (nvs_erase_key(h, QC_FLAG_KEY) == ESP_OK)
    nvs_commit(h);
  nvs_close(h);
}

}  // namespace esphome::gameday
