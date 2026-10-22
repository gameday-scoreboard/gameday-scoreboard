// Host stand-in for ESPHome's http_request, for test_logo_cache.cpp. Each URL
// answers from a table the test fills: a status and a body.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace esphome::http_request {

struct Header {
  std::string name;
  std::string value;
};

struct Reply {
  int status{200};
  std::string body;
};

class HttpContainer {
 public:
  int status_code{-1};
  size_t content_length{0};
  std::string body;
  size_t pos{0};
  int read(uint8_t *buf, size_t max_len) {
    size_t n = std::min(max_len, this->body.size() - this->pos);
    memcpy(buf, this->body.data() + this->pos, n);
    this->pos += n;
    return (int) n;
  }
  void end() {}
  std::string get_response_header(const std::string &) { return ""; }
};

class HttpRequestComponent {
 public:
  std::map<std::string, Reply> replies;  // a URL not here gets no answer
  int requests{0};
  std::shared_ptr<HttpContainer> get(const std::string &url, const std::vector<Header> &,
                                     const std::vector<std::string> &) {
    this->requests++;
    auto it = this->replies.find(url);
    if (it == this->replies.end())
      return nullptr;
    auto c = std::make_shared<HttpContainer>();
    c->status_code = it->second.status;
    c->body = it->second.body;
    c->content_length = c->body.size();
    return c;
  }
};

}  // namespace esphome::http_request
