// A GIF player for the picture card (picture.h): draws one frame at a time
// onto an RGB565 canvas the size of the GIF. Pure, with no ESPHome
// includes, so the host tests and the fuzzer run it.
//
// Transparent pixels and disposal to the background leave black, as the
// panel has no background of its own. The canvas starts black and goes
// black again when the animation starts over.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "psram.h"

namespace espn {

// A delay of 0 or 1 hundredths plays at 10 frames a second, as browsers do.
constexpr uint32_t kGifDefaultDelayMs = 100;

class GifPlayer {
 public:
  // data must outlive the player. False when it isn't a GIF.
  bool open(const uint8_t *data, size_t size) {
    *this = GifPlayer{};
    if (size < 13 || (std::memcmp(data, "GIF87a", 6) != 0 && std::memcmp(data, "GIF89a", 6) != 0))
      return false;
    this->data_ = data;
    this->size_ = size;
    this->w_ = (uint16_t) (data[6] | (data[7] << 8));
    this->h_ = (uint16_t) (data[8] | (data[9] << 8));
    if (this->w_ == 0 || this->h_ == 0)
      return false;
    const uint8_t packed = data[10];
    size_t pos = 13;
    if (packed & 0x80) {
      this->global_n_ = 2 << (packed & 7);
      if (pos + 3 * (size_t) this->global_n_ > size)
        return false;
      this->global_ = data + pos;
      pos += 3 * (size_t) this->global_n_;
    }
    this->first_ = pos;
    this->pos_ = pos;
    this->prefix_.resize(4096);
    this->suffix_.resize(4096);
    this->stack_.resize(4097);
    return true;
  }

  uint16_t width() const { return this->w_; }
  uint16_t height() const { return this->h_; }
  // True after next() has passed the last frame and started over.
  bool looped() const { return this->looped_; }

  // Draws the next frame onto canvas (width() x height()) and returns how
  // long to show it in ms. After the last frame it starts over at the
  // first. -1 for a broken file, and the next call starts over.
  int32_t next(uint16_t *canvas) {
    this->dispose_(canvas);
    bool wrapped = false;
    int delay_cs = -1, transparent = -1, disposal = 0;
    while (true) {
      // A file that stops after a frame without its trailer ends there just the same.
      const uint8_t b = this->pos_ < this->size_ ? this->data_[this->pos_++] : 0x3B;
      if (b == 0x3B) {  // the trailer: start over
        if (wrapped || this->frames_ == 0)
          return this->fail_();
        wrapped = true;
        this->restart_(canvas);
        continue;
      }
      if (b == 0x21) {  // an extension
        if (this->pos_ >= this->size_)
          return this->fail_();
        const uint8_t label = this->data_[this->pos_++];
        if (label == 0xF9 && this->pos_ + 5 <= this->size_ && this->data_[this->pos_] >= 4) {
          const uint8_t *g = this->data_ + this->pos_ + 1;
          disposal = (g[0] >> 2) & 7;
          delay_cs = g[1] | (g[2] << 8);
          transparent = (g[0] & 1) ? g[3] : -1;
        }
        if (!this->skip_blocks_())
          return this->fail_();
        continue;
      }
      if (b != 0x2C)
        return this->fail_();
      if (!this->frame_(canvas, transparent, disposal))
        return this->fail_();
      this->frames_++;
      if (delay_cs <= 1)
        return (int32_t) kGifDefaultDelayMs;
      return (int32_t) delay_cs * 10;
    }
  }

 protected:
  // The frame's image descriptor is next (its 0x2C already read).
  bool frame_(uint16_t *canvas, int transparent, int disposal) {
    if (this->pos_ + 9 > this->size_)
      return false;
    const uint8_t *d = this->data_ + this->pos_;
    const int fx = d[0] | (d[1] << 8), fy = d[2] | (d[3] << 8);
    const int fw = d[4] | (d[5] << 8), fh = d[6] | (d[7] << 8);
    const uint8_t packed = d[8];
    this->pos_ += 9;
    // A frame past the GIF's own screen is damaged. Browsers grow the screen
    // instead; refusing keeps a frame's work to the screen's pixels.
    if (fw == 0 || fh == 0 || fx + fw > this->w_ || fy + fh > this->h_)
      return false;
    const uint8_t *table = this->global_;
    int table_n = this->global_n_;
    if (packed & 0x80) {
      table_n = 2 << (packed & 7);
      if (this->pos_ + 3 * (size_t) table_n > this->size_)
        return false;
      table = this->data_ + this->pos_;
      this->pos_ += 3 * (size_t) table_n;
    }
    if (table == nullptr)
      return false;
    uint16_t colors[256];
    for (int i = 0; i < 256; i++) {
      if (i >= table_n) {
        colors[i] = 0;
        continue;
      }
      const uint8_t *c = table + 3 * i;
      colors[i] = (uint16_t) (((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3));
    }
    // What disposal puts back before the next frame: this frame's area.
    this->disp_ = disposal;
    this->dx0_ = fx;
    this->dy0_ = fy;
    this->dx1_ = fx + fw;
    this->dy1_ = fy + fh;
    if (disposal == 3) {
      if (this->saved_.size() != (size_t) this->w_ * this->h_)
        this->saved_.resize((size_t) this->w_ * this->h_);
      std::memcpy(this->saved_.data(), canvas, this->saved_.size() * sizeof(uint16_t));
    }
    const bool interlaced = (packed & 0x40) != 0;
    // Each decoded pixel's place: x across the frame, and the row by pass
    // when interlaced (every 8th from 0, every 8th from 4, every 4th from
    // 2, every 2nd from 1).
    int x = 0, row = 0, pass = 0;
    static const uint8_t kStart[4] = {0, 4, 2, 1}, kStep[4] = {8, 8, 4, 2};
    int step = interlaced ? kStep[0] : 1;
    auto put = [&](uint8_t index) {
      if (row >= fh)
        return;
      if ((int) index != transparent)
        canvas[(size_t) (fy + row) * this->w_ + fx + x] = colors[index];
      if (++x < fw)
        return;
      x = 0;
      row += step;
      while (interlaced && row >= fh && pass < 3) {
        pass++;
        row = kStart[pass];
        step = kStep[pass];
      }
    };
    return this->lzw_(put);
  }

  // The frame's LZW data, each pixel's color index to put().
  template<class Put> bool lzw_(Put &put) {
    if (this->pos_ >= this->size_)
      return false;
    const int min_size = this->data_[this->pos_++];
    if (min_size < 2 || min_size > 8)
      return false;
    const int clear = 1 << min_size, eoi = clear + 1;
    int size = min_size + 1, avail = clear + 2, old = -1;
    uint8_t first = 0;
    uint32_t bits = 0;
    int nbits = 0;
    size_t block_left = 0;
    bool ended = false;  // the data's own end (an empty block) came first
    for (int i = 0; i < clear; i++) {
      this->prefix_[i] = 0;
      this->suffix_[i] = (uint8_t) i;
    }
    while (true) {
      while (nbits < size) {
        if (block_left == 0) {
          if (this->pos_ >= this->size_)
            return false;
          block_left = this->data_[this->pos_++];
          if (block_left == 0) {
            ended = true;
            break;
          }
        }
        if (this->pos_ >= this->size_)
          return false;
        bits |= (uint32_t) this->data_[this->pos_++] << nbits;
        nbits += 8;
        block_left--;
      }
      if (ended)
        return true;  // no end code; what came is drawn
      int code = (int) (bits & ((1u << size) - 1));
      bits >>= size;
      nbits -= size;
      if (code == clear) {
        size = min_size + 1;
        avail = clear + 2;
        old = -1;
        continue;
      }
      if (code == eoi)
        break;
      if (old < 0) {
        if (code >= clear)
          return false;
        put((uint8_t) code);
        first = (uint8_t) code;
        old = code;
        continue;
      }
      const int in = code;
      if (code > avail)
        return false;
      size_t sp = 0;
      if (code == avail) {
        this->stack_[sp++] = first;
        code = old;
      }
      while (code >= clear) {
        if (sp >= 4096)
          return false;
        this->stack_[sp++] = this->suffix_[code];
        code = this->prefix_[code];
      }
      first = this->suffix_[code];
      this->stack_[sp++] = first;
      while (sp > 0)
        put(this->stack_[--sp]);
      if (avail < 4096) {
        this->prefix_[avail] = (uint16_t) old;
        this->suffix_[avail] = first;
        avail++;
        if (avail == (1 << size) && size < 12)
          size++;
      }
      old = in;
    }
    // Past the end code: the rest of the frame's blocks.
    if (block_left > 0) {
      if (this->pos_ + block_left > this->size_)
        return false;
      this->pos_ += block_left;
    }
    return this->skip_blocks_();
  }

  // Data sub-blocks up to and including the empty one that ends them.
  bool skip_blocks_() {
    while (true) {
      if (this->pos_ >= this->size_)
        return false;
      const uint8_t n = this->data_[this->pos_++];
      if (n == 0)
        return true;
      if (this->pos_ + n > this->size_)
        return false;
      this->pos_ += n;
    }
  }

  // The last frame's disposal, before the next one draws.
  void dispose_(uint16_t *canvas) {
    if (this->disp_ == 2 || (this->disp_ == 3 && !this->saved_.empty())) {
      for (int y = this->dy0_; y < this->dy1_; y++) {
        uint16_t *row = canvas + (size_t) y * this->w_;
        if (this->disp_ == 2) {
          std::memset(row + this->dx0_, 0, (size_t) (this->dx1_ - this->dx0_) * sizeof(uint16_t));
        } else {
          std::memcpy(row + this->dx0_, this->saved_.data() + (size_t) y * this->w_ + this->dx0_,
                      (size_t) (this->dx1_ - this->dx0_) * sizeof(uint16_t));
        }
      }
    }
    this->disp_ = 0;
  }

  void restart_(uint16_t *canvas) {
    std::memset(canvas, 0, (size_t) this->w_ * this->h_ * sizeof(uint16_t));
    this->pos_ = this->first_;
    this->disp_ = 0;
    this->looped_ = true;
  }

  int32_t fail_() {
    this->pos_ = this->first_;
    this->disp_ = 0;
    return -1;
  }

  const uint8_t *data_{nullptr};
  size_t size_{0};
  size_t first_{0};  // the first block after the header and global colors
  size_t pos_{0};
  uint16_t w_{0}, h_{0};
  const uint8_t *global_{nullptr};
  int global_n_{0};
  uint32_t frames_{0};
  bool looped_{false};
  int disp_{0};
  int dx0_{0}, dy0_{0}, dx1_{0}, dy1_{0};
  PsVector<uint16_t> prefix_;
  PsVector<uint8_t> suffix_;
  PsVector<uint8_t> stack_;
  PsVector<uint16_t> saved_;  // the canvas before a frame that restores it
};

}  // namespace espn
