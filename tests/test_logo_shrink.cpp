// Host tests for the logo shrink (components/gameday/logo_shrink.h). The
// expected LEDs are worked out by hand from the rule in that file's comment.
// Build with `make` in tests/.
#include <cstdio>
#include <vector>

#include "logo_shrink.h"

using namespace espn::shrink;

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

static const uint32_t NAVY = 0xFF000080;
static const uint32_t WHITE = 0xFFFFFFFF;

// An n x n source in the decoder's byte order (blue, green, red, alpha).
struct Source {
  int n;
  std::vector<uint8_t> px;
  explicit Source(int n, uint32_t fill) : n(n), px((size_t) n * n * 4) {
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++)
        set(x, y, fill);
  }
  void set(int x, int y, uint32_t c) {
    uint8_t *p = &px[((size_t) y * n + x) * 4];
    p[0] = c & 0xFF;
    p[1] = (c >> 8) & 0xFF;
    p[2] = (c >> 16) & 0xFF;
    p[3] = c >> 24;
  }
  std::vector<uint32_t> shrink() const {
    std::vector<uint32_t> scratch(scratch_words(n));
    std::vector<uint32_t> out(OUT * OUT);
    rare_vote(px.data(), n, scratch.data(), out.data());
    return out;
  }
};

static void test_png_size() {
  const uint8_t head[24] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                            0,    0,   0,   128, 0,    0,    0,    128};
  int w = 0, h = 0;
  CHECK(png_size(head, sizeof(head), w, h) && w == 128 && h == 128);
  CHECK(fits(w, h));
  CHECK(!png_size(head, 23, w, h));  // cut off
  uint8_t bad[24];
  for (int i = 0; i < 24; i++)
    bad[i] = head[i];
  bad[1] = 'X';
  CHECK(!png_size(bad, sizeof(bad), w, h));  // not a PNG
  bad[1] = 'P';
  bad[12] = 'X';
  CHECK(!png_size(bad, sizeof(bad), w, h));  // no header chunk first
  CHECK(!png_size(nullptr, 0, w, h));
}

static void test_fits() {
  CHECK(fits(64, 64));
  CHECK(fits(96, 96));
  CHECK(fits(128, 128));
  CHECK(!fits(256, 256));  // more PSRAM than a logo is worth
  CHECK(!fits(500, 500));  // ESPN's full size
  CHECK(!fits(100, 100));  // not a multiple of 32
  CHECK(!fits(128, 64));   // not square
  CHECK(!fits(32, 32));    // nothing to shrink
}

// A 2 px white line down a navy field, 128 px. At x = 40 and 41 it fills half
// of LED column 10's blocks. For an LED in column 10 (away from the edges):
// white covers 0.5 of its block and 0.5 of 3 of the 9 LEDs around it, so it
// scores 0.5 - 0.35 * 1.5 / 9 = 0.44; navy covers 0.5 and 7.5 of 9, scoring
// 0.5 - 0.35 * 7.5 / 9 = 0.21. White wins. Columns 9 and 11 have no white
// pixel, so they stay navy. Keeping one pixel in four would have kept x = 40
// and 41's first pixel only on every other LED, as the decoder does from 64 px.
static void test_thin_line_survives() {
  Source s(128, NAVY);
  for (int y = 0; y < 128; y++) {
    s.set(40, y, WHITE);
    s.set(41, y, WHITE);
  }
  auto out = s.shrink();
  int col10 = 0, col9 = 0, col11 = 0;
  for (int y = 0; y < OUT; y++) {
    col10 += out[y * OUT + 10] == WHITE;
    col9 += out[y * OUT + 9] == NAVY;
    col11 += out[y * OUT + 11] == NAVY;
  }
  CHECK(col10 == 32);
  CHECK(col9 == 32);
  CHECK(col11 == 32);
}

// A 1 px line covers a quarter of each block: white scores
// 0.25 - 0.35 * 0.75 / 9 = 0.22 and navy 0.75 - 0.35 * 8.25 / 9 = 0.43, so a
// line thinner than a third of an LED gives way to the field. A white square
// in the corner keeps white in the palette.
static void test_hairline_gives_way() {
  Source s(128, NAVY);
  for (int y = 0; y < 128; y++)
    s.set(40, y, WHITE);
  for (int y = 96; y < 128; y++)
    for (int x = 96; x < 128; x++)
      s.set(x, y, WHITE);
  auto out = s.shrink();
  int navy = 0;
  for (int y = 0; y < 24; y++)
    navy += out[y * OUT + 10] == NAVY;
  CHECK(navy == 24);
  CHECK(out[31 * OUT + 31] == WHITE);
}

// Antialiased edge colors are not in the palette: a gray between white and
// navy on 0.4% of the pixels (under 0.8%) snaps to its nearest kept color, so
// no LED shows it.
static void test_blend_is_not_a_color() {
  Source s(128, NAVY);
  for (int y = 0; y < 128; y++)
    for (int x = 64; x < 128; x++)
      s.set(x, y, WHITE);
  const uint32_t GRAY = 0xFF8080C0;
  for (int y = 0; y < 64; y++)
    s.set(63, y, GRAY);  // 64 of 16384 pixels
  auto out = s.shrink();
  int other = 0;
  for (uint32_t p : out)
    other += p != NAVY && p != WHITE;
  CHECK(other == 0);
  CHECK(out[0] == NAVY);
  CHECK(out[31] == WHITE);
}

// See-through pixels: black that wins over a mostly clear block stays clear;
// a solid color over a clear field is opaque.
static void test_clear_field() {
  Source s(64, 0x00000000);
  for (int y = 16; y < 48; y++)
    for (int x = 16; x < 48; x++)
      s.set(x, y, WHITE);
  auto out = s.shrink();
  CHECK((out[0] >> 24) == 0);           // corner LED, fully clear
  CHECK(out[16 * OUT + 16] == WHITE);  // middle LED
  CHECK((out[7 * OUT + 7] >> 24) == 0);
  CHECK(out[8 * OUT + 8] == WHITE);  // first LED inside the square (pixels 16, 17)
}

// A part-clear pixel counts as its color over black. Red at alpha 192 is
// (192, 0, 0), nearer red than black, so its LED is opaque red. At alpha 64
// it is (64, 0, 0), nearer black, and with most of the block under alpha 128
// the LED is clear.
static void test_part_clear_is_over_black() {
  Source s(64, 0xFFFF0000);
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 16; x++)
      s.set(x, y, 0xC0FF0000);
    for (int x = 16; x < 32; x++)
      s.set(x, y, 0x40FF0000);
  }
  auto out = s.shrink();
  CHECK(out[0] == 0xFFFF0000);
  CHECK((out[10] >> 24) == 0);
  CHECK(out[31] == 0xFFFF0000);
}

int main() {
  test_png_size();
  test_fits();
  test_thin_line_survives();
  test_hairline_gives_way();
  test_blend_is_not_a_color();
  test_clear_field();
  test_part_clear_is_over_black();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
