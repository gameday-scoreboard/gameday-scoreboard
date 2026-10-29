// The picture card: the owner's own PNG or GIF, between games. Up to four
// pictures are kept in the settings flash; the card shows the next one each
// turn, a GIF animating. Nothing shows until one is uploaded. The rules,
// pure, with no ESPHome includes, so the host tests and the fuzzer run them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "gif.h"

namespace espn {

constexpr int kPicSlots = 4;
constexpr size_t kPicMaxBytes = 64 * 1024;
// A picture never leaves less than this free in the settings flash, so the
// panel's own settings always have room.
constexpr size_t kPicFlashReserve = 128 * 1024;
constexpr int kPicMaxW = 128;
constexpr int kPicMaxH = 64;
// A GIF longer than this is refused: its frames are decoded once at upload
// to check it, on the web server's task.
constexpr uint32_t kPicMaxFrames = 300;
constexpr int kPicEveryMin = 3;               // minutes between the card's turns
constexpr uint32_t kPicPageMs = 10 * 1000;    // a picture's time on the board
constexpr uint32_t kPicMaxPageMs = 30 * 1000;  // a GIF plays through once, up to this

enum class PicKind : uint8_t { NONE = 0, PNG = 1, GIF = 2 };

// What the panel keeps about a stored picture, in its own small blob.
struct PicInfo {
  uint8_t kind{0};  // PicKind
  uint8_t spare{0};
  uint16_t w{0}, h{0};
  uint16_t frames{0};
  uint32_t bytes{0};
  uint32_t loop_ms{0};  // one pass of a GIF, 0 for a still picture
  uint32_t id{0};       // changes with each upload, for the apps' caches
};

struct PicCheck {
  PicInfo info;
  std::string error;  // empty when the file is good, else said to the owner
};

inline const char *pic_kind_name(PicKind k) {
  switch (k) {
    case PicKind::PNG:
      return "png";
    case PicKind::GIF:
      return "gif";
    default:
      return "";
  }
}

inline const char *pic_mime(PicKind k) { return k == PicKind::GIF ? "image/gif" : "image/png"; }

inline PicKind pic_sniff(const uint8_t *data, size_t size) {
  static const uint8_t kPng[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (size >= 8 && std::memcmp(data, kPng, 8) == 0)
    return PicKind::PNG;
  if (size >= 6 && (std::memcmp(data, "GIF87a", 6) == 0 || std::memcmp(data, "GIF89a", 6) == 0))
    return PicKind::GIF;
  return PicKind::NONE;
}

inline std::string pic_too_big_text() { return "Pictures can be up to 128 by 64 pixels."; }

// Checks an upload before it is kept. A PNG's size comes from its header
// (the panel decodes it when it shows); a GIF is played through once, every
// frame decoded, so a broken one never reaches the board.
inline PicCheck pic_check(const uint8_t *data, size_t size) {
  PicCheck out;
  if (size == 0) {
    out.error = "That file is empty.";
    return out;
  }
  if (size > kPicMaxBytes) {
    out.error = "That file is too big; a picture can be up to 64 KB.";
    return out;
  }
  const PicKind kind = pic_sniff(data, size);
  out.info.kind = (uint8_t) kind;
  out.info.bytes = (uint32_t) size;
  if (kind == PicKind::PNG) {
    // The IHDR chunk comes first: length, "IHDR", width, height.
    if (size < 24 || std::memcmp(data + 12, "IHDR", 4) != 0) {
      out.error = "That PNG file is damaged.";
      return out;
    }
    const uint32_t w = ((uint32_t) data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    const uint32_t h = ((uint32_t) data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    if (w == 0 || h == 0) {
      out.error = "That PNG file is damaged.";
      return out;
    }
    if (w > (uint32_t) kPicMaxW || h > (uint32_t) kPicMaxH) {
      out.error = pic_too_big_text();
      return out;
    }
    out.info.w = (uint16_t) w;
    out.info.h = (uint16_t) h;
    out.info.frames = 1;
    return out;
  }
  if (kind != PicKind::GIF) {
    out.error = "Pictures can be PNG or GIF files.";
    return out;
  }
  GifPlayer gif;
  if (!gif.open(data, size)) {
    out.error = "That GIF file is damaged.";
    return out;
  }
  if (gif.width() > kPicMaxW || gif.height() > kPicMaxH) {
    out.error = pic_too_big_text();
    return out;
  }
  PsVector<uint16_t> canvas((size_t) gif.width() * gif.height());
  uint32_t frames = 0, loop_ms = 0;
  while (true) {
    const int32_t ms = gif.next(canvas.data());
    if (ms < 0) {
      out.error = "That GIF file is damaged.";
      return out;
    }
    if (gif.looped())
      break;
    frames++;
    loop_ms += (uint32_t) ms;
    if (frames > kPicMaxFrames) {
      out.error = "That GIF has too many frames; it can have up to 300.";
      return out;
    }
  }
  out.info.w = gif.width();
  out.info.h = gif.height();
  out.info.frames = (uint16_t) frames;
  out.info.loop_ms = loop_ms;
  return out;
}

// How long the card stays up: a still picture its page time, a GIF one
// pass, at least the page time and at most kPicMaxPageMs.
inline uint32_t pic_page_ms(const PicInfo &p) {
  if (p.loop_ms == 0)
    return kPicPageMs;
  uint32_t ms = p.loop_ms;
  // Short loops play whole passes until the page time is up.
  while (ms < kPicPageMs)
    ms += p.loop_ms;
  return ms > kPicMaxPageMs ? kPicMaxPageMs : ms;
}

// The scale the board draws a picture at, in LVGL's 256ths: half size when
// it is wider than the display (a 128-wide picture on one 64x64 panel), so
// all of it shows.
inline int pic_scale(int pic_w, int display_w) { return pic_w > display_w ? 128 : 256; }

// The next stored picture after slot `after` (-1 for the first), or -1.
inline int pic_next_slot(const PicInfo *slots, int after) {
  for (int i = 1; i <= kPicSlots; i++) {
    const int s = ((after < 0 ? -1 : after) + i + kPicSlots) % kPicSlots;
    if (slots[s].kind != (uint8_t) PicKind::NONE)
      return s;
  }
  return -1;
}

// The slot an upload goes to: the first free one, or -1 when all are taken.
inline int pic_free_slot(const PicInfo *slots) {
  for (int i = 0; i < kPicSlots; i++)
    if (slots[i].kind == (uint8_t) PicKind::NONE)
      return i;
  return -1;
}

// Whether a file of `bytes` fits in the settings flash, given what is free
// there now (the slot's old picture, if it replaces one, already counted
// as free). NVS writes the new copy before it erases the old, so the room
// is checked against what is free now.
inline bool pic_fits(size_t bytes, size_t flash_free) {
  // Each 32-byte NVS entry holds 32 bytes of a blob; a blob's chunk headers
  // and page ends take about one entry in a hundred more.
  const size_t need = bytes + bytes / 100 + 256;
  return flash_free >= need && flash_free - need >= kPicFlashReserve;
}

// The biggest file pic_fits() takes, up to kPicMaxBytes; 0 for none.
inline size_t pic_room(size_t flash_free) {
  if (flash_free <= kPicFlashReserve + 256)
    return 0;
  // Down from what fits with no chunk headers at all: at most free / 100 steps.
  size_t bytes = flash_free - kPicFlashReserve - 256;
  if (bytes > kPicMaxBytes)
    bytes = kPicMaxBytes;
  while (bytes > 0 && !pic_fits(bytes, flash_free))
    bytes--;
  return bytes;
}

}  // namespace espn
