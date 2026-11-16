// Inflates a gzip response body as it streams in, so ArduinoJson can read
// ESPN's gzipped JSON without the whole body sitting in RAM. Pure C++ on top
// of miniz's tinfl (in the ESP32-S3 ROM), so the host tests can run it
// against recorded responses.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "miniz.h"

namespace gz {

// tinfl's state, the 32 KB window it copies matches back from and the
// compressed input: about 44 KB. The firmware takes one from PSRAM for each
// read and frees it after; none of it goes on the fetch task's stack.
struct Work {
  tinfl_decompressor decomp;
  uint8_t dict[TINFL_LZ_DICT_SIZE];
  uint8_t in[1024];
};

// Is a Content-Encoding header value gzip? ESPN sends "gzip"; a missing
// header means the body is plain and is read as it is.
inline bool is_gzip_encoding(const char *value) {
  if (value == nullptr)
    return false;
  while (*value == ' ')
    value++;
  return strncmp(value, "gzip", 4) == 0 && (value[4] == '\0' || value[4] == ' ' || value[4] == ';');
}

// Pulls compressed bytes from `Src` and hands out the inflated ones. Src is
// anything with `int read(uint8_t *buf, size_t max)` that returns the number
// of bytes read and 0 or less at the end; read() here works the same way and
// returns -1 once the data turns out to be corrupt or cut short.
template<typename Src> class Inflate {
 public:
  Inflate(Src &src, Work *work) : src_(src), work_(work) { tinfl_init(&work->decomp); }

  int read(uint8_t *dst, size_t max) {
    while (this->pending_len_ == 0) {
      if (this->failed_)
        return -1;
      if (this->done_)
        return 0;
      if (!this->header_done_ && !this->read_header_()) {
        this->failed_ = true;
        return -1;
      }
      this->step_();
    }
    size_t n = max < this->pending_len_ ? max : this->pending_len_;
    memcpy(dst, this->work_->dict + this->pending_start_, n);
    this->pending_start_ += n;
    this->pending_len_ -= n;
    return (int) n;
  }

  bool failed() const { return this->failed_; }

 private:
  bool refill_() {
    if (this->in_pos_ < this->in_len_)
      return true;
    if (this->src_eof_)
      return false;
    int n = this->src_.read(this->work_->in, sizeof(this->work_->in));
    if (n <= 0) {
      this->src_eof_ = true;
      return false;
    }
    this->in_pos_ = 0;
    this->in_len_ = (size_t) n;
    return true;
  }

  int in_byte_() {
    if (!this->refill_())
      return -1;
    return this->work_->in[this->in_pos_++];
  }

  bool skip_(size_t n) {
    for (size_t i = 0; i < n; i++)
      if (this->in_byte_() < 0)
        return false;
    return true;
  }

  bool skip_zero_terminated_() {
    for (;;) {
      int c = this->in_byte_();
      if (c < 0)
        return false;
      if (c == 0)
        return true;
    }
  }

  // RFC 1952: magic, method 8 (deflate), flags, then six fixed bytes and
  // whichever optional fields the flags announce.
  bool read_header_() {
    uint8_t h[10];
    for (auto &b : h) {
      int c = this->in_byte_();
      if (c < 0)
        return false;
      b = (uint8_t) c;
    }
    if (h[0] != 0x1f || h[1] != 0x8b || h[2] != 8)
      return false;
    uint8_t flags = h[3];
    if (flags & 0x04) {  // FEXTRA
      int lo = this->in_byte_(), hi = this->in_byte_();
      if (lo < 0 || hi < 0 || !this->skip_((size_t) (lo | (hi << 8))))
        return false;
    }
    if ((flags & 0x08) && !this->skip_zero_terminated_())  // FNAME
      return false;
    if ((flags & 0x10) && !this->skip_zero_terminated_())  // FCOMMENT
      return false;
    if ((flags & 0x02) && !this->skip_(2))  // FHCRC
      return false;
    this->header_done_ = true;
    return true;
  }

  // One tinfl call: whatever it writes into the window becomes pending. The
  // window is written in order and wraps at its end, so the bytes handed out
  // last are never overwritten before the caller has copied them.
  //
  // tinfl is always told more input may follow. Told otherwise, this miniz
  // (v1.15, the ROM's) reads zero bytes past the end and can inflate them
  // forever, so a body cut short has to show up as NEEDS_MORE_INPUT with
  // nothing left to give it.
  void step_() {
    this->refill_();
    size_t in_size = this->in_len_ - this->in_pos_;
    size_t out_size = TINFL_LZ_DICT_SIZE - this->dict_ofs_;
    tinfl_status status =
        tinfl_decompress(&this->work_->decomp, this->work_->in + this->in_pos_, &in_size, this->work_->dict,
                         this->work_->dict + this->dict_ofs_, &out_size, TINFL_FLAG_HAS_MORE_INPUT);
    this->in_pos_ += in_size;
    this->pending_start_ = this->dict_ofs_;
    this->pending_len_ = out_size;
    this->dict_ofs_ = (this->dict_ofs_ + out_size) & (TINFL_LZ_DICT_SIZE - 1);
    if (status == TINFL_STATUS_DONE) {
      // The CRC and length trailer follow; the JSON parser already has
      // everything it needs, so they are left unread.
      this->done_ = true;
    } else if (status < TINFL_STATUS_DONE) {
      this->failed_ = true;
    } else if (status == TINFL_STATUS_NEEDS_MORE_INPUT && this->src_eof_ && out_size == 0) {
      this->failed_ = true;  // the body ended mid-stream
    }
  }

  Src &src_;
  Work *work_;
  size_t in_pos_{0};
  size_t in_len_{0};
  size_t dict_ofs_{0};
  size_t pending_start_{0};
  size_t pending_len_{0};
  bool header_done_{false};
  bool src_eof_{false};
  bool done_{false};
  bool failed_{false};
};

// What ArduinoJson reads: read() and readBytes() over a response body,
// inflated on the way when it has `work` (the reply was gzip) and passed
// through as it is when not. Counts the bytes on the wire and the JSON bytes.
template<typename Src> class Body {
 public:
  Body(Src &src, Work *work) : wire_{src} {
    if (work != nullptr)
      this->inflate_.emplace(this->wire_, work);
  }
  Body(const Body &) = delete;
  Body &operator=(const Body &) = delete;

  int read() {
    if (!this->fill_())
      return -1;
    return this->buf_[this->pos_++];
  }

  size_t readBytes(char *dst, size_t n) {
    size_t copied = 0;
    while (copied < n) {
      if (!this->fill_())
        break;
      size_t take = n - copied < this->len_ - this->pos_ ? n - copied : this->len_ - this->pos_;
      memcpy(dst + copied, this->buf_ + this->pos_, take);
      this->pos_ += take;
      copied += take;
    }
    return copied;
  }

  bool gzip() const { return this->inflate_.has_value(); }
  size_t wire_bytes() const { return this->wire_.total; }
  size_t json_bytes() const { return this->json_total_; }

 private:
  struct Wire {
    Src &src;
    size_t total{0};
    int read(uint8_t *buf, size_t max) {
      int n = this->src.read(buf, max);
      if (n > 0)
        this->total += (size_t) n;
      return n;
    }
  };

  bool fill_() {
    if (this->pos_ < this->len_)
      return true;
    if (this->eof_)
      return false;
    int n = this->inflate_ ? this->inflate_->read(this->buf_, sizeof(this->buf_))
                           : this->wire_.read(this->buf_, sizeof(this->buf_));
    if (n <= 0) {
      this->eof_ = true;
      return false;
    }
    this->pos_ = 0;
    this->len_ = (size_t) n;
    this->json_total_ += (size_t) n;
    return true;
  }

  Wire wire_;
  std::optional<Inflate<Wire>> inflate_;
  uint8_t buf_[1024];
  size_t pos_{0};
  size_t len_{0};
  size_t json_total_{0};
  bool eof_{false};
};

}  // namespace gz
