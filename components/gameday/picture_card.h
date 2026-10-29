#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "picture.h"
#include "psram.h"

namespace esphome::gameday {

// The owner's pictures (picture.h): kept in the settings flash (NVS) in a
// namespace of their own, and the one on the board decoded into an RGB565
// canvas in PSRAM. Nothing is allocated while no picture is up.
//
// Uploads and deletes run on the web server's task, the board on the main
// loop; the list of what is kept is shared under a lock.
class PictureCard {
 public:
  // Main loop, once: reads what is kept.
  void setup();

  // Any task. A copy of what each slot holds.
  void slots(::espn::PicInfo out[::espn::kPicSlots]) const;
  int count() const;
  // Bytes the next upload can be, 0 when no slot is free or the flash is
  // too full for any picture.
  size_t room() const;

  // Web server task. Checks an upload and keeps it in the first free slot.
  // "" when kept, else what to tell the owner.
  std::string add(const uint8_t *data, size_t size, int &slot);
  bool remove(int slot);
  // The file in a slot, as uploaded. False when the slot is empty.
  bool read(int slot, ::espn::PsVector<uint8_t> &out) const;

  // Main loop: the picture on the board.
  // Loads a slot and draws its first frame. False when it can't.
  bool show(int slot);
  // Frees the file and the canvas.
  void hide();
  // Draws a GIF's next frame when it is due. True when one was drawn.
  bool tick(uint32_t now);
  bool shown() const { return this->shown_slot_ >= 0; }
  int shown_slot() const { return this->shown_slot_; }
  // The upload id of the picture up now, to notice it was deleted or replaced.
  uint32_t shown_id() const { return this->shown_id_; }
  uint16_t width() const { return this->w_; }
  uint16_t height() const { return this->h_; }
  const uint8_t *pixels() const { return reinterpret_cast<const uint8_t *>(this->canvas_.data()); }
  // How long the card stays up for the picture shown.
  uint32_t page_ms() const { return this->page_ms_; }

 protected:
  bool decode_png_();
  size_t flash_free_() const;

  mutable std::mutex lock_;
  ::espn::PicInfo slots_[::espn::kPicSlots];
  uint32_t ids_{0};  // a counter for upload ids

  int shown_slot_{-1};
  uint32_t shown_id_{0};
  uint16_t w_{0}, h_{0};
  uint32_t page_ms_{0};
  ::espn::PsVector<uint8_t> file_;     // a GIF's file while it plays
  ::espn::PsVector<uint16_t> canvas_;  // what the board shows
  ::espn::GifPlayer gif_;
  bool playing_{false};
  uint32_t frame_due_ms_{0};
};

}  // namespace esphome::gameday
