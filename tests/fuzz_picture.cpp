// Fuzz target: POST /gameday/picture (picture.h, gif.h). The input is an
// uploaded file. Checks what the card relies on: an accepted file fits the
// board, and a GIF that passed the check plays through without a broken
// frame, so the board never shows one.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "picture.h"

using namespace espn;

namespace {

void require(bool ok, const char *what) {
  if (ok)
    return;
  fprintf(stderr, "pic_check: %s\n", what);
  abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // The copy makes ASan catch a read past the file's end.
  std::vector<uint8_t> file(data, data + size);
  PicCheck c = pic_check(file.data(), file.size());
  if (!c.error.empty())
    return 0;
  require(c.info.w >= 1 && c.info.w <= kPicMaxW && c.info.h >= 1 && c.info.h <= kPicMaxH, "size out of range");
  require(c.info.bytes == size, "byte count");
  if (c.info.kind != (uint8_t) PicKind::GIF)
    return 0;
  require(c.info.frames >= 1 && c.info.frames <= kPicMaxFrames, "frame count");
  GifPlayer g;
  require(g.open(file.data(), file.size()), "reopen");
  std::vector<uint16_t> canvas((size_t) g.width() * g.height());
  for (uint32_t i = 0; i < 2u * c.info.frames; i++)
    require(g.next(canvas.data()) > 0, "a checked GIF failed to play");
  return 0;
}
