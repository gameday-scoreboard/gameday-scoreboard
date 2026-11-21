// Host tests for the Smart board's rows (components/gameday/smart_board.h):
// color tags, the recolor text LVGL draws, the layout and the font pick.
// Build with `make` in tests/.
#include <cstdint>
#include <cstdio>
#include <string>

#include "showing.h"
#include "smart_board.h"

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

static void test_plain_rows() {
  SbRow r = sb_parse("Front Door Locked");
  CHECK(r.text == "Front Door Locked");
  CHECK(r.plain == "Front Door Locked");
  CHECK(r.rgb == kSbWhite);
  CHECK(!r.recolor);
  CHECK(sb_parse("").empty());
  CHECK(sb_parse("   ").empty());
  // A row in one color keeps its own '#'
  CHECK(sb_parse("Room #2 open").text == "Room #2 open");
  // Trailing spaces go, inner ones stay
  CHECK(sb_parse("CO2  766  ").plain == "CO2  766");
}

static void test_leading_tag() {
  SbRow r = sb_parse("[red]Front Door Unlocked");
  CHECK(r.rgb == 0xFF2020);
  CHECK(r.text == "Front Door Unlocked");
  CHECK(!r.recolor);
  CHECK(sb_parse("[RED]x").rgb == 0xFF2020);  // any case
  CHECK(sb_parse("[Grey]x").rgb == 0x808080);
  CHECK(sb_parse("[#FF6400]Door").rgb == 0xFF6400);
  CHECK(sb_parse("[#ff6400]Door").rgb == 0xFF6400);
  // A space after the tag doesn't push the words over
  CHECK(sb_parse("[green] Locked").text == "Locked");
  // Two tags before any words: the last one wins
  SbRow two = sb_parse("[red][blue]Door");
  CHECK(two.rgb == 0x3070FF);
  CHECK(!two.recolor);
  // A tag with nothing after it is an empty row
  CHECK(sb_parse("[red]").empty());
  CHECK(sb_parse("[red]  ").empty());
}

static void test_not_tags() {
  // Brackets that aren't a color stay as written
  CHECK(sb_parse("[1] seed").text == "[1] seed");
  CHECK(sb_parse("[reddish]x").text == "[reddish]x");
  CHECK(sb_parse("[#12345]x").text == "[#12345]x");
  CHECK(sb_parse("[#GG0000]x").text == "[#GG0000]x");
  CHECK(sb_parse("[]x").text == "[]x");
  CHECK(sb_parse("x [red").text == "x [red");
  CHECK(sb_parse("a ] b").text == "a ] b");
  CHECK(!sb_parse("[1] seed").recolor);
}

static void test_mid_row_color() {
  SbRow r = sb_parse("CO2 766ppm [yellow]AQI 62");
  CHECK(r.recolor);
  CHECK(r.rgb == kSbWhite);
  CHECK(r.plain == "CO2 766ppm AQI 62");
  CHECK(r.text == "CO2 766ppm #ffd000 AQI 62#");
  // A leading tag colors the start, a later one the rest
  SbRow d = sb_parse("[green]Front Door [red]Unlocked");
  CHECK(d.rgb == 0x20E040);
  CHECK(d.text == "Front Door #ff2020 Unlocked#");
  // Back to the row's color: no command needed
  SbRow back = sb_parse("[blue]a [red]b [blue]c");
  CHECK(back.text == "a #ff2020 b #c");
  // A row that changes color drops its own '#', which LVGL would eat
  SbRow h = sb_parse("Room #2 [red]open");
  CHECK(h.text == "Room 2 #ff2020 open#");
  CHECK(h.plain == "Room #2 open");
  // A trailing tag with nothing after it changes nothing
  SbRow t = sb_parse("Door [red]");
  CHECK(!t.recolor);
  CHECK(t.text == "Door");
}

static void test_cleaning() {
  // Newlines and tabs become spaces, the row is one line
  CHECK(sb_parse("a\nb").text == "a b");
  // Cut at kSbRowMax bytes
  std::string longrow(400, 'x');
  CHECK(sb_parse(longrow).plain.size() == kSbRowMax);
}

static void test_layout() {
  int32_t y[kSbRows], h[kSbRows];
  bool all[kSbRows] = {true, true, true, true};
  CHECK(sb_layout(all, 64, y, h) == 4);
  CHECK(y[0] == 0 && h[0] == 16 && y[3] == 48 && h[3] == 16);
  bool one[kSbRows] = {false, false, true, false};
  CHECK(sb_layout(one, 64, y, h) == 1);
  CHECK(y[2] == 0 && h[2] == 64 && h[0] == 0);
  bool three[kSbRows] = {true, false, true, true};
  CHECK(sb_layout(three, 64, y, h) == 3);
  CHECK(y[0] == 0 && h[0] == 21);
  CHECK(h[1] == 0);
  CHECK(y[2] == 21 && h[2] == 21);
  CHECK(y[3] == 42 && h[3] == 22);
  bool none[kSbRows] = {false, false, false, false};
  CHECK(sb_layout(none, 64, y, h) == 0);
}

static void test_font() {
  // 32, 16, 12, 8 px tall
  const int32_t heights[] = {32, 16, 12, 8};
  // Fits at 16 on a 128-wide panel
  const int32_t fits16[] = {256, 128, 96, 80};
  CHECK(sb_font(16, 128, heights, fits16, 4, 12) == 1);
  // Too wide at 16, fits at 12
  const int32_t fits12[] = {272, 136, 102, 85};
  CHECK(sb_font(16, 128, heights, fits12, 4, 12) == 2);
  // Too wide everywhere above 8: the biggest that fits the height, and it scrolls
  const int32_t wide[] = {800, 400, 300, 120};
  CHECK(sb_font(16, 128, heights, wide, 4, 12) == 1);
  // One row on the whole panel takes the 32
  const int32_t short_[] = {96, 48, 36, 30};
  CHECK(sb_font(64, 128, heights, short_, 4, 12) == 0);
  // A lone row too wide at 32 drops to 16, not further: past that it scrolls at 32
  const int32_t mid[] = {272, 136, 102, 85};
  CHECK(sb_font(64, 128, heights, mid, 4, 12) == 0);
  const int32_t mid16[] = {200, 100, 75, 60};
  CHECK(sb_font(64, 128, heights, mid16, 4, 12) == 1);
  // A row shorter than every readable font still gets one
  CHECK(sb_font(10, 128, heights, short_, 4, 12) == 3);
}

// What Showing has up (showing.h): the Scoreboard follows games, a page
// somebody picked reads nothing, a page After the game put up keeps its reads.
static void test_showing() {
  CHECK(showing_now("Scoreboard", false, false) == Showing::BOARD);
  CHECK(showing_now(nullptr, false, false) == Showing::BOARD);  // before the first page
  CHECK(showing_now("", true, true) == Showing::BOARD);
  CHECK(showing_now("Clock", false, false) == Showing::PICKED);
  CHECK(showing_now("Clock", true, true) == Showing::PICKED);
  CHECK(showing_now("Effects", false, false) == Showing::PICKED);
  CHECK(showing_now("Effects", true, false) == Showing::AUTO);
  CHECK(showing_now("Smart board", false, true) == Showing::PICKED);
  CHECK(showing_now("Smart board", false, false) == Showing::AUTO);
  CHECK(showing_now("Smart board", true, true) == Showing::PICKED);
  CHECK(looking_for(true, "NFL ", "Dallas Cowboys") == "live NFL games");
  CHECK(looking_for(true, "", nullptr) == "live games");
  CHECK(looking_for(false, "", "Dallas Cowboys") == "Dallas Cowboys");
  CHECK(looking_for(false, "", nullptr) == "your teams");
  CHECK(looking_for(false, "", "") == "your teams");
}

// How often a dark screen reads (showing.h): never with nothing to light it,
// a minute near the owner's start, an hour on a game day, else 6 hours.
static void test_standby_every() {
  const int64_t now = 1'800'000'000;
  CHECK(standby_every_ms(false, false, now, now + 600, 15) == 0);  // off by hand, Wake off
  CHECK(standby_every_ms(true, false, now, 0, 15) == 6u * 3600u * 1000u);  // night, no game known
  CHECK(standby_every_ms(false, true, now, now + 3 * 86400, 15) == 6u * 3600u * 1000u);
  CHECK(standby_every_ms(false, true, now, now + 5 * 3600, 15) == 3600u * 1000u);  // game day
  CHECK(standby_every_ms(false, true, now, now + 21 * 60, 15) == 3600u * 1000u);  // just before the window
  CHECK(standby_every_ms(false, true, now, now + 20 * 60, 15) == 60u * 1000u);  // lead + 5 min
  CHECK(standby_every_ms(true, false, now, now - 600, 15) == 60u * 1000u);  // past the start, not live yet
  CHECK(standby_every_ms(false, true, now, now + 40 * 60, 30) == 3600u * 1000u);
  CHECK(standby_every_ms(false, true, now, now + 35 * 60, 30) == 60u * 1000u);  // a longer lead
  CHECK(standby_every_ms(false, true, now, now + 20 * 60, 5) == 60u * 1000u);  // never under 15
}

int main() {
  test_plain_rows();
  test_leading_tag();
  test_not_tags();
  test_mid_row_color();
  test_cleaning();
  test_layout();
  test_font();
  test_showing();
  test_standby_every();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
