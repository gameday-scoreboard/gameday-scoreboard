// Host stand-in for ESPHome 2026.10's RuntimeImage, for test_logo_cache.cpp.
// It keeps its decoder after end_decode() the way the real one does, and
// counts live decoders so the test can see who holds one. A body that starts
// with "PNG" decodes; anything else is a decode error.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace esphome::image {
enum ImageType { IMAGE_TYPE_RGB565 };
enum Transparency { TRANSPARENCY_ALPHA_CHANNEL };
class Image {
 public:
  virtual ~Image() = default;
};
}  // namespace esphome::image

namespace esphome::runtime_image {

enum ImageFormat { AUTO, PNG };

namespace fake_decoder {
inline int live = 0;  // decoder objects alive right now
}

class ImageDecoder {
 public:
  ImageDecoder() { fake_decoder::live++; }
  ~ImageDecoder() { fake_decoder::live--; }
  bool active{false};
};

class RuntimeImage : public image::Image {
 public:
  RuntimeImage(ImageFormat, image::ImageType, image::Transparency, image::Image * = nullptr, bool = false, int = 0,
               int = 0) {}
  bool begin_decode(size_t = 0, ImageFormat = AUTO) {
    if (!this->decoder_)
      this->decoder_ = std::make_unique<ImageDecoder>();
    this->decoder_->active = true;
    this->started_ = false;
    return true;
  }
  int feed_data(uint8_t *data, size_t len) {
    if (!this->decoder_ || !this->decoder_->active)
      return -1;
    if (!this->started_ && (len < 3 || memcmp(data, "PNG", 3) != 0))
      return -2;
    this->started_ = true;
    return (int) len;
  }
  bool end_decode() {
    if (!this->decoder_ || !this->decoder_->active)
      return false;
    this->decoder_->active = false;  // kept warm, as ESPHome 2026.10 does
    return true;
  }

 protected:
  std::unique_ptr<ImageDecoder> decoder_{nullptr};
  bool started_{false};
};

}  // namespace esphome::runtime_image
