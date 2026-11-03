// Host tests for the picture card: the GIF player (components/gameday/gif.h)
// and the upload checks and turn rules (picture.h), against the files in
// fixtures/pictures/. Build with `make` in tests/.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "picture.h"

using namespace espn;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(cond)) {                                                             \
      failures++;                                                              \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
    }                                                                          \
  } while (0)

static std::vector<uint8_t> load(const char *name) {
  std::ifstream f(std::string("fixtures/pictures/") + name, std::ios::binary);
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (d.empty())
    printf("FAIL missing fixture %s\n", name);
  return d;
}

static uint32_t fnv(const std::vector<uint16_t> &px) {
  uint32_t h = 2166136261u;
  for (uint16_t v : px) {
    h = (h ^ (v & 0xFF)) * 16777619u;
    h = (h ^ (v >> 8)) * 16777619u;
  }
  return h;
}

// The red box's left edge on a frame, or -1 when no red pixel or more
// than one 20x20 box's worth.
static int red_box_x(const std::vector<uint16_t> &px, int w) {
  int n = 0, left = 1 << 30;
  for (size_t i = 0; i < px.size(); i++)
    if (px[i] == 0xF800) {
      n++;
      left = std::min(left, (int) (i % w));
    }
  return n == 400 ? left : -1;
}

static void test_gif_noise() {
  // Interlaced, 256 colors, the code size growing to 12 bits. Its frames
  // matched Pillow's pixel for pixel when this test was written.
  std::vector<uint8_t> d = load("noise.gif");
  GifPlayer g;
  CHECK(g.open(d.data(), d.size()));
  CHECK(g.width() == 64 && g.height() == 32);
  std::vector<uint16_t> c(64 * 32);
  CHECK(g.next(c.data()) == 50);
  CHECK(fnv(c) == 0xb0be30d8u);
  CHECK(g.next(c.data()) == 50);
  CHECK(fnv(c) == 0xd3b57c11u);
  CHECK(!g.looped());
  // Starts over at the first frame.
  CHECK(g.next(c.data()) == 50);
  CHECK(g.looped());
  CHECK(fnv(c) == 0xb0be30d8u);
}

static void test_gif_disposal() {
  // A box stepping 20 px right each frame, the file's frames cropped to
  // what changed. Restoring to the background (2) or to the previous frame
  // (3) leaves one box on the board either way.
  for (const char *name : {"box_d2.gif", "box_d3.gif"}) {
    std::vector<uint8_t> d = load(name);
    GifPlayer g;
    CHECK(g.open(d.data(), d.size()));
    CHECK(g.width() == 128 && g.height() == 64);
    std::vector<uint16_t> c(128 * 64, 0);
    for (int f = 0; f < 4; f++) {
      CHECK(g.next(c.data()) == 80);
      CHECK(red_box_x(c, 128) == 10 + 20 * f);
    }
    CHECK(g.next(c.data()) == 80);
    CHECK(g.looped());
    CHECK(red_box_x(c, 128) == 10);
  }
}

static void test_gif_transparent_still() {
  std::vector<uint8_t> d = load("still.gif");
  GifPlayer g;
  CHECK(g.open(d.data(), d.size()));
  std::vector<uint16_t> c(40 * 30, 0);
  CHECK(g.next(c.data()) == (int32_t) kGifDefaultDelayMs);  // no delay given
  CHECK(c[0] == 0);                                         // transparent: black
  CHECK(c[10 * 40 + 10] == 0xFFE0);                         // yellow
  CHECK(c[25 * 40 + 35] == 0);
}

static void test_gif_broken() {
  std::vector<uint8_t> d = load("box_d2.gif");
  GifPlayer g;
  std::vector<uint16_t> c(128 * 64);
  // Cut in the middle of the first frame's data: -1, every time.
  std::vector<uint8_t> cut(d.begin(), d.begin() + 60);
  CHECK(g.open(cut.data(), cut.size()));
  CHECK(g.next(c.data()) == -1);
  CHECK(g.next(c.data()) == -1);
  // Without its trailer the file ends after its last whole frame.
  std::vector<uint8_t> no_trailer(d.begin(), d.end() - 1);
  CHECK(no_trailer.size() + 1 == d.size() && d.back() == 0x3B);
  PicCheck pc = pic_check(no_trailer.data(), no_trailer.size());
  CHECK(pc.error.empty());
  CHECK(pc.info.frames == 4);
  // Not a GIF at all.
  const uint8_t junk[16] = {'G', 'I', 'F', '8', '8', 'a'};
  CHECK(!g.open(junk, sizeof junk));
  // A header with no frames.
  const uint8_t empty[14] = {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0, 0, 0, 0x3B};
  CHECK(g.open(empty, sizeof empty));
  CHECK(g.next(c.data()) == -1);
  CHECK(!pic_check(empty, sizeof empty).error.empty());
}

static void test_check() {
  std::vector<uint8_t> d = load("box_d3.gif");
  PicCheck c = pic_check(d.data(), d.size());
  CHECK(c.error.empty());
  CHECK(c.info.kind == (uint8_t) PicKind::GIF);
  CHECK(c.info.w == 128 && c.info.h == 64);
  CHECK(c.info.frames == 4);
  CHECK(c.info.loop_ms == 320);
  CHECK(c.info.bytes == d.size());

  d = load("blue.png");
  c = pic_check(d.data(), d.size());
  CHECK(c.error.empty());
  CHECK(c.info.kind == (uint8_t) PicKind::PNG);
  CHECK(c.info.w == 64 && c.info.h == 32);
  CHECK(c.info.frames == 1 && c.info.loop_ms == 0);

  d = load("tall.png");
  CHECK(pic_check(d.data(), d.size()).error == "Pictures can be up to 128 by 64 pixels.");
  d = load("wide.gif");
  CHECK(pic_check(d.data(), d.size()).error == "Pictures can be up to 128 by 64 pixels.");

  const uint8_t jpeg[8] = {0xFF, 0xD8, 0xFF, 0xE0, 0, 0x10, 'J', 'F'};
  CHECK(pic_check(jpeg, sizeof jpeg).error == "Pictures can be PNG or GIF files.");
  CHECK(pic_check(jpeg, 0).error == "That file is empty.");
  std::vector<uint8_t> big(kPicMaxBytes + 1, 0);
  CHECK(pic_check(big.data(), big.size()).error == "That file is too big; a picture can be up to 64 KB.");
  d = load("blue.png");
  d[13] = 'X';  // IHDR spelled wrong
  CHECK(pic_check(d.data(), d.size()).error == "That PNG file is damaged.");
}

static void test_turns() {
  PicInfo slots[kPicSlots];
  CHECK(pic_next_slot(slots, -1) == -1);
  CHECK(pic_free_slot(slots) == 0);
  slots[1].kind = (uint8_t) PicKind::PNG;
  slots[3].kind = (uint8_t) PicKind::GIF;
  CHECK(pic_next_slot(slots, -1) == 1);
  CHECK(pic_next_slot(slots, 1) == 3);
  CHECK(pic_next_slot(slots, 3) == 1);
  CHECK(pic_next_slot(slots, 2) == 3);
  CHECK(pic_free_slot(slots) == 0);
  slots[0].kind = slots[2].kind = (uint8_t) PicKind::PNG;
  CHECK(pic_free_slot(slots) == -1);
  slots[1].kind = slots[2].kind = slots[3].kind = 0;
  CHECK(pic_next_slot(slots, 0) == 0);  // one picture: it every turn

  PicInfo still;
  CHECK(pic_page_ms(still) == kPicPageMs);
  PicInfo gif;
  gif.loop_ms = 320;  // whole passes up to the page time
  CHECK(pic_page_ms(gif) == 10240);
  gif.loop_ms = 12000;
  CHECK(pic_page_ms(gif) == 12000);
  gif.loop_ms = 90000;
  CHECK(pic_page_ms(gif) == kPicMaxPageMs);

  CHECK(pic_scale(128, 64) == 128);
  CHECK(pic_scale(64, 64) == 256);
  CHECK(pic_scale(128, 128) == 256);

  CHECK(pic_fits(64 * 1024, 300 * 1024));
  CHECK(!pic_fits(64 * 1024, 190 * 1024));  // would leave under 128 KB
  CHECK(pic_fits(1000, 130 * 1024));
  CHECK(!pic_fits(1000, 1000));
  CHECK(pic_room(1000) == 0);
  CHECK(pic_room(kPicFlashReserve + 256) == 0);
  CHECK(pic_room(400 * 1024) == kPicMaxBytes);
  for (size_t free : {(size_t) 140 * 1024, (size_t) 150 * 1024 + 7, (size_t) 190 * 1024}) {
    const size_t room = pic_room(free);
    CHECK(room > 0 && room < kPicMaxBytes);
    CHECK(pic_fits(room, free));
    CHECK(!pic_fits(room + 1, free));
  }
}

int main() {
  test_gif_noise();
  test_gif_disposal();
  test_gif_transparent_still();
  test_gif_broken();
  test_check();
  test_turns();
  printf("%d/%d picture checks passed\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}
