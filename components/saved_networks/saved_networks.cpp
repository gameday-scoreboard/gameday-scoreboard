#include "saved_networks.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_IMPROV_BLE
#include "esphome/components/improv_ble/improv_ble_component.h"
#endif

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
  if (w->has_sta()) {
    // ESPHome's own slot, loaded by WiFiComponent::start() a moment ago with
    // set_sta(), which leaves it selected. On a panel updating from firmware
    // that kept one network this is where its home network comes from.
    const wifi::WiFiAP slot = w->get_sta();
    const std::string ssid = slot.get_ssid().str();
    const uint8_t before = this->list_.count;
    if (wifi_list::merge_slot(this->list_, ssid.c_str(), slot.get_password().c_str())) {
      if (ssid.empty())
        ESP_LOGI(TAG, "Wi-Fi was reset; forgetting %u saved network(s)", before);
      else
        ESP_LOGI(TAG, "Network set up since the last boot: %s", ssid.c_str());
      this->save_();
    }
  }
  // A blank network that still counts as has_sta() is what the on_boot
  // checks in gameday-common.yaml and the setup screen read as "reset": the
  // hotspot and Bluetooth setup come up at once and nothing is joined. The
  // saved networks stay in flash for the next boot. A panel with none to go
  // back to is just a panel in setup, as before.
  if (reset && (this->list_.count > 0 || !w->get_sta().get_ssid().empty())) {
    this->in_setup_ = true;
    w->set_sta(wifi::WiFiAP{});
    ESP_LOGI(TAG, "Wi-Fi was reset: in setup mode, %u saved network(s) kept for the next boot", this->list_.count);
    return;
  }
  this->apply_();
}

void SavedNetworks::apply_() {
  auto *w = wifi::global_wifi_component;
  // Nothing saved: leave ESPHome's state alone, so a fresh panel and one
  // after "Reset Wi-Fi" (a blank network that still counts as has_sta())
  // enter setup mode exactly as before.
  if (w == nullptr || this->list_.count == 0)
    return;
  auto ap_at = [this](size_t i) {
    wifi::WiFiAP ap{};
    ap.set_ssid(this->list_.nets[i].ssid);
    ap.set_password(this->list_.nets[i].password);
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
  w->init_sta(this->list_.count);
  for (size_t i = 0; i < this->list_.count; i++)
    w->add_sta(ap_at(i));
  char names[NAMES_SIZE];
  names_to(this->list_, names, sizeof(names));
  ESP_LOGI(TAG, "Saved networks (%u), in order: %s", this->list_.count, names);
#ifdef USE_IMPROV_BLE
  // The network a failed setup from the app goes back to, until the panel
  // connects to something this boot. Without it a wrong password typed
  // somewhere the front network is not would leave the panel with none.
  if (improv_ble::global_improv_component != nullptr) {
    wifi::WiFiAP front{};
    front.set_ssid(this->list_.nets[0].ssid);
    front.set_password(this->list_.nets[0].password);
    improv_ble::global_improv_component->remember_sta(front);
  }
#endif
}

void SavedNetworks::note_connected(const wifi::WiFiAP &ap) {
  const std::string ssid = ap.get_ssid().str();
  if (ssid.empty())
    return;
  bool changed;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    changed = wifi_list::put_front(this->list_, ssid.c_str(), ap.get_password().c_str());
  }
  if (!changed)
    return;
  ESP_LOGI(TAG, "%s is now the first network to try", ssid.c_str());
  this->save_();
}

bool SavedNetworks::forget(const std::string &ssid) {
  bool removed;
  bool rewrite_slot;
  {
    std::lock_guard<std::mutex> lock(this->mutex_);
    rewrite_slot = wifi_list::forget(this->list_, ssid.c_str(), &removed) != nullptr;
  }
  if (!removed)
    return false;
  ESP_LOGI(TAG, "Forgot %s", ssid.c_str());
  this->defer([this, rewrite_slot]() {
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
      if (front.ssid[0] != '\0' && wifi::global_wifi_component != nullptr)
        wifi::global_wifi_component->save_wifi_sta(front.ssid, front.password);
    }
    this->save_();
  });
  return true;
}

void SavedNetworks::start_setup() {
  uint32_t setup = SETUP_PENDING;
  this->setup_pref_.save(&setup);
  global_preferences->sync();
  ESP_LOGI(TAG, "Wi-Fi reset: the next boot starts in setup mode");
}

std::vector<Entry> SavedNetworks::list() {
  std::array<char, wifi::SSID_BUFFER_SIZE> now{};
  auto *w = wifi::global_wifi_component;
  // Read from the driver, not from get_sta(): that one is blank while
  // ESPHome rescans, and this runs on the HTTP task.
  if (w != nullptr && w->is_connected())
    w->wifi_ssid_to(now);
  std::vector<Entry> out;
  std::lock_guard<std::mutex> lock(this->mutex_);
  out.reserve(this->list_.count);
  for (size_t i = 0; i < this->list_.count; i++) {
    const char *ssid = this->list_.nets[i].ssid;
    out.push_back(Entry{ssid, now[0] != '\0' && strncmp(ssid, now.data(), now.size()) == 0});
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
