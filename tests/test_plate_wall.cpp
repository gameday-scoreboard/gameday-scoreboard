// Host tests for the plate wall on the bar build (components/gameday/plate_wall.h):
// reading plate list lines, seeing a new plate at the top of the list, a
// name fitting inside a plate, the quote's lines and rise, page times and
// the event page's day. Build with `make` in tests/.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "message.h"
#include "plate_wall.h"

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

static void test_line() {
  PlateLine e = plate_line("12|Ann B.|Cheers to that|2");
  CHECK(e.num == "12" && e.name == "Ann B." && e.quote == "Cheers to that" && e.round2);
  e = plate_line("7|Bob");
  CHECK(e.num == "7" && e.name == "Bob" && e.quote.empty() && !e.round2);
  e = plate_line("|Cara|quote");
  CHECK(e.num.empty() && e.name == "Cara" && e.quote == "quote");
  e = plate_line(" 9 |  Dee Q.  | Hi there |  ");
  CHECK(e.num == "9" && e.name == "Dee Q." && e.quote == "Hi there" && !e.round2);
  e = plate_line("Just a name");
  CHECK(e.num.empty() && e.name == "Just a name" && e.quote.empty());
  // Fields past the fourth are left alone; round is only "2".
  e = plate_line("31|Eve|Salud|2|https://example.com/eve.png|extra");
  CHECK(e.name == "Eve" && e.quote == "Salud" && e.round2);
  e = plate_line("32|Fay||1");
  CHECK(e.quote.empty() && !e.round2);
  e = plate_line("33|Gus|A quote with || in it");
  CHECK(e.quote == "A quote with" && !e.round2);  // a '|' always splits
  // The plate list keeps lines long enough for a quote; message lists cut at 64.
  const std::string line = "40|Hal|" + std::string(150, 'q') + "|2\n";
  MsgReader<std::string, kPlateLineMax> plates(0, 10);
  plates.feed(line.data(), line.size());
  plates.finish();
  CHECK(plates.list() == line.substr(0, line.size() - 1));
  MsgReader<std::string> msgs(0, 10);
  msgs.feed(line.data(), line.size());
  msgs.finish();
  CHECK(msgs.list().size() == kMsgLineMax);
}

static void test_party() {
  CHECK(plate_id(plate_line("212|Ann B.|Cheers")) == "212|");
  CHECK(plate_id(plate_line("212|Ann B.|Cheers|2")) == "212|2");
  CHECK(plate_id(plate_line("Just a name")) == "");
  CHECK(plate_id(plate_line(std::string(40, '9') + "|X")).size() == kPlateIdMax);
  // First read: save, no party.
  CHECK(plate_news("", "212|") == PlateNews::FIRST);
  // The same plate on top: nothing.
  CHECK(plate_news("212|", "212|") == PlateNews::NONE);
  // A new plate on top, and the same member's second round.
  CHECK(plate_news("212|", "213|") == PlateNews::NEW);
  CHECK(plate_news("212|", "212|2") == PlateNews::NEW);
  // An empty list, or a top line with no number, never throws a party.
  CHECK(plate_news("212|", "") == PlateNews::NONE);
  CHECK(plate_news("", "") == PlateNews::NONE);
}

// Every drawn line of a fit inside the plate's text room.
static bool all_inside(const PlateFit &f, const std::string &tag, float cx, float cy, float rx, float ry) {
  if (!tag.empty() && !plate_box_in(cx, cy, rx, ry, plate_text_w(plate_letters(tag), f.tag_px), f.tag_y, f.tag_px))
    return false;
  for (int i = 0; i < f.lines; i++)
    if (!plate_box_in(cx, cy, rx, ry, plate_text_w(plate_letters(f.line[i]), f.px), f.name_y + i * f.px, f.px))
      return false;
  return true;
}

static void test_fit() {
  CHECK(plate_text_w(0, 8) == 0);
  CHECK(plate_text_w(4, 8) == 19 && plate_text_w(4, 12) == 24 && plate_text_w(4, 16) == 32);
  CHECK(plate_letters("Zoë") == 3);
  // The big oval over two panels: a short name in 16 px on one line, the
  // number in 12 px above it.
  const float bx = 64, by = 32, brx = 63.5f - 3, bry = 31.5f - 3;
  PlateFit f = plate_fit("Ann B.", "#12", bx, by, brx, bry, true);
  CHECK(f.px == 16 && f.lines == 1 && f.tag_px == 12 && !f.cut);
  CHECK(f.line[0] == "Ann B.");
  CHECK(f.name_y == f.tag_y + 12 + kPlateTagGap);
  CHECK(all_inside(f, "#12", bx, by, brx, bry));
  // A round plate on one panel: the same name smaller.
  const float px = 32, py = 32, pr = 31.5f - 3;
  f = plate_fit("Ann B.", "#12", px, py, pr, pr, false);
  CHECK(f.tag_px == 8 && !f.cut && f.lines == 1);
  CHECK(f.px == 12 || f.px == 16);
  CHECK(all_inside(f, "#12", px, py, pr, pr));
  // A long name goes to two lines, split at the last space, before it gets cut.
  f = plate_fit("Christopher M.", "#7", px, py, pr, pr, false);
  CHECK(!f.cut && f.lines == 2 && f.line[0] == "Christopher" && f.line[1] == "M.");
  CHECK(all_inside(f, "#7", px, py, pr, pr));
  // The big oval keeps it in 16 px, on two lines.
  f = plate_fit("Christopher M.", "#7", bx, by, brx, bry, true);
  CHECK(!f.cut && f.px == 16 && f.lines == 2);
  CHECK(all_inside(f, "#7", bx, by, brx, bry));
  // Too long for any size: 8 px, cut at the rim, still inside.
  f = plate_fit("Anna-Katharina Schwarzenberg", "#88", px, py, pr, pr, false);
  CHECK(f.cut && f.px == 8 && f.lines == 2);
  CHECK(f.line[0].size() < std::string("Anna-Katharina").size());
  CHECK(all_inside(f, "#88", px, py, pr, pr));
  // No tag: the name alone, in the middle.
  f = plate_fit("Bob", "", px, py, pr, pr, false);
  CHECK(f.px == 16 && f.lines == 1 && f.name_y == (int) lroundf(32 - 8.0f));
  // The round-two tag is the pack's words without the "!".
  CHECK(plate_tag(plate_line("5|Ann|q|2"), "ROUND TWO!") == "ROUND TWO");
  CHECK(plate_tag(plate_line("5|Ann|q|2"), "DOUBLE MUG!!") == "DOUBLE MUG");
  CHECK(plate_tag(plate_line("5|Ann|q|2"), "!") == "ROUND TWO");
  CHECK(plate_tag(plate_line("5|Ann"), "ROUND TWO!") == "#5");
  CHECK(plate_tag(plate_line("Ann"), "ROUND TWO!") == "");
  // A round-two plate has a thicker rim: its tag still fits on one panel.
  const float r2 = 31.5f - 5;
  f = plate_fit("Ann B.", "ROUND TWO", px, py, r2, r2, false);
  CHECK(!f.cut && all_inside(f, "ROUND TWO", px, py, r2, r2));
  CHECK(plate_fit_px("NEW PLATE!", 126) == 16);
  CHECK(plate_fit_px("NEW PLATE!", 62) == 12);
  CHECK(plate_fit_px("NEW PLATE!", 62, 8) == 8);
  CHECK(plate_fit_px("A VERY LONG PARTY WORD", 40) == 8);
}

static void test_quote() {
  CHECK(plate_quote_lines("").empty());
  std::vector<std::string> q = plate_quote_lines("Cheers");
  CHECK(q.size() == 1 && q[0] == "\"Cheers\"");
  q = plate_quote_lines("Here's to the next two hundred");
  CHECK(q.size() == 4);
  for (const auto &l : q)
    CHECK(plate_letters(l) <= (size_t) kQuoteChars);
  CHECK(q[0] == "\"Here's to");
  // A word longer than a line is broken across lines.
  q = plate_wrap("Supercalifragilistic", 10);
  CHECK(q.size() == 2 && q[0] == "Supercalif" && q[1] == "ragilistic");
  // A short quote rises from below the board to the middle and holds there.
  const int H = 64, h = 2 * kQuotePx;
  CHECK(plate_rise_y(0, h, H) == H);
  CHECK(plate_rise_y(1000, h, H) == H - kPlateRisePxS);
  CHECK(plate_rise_y(60000, h, H) == (H - h) / 2);
  CHECK(plate_rise_moving(0, h, H));
  CHECK(!plate_rise_moving(60000, h, H));
  CHECK(plate_rise_ms(h, H) == (uint32_t) (H - (H - h) / 2) * 1000 / kPlateRisePxS + kPlateHoldMs);
  // A quote taller than the board keeps rising off the top.
  const int tall = 8 * kQuotePx;
  CHECK(!plate_rise_holds(tall, H));
  CHECK(plate_rise_y(60000, tall, H) < -tall);
  CHECK(!plate_rise_moving(60000, tall, H));
  CHECK(plate_rise_moving(5000, tall, H));
  CHECK(plate_rise_ms(tall, H) == (uint32_t) (H + tall) * 1000 / kPlateRisePxS + 500);
  CHECK(plate_rise_ms(0, H) == 0);
}

static void test_page_ms() {
  // No quote: the big oval alone.
  CHECK(plate_page_ms(true, 0, 64) == kPlateNoQuoteMs);
  CHECK(plate_page_ms(false, 0, 64) == kPlateNoQuoteMs);
  // Two panels: the oval, the fold, the quote's rise and half a second.
  CHECK(plate_page_ms(true, 2, 64) == kPlateBigMs + kPlateFoldMs + plate_rise_ms(24, 64) + 500);
  // One panel: no fold.
  CHECK(plate_page_ms(false, 2, 64) == kPlateBigMs + plate_rise_ms(24, 64) + 500);
  // A longer quote takes longer.
  CHECK(plate_page_ms(true, 9, 64) > plate_page_ms(true, 2, 64));
  CHECK(kPartyMs == 12000);
  CHECK(plate_ease(0) == 0 && plate_ease(1) == 1 && plate_ease(0.5f) == 0.5f);
  CHECK(plate_ease(-1) == 0 && plate_ease(2) == 1);
}

static void test_event() {
  // 2026-11-05 is a Thursday.
  PlateEvent ev = plate_event(20261105, 18 * 60, 2026, 11, 5);
  CHECK(ev.show && ev.day == "TONIGHT" && ev.time == "6 PM");
  ev = plate_event(20261105, 11 * 60 + 30, 2026, 11, 5);
  CHECK(ev.show && ev.day == "TODAY" && ev.time == "11:30 AM");
  ev = plate_event(20261105, -1, 2026, 11, 5);
  CHECK(ev.show && ev.day == "TODAY" && ev.time.empty());
  ev = plate_event(20261106, 18 * 60, 2026, 11, 5);
  CHECK(ev.show && ev.day == "TOMORROW");
  ev = plate_event(20261112, 18 * 60, 2026, 11, 7);
  CHECK(ev.show && ev.day == "THURSDAY");
  ev = plate_event(20261108, 12 * 60, 2026, 11, 5);
  CHECK(ev.show && ev.day == "SUNDAY" && ev.time == "12 PM");
  // A week or more out: the date.
  ev = plate_event(20261112, 0, 2026, 11, 5);
  CHECK(ev.show && ev.day == "NOV 12" && ev.time == "12 AM");
  // Fourteen days out shows; fifteen, or a day gone by, doesn't.
  CHECK(plate_event(20261119, 18 * 60, 2026, 11, 5).show);
  CHECK(!plate_event(20261120, 18 * 60, 2026, 11, 5).show);
  CHECK(!plate_event(20261104, 18 * 60, 2026, 11, 5).show);
  CHECK(!plate_event(0, 18 * 60, 2026, 11, 5).show);
  // Across a month and a year.
  ev = plate_event(20270102, 20 * 60, 2026, 12, 31);
  CHECK(ev.show && ev.day == "SATURDAY");
  CHECK(plate_time_text(13 * 60 + 5) == "1:05 PM");
  CHECK(plate_time_text(-1).empty() && plate_time_text(24 * 60).empty());
}

int main() {
  test_line();
  test_party();
  test_fit();
  test_quote();
  test_page_ms();
  test_event();
  printf("%d/%d plate wall checks passed\n", checks - failures, checks);
  return failures == 0 ? 0 : 1;
}
