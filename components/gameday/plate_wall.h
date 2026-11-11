// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The plate wall, on the bar build: the message card showing the brand pack's
// plate list (brand.h) one member a page, the way a bar's mug club hangs its
// plates. Each list line is "number|name|quote|round": round 2 marks a
// second-round plate, and fields past the fourth are left alone.
//
// A member's page: the pack's sprite flies across and uncovers the name in a
// big oval over the whole board, the oval folds into a round plate on one
// panel and the quote rises on the other, a line at a time; the next member
// trades sides. On one panel the plate comes first and the quote rises after
// it. A new plate at the top of the list (the list comes newest first) gets
// a plate party once, and the pack's event gets a page of its own in the two
// weeks before it.
//
// What is drawn where, and for how long, is worked out here; the drawing
// itself is plate_wall_lvgl.cpp. Pure, with no ESPHome includes, so the host
// tests compile it directly.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "weather.h"

namespace espn {

// One plate list line, read from the list, in bytes: room for a name and a
// quote of about a hundred letters. The panel's own message lists keep
// kMsgLineMax.
constexpr size_t kPlateLineMax = 200;
// The first line's "number|round", kept in flash to see a new plate.
constexpr size_t kPlateIdMax = 31;

// The message card's pages for the plate wall start with a control
// character (message_clean() keeps them out of every real page): a member's
// plate, a plate party, the event page. The first two carry the list line.
constexpr char kPlatePageMark = '\x02';
constexpr char kPartyPageMark = '\x03';
constexpr char kEventPageMark = '\x04';

struct PlateLine {
  std::string num, name, quote;
  bool round2{false};
};

namespace detail {

inline std::string plate_trim(const std::string &s) {
  const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

}  // namespace detail

// "12|Ann B.|Cheers to that|2" to its fields, each trimmed. A line with no
// '|' is a name alone.
inline PlateLine plate_line(const std::string &line) {
  PlateLine e;
  std::string f[4];
  size_t n = 0, p = 0;
  while (n < 4) {
    const size_t bar = line.find('|', p);
    f[n++] = line.substr(p, bar == std::string::npos ? std::string::npos : bar - p);
    if (bar == std::string::npos)
      break;
    p = bar + 1;
  }
  if (n == 1) {
    e.name = detail::plate_trim(f[0]);
    return e;
  }
  e.num = detail::plate_trim(f[0]);
  e.name = detail::plate_trim(f[1]);
  e.quote = detail::plate_trim(f[2]);
  e.round2 = detail::plate_trim(f[3]) == "2";
  return e;
}

// ---- A new plate: the plate party --------------------------------------------

// Which plate a line is, for seeing a new one: "212|" or, for a second-round
// plate, "212|2". "" for a line with no number.
inline std::string plate_id(const PlateLine &e) {
  if (e.num.empty())
    return "";
  std::string id = e.num + "|" + (e.round2 ? "2" : "");
  if (id.size() > kPlateIdMax)
    id.resize(kPlateIdMax);
  return id;
}

enum class PlateNews : uint8_t {
  NONE,   // the same plate on top as last time, or nothing to go by
  FIRST,  // nothing saved yet: save it, no party
  NEW,    // a different plate on top: save it and throw its party
};

// saved is the id kept from the last read of the list's top, first the id of
// the top line of this one.
inline PlateNews plate_news(const std::string &saved, const std::string &first) {
  if (first.empty() || first == saved)
    return PlateNews::NONE;
  return saved.empty() ? PlateNews::FIRST : PlateNews::NEW;
}

// ---- Type ----------------------------------------------------------------------

// The panel's Spleen fonts by height: 16 px is 8 px a letter, 12 px is 6, and
// 8 px is 5 with the last letter's blank column left off.
inline int plate_text_w(size_t letters, int px) {
  if (letters == 0)
    return 0;
  return px >= 16 ? (int) letters * 8 : px >= 12 ? (int) letters * 6 : (int) letters * 5 - 1;
}

inline size_t plate_letters(const std::string &s) {
  size_t n = 0;
  for (unsigned char c : s)
    n += (c & 0xC0) != 0x80;
  return n;
}

// s without its last letter (a whole UTF-8 character).
inline void plate_pop_letter(std::string &s) {
  while (!s.empty() && ((unsigned char) s.back() & 0xC0) == 0x80)
    s.pop_back();
  if (!s.empty())
    s.pop_back();
  while (!s.empty() && s.back() == ' ')
    s.pop_back();
}

// The biggest of 16, 12 and 8 px (no bigger than max_px) that puts text on
// one line in width; 8 when none does.
inline int plate_fit_px(const std::string &text, int width, int max_px = 16) {
  const size_t n = plate_letters(text);
  for (int px : {16, 12, 8})
    if (px <= max_px && plate_text_w(n, px) <= width)
      return px;
  return 8;
}

// Two lines from one, split at the last space; second is "" with no space.
inline void plate_split(const std::string &s, std::string &first, std::string &second) {
  const size_t sp = s.rfind(' ');
  if (sp == std::string::npos) {
    first = s;
    second.clear();
    return;
  }
  first = detail::plate_trim(s.substr(0, sp));
  second = detail::plate_trim(s.substr(sp + 1));
}

// ---- A name inside a plate ------------------------------------------------------

// Whether pixel (x, y) is inside the ellipse centered at cx, cy: its middle,
// so a plate drawn from this has smooth edges at any size.
inline bool plate_inside(float cx, float cy, float rx, float ry, int x, int y) {
  if (rx <= 0 || ry <= 0)
    return false;
  const float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
  return dx * dx + dy * dy <= 1.0f;
}

// Left edge of a line w px wide centered on cx.
inline int plate_left(float cx, int w) { return (int) lroundf(cx - w / 2.0f); }

// A line of text w px wide and px tall, centered on cx with its top at y,
// stays inside the ellipse: the corners of where the letters are drawn.
inline bool plate_box_in(float cx, float cy, float rx, float ry, int w, int y, int px) {
  const int l = plate_left(cx, w), r = l + w - 1;
  return plate_inside(cx, cy, rx, ry, l, y + 1) && plate_inside(cx, cy, rx, ry, r, y + 1) &&
         plate_inside(cx, cy, rx, ry, l, y + px - 2) && plate_inside(cx, cy, rx, ry, r, y + px - 2);
}

// How a name and its tag (the number, "#12", or the round-two words) sit in a
// plate: the tag on top, the name under it on one line or two.
struct PlateFit {
  int px{8};  // the name's type
  int tag_px{8};
  std::string line[2];
  int lines{1};
  int tag_y{0};   // unused with no tag
  int name_y{0};  // the first line of the name
  bool cut{false};  // nothing fit: letters were left off at the rim
};

constexpr int kPlateTagGap = 2;  // px between the tag and the name

// rx, ry are the room inside the rim. The name tries 16, 12 and 8 px, one
// line and then two (split at the last space), each with the tag in 12 px
// (big plates) or 8. When nothing fits, the name goes in 8 px, letters left
// off the end of each line until it stays inside the rim.
inline PlateFit plate_fit(const std::string &name, const std::string &tag, float cx, float cy, float rx, float ry,
                          bool big) {
  std::string two[2];
  plate_split(name, two[0], two[1]);
  const bool can_split = !two[1].empty();
  const size_t tag_n = plate_letters(tag);
  auto place = [&](PlateFit &f) {
    const int tag_h = tag.empty() ? 0 : f.tag_px + kPlateTagGap;
    const int total = tag_h + f.px * f.lines;
    const int top = (int) lroundf(cy - total / 2.0f);
    f.tag_y = top;
    f.name_y = top + tag_h;
  };
  auto fits = [&](const PlateFit &f) {
    if (!tag.empty() && !plate_box_in(cx, cy, rx, ry, plate_text_w(tag_n, f.tag_px), f.tag_y, f.tag_px))
      return false;
    for (int i = 0; i < f.lines; i++)
      if (!plate_box_in(cx, cy, rx, ry, plate_text_w(plate_letters(f.line[i]), f.px), f.name_y + i * f.px, f.px))
        return false;
    return true;
  };
  for (int px : {16, 12, 8}) {
    for (int lines = 1; lines <= (can_split ? 2 : 1); lines++) {
      for (int tag_px : {12, 8}) {
        if (tag_px == 12 && !big)
          continue;
        PlateFit f;
        f.px = px;
        f.tag_px = tag_px;
        f.lines = lines;
        if (lines == 1)
          f.line[0] = name;
        else {
          f.line[0] = two[0];
          f.line[1] = two[1];
        }
        place(f);
        if (fits(f))
          return f;
        if (tag.empty())
          break;  // the tag size doesn't matter
      }
    }
  }
  // Nothing fits: 8 px, two lines when there is a space, cut at the rim.
  PlateFit f;
  f.cut = true;
  f.lines = can_split ? 2 : 1;
  f.line[0] = can_split ? two[0] : name;
  f.line[1] = can_split ? two[1] : "";
  place(f);
  for (int i = 0; i < f.lines; i++)
    while (!f.line[i].empty() &&
           !plate_box_in(cx, cy, rx, ry, plate_text_w(plate_letters(f.line[i]), 8), f.name_y + i * 8, 8))
      plate_pop_letter(f.line[i]);
  return f;
}

// The tag over the name: "#12", or for a second-round plate the pack's
// round-two words without the "!" ("ROUND TWO!" reads "ROUND TWO"). "" for
// a plate with no number.
inline std::string plate_tag(const PlateLine &e, const std::string &round2_words) {
  if (e.round2) {
    std::string w = round2_words;
    while (!w.empty() && (w.back() == '!' || w.back() == ' '))
      w.pop_back();
    return w.empty() ? "ROUND TWO" : w;
  }
  return e.num.empty() ? "" : "#" + e.num;
}

// ---- The quote ----------------------------------------------------------------

constexpr int kQuotePx = 12;
constexpr int kQuoteChars = 10;  // 12 px letters across one panel, a pixel each side
constexpr int kPlateRisePxS = 14;  // the quote rises this many px a second
constexpr uint32_t kPlateHoldMs = 4000;  // a quote that fits holds in the middle

// Words into lines of at most max_chars letters; a longer word is broken.
inline std::vector<std::string> plate_wrap(const std::string &s, size_t max_chars) {
  std::vector<std::string> lines;
  std::string line;
  size_t p = 0;
  while (p < s.size()) {
    size_t sp = s.find(' ', p);
    if (sp == std::string::npos)
      sp = s.size();
    std::string w = s.substr(p, sp - p);
    p = sp + 1;
    while (plate_letters(w) > max_chars) {
      // A word longer than a line: as much as fits on a line of its own.
      if (!line.empty()) {
        lines.push_back(line);
        line.clear();
      }
      size_t cut = 0, n = 0;
      while (cut < w.size() && n < max_chars) {
        cut++;
        while (cut < w.size() && ((unsigned char) w[cut] & 0xC0) == 0x80)
          cut++;
        n++;
      }
      lines.push_back(w.substr(0, cut));
      w = w.substr(cut);
    }
    if (w.empty())
      continue;
    if (line.empty())
      line = w;
    else if (plate_letters(line) + 1 + plate_letters(w) <= max_chars)
      line += " " + w;
    else {
      lines.push_back(line);
      line = w;
    }
  }
  if (!line.empty())
    lines.push_back(line);
  return lines;
}

// A member's quote as the panel shows it, in quote marks; none for no quote.
inline std::vector<std::string> plate_quote_lines(const std::string &quote) {
  if (quote.empty())
    return {};
  return plate_wrap("\"" + quote + "\"", kQuoteChars);
}

// How long a quote text_h px tall takes on a board H px tall: one that fits
// rises to the middle and holds there; a longer one keeps rising off the
// top, so every line gets its turn.
inline bool plate_rise_holds(int text_h, int H) { return text_h <= H - 2; }

inline uint32_t plate_rise_ms(int text_h, int H) {
  if (text_h <= 0)
    return 0;
  if (plate_rise_holds(text_h, H))
    return (uint32_t) (H - (H - text_h) / 2) * 1000u / kPlateRisePxS + kPlateHoldMs;
  return (uint32_t) (H + text_h) * 1000u / kPlateRisePxS + 500;
}

// The top of the quote t ms after it starts: in from below the bottom edge.
inline int plate_rise_y(uint32_t t, int text_h, int H) {
  int y = H - (int) ((uint64_t) t * kPlateRisePxS / 1000u);
  if (plate_rise_holds(text_h, H) && y < (H - text_h) / 2)
    y = (H - text_h) / 2;
  return y;
}

// Whether the quote is still moving at t (the drawing redraws only then).
inline bool plate_rise_moving(uint32_t t, int text_h, int H) {
  if (plate_rise_holds(text_h, H))
    return plate_rise_y(t, text_h, H) > (H - text_h) / 2;
  return plate_rise_y(t, text_h, H) > -text_h;
}

// ---- Page times ------------------------------------------------------------------

constexpr uint32_t kPlateWipeMs = 1300;  // the sprite crosses the board, uncovering the name
constexpr uint32_t kPlateBigMs = 4000;   // the big oval, the crossing included
constexpr uint32_t kPlateFoldMs = 700;   // the oval folds into one panel's plate
constexpr uint32_t kPlateNoQuoteMs = 6000;  // a member with no quote: the big oval alone
constexpr uint32_t kPartyMs = 12000;
constexpr uint32_t kPartyFlyMs = 1600;    // the sprite flies in
constexpr uint32_t kPartyHoverMs = 5800;  // it hovers to here, then the member's big oval
constexpr uint32_t kPartyFireFromMs = 1400, kPartyFireToMs = 4400;

// How long a member's page stays up: the oval, the fold (two panels only)
// and the quote's rise, with half a second to spare.
inline uint32_t plate_page_ms(bool two, size_t quote_lines, int H) {
  if (quote_lines == 0)
    return kPlateNoQuoteMs;
  return kPlateBigMs + (two ? kPlateFoldMs : 0) + plate_rise_ms((int) quote_lines * kQuotePx, H) + 500;
}

// The fold's ease: slow at both ends.
inline float plate_ease(float k) {
  k = k < 0 ? 0 : k > 1 ? 1 : k;
  return k * k * (3 - 2 * k);
}

// ---- The event page ----------------------------------------------------------------

constexpr int kPlateEventDays = 14;  // the event page shows this many days ahead

struct PlateEvent {
  bool show{false};
  std::string day;   // "TONIGHT", "TOMORROW", "THURSDAY", "NOV 12"
  std::string time;  // "6 PM", "6:30 PM", "" for no time
};

// "6 PM" for minute 1080, "6:30 PM" for 1110, "" for -1.
inline std::string plate_time_text(int minute) {
  if (minute < 0 || minute >= 24 * 60)
    return "";
  const int h = minute / 60, m = minute % 60;
  const int h12 = h % 12 == 0 ? 12 : h % 12;
  char buf[16];
  if (m == 0)
    snprintf(buf, sizeof(buf), "%d %s", h12, h < 12 ? "AM" : "PM");
  else
    snprintf(buf, sizeof(buf), "%d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
  return buf;
}

// The pack's event (YYYYMMDD, minutes after midnight or -1) on the panel's
// local day y-m-d: shown on its day and the kPlateEventDays before it, never
// after. Its day reads TODAY (TONIGHT from 5 PM), TOMORROW, the weekday
// within the week, and the date past that.
inline PlateEvent plate_event(uint32_t event_ymd, int event_minute, int y, unsigned m, unsigned d) {
  PlateEvent ev;
  if (event_ymd == 0)
    return ev;
  const int32_t day = wx_days_from_civil((int) (event_ymd / 10000), event_ymd / 100 % 100, event_ymd % 100);
  const int32_t today = wx_days_from_civil(y, m, d);
  const int32_t ahead = day - today;
  if (ahead < 0 || ahead > kPlateEventDays)
    return ev;
  ev.show = true;
  ev.time = plate_time_text(event_minute);
  static const char *const kDays[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
  static const char *const kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  if (ahead == 0)
    ev.day = event_minute >= 17 * 60 ? "TONIGHT" : "TODAY";
  else if (ahead == 1)
    ev.day = "TOMORROW";
  else if (ahead < 7)
    ev.day = kDays[((day % 7) + 11) % 7];  // 1970-01-01 was a Thursday
  else
    ev.day = std::string(kMonths[(event_ymd / 100 % 100) - 1]) + " " + std::to_string(event_ymd % 100);
  return ev;
}

}  // namespace espn
