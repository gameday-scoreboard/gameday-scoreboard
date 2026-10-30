#ifdef USE_ESP32

#include "web_stack.h"

#include <esp_http_server.h>

// ESPHome starts its web server task with a fixed 4,512-byte stack (in
// web_server_idf.cpp, not a config option). Every reply goes out through lwIP
// and the Wi-Fi driver on that stack, and an interrupt's frame lands on top:
// on a bench the device page's own entity reads used up to 3,916 bytes and
// our /gameday/settings import 4,092. The build wraps httpd_start
// (-Wl,--wrap=httpd_start, __init__.py) to give the task more room.

extern "C" esp_err_t __real_httpd_start(httpd_handle_t *handle, const httpd_config_t *config);

extern "C" esp_err_t __wrap_httpd_start(httpd_handle_t *handle, const httpd_config_t *config) {
  httpd_config_t c = *config;
  if (c.stack_size < esphome::gameday::WEB_STACK_SIZE)
    c.stack_size = esphome::gameday::WEB_STACK_SIZE;
  return __real_httpd_start(handle, &c);
}

#endif  // USE_ESP32
