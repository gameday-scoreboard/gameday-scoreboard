// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#include "saved_networks.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_IMPROV_BLE
#include "esphome/components/improv_ble/improv_ble_component.h"
#endif
#ifdef USE_ESP32
#include <esp_wifi.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>

namespace esphome {
namespace saved_networks {

static const char *const TAG = "saved_networks";
// What start_setup() saves. Any other value, or none, is a normal boot.
static constexpr uint32_t SETUP_PENDING = 0x53455455;

SavedNetworks *global_saved_networks = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// The names only, for the log and dump_config. The device page streams the
// log to anyone on the network, so passwords never go through here.
// Five 32-character names, four ", " between them and the terminator.
static constexpr size_t NAMES_SIZE = MAX_NETWORKS * (wifi_list::SSID_SIZE + 1);
static void names_to(const NetworkList &list, char *buf, size_t size) {
  size_t n = 0;
  buf[0] = '\0';
  for (size_t i = 0; i < list.count && n < size; i++)
    n += snprintf(buf + n, size - n, "%s%s", i == 0 ? "" : ", ", list.nets[i].ssid);
}

void SavedNetworks::setup() {
  global_saved_networks = this;
  // v2 added slot_hash. v1 only ever ran on bench panels before release; a
  // panel coming from it starts over from ESPHome's slot like one from 1.5.5.
  this->pref_ = global_preferences->make_preference<NetworkList>(fnv1_hash("gameday_wifi_list_v2"), true);
  if (!this->pref_.load(&this->list_) || !wifi_list::valid(this->list_))
    wifi_list::clear(this->list_);
  // Set by "Reset Wi-Fi" (start_setup()) and cleared here, so only the first
  // boot after the reset is in setup mode.
  this->setup_pref_ = global_preferences->make_preference<uint32_t>(fnv1_hash("gameday_wifi_setup_v1"), true);
  uint32_t setup = 0;
  const bool reset = this->setup_pref_.load(&setup) && setup == SETUP_PENDING;
  if (reset) {
    setup = 0;
    this->setup_pref_.save(&setup);
    global_preferences->sync();
  }

  auto *w = wifi::global_wifi_component;
  if (w == nullptr)
    return;
  // Every scan, the Wi-Fi component's own and scan()'s, says which saved
  // networks are in range.
  w->add_scan_results_listener(this);
#ifdef USE_IMPROV_BLE
  // A failed setup from the app hands the whole list back, not just the
  // network it was on.
  if (improv_ble::global_improv_component != nullptr)
    improv_ble::global_improv_component->set_restore_networks([this]() { this->reapply(); });
#endif
  if (w->has_sta()) {
    // ESPHome's own slot, loaded by WiFiComponent::start() a moment ago with
    // set_sta(), which leaves it selected. On a panel updating from firmware
    // that kept one network this is where its home network comes from.
    const wifi::WiFiAP slot = w->get_sta();
    const std::string ssid = slot.get_ssid().str();
    if (wifi_list::merge_slot(this->list_, ssid.c_str(), slot.get_password().c_str(), &this->pending_)) {
      if (this->pending_.ssid[0] != '\0') {
        ESP_LOGI(TAG, "New password for %s since the last boot: trying it first, the saved one stays until it connects",
                 ssid.c_str());
      } else {
        ESP_LOGI(TAG, "Network set up since the last boot: %s", ssid.c_str());
      }
      this->save_();
    } else if (ssid.empty() && this->list_.count > 0) {
      ESP_LOGI(TAG, "ESPHome's network slot is blank; keeping %u saved network(s)", this->list_.count);
    }
  }
  // A blank network that still counts as has_sta() is what the on_boot
  // checks in gameday-common.yaml and the setup screen read as "reset": the
  // hotspot and Bluetooth setup come up at once and nothing is joined. The
  // saved networks stay in flash for the next boot. A panel with none to go
  // back to is just a panel in setup, as before.
  if (reset && (this->list_.count > 0 || !w->get_sta().get_ssid().empty())) {
    this->in_setup_ = true;
    this->pending_ = {};
    w->set_sta(setup_network());
#ifdef USE_IMPROV_BLE
    // A wrong name or password from the app keeps the panel in setup for
    // another try, and the advert says it has networks, so the app lists it
    // as a panel that lost its Wi-Fi rather than a new one.
    if (improv_ble::global_improv_component != nullptr) {
      improv_ble::global_improv_component->set_setup_mode(true);
      improv_ble::global_improv_component->set_has_saved_network(true);
    }
#endif
    ESP_LOGI(TAG, "Wi-Fi was reset: in setup mode, %u saved network(s) kept for the next boot", this->list_.count);
    return;
  }
  // A blank slot from older firmware's "Reset Wi-Fi" with nothing saved:
  // that panel is in setup too, so it gets the setup network, which joins
  // nothing by itself (setup_network.h).
  if (this->list_.count == 0 && w->has_sta() && w->get_sta().get_ssid().empty())
    w->set_sta(setup_network());
  this->apply_();
  if (this->pending_.ssid[0] != '\0')
    this->set_timeout("pending", PENDING_MS, [this]() { this->pending_timeout_(); });
}

void SavedNetworks::reapply() {
  // In setup mode the blank network is what keeps the panel in setup.
  if (!this->in_setup_)
    this->apply_();
}

// The new password has had its minute and the panel is not on it: back to
// the one that worked. Not in the middle of a setup from the app, which owns
// the live list until it ends.
void SavedNetworks::pending_timeout_() {
  if (this->pending_.ssid[0] == '\0')
    return;
#ifdef USE_IMPROV_BLE
  if (improv_ble::global_improv_component != nullptr &&
      improv_ble::global_improv_component->get_improv_state() == improv::STATE_PROVISIONING) {
    this->set_timeout("pending", 10000, [this]() { this->pending_timeout_(); });
    return;
  }
#endif
  ESP_LOGW(TAG, "The new password for %s did not connect; back to the saved one", this->pending_.ssid);
  this->pending_ = {};
  this->reapply();
}

void SavedNetworks::apply_() {
  auto *w = wifi::global_wifi_component;
  // Copied out under the lock: the HTTP task may change the list meanwhile.
  NetworkList list{};
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    list = this->list_;
  }
  // Nothing saved: leave ESPHome's state alone, so a fresh panel and one
  // after "Reset Wi-Fi" (a blank network that still counts as has_sta())
  // enter setup mode exactly as before.
  if (w == nullptr || list.count == 0)
    return;
  // The order they are tried in: a pending new password first, in place of
  // the saved entry for that network (ESPHome joins a scanned network with
  // the first entry that matches it, so two entries for one name would never
  // reach the second), then the rest.
  std::array<wifi_list::Network, MAX_NETWORKS> order{};
  size_t n = 0;
  const bool pending = this->pending_.ssid[0] != '\0';
  if (pending)
    order[n++] = this->pending_;
  for (size_t i = 0; i < list.count && n < order.size(); i++) {
    if (!pending || strncmp(list.nets[i].ssid, this->pending_.ssid, wifi_list::SSID_SIZE) != 0)
      order[n++] = list.nets[i];
  }
  auto ap_at = [&order](size_t i) {
    wifi::WiFiAP ap{};
    ap.set_ssid(order[i].ssid);
    ap.set_password(order[i].password);
    // The last network that worked outranks the rest; among those the scan
    // sort falls through to signal strength. ESPHome lowers a priority by
    // one per failed attempt, so a front network that is not answering
    // gives way after one try.
    ap.set_priority(i == 0 ? 1 : 0);
    return ap;
  };
  // set_sta() first, for the selected index: it leaves the front network
  // selected, as start() left the slot's. clear_sta() alone leaves none,
  // and get_sta() would read blank until the first scan finishes, which the
  // on_boot checks in gameday-common.yaml take for a panel after Reset
  // Wi-Fi. init_sta() then makes room for the whole list and keeps the
  // index, and the front network goes back in at 0.
  w->set_sta(ap_at(0));
  w->init_sta(n);
  for (size_t i = 0; i < n; i++)
    w->add_sta(ap_at(i));
  char names[NAMES_SIZE];
  names_to(list, names, sizeof(names));
  ESP_LOGI(TAG, "Saved networks (%u), in order: %s", list.count, names);
#ifdef USE_IMPROV_BLE
  // The network a failed setup from the app goes back to, until the panel
  // connects to something this boot. Without it a wrong password typed
  // somewhere the front network is not would leave the panel with none.
  if (improv_ble::global_improv_component != nullptr) {
    wifi::WiFiAP front{};
    front.set_ssid(list.nets[0].ssid);
    front.set_password(list.nets[0].password);
    improv_ble::global_improv_component->remember_sta(front);
  }
#endif
}

void SavedNetworks::note_connected(const wifi::WiFiAP &ap) {
  const std::string ssid = ap.get_ssid().str();
  if (ssid.empty())
    return;
  // switch_to() worked. It narrowed the live list to this network, so the
  // whole list goes back below, with this one now at the front.
  bool switched = false;
  if (this->switching_) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    switched = ssid == this->switch_to_.ssid;
    if (switched)
      this->switch_to_ = {};
  }
  if (switched) {
    this->switching_ = false;
    this->cancel_timeout("switch");
    ESP_LOGI(TAG, "Switched to %s", ssid.c_str());
  }
  // A join that landed just after the fallback gave up on it did work.
  if (this->switch_failed_ms_ != 0) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (ssid == this->switch_failed_.ssid) {
      this->switch_failed_ = {};
      this->switch_failed_ms_ = 0;
    }
  }
  // The pending new password worked, so put_front() below stores it.
  if (this->pending_.ssid[0] != '\0' && ssid == this->pending_.ssid &&
      strcmp(ap.get_password().c_str(), this->pending_.password) == 0) {
    this->pending_ = {};
    this->cancel_timeout("pending");
  }
  bool changed;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    changed = wifi_list::put_front(this->list_, ssid.c_str(), ap.get_password().c_str());
  }
  if (changed) {
    ESP_LOGI(TAG, "%s is now the first network to try", ssid.c_str());
    this->save_();
  }
  if (switched)
    this->apply_();
}

static bool app_provisioning() {
#ifdef USE_IMPROV_BLE
  return improv_ble::global_improv_component != nullptr &&
         improv_ble::global_improv_component->get_improv_state() == improv::STATE_PROVISIONING;
#else
  return false;
#endif
}

AddResult SavedNetworks::add(const std::string &ssid, const std::string &password) {
  // Its password is the one that works.
  if (ssid == this->connected_ssid_().data())
    return AddResult::ON_IT;
  wifi_list::Added added;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    added = wifi_list::add_second(this->list_, ssid.c_str(), password.c_str());
  }
  if (added == wifi_list::Added::NONE)
    return AddResult::BAD;
  ESP_LOGI(TAG, "%s %s", added == wifi_list::Added::NEW ? "Added" : "New password for", ssid.c_str());
  this->defer([this, ssid]() {
    // The password typed in now replaces one from a setup that has not
    // connected yet.
    if (ssid == this->pending_.ssid) {
      this->pending_ = {};
      this->cancel_timeout("pending");
    }
    this->save_();
    // Into the live list, so a drop from this network can land on it. Not
    // while a switch or a setup from the app has the live list narrowed.
    if (!this->switching_ && !app_provisioning())
      this->reapply();
  });
  this->scan();
  return added == wifi_list::Added::NEW ? AddResult::ADDED : AddResult::UPDATED;
}

void SavedNetworks::scan() {
  this->defer([this]() {
    auto *w = wifi::global_wifi_component;
    if (w == nullptr || !w->is_connected() || this->switching_ || this->scanning_)
      return;
    // In the driver, as improv_ble does, rather than with the Wi-Fi
    // component's start_scanning(), which would take the panel off its
    // network. The component stores the results and hands them to
    // on_wifi_scan_results(). Same settings as its own scans.
    // Once the panel is connected, ESPHome keeps only the saved networks
    // from a scan unless asked for all of them. Asked for this scan only,
    // so its own scans go on dropping (and freeing) the rest.
    w->set_keep_scan_results(true);
#ifdef USE_ESP32
    wifi_scan_config_t config{};
    config.show_hidden = false;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.min = 100;
    config.scan_time.active.max = 300;
    const esp_err_t err = esp_wifi_scan_start(&config, false);
    // Usually a scan of the Wi-Fi component's own is running; its results
    // come here too.
    if (err != ESP_OK)
      ESP_LOGD(TAG, "Scan not started (%s); waiting for the running one", esp_err_to_name(err));
#endif
    this->scanning_ = true;
    // The component tells no listener about a scan that found nothing.
    this->set_timeout("scan", 10000, [this]() {
      {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->scanned_n_ = 0;
        this->scanned_ms_ = std::max<uint32_t>(millis(), 1);
      }
      this->end_scan_();
    });
  });
}

void SavedNetworks::on_wifi_scan_results(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) {
  // One entry per name: a mesh or a building with several access points
  // lists the same network once per access point. Filled in place, with no
  // allocation: this runs on every scan, ESPHome's own included. Once the
  // list is full a stronger name takes the weakest one's place.
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->fill_scanned_(results);
  }
  if (this->scanning_) {
    this->cancel_timeout("scan");
    this->end_scan_();
  }
}

void SavedNetworks::fill_scanned_(const wifi::wifi_scan_vector_t<wifi::WiFiScanResult> &results) {
  size_t n = 0;
  for (const auto &r : results) {
    if (r.get_is_hidden() || r.get_ssid().empty())
      continue;
    char name[wifi_list::SSID_SIZE];
    const auto ssid = r.get_ssid();
    const size_t len = std::min(ssid.size(), wifi_list::SSID_SIZE - 1);
    memcpy(name, ssid.c_str(), len);
    name[len] = '\0';
    const int8_t rssi = r.get_rssi();
    Nearby *same = nullptr;
    for (size_t i = 0; i < n && same == nullptr; i++)
      if (strncmp(this->scanned_[i].ssid, name, wifi_list::SSID_SIZE) == 0)
        same = &this->scanned_[i];
    if (same != nullptr) {
      same->rssi = std::max(same->rssi, rssi);
      continue;
    }
    Nearby *slot = nullptr;
    if (n < MAX_SCANNED) {
      slot = &this->scanned_[n++];
    } else {
      Nearby *weakest = &this->scanned_[0];
      for (size_t i = 1; i < n; i++)
        if (this->scanned_[i].rssi < weakest->rssi)
          weakest = &this->scanned_[i];
      if (rssi <= weakest->rssi)
        continue;
      slot = weakest;
    }
    memcpy(slot->ssid, name, len + 1);
    slot->rssi = rssi;
    slot->open = !r.get_with_auth();
  }
  // Strongest first; an insertion sort keeps equal signals in scan order.
  for (size_t i = 1; i < n; i++) {
    const Nearby cur = this->scanned_[i];
    size_t j = i;
    for (; j > 0 && this->scanned_[j - 1].rssi < cur.rssi; j--)
      this->scanned_[j] = this->scanned_[j - 1];
    this->scanned_[j] = cur;
  }
  this->scanned_n_ = n;
  this->scanned_ms_ = std::max<uint32_t>(millis(), 1);
}

void SavedNetworks::end_scan_() {
  if (wifi::global_wifi_component != nullptr)
    wifi::global_wifi_component->set_keep_scan_results(false);
  this->scanning_ = false;
}

bool SavedNetworks::switch_to(const std::string &ssid) {
  auto *w = wifi::global_wifi_component;
  if (w == nullptr || !w->is_connected() || this->in_setup_ || this->switching_ ||
      ssid == this->connected_ssid_().data() ||
      app_provisioning())
    return false;
  wifi_list::Network target{};
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    const int at = wifi_list::find(this->list_, ssid.c_str());
    if (at < 0)
      return false;
    target = this->list_.nets[at];
    this->switch_to_ = target;
    this->switch_failed_ = {};
  }
  this->switch_failed_ms_ = 0;
  this->switch_grace_ = false;
  this->switching_ = true;
  // A moment first, so the answer to the request that asked for this is out
  // before the panel leaves the network it came in on.
  this->set_timeout("switch-start", 500, [this, target]() {
    if (!this->switching_)
      return;
    ESP_LOGI(TAG, "Switching to %s; back to the saved networks if it has not connected in %u s", target.ssid,
             (unsigned) (SWITCH_MS / 1000));
    wifi::WiFiAP ap{};
    ap.set_ssid(target.ssid);
    ap.set_password(target.password);
    // The live list narrows to this network, as a setup from the app does,
    // so ESPHome tries nothing else meanwhile. Then leave the network the
    // panel is on, and ESPHome's own reconnect joins this one. Not
    // start_connecting() while still connected: it leaves the old network
    // itself, and that disconnect arrives after it has cleared its error
    // flag, so ESPHome takes the new attempt for a failed one and tears it
    // down (seen on the bench: on the new network with an address, never
    // counted as connected).
    wifi::global_wifi_component->set_sta(ap);
#ifdef USE_ESP32
    esp_wifi_disconnect();
#endif
    this->set_timeout("switch", SWITCH_MS, [this]() { this->switch_timeout_(); });
  });
  return true;
}

// The network switch_to() was given has not connected: back to the whole
// list, starting with the network the panel was on, still at the front.
// Left the same way switch_to() leaves: hand ESPHome the list and drop the
// link, and its reconnect picks the front network. Not start_connecting():
// a join that finished at the deadline is a live link, and that call while
// one is up reset DHCP on it and left the panel associated but unreachable,
// with ESPHome never noticing the drop (bench, 2026-11-15).
void SavedNetworks::switch_timeout_() {
  if (!this->switching_)
    return;
  wifi_list::Network target{};
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    target = this->switch_to_;
  }
#ifdef USE_ESP32
  // Already associated with it, still getting an address: once, a little
  // longer, rather than pulling it off a network that is working.
  wifi_ap_record_t ap{};
  if (!this->switch_grace_ && esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
      strncmp(reinterpret_cast<const char *>(ap.ssid), target.ssid, wifi_list::SSID_SIZE) == 0) {
    this->switch_grace_ = true;
    ESP_LOGI(TAG, "On %s but not connected yet; %u s more", target.ssid, (unsigned) (SWITCH_GRACE_MS / 1000));
    this->set_timeout("switch", SWITCH_GRACE_MS, [this]() { this->switch_timeout_(); });
    return;
  }
#endif
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->switch_failed_ = target;
    this->switch_to_ = {};
  }
  this->switch_failed_ms_ = std::max<uint32_t>(millis(), 1);
  this->switching_ = false;
  ESP_LOGW(TAG, "%s did not connect; back to the saved networks", target.ssid);
  this->apply_();
#ifdef USE_ESP32
  esp_wifi_disconnect();
#endif
}

std::string SavedNetworks::switching_ssid() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->switching_ ? std::string(this->switch_to_.ssid) : std::string();
}

std::string SavedNetworks::switch_failed() {
  const uint32_t at = this->switch_failed_ms_;
  if (at == 0 || millis() - at >= FAILED_MS)
    return {};
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->switch_failed_.ssid;
}

Forgot SavedNetworks::forget(const std::string &ssid) {
  const auto now = this->connected_ssid_();
  bool removed;
  bool rewrite_slot;
  bool leaves;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    rewrite_slot = wifi_list::forget(this->list_, ssid.c_str(), &removed) != nullptr;
    leaves = removed && wifi_list::leaves_now(this->list_, ssid.c_str(), now.data());
  }
  if (!removed)
    return Forgot::NOT_SAVED;
  ESP_LOGI(TAG, "Forgot %s%s", ssid.c_str(), leaves ? "; leaving it for the next saved network" : "");
  this->defer([this, rewrite_slot, leaves, ssid]() {
    if (ssid == this->pending_.ssid)
      this->pending_ = {};
    if (rewrite_slot) {
      // ESPHome's slot becomes the front network, so the one just forgotten
      // is not read back from it at the next boot. Copied out under the lock
      // because the HTTP task may change the list meanwhile.
      wifi_list::Network front{};
      {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->list_.count > 0) {
          front = this->list_.nets[0];
          this->list_.slot_hash = wifi_list::slot_hash(front.ssid, front.password);
        }
      }
      auto *w = wifi::global_wifi_component;
      if (front.ssid[0] != '\0' && w != nullptr) {
        // save_wifi_sta() also narrows the live list to that one network.
        // Hand the rest back, or keep setup mode's blank network.
        w->save_wifi_sta(front.ssid, front.password);
        if (this->in_setup_)
          w->set_sta(setup_network());
        else
          this->apply_();
      }
    }
    this->save_();
    // A second for the HTTP reply to reach the page or the app first.
    if (leaves)
      this->set_timeout("leave", 1000, [this, ssid]() { this->leave_(ssid); });
  });
  return leaves ? Forgot::LEAVES : Forgot::STAYS;
}

// The live list no longer has the forgotten network, so once the panel is
// off it ESPHome's reconnect scans and joins one that is left: the front
// one if it can see it (priority 1 in apply_()), else the strongest. Checked
// again here: the panel may have moved on its own in the second since.
void SavedNetworks::leave_(const std::string &ssid) {
  if (wifi::global_wifi_component == nullptr || this->in_setup_ || this->count() == 0)
    return;
  const auto now = this->connected_ssid_();
  if (ssid != now.data())
    return;
  ESP_LOGI(TAG, "Leaving %s for the next saved network", ssid.c_str());
#ifdef USE_ESP32
  esp_wifi_disconnect();
#endif
}

std::array<char, wifi::SSID_BUFFER_SIZE> SavedNetworks::connected_ssid_() const {
  std::array<char, wifi::SSID_BUFFER_SIZE> now{};
  auto *w = wifi::global_wifi_component;
  // Read from the driver, not from get_sta(): that one is blank while
  // ESPHome rescans, and this runs on the HTTP task too.
  if (w != nullptr && w->is_connected())
    w->wifi_ssid_to(now);
  return now;
}

void SavedNetworks::start_setup() {
  uint32_t setup = SETUP_PENDING;
  this->setup_pref_.save(&setup);
  global_preferences->sync();
  ESP_LOGI(TAG, "Wi-Fi reset: the next boot starts in setup mode");
}

std::vector<Entry> SavedNetworks::list() {
  const auto now = this->connected_ssid_();
  std::vector<Entry> out;
  std::lock_guard<std::mutex> lock(this->mutex_);
  const bool fresh = this->scanned_ms_ != 0 && millis() - this->scanned_ms_ < SEEN_MS;
  out.reserve(this->list_.count);
  for (size_t i = 0; i < this->list_.count; i++) {
    const char *ssid = this->list_.nets[i].ssid;
    const bool seen = fresh && std::any_of(this->scanned_.begin(), this->scanned_.begin() + this->scanned_n_,
                                           [&](const Nearby &n) {
                                             return strncmp(n.ssid, ssid, wifi_list::SSID_SIZE) == 0;
                                           });
    out.push_back(Entry{ssid, now[0] != '\0' && strncmp(ssid, now.data(), now.size()) == 0, seen});
  }
  return out;
}


size_t SavedNetworks::count() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->list_.count;
}

void SavedNetworks::save_() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  this->pref_.save(&this->list_);
  global_preferences->sync();
}

void SavedNetworks::dump_config() {
  std::lock_guard<std::mutex> lock(this->mutex_);
  char names[NAMES_SIZE];
  names_to(this->list_, names, sizeof(names));
  ESP_LOGCONFIG(TAG, "Saved networks: %u of %u: %s", this->list_.count, (unsigned) MAX_NETWORKS, names);
}

}  // namespace saved_networks
}  // namespace esphome
