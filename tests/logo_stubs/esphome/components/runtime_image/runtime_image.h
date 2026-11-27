// Host stand-in for ESPHome 2026.11's RuntimeImage, for test_logo_cache.cpp.
// It keeps its decoder after end_decode() the way the real one does, and
// counts live decoders so the test can see who holds one. A body that starts
// with "PNG" decodes; anything else is a decode error. A body that starts with
// a real PNG signature decodes too: into an external buffer it paints every
// pixel fake_decoder::fill (0xAARRGGBB) at the size its header gives.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace esphome {
struct Color {
  uint8_t r, g, b, w;
  Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) : r(r), g(g), b(b), w(w) {}
};
}  // namespace esphome

namespace esphome::image {
enum ImageType { IMAGE_TYPE_RGB565, IMAGE_TYPE_RGB };
enum Transparency { TRANSPARENCY_ALPHA_CHANNEL };
class Image {
 public:
  Image(ImageType type) : type_(type) {}
  virtual ~Image() = default;
  int get_width() const { return this->width_; }
  int get_height() const { return this->height_; }
  const uint8_t *get_data_start() const { return this->data_start_; }

 protected:
  ImageType type_;
  int width_{0};
  int height_{0};
  const uint8_t *data_start_{nullptr};
};
}  // namespace esphome::image

namespace esphome::runtime_image {

enum ImageFormat { AUTO, PNG };

namespace fake_decoder {
inline int live = 0;           // decoder objects alive right now
inline uint32_t fill = 0;      // what a real-signature PNG decodes to
inline bool fail_size = false;  // a real-signature PNG decodes at 1 px less than its header says
}  // namespace fake_decoder

class ImageDecoder {
 public:
  ImageDecoder() { fake_decoder::live++; }
  ~ImageDecoder() { fake_decoder::live--; }
  bool active{false};
};

class RuntimeImage : public image::Image {
 public:
  RuntimeImage(ImageFormat, image::ImageType type, image::Transparency, image::Image * = nullptr, bool = false,
               int fixed_width = 0, int fixed_height = 0)
      : Image(type), fixed_width_(fixed_width), fixed_height_(fixed_height) {}
  bool begin_decode(size_t = 0, ImageFormat = AUTO) {
    if (!this->decoder_)
      this->decoder_ = std::make_unique<ImageDecoder>();
    this->decoder_->active = true;
    this->started_ = false;
    this->png_w_ = 0;
    return true;
  }
  int feed_data(uint8_t *data, size_t len) {
    if (!this->decoder_ || !this->decoder_->active)
      return -1;
    if (!this->started_) {
      static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
      if (len >= 24 && memcmp(data, SIG, 8) == 0) {
        this->png_w_ = (int) (data[16] << 24 | data[17] << 16 | data[18] << 8 | data[19]);
        this->png_h_ = (int) (data[20] << 24 | data[21] << 16 | data[22] << 8 | data[23]);
      } else if (len < 3 || memcmp(data, "PNG", 3) != 0) {
        return -2;
      }
    }
    this->started_ = true;
    return (int) len;
  }
  bool end_decode() {
    if (!this->decoder_ || !this->decoder_->active)
      return false;
    this->decoder_->active = false;  // kept warm, as ESPHome does
    if (this->png_w_ > 0 && this->external_) {
      int w = this->png_w_ - (fake_decoder::fail_size ? 1 : 0);
      int h = this->png_h_ - (fake_decoder::fail_size ? 1 : 0);
      if (w != this->buffer_width_ || h != this->buffer_height_)
        return true;  // a real decode would have let the buffer go
      uint32_t f = fake_decoder::fill;
      for (int i = 0; i < w * h; i++) {
        uint8_t *p = this->buffer_ + i * 4;
        p[0] = f & 0xFF;
        p[1] = (f >> 8) & 0xFF;
        p[2] = (f >> 16) & 0xFF;
        p[3] = f >> 24;
      }
      this->width_ = w;
      this->height_ = h;
      this->data_start_ = this->buffer_;
    }
    return true;
  }
  size_t get_buffer_size(int width, int height) const {
    return (size_t) width * height * (this->type_ == image::IMAGE_TYPE_RGB ? 4 : 3);
  }
  bool set_external_buffer(uint8_t *buffer, int width, int height) {
    this->buffer_ = buffer;
    this->buffer_width_ = width;
    this->buffer_height_ = height;
    this->external_ = true;
    return buffer != nullptr;
  }
  int resize(int width, int height) {
    if (this->fixed_width_)
      width = this->fixed_width_;
    if (this->fixed_height_)
      height = this->fixed_height_;
    this->owned_.assign(this->get_buffer_size(width, height), 0);
    this->pixels.assign((size_t) width * height, 0);
    this->buffer_ = this->owned_.data();
    this->buffer_width_ = width;
    this->buffer_height_ = height;
    return (int) this->owned_.size();
  }
  // Keeps what was painted as 0xAARRGGBB, for the test to read back.
  void draw_pixel(int x, int y, const Color &c) {
    this->pixels[(size_t) y * this->buffer_width_ + x] =
        (uint32_t) c.w << 24 | (uint32_t) c.r << 16 | (uint32_t) c.g << 8 | c.b;
  }
  int get_buffer_width() const { return this->buffer_width_; }
  int get_buffer_height() const { return this->buffer_height_; }
  void release() {
    if (this->external_) {
      this->buffer_ = nullptr;
      this->external_ = false;
    }
    if (this->decoder_)
      this->decoder_->active = false;
  }

  std::vector<uint32_t> pixels;

 protected:
  void refresh_lvgl_() {}

  std::unique_ptr<ImageDecoder> decoder_{nullptr};
  uint8_t *buffer_{nullptr};
  int buffer_width_{0};
  int buffer_height_{0};
  int fixed_width_{0};
  int fixed_height_{0};
  bool started_{false};
  bool external_{false};
  int png_w_{0};
  int png_h_{0};
  std::vector<uint8_t> owned_;
};

}  // namespace esphome::runtime_image
