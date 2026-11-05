// Host tests for brand packs on the bar build (components/gameday/brand.h):
// reading a pack file, the plain defaults a missing or bad field falls back
// to, the logo's colors and the opening card's layout. Build with `make` in
// tests/.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "brand.h"

using namespace espn;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    checks++;                                                    \
    if (!(cond)) {                                               \
      failures++;                                                \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                            \
  } while (0)

static bool parse(const std::string &json, BrandPack &p, std::string &err) {
  return brand_parse(json.data(), json.size(), p, err);
}

static void test_full_pack() {
  const std::string json = R"({
    "name": "Main Street Taproom", "title": "MUG CLUB", "wall": "WALL OF FAME",
    "colors": {"rim": "#D4A017", "number": "c8102e", "quote": "#4FA3D9", "background": "#0B3D2E"},
    "logo": "https://example.com/logo.png", "logo_color": "http://192.168.1.20/logo-color.png",
    "list": "https://example.com/plates.txt",
    "event": {"name": "GLASS NIGHT", "date": "2026-11-12", "time": "18:00"},
    "party": {"new": "NEW MUG!", "round2": "DOUBLE MUG!"}
  })";
  BrandPack p;
  std::string err;
  CHECK(parse(json, p, err));
  CHECK(err.empty());
  CHECK(p.name == "Main Street Taproom");
  CHECK(p.title == "MUG CLUB");
  CHECK(p.wall == "WALL OF FAME");
  CHECK(p.rim == 0xD4A017);
  CHECK(p.number == 0xC8102E);
  CHECK(p.quote == 0x4FA3D9);
  CHECK(p.background == 0x0B3D2E);
  CHECK(p.logo == "https://example.com/logo.png");
  CHECK(p.logo_color == "http://192.168.1.20/logo-color.png");
  CHECK(p.list == "https://example.com/plates.txt");
  CHECK(p.event_name == "GLASS NIGHT");
  CHECK(p.event_ymd == 20261112);
  CHECK(p.event_minute == 18 * 60);
  CHECK(p.party_new == "NEW MUG!");
  CHECK(p.party_round2 == "DOUBLE MUG!");
  CHECK(p.warning.empty());
}

static void test_defaults() {
  // A title alone is a pack: everything else is the plain look.
  BrandPack p;
  std::string err;
  CHECK(parse(R"({"title":"MUG CLUB"})", p, err));
  CHECK(p.rim == kBrandWhite && p.number == kBrandWhite && p.quote == kBrandWhite && p.background == kBrandBlack);
  CHECK(p.logo.empty() && p.logo_color.empty() && p.list.empty());
  CHECK(p.event_ymd == 0 && p.event_minute == -1 && p.event_name.empty());
  CHECK(p.party_new == "NEW PLATE!" && p.party_round2 == "ROUND TWO!");
  CHECK(p.warning.empty());
  // A logo alone too.
  CHECK(parse(R"({"logo":"https://example.com/l.png"})", p, err));
  CHECK(p.title.empty() && p.logo == "https://example.com/l.png");
}

static void test_bad_fields_fall_back() {
  BrandPack p;
  std::string err;
  CHECK(parse(R"({"title":"MUG CLUB","colors":{"rim":"gold","number":"#12345","quote":7,"background":"#102030"},
                  "logo":"ftp://example.com/l.png","list":"not a url","wall":5,
                  "event":{"name":"X","date":"2026-02-30"},"party":{"new":3}})",
              p, err));
  CHECK(p.rim == kBrandWhite && p.number == kBrandWhite && p.quote == kBrandWhite);
  CHECK(p.background == 0x102030);  // the good one is kept
  CHECK(p.logo.empty() && p.list.empty() && p.wall.empty());
  CHECK(p.event_ymd == 0 && p.event_name.empty());
  CHECK(p.party_new == "NEW PLATE!");
  CHECK(p.warning == "wall isn't text");  // the first one found, in the file's field order
  // colors, event and party of the wrong shape
  CHECK(parse(R"({"title":"A","colors":"red"})", p, err) && p.warning == "colors isn't a list of colors");
  CHECK(parse(R"({"title":"A","event":"tonight"})", p, err) && p.warning == "event isn't a name and a date");
  CHECK(parse(R"({"title":"A","party":[1]})", p, err) && p.warning == "party isn't a list of words");
  // an event time with no day is dropped with the day
  CHECK(parse(R"({"title":"A","event":{"name":"X","time":"18:00"}})", p, err));
  CHECK(p.event_minute == -1 && p.event_name.empty());
  CHECK(parse(R"({"title":"A","event":{"name":"X","date":"2026-11-12","time":"6 PM"}})", p, err));
  CHECK(p.event_ymd == 20261112 && p.event_minute == -1 && p.warning == "event.time isn't a time like 18:00");
}

static void test_refused_keeps_last_good() {
  BrandPack p;
  std::string err;
  CHECK(parse(R"({"title":"FIRST","colors":{"rim":"#FF0000"}})", p, err));
  CHECK(!parse("", p, err) && err == "The brand file is empty.");
  CHECK(!parse("<html>Not found</html>", p, err) && err == "The brand file isn't JSON.");
  CHECK(!parse(R"(["title"])", p, err) && err == "The brand file isn't JSON.");
  CHECK(!parse(R"({"title":"A")", p, err) && err == "The brand file isn't JSON.");  // cut short
  CHECK(!parse(R"({"name":"Just a name","colors":{"rim":"#00FF00"}})", p, err) &&
        err == "The brand file needs a title or a logo.");
  CHECK(!parse(R"({"title":""})", p, err) && err == "The brand file needs a title or a logo.");
  std::string big = R"({"title":"A","name":")" + std::string(kBrandMaxBytes, 'x') + "\"}";
  CHECK(!parse(big, p, err) && err == "The brand file is too big; it can be up to 2 KB.");
  // None of those touched the pack on hand.
  CHECK(p.title == "FIRST" && p.rim == 0xFF0000);
}

static void test_text_cleaned() {
  BrandPack p;
  std::string err;
  CHECK(parse(R"({"title":"  MUG\tCLUB 🍺 ","name":")" + std::string(80, 'n') + R"("})", p, err));
  CHECK(p.title == "MUG CLUB");  // tab to a space, the emoji and the spaces round it gone
  CHECK(p.name.size() == kBrandNameMax);
  CHECK(parse(R"({"title":"line one\nline two"})", p, err) && p.title == "line one line two");
}

static void test_colors() {
  CHECK(brand_color("#000000") == 0);
  CHECK(brand_color("FFFFFF") == 0xFFFFFF);
  CHECK(brand_color("#aBcDeF") == 0xABCDEF);
  CHECK(brand_color("#ABCDE") == -1);
  CHECK(brand_color("#ABCDEFF") == -1);
  CHECK(brand_color("#GG0000") == -1);
  CHECK(brand_color(nullptr) == -1);
  CHECK(brand_565(0xFFFFFF) == 0xFFFF);
  CHECK(brand_565(0xFF0000) == 0xF800);
  CHECK(brand_565(0x00FF00) == 0x07E0);
  CHECK(brand_565(0x0000FF) == 0x001F);
}

static void test_logo_pixels() {
  // Four pixels: opaque white, opaque black, see-through white, half see-through red.
  const uint8_t cut[] = {255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 255, 0, 255, 0, 0, 128};
  uint16_t out[4];
  const uint32_t fg = 0xD4A017, bg = 0x102030;
  // One color with transparency: opaque is lit, whatever its color.
  brand_logo_pixels(cut, 4, 1, true, fg, bg, out);
  CHECK(out[0] == brand_565(fg) && out[1] == brand_565(fg) && out[2] == brand_565(bg) && out[3] == brand_565(fg));
  // One color with no transparency: light is lit.
  const uint8_t solid[] = {255, 255, 255, 255, 0, 0, 0, 255, 200, 200, 200, 255, 40, 40, 40, 255};
  brand_logo_pixels(solid, 4, 1, true, fg, bg, out);
  CHECK(out[0] == brand_565(fg) && out[1] == brand_565(bg) && out[2] == brand_565(fg) && out[3] == brand_565(bg));
  // In color: each pixel over the background by its alpha.
  brand_logo_pixels(cut, 4, 1, false, fg, bg, out);
  CHECK(out[0] == 0xFFFF && out[1] == 0x0000 && out[2] == brand_565(bg));
  CHECK(out[3] == brand_565(((uint32_t) ((255 * 128 + 0x10 * 127) / 255) << 16) | ((0x20 * 127 / 255) << 8) |
                            (0x30 * 127 / 255)));
}

static void test_layout() {
  // Two panels, a 64 x 36 logo, title and wall: logo on top at its own size,
  // the words under it.
  BrandLayout L = brand_layout(128, 64, 64, 36, "MUG CLUB", "WALL OF FAME");
  CHECK(L.scale == 256);
  CHECK(L.logo_x == 32);
  CHECK(L.title_big);
  CHECK(L.title_y == 64 - 22 - 1);   // 12 + 2 + 8 of words, a row under
  CHECK(L.wall_y == L.title_y + 14);
  CHECK(L.logo_y >= 1 && L.logo_y + 36 <= L.title_y);
  // A taller one shrinks to the room the words leave.
  L = brand_layout(128, 64, 64, 64, "MUG CLUB", "WALL OF FAME");
  CHECK(L.scale == 39 * 256 / 64 && L.logo_y + 64 * (int) L.scale / 256 <= L.title_y);
  // One panel: the same logo halves-ish to fit 64 wide and the room left.
  L = brand_layout(64, 64, 128, 64, "MUG CLUB", "WALL OF FAME");
  CHECK(L.scale < 256);
  const int shown_w = 128 * (int) L.scale / 256, shown_h = 64 * (int) L.scale / 256;
  CHECK(shown_w <= 64 && L.logo_y + shown_h <= L.title_y);
  CHECK(L.logo_x == (64 - shown_w) / 2);
  // A title too wide for 12 px drops to 8 px.
  L = brand_layout(64, 64, 0, 0, "WALL OF LEGENDS", "");
  CHECK(!L.title_big);  // 15 letters: 90 px at 12, 74 at 8 (cut at the edge)
  CHECK(L.wall_y == -1);
  CHECK(L.title_y == (64 - 8) / 2);  // no logo: in the middle
  // No words: the logo in the middle at its own size.
  L = brand_layout(128, 64, 100, 50, "", "");
  CHECK(L.scale == 256 && L.title_y == -1 && L.wall_y == -1);
  CHECK(L.logo_x == 14 && L.logo_y == 1 + (62 - 50) / 2);
  // A logo bigger than the board in both ways never grows, only shrinks.
  L = brand_layout(128, 64, 32, 16, "", "");
  CHECK(L.scale == 256);
}

static void test_list_line() {
  CHECK(brand_list_line("12|Ann B.|Cheers to that|2") == "#12 Ann B.");
  CHECK(brand_list_line("7|Bob") == "#7 Bob");
  CHECK(brand_list_line("|Cara|quote") == "Cara");
  CHECK(brand_list_line("9|  |quote") == "#9");
  CHECK(brand_list_line("Just a name") == "Just a name");
}

int main() {
  test_full_pack();
  test_defaults();
  test_bad_fields_fall_back();
  test_refused_keeps_last_good();
  test_text_cleaned();
  test_colors();
  test_logo_pixels();
  test_layout();
  test_list_line();
  printf("%d/%d brand checks passed\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}
