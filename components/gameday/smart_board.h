// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The Smart board: up to four rows of text the owner sends, usually from
// Home Assistant templates (firmware/pages/smart_board.yaml). A row can carry
// color tags: [red] or [#FF6400] at the start colors the row, and a tag
// later in the row colors the words after it. A bracket that isn't a known
// color stays as written, so "[1]" shows. HA templates treat "{#" as the
// start of a comment, which is why the tags use square brackets.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "panel_text.h"

namespace espn {

constexpr size_t kSbRows = 4;
constexpr size_t kSbRowMax = 255;  // a row's text as sent, tags included
constexpr uint32_t kSbWhite = 0xFFFFFF;
// The rows' color once Home Assistant has been gone kSbStaleMs: what they
// say may no longer be true.
constexpr uint32_t kSbStaleRgb = 0x505050;
constexpr uint32_t kSbStaleMs = 2 * 60 * 1000;

struct SbColorName {
  const char *name;
  uint32_t rgb;
};

// clang-format off
constexpr SbColorName kSbColors[] = {
    {"red", 0xFF2020},   {"orange", 0xFF8000}, {"yellow", 0xFFD000}, {"green", 0x20E040},
    {"cyan", 0x00E0E0},  {"blue", 0x3070FF},   {"purple", 0xA040FF}, {"pink", 0xFF60C0},
    {"white", 0xFFFFFF}, {"gray", 0x808080},   {"grey", 0x808080},
};
// clang-format on

struct SbRow {
  // For the label: the words, or with LVGL's recolor commands when the row
  // changes color partway (recolor).
  std::string text;
  // The words with the tags taken out: the stale look shows these.
  std::string plain;
  uint32_t rgb{kSbWhite};  // the row's color, from a tag at its start
  bool recolor{false};
  bool empty() const { return plain.empty(); }
};

namespace detail {

inline int sb_hex(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// A color tag at s[at] ('['): its length, with its color in rgb; 0 when the
// bracket isn't one.
inline size_t sb_tag(const std::string &s, size_t at, uint32_t &rgb) {
  size_t close = s.find(']', at + 1);
  if (close == std::string::npos || close - at - 1 > 7 || close == at + 1)
    return 0;
  const std::string in = s.substr(at + 1, close - at - 1);
  if (in[0] == '#') {
    if (in.size() != 7)
      return 0;
    uint32_t v = 0;
    for (size_t i = 1; i < 7; i++) {
      int h = sb_hex(in[i]);
      if (h < 0)
        return 0;
      v = (v << 4) | (uint32_t) h;
    }
    rgb = v;
    return close - at + 1;
  }
  for (const auto &c : kSbColors) {
    const char *n = c.name;
    size_t i = 0;
    while (n[i] != '\0' && i < in.size() && (in[i] | 0x20) == n[i])
      i++;
    if (n[i] == '\0' && i == in.size()) {
      rgb = c.rgb;
      return close - at + 1;
    }
  }
  return 0;
}

}  // namespace detail

// One row as sent to how the label draws it. LVGL's recolor treats every
// '#' as the start of a color and has no escape for it, so a row that
// changes color partway drops its own '#' characters; a row in one color
// keeps them.
inline SbRow sb_parse(const std::string &raw) {
  struct Seg {
    uint32_t rgb;
    std::string text;
  };
  const std::string s = ::esphome::gameday::panel_text(raw.size() > kSbRowMax ? raw.substr(0, kSbRowMax) : raw);
  std::vector<Seg> segs{{kSbWhite, ""}};
  for (size_t i = 0; i < s.size();) {
    uint32_t rgb = 0;
    size_t n = s[i] == '[' ? detail::sb_tag(s, i, rgb) : 0;
    if (n == 0) {
      // Spaces before the first word don't count, so "[red] Door" lines up.
      if (!(s[i] == ' ' && segs.size() == 1 && segs[0].text.empty()))
        segs.back().text += s[i];
      i++;
      continue;
    }
    if (segs.back().text.empty())
      segs.back().rgb = rgb;  // a tag at the start, or two in a row: the last wins
    else
      segs.push_back({rgb, ""});
    i += n;
  }
  while (!segs.empty() && segs.back().text.empty() && segs.size() > 1)
    segs.pop_back();
  while (!segs.back().text.empty() && segs.back().text.back() == ' ')
    segs.back().text.pop_back();

  SbRow row;
  row.rgb = segs[0].rgb;
  for (const auto &g : segs)
    row.plain += g.text;
  if (segs.size() == 1) {
    row.text = row.plain;
    return row;
  }
  row.recolor = true;
  auto no_hash = [](const std::string &t) {
    std::string o;
    for (char c : t)
      if (c != '#')
        o += c;
    return o;
  };
  row.text = no_hash(segs[0].text);
  for (size_t k = 1; k < segs.size(); k++) {
    const std::string t = no_hash(segs[k].text);
    if (t.empty())
      continue;
    if (segs[k].rgb == row.rgb) {
      row.text += t;
      continue;
    }
    char cmd[10];
    snprintf(cmd, sizeof cmd, "#%06x ", (unsigned) (segs[k].rgb & 0xFFFFFF));
    row.text += cmd;
    row.text += t;
    row.text += '#';
  }
  return row;
}

// Where the rows go: the filled ones share the height evenly, top to bottom,
// so fewer rows get bigger text. y and h get one entry per row, h 0 for an
// empty row. Returns how many rows are filled.
inline int sb_layout(const bool filled[kSbRows], int32_t H, int32_t y[kSbRows], int32_t h[kSbRows]) {
  int n = 0;
  for (size_t i = 0; i < kSbRows; i++)
    n += filled[i] ? 1 : 0;
  int k = 0;
  for (size_t i = 0; i < kSbRows; i++) {
    if (!filled[i] || n == 0) {
      y[i] = 0;
      h[i] = 0;
      continue;
    }
    y[i] = H * k / n;
    h[i] = H * (k + 1) / n - y[i];
    k++;
  }
  return n;
}

// The font for a row: the biggest that fits the row's height, or the size
// under it when that one makes the words fit the panel's width; otherwise the
// biggest, and the row scrolls. One step down at most, so a lone row doesn't
// shrink to small print to dodge a scroll, and never below min_px tall, so the
// text stays readable across a room. heights and widths are the fonts' line
// height and this row's text width in each, biggest first. Returns the font's
// index.
inline size_t sb_font(int32_t row_h, int32_t W, const int32_t *heights, const int32_t *widths, size_t n,
                      int32_t min_px) {
  size_t tallest = n;
  for (size_t i = 0; i < n; i++) {
    if (heights[i] <= row_h && heights[i] >= min_px) {
      tallest = i;
      break;
    }
  }
  if (tallest == n)
    return n - 1;
  if (widths[tallest] <= W)
    return tallest;
  const size_t next = tallest + 1;
  if (next < n && heights[next] >= min_px && widths[next] <= W)
    return next;
  return tallest;
}

}  // namespace espn
