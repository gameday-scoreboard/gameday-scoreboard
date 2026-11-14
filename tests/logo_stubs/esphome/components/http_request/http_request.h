// Host stand-in for ESPHome's http_request, for test_logo_cache.cpp. Each URL
// answers from a table the test fills: a status, a body and the length the
// reply claims. on_get runs as a request goes out, which is the logo task in
// the middle of a download.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
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
  size_t length{0};  // the Content-Length it claims; 0 for the body's own
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
  std::vector<std::string> asked;  // every URL requested, in order
  std::function<void(const std::string &)> on_get;
  std::shared_ptr<HttpContainer> get(const std::string &url, const std::vector<Header> &,
                                     const std::vector<std::string> &) {
    this->requests++;
    this->asked.push_back(url);
    if (this->on_get)
      this->on_get(url);
    auto it = this->replies.find(url);
    if (it == this->replies.end())
      return nullptr;
    auto c = std::make_shared<HttpContainer>();
    c->status_code = it->second.status;
    c->body = it->second.body;
    c->content_length = it->second.length ? it->second.length : c->body.size();
    return c;
  }
};

}  // namespace esphome::http_request
