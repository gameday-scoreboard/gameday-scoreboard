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

// A GIF built here, frame by frame, so a test can say exactly what each
// pixel should be. Four colors; the LZW data is every pixel as its own code
// with a clear code after every two, so the code size never grows.
struct GifFrame {
  int x, y, w, h;
  int disposal;     // 0 to 3
  int transparent;  // a color index, or -1 for none
  bool interlaced;
  std::vector<uint8_t> px;  // w * h color indexes, top row first
};

static std::vector<uint8_t> make_gif(int w, int h, const uint32_t (&palette)[4], const std::vector<GifFrame> &frames) {
  std::vector<uint8_t> out = {'G', 'I', 'F', '8', '9', 'a'};
  auto u16 = [&](int v) {
    out.push_back((uint8_t) (v & 0xFF));
    out.push_back((uint8_t) (v >> 8));
  };
  u16(w);
  u16(h);
  out.insert(out.end(), {0x81, 0, 0});  // four global colors
  for (uint32_t c : palette)
    out.insert(out.end(), {(uint8_t) (c >> 16), (uint8_t) (c >> 8), (uint8_t) c});
  for (const GifFrame &f : frames) {
    out.insert(out.end(), {0x21, 0xF9, 4, (uint8_t) ((f.disposal << 2) | (f.transparent >= 0 ? 1 : 0)), 10, 0,
                           (uint8_t) (f.transparent >= 0 ? f.transparent : 0), 0});
    out.push_back(0x2C);
    u16(f.x);
    u16(f.y);
    u16(f.w);
    u16(f.h);
    out.push_back(f.interlaced ? 0x40 : 0);
    // Rows in the order the file stores them.
    std::vector<int> rows;
    if (f.interlaced) {
      const int start[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
      for (int pass = 0; pass < 4; pass++)
        for (int r = start[pass]; r < f.h; r += step[pass])
          rows.push_back(r);
    } else {
      for (int r = 0; r < f.h; r++)
        rows.push_back(r);
    }
    std::vector<int> codes = {4};  // clear
    int run = 0;
    for (int r : rows)
      for (int x = 0; x < f.w; x++) {
        if (run == 2) {
          codes.push_back(4);
          run = 0;
        }
        codes.push_back(f.px[(size_t) r * f.w + x]);
        run++;
      }
    codes.push_back(5);  // end
    std::vector<uint8_t> data;
    uint32_t bits = 0;
    int nbits = 0;
    for (int c : codes) {
      bits |= (uint32_t) c << nbits;
      nbits += 3;
      while (nbits >= 8) {
        data.push_back((uint8_t) bits);
        bits >>= 8;
        nbits -= 8;
      }
    }
    if (nbits > 0)
      data.push_back((uint8_t) bits);
    out.push_back(2);  // the LZW minimum code size
    for (size_t i = 0; i < data.size(); i += 255) {
      size_t n = std::min<size_t>(255, data.size() - i);
      out.push_back((uint8_t) n);
      out.insert(out.end(), data.begin() + i, data.begin() + i + n);
    }
    out.push_back(0);
  }
  out.push_back(0x3B);
  return out;
}

static const uint32_t kPalette[4] = {0x000000, 0xFF0000, 0x00FF00, 0x0000FF};
static const uint16_t kRgb565[4] = {0x0000, 0xF800, 0x07E0, 0x001F};

static GifFrame solid(int x, int y, int w, int h, uint8_t color, int disposal) {
  return {x, y, w, h, disposal, -1, false, std::vector<uint8_t>((size_t) w * h, color)};
}

static void test_gif_noise() {
  // 256 colors, the code size growing to 12 bits. Its frames matched
  // Pillow's pixel for pixel when this test was written.
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
  // The canvas starts blue here, so a transparent pixel that was drawn
  // black shows.
  std::vector<uint16_t> c(40 * 30, 0x001F);
  CHECK(g.next(c.data()) == (int32_t) kGifDefaultDelayMs);  // no delay given
  CHECK(c[0] == 0x001F);                                    // transparent: left as it was
  CHECK(c[10 * 40 + 10] == 0xFFE0);                         // yellow
  CHECK(c[25 * 40 + 35] == 0x001F);
}

// A transparent pixel shows the frame before it, whichever color index is
// the transparent one; the other indexes, black among them, draw.
static void test_gif_transparency_over_a_frame() {
  for (int t : {0, 3}) {
    GifFrame over{0, 0, 8, 4, 1, t, false, {}};
    for (int i = 0; i < 32; i++)
      over.px.push_back(i % 2 ? (uint8_t) 2 : (uint8_t) t);
    over.px[1] = t == 0 ? 3 : 0;  // the one color that isn't transparent or green
    std::vector<uint8_t> d = make_gif(8, 4, kPalette, {solid(0, 0, 8, 4, 1, 1), over});
    GifPlayer g;
    CHECK(g.open(d.data(), d.size()));
    std::vector<uint16_t> c(8 * 4, 0);
    CHECK(g.next(c.data()) == 100);
    CHECK(c[0] == 0xF800 && c[31] == 0xF800);
    CHECK(g.next(c.data()) == 100);
    int red = 0, green = 0;
    for (int i = 0; i < 32; i++) {
      red += c[i] == 0xF800 && i % 2 == 0;
      green += c[i] == 0x07E0 && i % 2 == 1;
    }
    CHECK(red == 16);
    CHECK(green == 15);
    CHECK(c[1] == kRgb565[t == 0 ? 3 : 0]);
  }
}

// What a frame's disposal leaves for the next one: 1 keeps it, 2 clears its
// area to black, 3 puts back what was under it. Only its own area changes.
static void test_gif_disposal_modes() {
  const uint16_t want[4] = {0x07E0, 0x07E0, 0x0000, 0xF800};
  for (int disposal = 1; disposal <= 3; disposal++) {
    std::vector<uint8_t> d =
        make_gif(8, 4, kPalette, {solid(0, 0, 8, 4, 1, 1), solid(2, 1, 3, 2, 2, disposal), solid(7, 3, 1, 1, 3, 1)});
    GifPlayer g;
    CHECK(g.open(d.data(), d.size()));
    std::vector<uint16_t> c(8 * 4, 0);
    CHECK(g.next(c.data()) == 100);
    CHECK(g.next(c.data()) == 100);
    CHECK(c[1 * 8 + 2] == 0x07E0 && c[2 * 8 + 4] == 0x07E0);
    CHECK(g.next(c.data()) == 100);
    for (int y = 0; y < 4; y++)
      for (int x = 0; x < 8; x++) {
        bool box = x >= 2 && x < 5 && y >= 1 && y < 3;
        uint16_t expect = box ? want[disposal] : (x == 7 && y == 3) ? 0x001F : 0xF800;
        if (c[(size_t) y * 8 + x] != expect) {
          printf("FAIL disposal %d: pixel %d,%d is %04x, expected %04x\n", disposal, x, y, c[(size_t) y * 8 + x],
                 expect);
          failures++;
        }
        checks++;
      }
  }
}

// An interlaced frame's rows land where they belong, at every height the
// four passes can end on, and in a frame that starts below the top.
static void test_gif_interlaced() {
  for (int h = 1; h <= 17; h++) {
    // Each row's three pixels spell its number in base 4, so no two rows match.
    std::vector<uint8_t> px;
    for (int r = 0; r < h; r++)
      for (int x = 0; x < 3; x++)
        px.push_back((uint8_t) ((r >> (2 * x)) & 3));
    for (int y0 : {0, 3}) {
      GifFrame f{0, y0, 3, h, 0, -1, true, px};
      std::vector<uint8_t> d = make_gif(3, h + y0, kPalette, {f});
      GifPlayer g;
      CHECK(g.open(d.data(), d.size()));
      std::vector<uint16_t> c((size_t) 3 * (h + y0), 0);
      CHECK(g.next(c.data()) == 100);
      bool same = true;
      for (int r = 0; r < h; r++)
        for (int x = 0; x < 3; x++)
          same = same && c[(size_t) (y0 + r) * 3 + x] == kRgb565[px[(size_t) r * 3 + x]];
      if (!same)
        printf("FAIL interlaced frame %d rows high at row %d: rows out of place\n", h, y0);
      failures += !same;
      checks++;
    }
  }
}

static void test_gif_trailing_control() {
  // One white pixel with no control block of its own, and a control block
  // after it (a 5 s delay, color 0 transparent) that belongs to no frame.
  const uint8_t gif[] = {'G',  'I',  'F',  '8',  '9',  'a',  1,    0,    1,    0,    0x80, 0,    0,
                         0xFF, 0xFF, 0xFF, 0,    0,    0,    0x2C, 0,    0,    0,    0,    1,    0,
                         1,    0,    0,    2,    2,    0x44, 0x01, 0,    0x21, 0xF9, 4,    1,    0xF4,
                         1,    0,    0,    0x3B};
  GifPlayer g;
  CHECK(g.open(gif, sizeof gif));
  uint16_t c = 0;
  CHECK(g.next(&c) == (int32_t) kGifDefaultDelayMs);
  CHECK(c == 0xFFFF);
  // Round again: the stray block doesn't carry over to the first frame.
  c = 0;
  CHECK(g.next(&c) == (int32_t) kGifDefaultDelayMs);
  CHECK(g.looped());
  CHECK(c == 0xFFFF);
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
  test_gif_transparency_over_a_frame();
  test_gif_disposal_modes();
  test_gif_interlaced();
  test_gif_trailing_control();
  test_gif_broken();
  test_check();
  test_turns();
  printf("%d/%d picture checks passed\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}
