#include "picture_card.h"

#include <cstring>

#include <esp_random.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "pngle.h"

namespace esphome::gameday {

static const char *const TAG = "gameday.picture";
static const char *const NVS_NAMESPACE = "gd_pics";

using ::espn::kPicSlots;
using ::espn::PicInfo;
using ::espn::PicKind;

namespace {

void info_key(int slot, char key[4]) { snprintf(key, 4, "i%d", slot); }
void file_key(int slot, char key[4]) { snprintf(key, 4, "p%d", slot); }

struct PngTarget {
  uint16_t *canvas;
  uint32_t w, h;
  uint32_t drawn;  // pixels drawn; an interlaced file draws some more than once
};

// Each pixel over black, by its alpha.
void png_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t rgba[4]) {
  auto *t = static_cast<PngTarget *>(pngle_get_user_data(pngle));
  const uint32_t a = rgba[3];
  const uint32_t r = rgba[0] * a / 255, g = rgba[1] * a / 255, b = rgba[2] * a / 255;
  const uint16_t c = (uint16_t) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  for (uint32_t yy = y; yy < y + h && yy < t->h; yy++)
    for (uint32_t xx = x; xx < x + w && xx < t->w; xx++) {
      t->canvas[yy * t->w + xx] = c;
      t->drawn++;
    }
}

// A whole PNG of w x h onto canvas. False for a damaged file or another size.
bool decode_png(const uint8_t *data, size_t size, uint16_t *canvas, uint32_t w, uint32_t h) {
  RAMAllocator<pngle_t> allocator;
  pngle_t *pngle = allocator.allocate(1, PNGLE_T_SIZE);
  if (pngle == nullptr)
    return false;
  memset(pngle, 0, PNGLE_T_SIZE);
  pngle_reset(pngle);
  PngTarget target{canvas, w, h, 0};
  pngle_set_user_data(pngle, &target);
  pngle_set_draw_callback(pngle, png_draw);
  // pngle_feed takes a non-const pointer but only reads it.
  const int fed = pngle_feed(pngle, const_cast<uint8_t *>(data), size);
  if (fed < 0)
    ESP_LOGW(TAG, "PNG: %s", pngle_error(pngle));
  // Every pixel drawn: a file cut short decodes its top rows and stops.
  const bool ok = fed >= 0 && target.drawn >= w * h && pngle_get_width(pngle) == w && pngle_get_height(pngle) == h;
  pngle_reset(pngle);
  allocator.deallocate(pngle, PNGLE_T_SIZE);
  return ok;
}

}  // namespace

void PictureCard::setup() {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    return;  // nothing uploaded yet
  for (int s = 0; s < kPicSlots; s++) {
    char ik[4], fk[4];
    info_key(s, ik);
    file_key(s, fk);
    PicInfo info;
    size_t len = sizeof(info);
    size_t file_len = 0;
    if (nvs_get_blob(h, ik, &info, &len) != ESP_OK || len != sizeof(info))
      continue;
    // The info is written after the file, so a power cut between the two
    // leaves a file with no info, never the other way round.
    if (nvs_get_blob(h, fk, nullptr, &file_len) != ESP_OK || file_len != info.bytes ||
        (info.kind != (uint8_t) PicKind::PNG && info.kind != (uint8_t) PicKind::GIF))
      continue;
    this->slots_[s] = info;
    ESP_LOGI(TAG, "Picture %d: %s %ux%u, %u frames, %u bytes", s + 1, ::espn::pic_kind_name((PicKind) info.kind),
             info.w, info.h, info.frames, (unsigned) info.bytes);
  }
  nvs_close(h);
}

void PictureCard::slots(PicInfo out[kPicSlots]) const {
  std::lock_guard<std::mutex> guard(this->lock_);
  for (int s = 0; s < kPicSlots; s++)
    out[s] = this->slots_[s];
}

size_t PictureCard::flash_free_() const {
  nvs_stats_t st;
  if (nvs_get_stats(nullptr, &st) != ESP_OK)
    return 0;
  return st.available_entries * 32;
}

size_t PictureCard::room() const {
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    if (::espn::pic_free_slot(this->slots_) < 0)
      return 0;
  }
  return ::espn::pic_room(this->flash_free_());
}

std::string PictureCard::add(const uint8_t *data, size_t size, int &slot) {
  ::espn::PicCheck c = ::espn::pic_check(data, size);
  if (!c.error.empty())
    return c.error;
  if (c.info.kind == (uint8_t) PicKind::PNG) {
    // pic_check() reads only a PNG's header; decode it once, as a GIF is
    // played through, so a damaged one never reaches the board.
    ::espn::PsVector<uint16_t> canvas((size_t) c.info.w * c.info.h);
    if (!decode_png(data, size, canvas.data(), c.info.w, c.info.h))
      return "That PNG file is damaged.";
  }
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    slot = ::espn::pic_free_slot(this->slots_);
  }
  if (slot < 0)
    return "The panel keeps 4 pictures. Delete one to add another.";
  const size_t free = this->flash_free_();
  if (!::espn::pic_fits(size, free)) {
    ESP_LOGW(TAG, "No room for %u bytes: %u bytes free in the settings flash", (unsigned) size, (unsigned) free);
    return "The panel has no room for that picture. Try a smaller file, or delete a picture.";
  }
  c.info.id = esp_random() | 1;
  nvs_handle_t h;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
  if (err == ESP_OK) {
    char ik[4], fk[4];
    info_key(slot, ik);
    file_key(slot, fk);
    err = nvs_set_blob(h, fk, data, size);
    if (err == ESP_OK)
      err = nvs_set_blob(h, ik, &c.info, sizeof(c.info));
    if (err != ESP_OK)
      nvs_erase_key(h, fk);
    nvs_commit(h);
    nvs_close(h);
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Saving picture %d failed: %s", slot + 1, esp_err_to_name(err));
    return "The panel couldn't save that picture.";
  }
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    this->slots_[slot] = c.info;
  }
  ESP_LOGI(TAG, "Picture %d saved: %s %ux%u, %u frames, %u bytes; %u bytes free in the settings flash", slot + 1,
           ::espn::pic_kind_name((PicKind) c.info.kind), c.info.w, c.info.h, c.info.frames, (unsigned) size,
           (unsigned) this->flash_free_());
  return "";
}

bool PictureCard::remove(int slot) {
  if (slot < 0 || slot >= kPicSlots)
    return false;
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    if (this->slots_[slot].kind == (uint8_t) PicKind::NONE)
      return false;
    this->slots_[slot] = PicInfo{};
  }
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
    char ik[4], fk[4];
    info_key(slot, ik);
    file_key(slot, fk);
    nvs_erase_key(h, ik);
    nvs_erase_key(h, fk);
    nvs_commit(h);
    nvs_close(h);
  }
  ESP_LOGI(TAG, "Picture %d deleted", slot + 1);
  return true;
}

bool PictureCard::read(int slot, ::espn::PsVector<uint8_t> &out) const {
  if (slot < 0 || slot >= kPicSlots)
    return false;
  size_t bytes;
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    bytes = this->slots_[slot].bytes;
    if (this->slots_[slot].kind == (uint8_t) PicKind::NONE)
      return false;
  }
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    return false;
  char fk[4];
  file_key(slot, fk);
  out.resize(bytes);
  size_t len = bytes;
  esp_err_t err = nvs_get_blob(h, fk, out.data(), &len);
  nvs_close(h);
  if (err != ESP_OK || len != bytes) {
    ::espn::PsVector<uint8_t>().swap(out);
    return false;
  }
  return true;
}

bool PictureCard::show(int slot) {
  this->hide();
  PicInfo info;
  {
    std::lock_guard<std::mutex> guard(this->lock_);
    if (slot < 0 || slot >= kPicSlots)
      return false;
    info = this->slots_[slot];
  }
  if (info.kind == (uint8_t) PicKind::NONE || !this->read(slot, this->file_))
    return false;
  this->w_ = info.w;
  this->h_ = info.h;
  this->canvas_.assign((size_t) info.w * info.h, 0);
  bool ok;
  if (info.kind == (uint8_t) PicKind::GIF) {
    ok = this->gif_.open(this->file_.data(), this->file_.size()) && this->gif_.width() == info.w &&
         this->gif_.height() == info.h;
    const int32_t ms = ok ? this->gif_.next(this->canvas_.data()) : -1;
    ok = ms > 0;
    this->playing_ = ok && info.frames > 1;
    this->frame_due_ms_ = millis() + (uint32_t) ms;
  } else {
    ok = decode_png(this->file_.data(), this->file_.size(), this->canvas_.data(), info.w, info.h);
  }
  if (!this->playing_) {
    // A still picture needs only its canvas.
    this->gif_ = ::espn::GifPlayer{};
    ::espn::PsVector<uint8_t>().swap(this->file_);
  }
  if (!ok) {
    ESP_LOGW(TAG, "Picture %d didn't decode", slot + 1);
    this->hide();
    return false;
  }
  this->shown_slot_ = slot;
  this->shown_id_ = info.id;
  this->page_ms_ = ::espn::pic_page_ms(info);
  return true;
}

void PictureCard::hide() {
  this->shown_slot_ = -1;
  this->shown_id_ = 0;
  this->playing_ = false;
  this->gif_ = ::espn::GifPlayer{};
  ::espn::PsVector<uint8_t>().swap(this->file_);
  ::espn::PsVector<uint16_t>().swap(this->canvas_);
}

bool PictureCard::tick(uint32_t now) {
  if (!this->playing_ || (int32_t) (now - this->frame_due_ms_) < 0)
    return false;
  const int32_t ms = this->gif_.next(this->canvas_.data());
  if (ms < 0) {
    this->playing_ = false;  // checked at upload, so not expected; the frame on hand stays
    return false;
  }
  this->frame_due_ms_ += (uint32_t) ms;
  // Behind by more than a second (the loop was held up): pick up from now.
  if ((int32_t) (now - this->frame_due_ms_) > 1000)
    this->frame_due_ms_ = now + (uint32_t) ms;
  return true;
}

}  // namespace esphome::gameday
