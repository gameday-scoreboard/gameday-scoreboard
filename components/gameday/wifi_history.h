// Wi-Fi history for the device page's Diagnostics: the signal once a minute
// for the last 24 hours, and each time the panel lost its network and why.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace espn {

static const uint32_t kWifiMinutes = 24 * 60;  // one signal reading a minute
static const uint32_t kWifiDrops = 64;         // the last drops kept, with their cause
static const uint32_t kWifiStep = 5;           // minutes per point in the JSON (288 for a day)
static const uint32_t kWifiWeakWindow = 30;    // minutes the weak-signal hint looks back
static const uint32_t kWifiWeakMin = 10;       // readings it needs in that window
static const int kWifiWeakDbm = -75;           // "weak" is a median below this

// Why a connection ended, from the ESP-IDF reason code. SIGNAL: the router
// stopped hearing the panel or the panel stopped hearing it. ROUTER: the
// router sent the panel away or restarted. PASSWORD: the router refused the
// panel's sign-in. ROAM: the panel moved to another access point of the same
// network. PANEL: the panel left on purpose (Reset Wi-Fi, a forgotten
// network), which isn't counted.
enum class WifiCause : uint8_t { SIGNAL, ROUTER, PASSWORD, ROAM, OTHER, PANEL };
static const uint8_t kWifiCauses = 5;  // the counted ones, SIGNAL to OTHER

// wifi_err_reason_t values (esp_wifi_types_generic.h, ESP-IDF 5.5)
inline WifiCause wifi_cause(int reason) {
  switch (reason) {
    case 200:  // BEACON_TIMEOUT
    case 201:  // NO_AP_FOUND
    case 205:  // CONNECTION_FAIL
    case 209:  // SA_QUERY_TIMEOUT
    case 212:  // NO_AP_FOUND_IN_RSSI_THRESHOLD
    case 34:   // MISSING_ACKS
    case 39:   // TIMEOUT
      return WifiCause::SIGNAL;
    case 2:    // AUTH_EXPIRE
    case 3:    // AUTH_LEAVE: the router sent a deauth
    case 4:    // DISASSOC_DUE_TO_INACTIVITY
    case 5:    // ASSOC_TOOMANY
    case 6:    // CLASS2_FRAME_FROM_NONAUTH_STA
    case 7:    // CLASS3_FRAME_FROM_NONASSOC_STA
    case 47:   // AP_INITIATED
    case 206:  // AP_TSF_RESET: the router restarted
      return WifiCause::ROUTER;
    case 14:   // MIC_FAILURE
    case 15:   // 4WAY_HANDSHAKE_TIMEOUT
    case 16:   // GROUP_KEY_UPDATE_TIMEOUT
    case 23:   // 802_1X_AUTH_FAILED
    case 202:  // AUTH_FAIL
    case 204:  // HANDSHAKE_TIMEOUT
      return WifiCause::PASSWORD;
    case 12:   // BSS_TRANSITION_DISASSOC
    case 207:  // ROAMING
      return WifiCause::ROAM;
    case 8:    // ASSOC_LEAVE: this panel disconnected
      return WifiCause::PANEL;
    default:
      return WifiCause::OTHER;
  }
}

inline const char *wifi_cause_key(WifiCause c) {
  switch (c) {
    case WifiCause::SIGNAL:
      return "signal";
    case WifiCause::ROUTER:
      return "router";
    case WifiCause::PASSWORD:
      return "password";
    case WifiCause::ROAM:
      return "roam";
    case WifiCause::PANEL:
      return "panel";
    default:
      return "other";
  }
}

struct WifiDrop {
  uint32_t at_s;    // seconds since start when the connection ended
  uint32_t down_s;  // seconds until it came back, UINT32_MAX while it hasn't
  uint8_t cause;    // WifiCause
};

// The buffers come from outside (PSRAM on the panel): kWifiMinutes readings
// and kWifiDrops drops. Times are seconds since start. Main loop only; the
// JSON is read under the caller's lock.
class WifiHistory {
 public:
  void attach(int8_t *rssi, WifiDrop *drops) {
    this->rssi_ = rssi;
    this->drops_ = drops;
  }
  bool ready() const { return this->rssi_ != nullptr && this->drops_ != nullptr; }

  // Every few seconds: the signal now, when connected. Each finished minute
  // keeps the mean of its readings, or 0 when the panel was off Wi-Fi all
  // minute (a real reading is never 0 dBm).
  void sample(uint32_t now_s, bool connected, int8_t rssi) {
    if (!this->ready())
      return;
    if (!this->started_) {
      this->started_ = true;
      this->minute_s_ = now_s;
    }
    for (uint32_t gap = 0; now_s - this->minute_s_ >= 60 && gap < kWifiMinutes; gap++) {
      int8_t v = 0;
      if (this->n_ > 0)
        v = (int8_t) std::lround((double) this->sum_ / this->n_);
      this->rssi_[this->minutes_ % kWifiMinutes] = v;
      this->minutes_++;
      this->sum_ = 0;
      this->n_ = 0;
      this->minute_s_ += 60;
    }
    if (now_s - this->minute_s_ >= 60)  // a gap longer than the ring
      this->minute_s_ = now_s;
    if (connected && rssi < 0 && rssi > -127) {
      this->sum_ += rssi;
      this->n_++;
    }
  }

  // The connection ended. The first connection after start isn't a drop.
  void down(uint32_t now_s, WifiCause cause) {
    if (!this->up_)
      return;
    this->up_ = false;
    this->down_s_ = now_s;
    this->open_ = false;
    if (cause == WifiCause::PANEL)
      return;
    if (cause != WifiCause::ROAM)
      this->total_++;
    if (!this->ready())  // no buffers: the total alone
      return;
    this->drops_[this->drop_n_ % kWifiDrops] = {now_s, UINT32_MAX, (uint8_t) cause};
    this->drop_n_++;
    this->open_ = true;
  }

  void up(uint32_t now_s) {
    if (this->up_)
      return;
    this->up_ = true;
    if (this->open_)
      this->drops_[(this->drop_n_ - 1) % kWifiDrops].down_s = now_s - this->down_s_;
    this->open_ = false;
  }

  // Drops since start, roams left out
  uint32_t total() const { return this->total_; }
  uint32_t minutes() const { return this->minutes_; }

  // The last kWifiWeakWindow minutes' median is below kWifiWeakDbm, from at
  // least kWifiWeakMin minutes on Wi-Fi.
  bool weak() const {
    if (!this->ready())
      return false;
    int8_t v[kWifiWeakWindow];
    uint32_t n = 0;
    for (uint32_t i = 0; i < kWifiWeakWindow && i < this->minutes_ && i < kWifiMinutes; i++) {
      int8_t r = this->rssi_[(this->minutes_ - 1 - i) % kWifiMinutes];
      if (r != 0)
        v[n++] = r;
    }
    if (n < kWifiWeakMin)
      return false;
    std::nth_element(v, v + n / 2, v + n);
    return v[n / 2] < kWifiWeakDbm;
  }

  // The "wifi" object of GET /gameday/state:
  //   rssi       the signal now (left out off Wi-Fi)
  //   step       seconds per point of hist
  //   hist       the mean signal per step, oldest first, null where the panel
  //              was off Wi-Fi; as far back as the panel has been up, a day at most
  //   weak       the weak-signal hint is due (weak())
  //   drops      times the connection was lost since start
  //   day        of those, in the last 24 hours (up to kWifiDrops)
  //   causes     the last 24 hours by cause, roams too, nonzero only
  //   last, last_ago, last_down   the newest drop: cause, seconds since, and
  //              how long it lasted (left out while it lasts)
  std::string json(uint32_t now_s, bool connected, int8_t rssi) const {
    std::string out = "{";
    char buf[96];  // room for the weak, drops and day piece at any count
    if (connected && rssi < 0 && rssi > -127) {
      snprintf(buf, sizeof(buf), "\"rssi\":%d,", (int) rssi);
      out += buf;
    }
    snprintf(buf, sizeof(buf), "\"step\":%u,\"hist\":[", (unsigned) (kWifiStep * 60));
    out += buf;
    const uint32_t have = this->ready() ? std::min(this->minutes_, kWifiMinutes) : 0;
    // Points end at the newest minute, so the oldest one may be partial.
    const uint32_t points = (have + kWifiStep - 1) / kWifiStep;
    for (uint32_t p = 0; p < points; p++) {
      // Minutes back from the newest, this point's oldest and newest
      const uint32_t newest = (points - 1 - p) * kWifiStep;
      const uint32_t oldest = std::min(newest + kWifiStep, have) - 1;
      int32_t sum = 0, n = 0;
      for (uint32_t back = newest; back <= oldest; back++) {
        int8_t r = this->rssi_[(this->minutes_ - 1 - back) % kWifiMinutes];
        if (r != 0) {
          sum += r;
          n++;
        }
      }
      if (p > 0)
        out += ',';
      if (n == 0) {
        out += "null";
      } else {
        snprintf(buf, sizeof(buf), "%ld", std::lround((double) sum / n));
        out += buf;
      }
    }
    uint32_t day = 0;
    uint32_t causes[kWifiCauses] = {};
    const uint32_t kept = std::min(this->drop_n_, kWifiDrops);
    for (uint32_t i = 0; i < kept; i++) {
      const WifiDrop &d = this->drops_[(this->drop_n_ - 1 - i) % kWifiDrops];
      if (now_s - d.at_s >= 24 * 60 * 60)
        break;
      if (d.cause < kWifiCauses)
        causes[d.cause]++;
      if (d.cause != (uint8_t) WifiCause::ROAM)
        day++;
    }
    snprintf(buf, sizeof(buf), "],\"weak\":%s,\"drops\":%u,\"day\":%u,\"causes\":{", this->weak() ? "true" : "false",
             (unsigned) this->total_, (unsigned) day);
    out += buf;
    bool first = true;
    for (uint8_t c = 0; c < kWifiCauses; c++) {
      if (causes[c] == 0)
        continue;
      snprintf(buf, sizeof(buf), "%s\"%s\":%u", first ? "" : ",", wifi_cause_key((WifiCause) c), (unsigned) causes[c]);
      out += buf;
      first = false;
    }
    out += '}';
    if (kept > 0) {
      const WifiDrop &d = this->drops_[(this->drop_n_ - 1) % kWifiDrops];
      snprintf(buf, sizeof(buf), ",\"last\":\"%s\",\"last_ago\":%u", wifi_cause_key((WifiCause) d.cause),
               (unsigned) (now_s - d.at_s));
      out += buf;
      if (d.down_s != UINT32_MAX) {
        snprintf(buf, sizeof(buf), ",\"last_down\":%u", (unsigned) d.down_s);
        out += buf;
      }
    }
    out += '}';
    return out;
  }

 protected:
  int8_t *rssi_{nullptr};
  WifiDrop *drops_{nullptr};
  uint32_t minutes_{0};  // finished minutes; the next one goes at minutes_ % kWifiMinutes
  uint32_t minute_s_{0};
  int32_t sum_{0};
  uint32_t n_{0};
  bool started_{false};
  bool up_{false};
  bool open_{false};  // the newest drop hasn't come back yet
  uint32_t down_s_{0};
  uint32_t drop_n_{0};  // drops recorded, roams too
  uint32_t total_{0};
};

}  // namespace espn
