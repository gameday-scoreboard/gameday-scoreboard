#include "stack_bench.h"

#include <cstring>
#include <memory>

#include <esp_attr.h>
#include <esp_cpu.h>
#include <esp_heap_caps.h>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/logger/logger.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/gameday/espn_parse.h"

namespace esphome::stack_bench {

static const char *const TAG = "stack";

// FreeRTOS fills a fresh stack with this; the high-water mark is where it
// stops being this byte, counted from the low end.
static constexpr uint8_t FILL = 0xa5;
static constexpr uint32_t FILL_WORD = 0xa5a5a5a5u;
// Never write the lowest 64 bytes: the kernel's end-of-stack watchpoint (when
// on) sits there, and a store of the same byte still trips it.
static constexpr size_t FLOOR = 64;
// The meter's own frame and anything it calls land right under the caller's
// stack pointer; leave that much unpainted so they aren't counted.
static constexpr size_t HEADROOM = 256;
static constexpr size_t LOOP_STACK = 16384;

static constexpr size_t NAME = 16;
static constexpr size_t MAX_TASKS = 28;
static constexpr size_t MAX_METERS = 20;
static constexpr size_t MAX_PROBES = 8;
static constexpr uint32_t MAGIC = 0x53544b32;  // "STK2"

struct TaskRow {
  char name[NAME];
  uint32_t min_free;
  uint32_t at_ms;
  char label[NAME];
};
struct MeterRow {
  char name[NAME];
  uint32_t max_used;   // deepest the scope went below its caller
  uint32_t left;       // free stack at that deepest point
  uint32_t count;
  char label[NAME];
};
struct ProbeRow {
  char name[NAME];
  uint32_t min_free;
  char label[NAME];
};
struct Table {
  uint32_t magic;
  uint32_t resets;
  uint32_t setup_cut_s;
  char label[NAME];
  TaskRow tasks[MAX_TASKS];
  MeterRow meters[MAX_METERS];
  ProbeRow probes[MAX_PROBES];
};

RTC_NOINIT_ATTR static Table table;  // NOLINT
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t main_task = nullptr;

static inline uint8_t *current_sp() {
  uint32_t sp;
  asm volatile("mov %0, a1" : "=r"(sp));
  return reinterpret_cast<uint8_t *>(sp);
}

static void table_reset() {
  memset(&table, 0, sizeof(table));
  table.magic = MAGIC;
}

static void table_check() {
  if (table.magic != MAGIC)
    table_reset();
}

static void copy_name(char *dst, const char *src) {
  strncpy(dst, src, NAME - 1);
  dst[NAME - 1] = '\0';
}

void set_label(const char *label) {
  portENTER_CRITICAL(&mux);
  copy_name(table.label, label);
  portEXIT_CRITICAL(&mux);
}

uint32_t setup_cut_s() { return table.magic == MAGIC ? table.setup_cut_s : 0; }
void set_setup_cut(uint32_t s) { table.setup_cut_s = s; }

void fold_task(const char *name, uint32_t free_bytes) {
  portENTER_CRITICAL(&mux);
  TaskRow *row = nullptr;
  for (auto &t : table.tasks) {
    if (t.name[0] == '\0') {
      row = &t;
      copy_name(row->name, name);
      row->min_free = UINT32_MAX;
      break;
    }
    if (strncmp(t.name, name, NAME - 1) == 0) {
      row = &t;
      break;
    }
  }
  if (row != nullptr && free_bytes < row->min_free) {
    row->min_free = free_bytes;
    row->at_ms = millis();
    copy_name(row->label, table.label);
  }
  portEXIT_CRITICAL(&mux);
}

static void fold_meter(const char *name, uint32_t used, uint32_t left) {
  portENTER_CRITICAL(&mux);
  MeterRow *row = nullptr;
  for (auto &m : table.meters) {
    if (m.name[0] == '\0') {
      row = &m;
      copy_name(row->name, name);
      row->left = UINT32_MAX;
      break;
    }
    if (strncmp(m.name, name, NAME - 1) == 0) {
      row = &m;
      break;
    }
  }
  if (row != nullptr) {
    row->count++;
    if (used > row->max_used || (used == row->max_used && left < row->left)) {
      row->max_used = used;
      row->left = left;
      copy_name(row->label, table.label);
    }
  }
  portEXIT_CRITICAL(&mux);
}

void probe(const char *name) {
  uint8_t *start = pxTaskGetStackStart(nullptr);
  uint32_t free_now = (uint32_t) (current_sp() - start);
  fold_task(pcTaskGetName(nullptr), (uint32_t) uxTaskGetStackHighWaterMark(nullptr));
  portENTER_CRITICAL(&mux);
  ProbeRow *row = nullptr;
  for (auto &p : table.probes) {
    if (p.name[0] == '\0') {
      row = &p;
      copy_name(row->name, name);
      row->min_free = UINT32_MAX;
      break;
    }
    if (strncmp(p.name, name, NAME - 1) == 0) {
      row = &p;
      break;
    }
  }
  if (row != nullptr && free_now < row->min_free) {
    row->min_free = free_now;
    copy_name(row->label, table.label);
  }
  portEXIT_CRITICAL(&mux);
}

void note_exit() { fold_task(pcTaskGetName(nullptr), (uint32_t) uxTaskGetStackHighWaterMark(nullptr)); }

Meter::Meter(const char *name) : name_(name) {
  this->start_ = pxTaskGetStackStart(nullptr);
  this->sp0_ = current_sp();
  // What the task reached before now, before the paint erases that record.
  fold_task(pcTaskGetName(nullptr), (uint32_t) uxTaskGetStackHighWaterMark(nullptr));
  uint8_t *lo = this->start_ + FLOOR;
  uint8_t *hi = this->sp0_ - HEADROOM;
  this->paint_top_ = hi > lo ? hi : lo;
  if (hi > lo)
    memset(lo, FILL, (size_t) (hi - lo));
}

Meter::~Meter() {
  // First byte from the low end that is no longer the fill: the deepest
  // point anything reached while the scope ran.
  uint8_t *p = this->start_;
  auto *w = reinterpret_cast<uint32_t *>((reinterpret_cast<uintptr_t>(p) + 3) & ~(uintptr_t) 3);
  while (reinterpret_cast<uint8_t *>(w) < this->paint_top_ && *w == FILL_WORD)
    w++;
  uint8_t *first = reinterpret_cast<uint8_t *>(w);
  while (first < this->paint_top_ && *first == FILL)
    first++;
  uint32_t left = (uint32_t) (first - this->start_);
  // Under HEADROOM bytes shows as 0: the scope stayed inside the unpainted band.
  uint32_t used = first >= this->paint_top_ ? 0 : (uint32_t) (this->sp0_ - first);
  fold_task(pcTaskGetName(nullptr), left);
  fold_meter(this->name_, used, used == 0 ? UINT32_MAX : left);
}

// ---- the log-callback sandwich: web_server's and api's send on the main task

alignas(Meter) static uint8_t log_meter_storage[sizeof(Meter)];
static bool log_meter_active = false;

static void log_cb_first(void *, uint8_t, const char *, const char *, size_t) {
  if (xTaskGetCurrentTaskHandle() != main_task || log_meter_active)
    return;
  new (log_meter_storage) Meter("log_send");
  log_meter_active = true;
}

static void log_cb_last(void *, uint8_t, const char *, const char *, size_t) {
  if (!log_meter_active || xTaskGetCurrentTaskHandle() != main_task)
    return;
  log_meter_active = false;
  reinterpret_cast<Meter *>(log_meter_storage)->~Meter();
}

void StackBenchEarly::setup() {
  table_check();
  main_task = xTaskGetCurrentTaskHandle();
  if (logger::global_logger != nullptr)
    logger::global_logger->add_log_callback(this, &log_cb_first);
}

// ---- LVGL: one meter per display refresh, from its first to its last event

alignas(Meter) static uint8_t lv_meter_storage[sizeof(Meter)];
static bool lv_meter_active = false;

static void lv_refr_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_REFR_START) {
    if (lv_meter_active)
      reinterpret_cast<Meter *>(lv_meter_storage)->~Meter();
    new (lv_meter_storage) Meter("lvgl_refresh");
    lv_meter_active = true;
  } else if (code == LV_EVENT_REFR_READY) {
    if (!lv_meter_active)
      return;
    lv_meter_active = false;
    reinterpret_cast<Meter *>(lv_meter_storage)->~Meter();
  }
}

// ---- the component

void StackBench::setup() {
  table_check();
  if (logger::global_logger != nullptr)
    logger::global_logger->add_log_callback(this, &log_cb_last);
  lv_display_t *disp = lv_display_get_default();
  if (disp != nullptr) {
    lv_display_add_event_cb(disp, &lv_refr_cb, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(disp, &lv_refr_cb, LV_EVENT_REFR_READY, nullptr);
  }
  this->base_->add_handler(this);
}

void StackBench::dump_config() { ESP_LOGCONFIG(TAG, "Stack bench on, GET /bench/stack"); }

void StackBench::sample_() {
  UBaseType_t n = uxTaskGetNumberOfTasks();
  auto *tasks = static_cast<TaskStatus_t *>(heap_caps_malloc(n * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM));
  if (tasks == nullptr)
    tasks = static_cast<TaskStatus_t *>(malloc(n * sizeof(TaskStatus_t)));
  if (tasks == nullptr)
    return;
  UBaseType_t got = uxTaskGetSystemState(tasks, n, nullptr);
  for (UBaseType_t i = 0; i < got; i++)
    fold_task(tasks[i].pcTaskName, (uint32_t) tasks[i].usStackHighWaterMark);
  if (millis() - this->last_log_ms_ >= 30000) {
    this->last_log_ms_ = millis();
    std::string line;
    for (UBaseType_t i = 0; i < got; i++) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s=%u ", tasks[i].pcTaskName, (unsigned) tasks[i].usStackHighWaterMark);
      line += buf;
      if (line.size() > 160) {
        ESP_LOGI(TAG, "hwm bytes: %s", line.c_str());
        line.clear();
      }
    }
    if (!line.empty())
      ESP_LOGI(TAG, "hwm bytes: %s", line.c_str());
  }
  free(tasks);
}

void StackBench::loop() {
  if (millis() - this->last_sample_ms_ < 1000)
    return;
  this->last_sample_ms_ = millis();
  this->sample_();
  if (!this->booted_logged_ && millis() > 20000) {
    // The tables as they came through the restart, for the record.
    this->booted_logged_ = true;
    ESP_LOGI(TAG, "tables kept through the restart: resets=%u label=%s", (unsigned) table.resets, table.label);
  }
}

static std::string table_text() {
  // Called on the web server task, whose 4.5 KB stack has a few hundred bytes
  // spare: the copy goes on the heap, never on that stack.
  std::unique_ptr<Table> snap(new Table);
  if (!snap)
    return "no memory\n";
  portENTER_CRITICAL(&mux);
  memcpy(snap.get(), &table, sizeof(Table));
  portEXIT_CRITICAL(&mux);
  const Table &copy = *snap;
  std::string out;
  char buf[160];
  snprintf(buf, sizeof(buf), "label=%s uptime_s=%u resets=%u setup_cut_s=%u\n", copy.label,
           (unsigned) (millis() / 1000), (unsigned) copy.resets, (unsigned) copy.setup_cut_s);
  out += buf;
  out += "tasks: name min_free_bytes at_s label\n";
  for (const auto &t : copy.tasks) {
    if (t.name[0] == '\0')
      continue;
    snprintf(buf, sizeof(buf), "  %-16s %6u %7u %s\n", t.name, (unsigned) t.min_free, (unsigned) (t.at_ms / 1000),
             t.label);
    out += buf;
  }
  out += "meters: name max_used_bytes left_at_deepest count label\n";
  for (const auto &m : copy.meters) {
    if (m.name[0] == '\0')
      continue;
    snprintf(buf, sizeof(buf), "  %-16s %6u %6s %7u %s\n", m.name, (unsigned) m.max_used,
             m.left == UINT32_MAX ? "-" : std::to_string(m.left).c_str(), (unsigned) m.count, m.label);
    out += buf;
  }
  out += "probes: name min_free_bytes label\n";
  for (const auto &p : copy.probes) {
    if (p.name[0] == '\0')
      continue;
    snprintf(buf, sizeof(buf), "  %-16s %6u %s\n", p.name, (unsigned) p.min_free, p.label);
    out += buf;
  }
  return out;
}

bool StackBench::canHandle(AsyncWebServerRequest *request) const {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  return request->url_to(buf) == StringRef("/bench/stack");
}

// GET /bench/stack             the tables
//   ?label=<text>              name the situation for the rows that follow
//   ?reset=1                   start the tables over
//   ?arm=<used bytes>          a store watchpoint that depth into the loop
//                              task's stack: the next frame to reach it panics
//                              with a backtrace (GET /gameday/crash after the
//                              restart)
//   ?disarm=1
//   ?espn=bad|good             ESPN at an address that never answers, and back
void StackBench::handleRequest(AsyncWebServerRequest *request) {
  if (request->hasParam("label"))
    set_label(request->getParam("label")->value().c_str());
  if (request->hasParam("reset")) {
    portENTER_CRITICAL(&mux);
    uint32_t resets = table.resets + 1, cut = table.setup_cut_s;
    char label[NAME];
    memcpy(label, table.label, NAME);
    table_reset();
    table.resets = resets;
    table.setup_cut_s = cut;
    memcpy(table.label, label, NAME);
    portEXIT_CRITICAL(&mux);
  }
  if (request->hasParam("setup_cut"))
    set_setup_cut((uint32_t) atoi(request->getParam("setup_cut")->value().c_str()));
  if (request->hasParam("espn")) {
    std::string which = request->getParam("espn")->value();
    if (which == "bad")
      ::espn::set_site_root("https://192.0.2.1/apis/site/v2/sports/");
    else
      ::espn::set_site_root("https://site.api.espn.com/apis/site/v2/sports/");
    ESP_LOGW(TAG, "ESPN root now %s", which.c_str());
  }
  if (request->hasParam("arm")) {
    uint32_t used = (uint32_t) atoi(request->getParam("arm")->value().c_str());
    this->defer([used]() {
      // Runs on the loop task, on its core: watchpoints are per core.
      uint8_t *start = pxTaskGetStackStart(nullptr);
      uint8_t *addr = start + LOOP_STACK - used;
      addr = reinterpret_cast<uint8_t *>(reinterpret_cast<uintptr_t>(addr) & ~(uintptr_t) 31);
      esp_err_t err = esp_cpu_set_watchpoint(0, addr, 32, ESP_CPU_WATCHPOINT_STORE);
      ESP_LOGW(TAG, "Watchpoint 0 at %p (%u bytes into the loop stack, %u free there): %s", addr,
               (unsigned) (start + LOOP_STACK - addr), (unsigned) (addr - start), esp_err_to_name(err));
    });
  }
  if (request->hasParam("disarm")) {
    this->defer([]() {
      esp_cpu_clear_watchpoint(0);
      ESP_LOGW(TAG, "Watchpoint 0 cleared");
    });
  }
  std::string body = table_text();
  request->send(200, "text/plain", body.c_str());
}

}  // namespace esphome::stack_bench
