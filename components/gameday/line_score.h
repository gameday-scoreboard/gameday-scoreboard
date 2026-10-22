#pragma once

// The line score at halftime and at the final: each side's points by
// quarter, period or inning, and the total. Pure, so the host tests check
// the text; firmware/pages/gameday-live.yaml draws it in the Spleen fonts,
// which are monospaced, so columns are made of spaces.

#include <algorithm>
#include <string>
#include <vector>

#include "espn_parse.h"

namespace espn {

// Football and basketball halftime. Hockey's intermissions and soccer's half
// time are left out: hockey's board already says which period ended, and
// ESPN sends soccer no line score.
inline bool at_halftime(const GameSnapshot &g) {
  if (g.state != GameState::IN)
    return false;
  if (g.sport == Sport::FOOTBALL)
    return g.short_detail.find("Halftime") != std::string::npos;
  if (g.sport == Sport::BASKETBALL)
    return g.nba.phase == Phase::HALFTIME;
  return false;
}

// The board shows the line score: halftime or a completed final, with
// ESPN's points by period for both sides.
inline bool line_score_due(const GameSnapshot &g) {
  if (!g.valid || g.sport == Sport::SOCCER || g.team_line_count == 0 || g.opp_line_count == 0)
    return false;
  return (g.state == GameState::POST && g.completed) || at_halftime(g);
}

struct LineScoreText {
  // 0 when there is nothing to draw, else the Spleen font's height: 12
  // (6 px a character) or 8 (5 px).
  uint8_t font{0};
  // A display under 128 wide puts the teams side by side, in the logos'
  // places, with the periods down the left (body only). Wider ones draw a
  // classic line score: the abbreviation column, then head over body.
  bool columns{false};
  uint8_t abbr_cols{0};  // characters the abbreviation column takes, gap included
  std::string head;      // the period numbers, over body's columns
  std::string body;      // one line per team (ours first), or per period in columns
  std::string title;     // "Final", "Final/OT", "Halftime"
};

namespace detail {

struct LineCol {
  std::string head, a, b;
  uint8_t gap{1};  // spaces before the column
  uint8_t width{0};
};

inline std::string pad_left(const std::string &s, size_t w) {
  return s.size() >= w ? s : std::string(w - s.size(), ' ') + s;
}

inline std::string pad_right(const std::string &s, size_t w) {
  return s.size() >= w ? s : s + std::string(w - s.size(), ' ');
}

inline size_t line_cols_chars(std::vector<LineCol> &cols) {
  size_t n = 0;
  for (auto &c : cols) {
    size_t w = std::max(c.head.size(), std::max(c.a.size(), c.b.size()));
    c.width = (uint8_t) std::max<size_t>(c.width, w);
    n += c.gap + c.width;
  }
  return n;
}

// The points of periods [from, to) for one side, "" when the side has none
// of them yet.
inline std::string line_sum(const uint8_t *lines, uint8_t count, size_t from, size_t to) {
  if (from >= count)
    return "";
  int sum = 0;
  for (size_t i = from; i < to && i < count; i++)
    sum += lines[i];
  return std::to_string(sum);
}

inline uint8_t line_regulation(const GameSnapshot &g) {
  switch (g.sport) {
    case Sport::BASEBALL:
      return 9;
    case Sport::HOCKEY:
      return 3;
    case Sport::BASKETBALL:
      return g.nba.regulation;
    default:
      return 4;
  }
}

// What the periods past regulation are called in one column.
inline const char *line_extra_name(const GameSnapshot &g, uint8_t periods) {
  if (g.sport == Sport::HOCKEY && !g.nhl.playoffs && periods >= 5)
    return "SO";
  return "OT";
}

// Quarters, periods or halves, an OT column for everything past regulation,
// and the total.
inline std::vector<LineCol> line_period_cols(const GameSnapshot &g) {
  std::vector<LineCol> cols;
  uint8_t reg = line_regulation(g);
  uint8_t periods = std::max(g.team_line_count, g.opp_line_count);
  for (uint8_t p = 0; p < reg; p++) {
    LineCol c;
    c.head = std::to_string(p + 1);
    c.a = p < g.team_line_count ? std::to_string(g.team_lines[p]) : "";
    c.b = p < g.opp_line_count ? std::to_string(g.opp_lines[p]) : "";
    c.width = 2;
    c.gap = p == 0 ? 0 : 1;
    cols.push_back(c);
  }
  if (periods > reg) {
    LineCol c;
    c.head = line_extra_name(g, periods);
    c.a = line_sum(g.team_lines, g.team_line_count, reg, GameSnapshot::kLinesMax);
    c.b = line_sum(g.opp_lines, g.opp_line_count, reg, GameSnapshot::kLinesMax);
    c.width = 2;
    cols.push_back(c);
  }
  LineCol t;
  t.head = "T";
  t.a = std::to_string(g.team_score);
  t.b = std::to_string(g.opp_score);
  t.gap = 2;
  cols.push_back(t);
  return cols;
}

// Baseball's innings in one of three shapes, then the runs, hits and errors:
// grouped in threes like a box score (style 0), one character each with no
// gaps (1), or a column each (2). Style 3 is the runs, hits and errors alone.
// An inning a side never batted in a final (the home side ahead after the
// top of the 9th) is an x.
inline std::vector<LineCol> line_inning_cols(const GameSnapshot &g, int style, bool hits_errors) {
  std::vector<LineCol> cols;
  uint8_t innings = std::max(g.team_line_count, g.opp_line_count);
  bool final = g.state == GameState::POST;
  auto cell = [&](const uint8_t *lines, uint8_t count, uint8_t i) -> std::string {
    if (i < count)
      return std::to_string(lines[i]);
    return final ? "x" : "";
  };
  if (style < 3) {
    for (uint8_t i = 0; i < innings; i++) {
      LineCol c;
      c.head = std::to_string(i + 1);
      if (style < 2)
        c.head = c.head.substr(c.head.size() - 1);
      c.a = cell(g.team_lines, g.team_line_count, i);
      c.b = cell(g.opp_lines, g.opp_line_count, i);
      c.width = 1;
      c.gap = i == 0 ? 0 : style == 0 ? (i % 3 == 0 ? 1 : 0) : style == 1 ? 0 : 1;
      cols.push_back(c);
    }
  }
  LineCol r;
  r.head = "R";
  r.a = std::to_string(g.team_score);
  r.b = std::to_string(g.opp_score);
  r.gap = style < 3 ? 2 : 0;
  cols.push_back(r);
  if (!hits_errors)
    return cols;
  LineCol h;
  h.head = "H";
  h.a = std::to_string(g.mlb.team_hits);
  h.b = std::to_string(g.mlb.opp_hits);
  cols.push_back(h);
  LineCol e;
  e.head = "E";
  e.a = std::to_string(g.mlb.team_errors);
  e.b = std::to_string(g.mlb.opp_errors);
  cols.push_back(e);
  return cols;
}

inline bool line_single_digits(const GameSnapshot &g) {
  for (uint8_t i = 0; i < g.team_line_count; i++)
    if (g.team_lines[i] > 9)
      return false;
  for (uint8_t i = 0; i < g.opp_line_count; i++)
    if (g.opp_lines[i] > 9)
      return false;
  return true;
}

// The side-by-side form's rows: a label, then our points and theirs.
struct LineRow {
  std::string label, a, b;
};

inline std::vector<LineRow> line_rows(const GameSnapshot &g) {
  std::vector<LineRow> rows;
  auto add = [&](const std::string &label, size_t from, size_t to) {
    rows.push_back({label, line_sum(g.team_lines, g.team_line_count, from, to),
                    line_sum(g.opp_lines, g.opp_line_count, from, to)});
  };
  uint8_t reg = line_regulation(g);
  uint8_t periods = std::max(g.team_line_count, g.opp_line_count);
  if (g.sport == Sport::BASEBALL) {
    rows.push_back({"R", std::to_string(g.team_score), std::to_string(g.opp_score)});
    rows.push_back({"H", std::to_string(g.mlb.team_hits), std::to_string(g.mlb.opp_hits)});
    rows.push_back({"E", std::to_string(g.mlb.team_errors), std::to_string(g.mlb.opp_errors)});
    return rows;
  }
  if (g.sport == Sport::HOCKEY) {
    for (uint8_t p = 0; p < reg; p++)
      add(std::to_string(p + 1), p, p + 1);
  } else if (reg == 4) {
    // Quarters by halves: six rows don't fit over the ticker
    add("1H", 0, 2);
    add("2H", 2, 4);
  } else {
    add("1H", 0, 1);
    add("2H", 1, 2);
  }
  if (periods > reg)
    add(line_extra_name(g, periods), reg, GameSnapshot::kLinesMax);
  rows.push_back({"T", std::to_string(g.team_score), std::to_string(g.opp_score)});
  return rows;
}

}  // namespace detail

// The line score for a display width_px wide, in the largest font that fits.
// Empty (font 0) when line_score_due() is false.
inline LineScoreText line_score_text(const GameSnapshot &g, int width_px) {
  LineScoreText out;
  if (!line_score_due(g))
    return out;
  if (g.state == GameState::POST)
    out.title = g.short_detail.rfind("Final", 0) == 0 ? g.short_detail : "Final";
  else
    out.title = "Halftime";

  if (width_px < 128) {
    // Two team columns of five, under the abbreviations, after a two
    // character label: 12 characters, 60 px in the small font.
    out.font = 8;
    out.columns = true;
    for (const auto &r : detail::line_rows(g)) {
      if (!out.body.empty())
        out.body += '\n';
      out.body += detail::pad_right(r.label, 2) + detail::pad_left(r.a, 5) + detail::pad_left(r.b, 5);
    }
    return out;
  }

  out.abbr_cols = (uint8_t) (std::max(g.team_abbr.size(), g.opp_abbr.size()) + 1);
  std::vector<std::vector<detail::LineCol>> tries;
  if (g.sport == Sport::BASEBALL) {
    bool single = detail::line_single_digits(g);
    if (single) {
      tries.push_back(detail::line_inning_cols(g, 0, true));
      tries.push_back(detail::line_inning_cols(g, 1, true));
    }
    tries.push_back(detail::line_inning_cols(g, 2, true));
    tries.push_back(detail::line_inning_cols(g, 2, false));
    tries.push_back(detail::line_inning_cols(g, 3, true));
  } else {
    tries.push_back(detail::line_period_cols(g));
  }
  std::vector<detail::LineCol> *pick = nullptr;
  for (auto &cols : tries) {
    size_t chars = out.abbr_cols + detail::line_cols_chars(cols);
    if ((int) chars * 6 <= width_px) {
      out.font = 12;
    } else if ((int) chars * 5 <= width_px) {
      out.font = 8;
    } else {
      continue;
    }
    pick = &cols;
    break;
  }
  if (pick == nullptr) {
    // Nothing fits: the last try in the small font, cut at the edge.
    pick = &tries.back();
    out.font = 8;
  }
  std::string a, b;
  for (const auto &c : *pick) {
    std::string gap(c.gap, ' ');
    out.head += gap + detail::pad_left(c.head, c.width);
    a += gap + detail::pad_left(c.a, c.width);
    b += gap + detail::pad_left(c.b, c.width);
  }
  out.body = a + "\n" + b;
  return out;
}

}  // namespace espn
