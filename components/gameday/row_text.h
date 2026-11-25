// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

// The text on a wide board's situation row while a game is on
// (firmware/pages/gameday-live.yaml, gd_row_show): the lines the page takes
// turns showing under the clock. Each line comes in up to three forms, the
// longest first, and the page draws the longest that fits the room the row
// has (the clear part of the field strip in football, the space between the logos'
// marks in soccer, the full width in basketball). Pure, with no ESPHome
// includes, so the host tests build it.
//
// Football: down and distance, then the win chance after a big swing
// (WinSwing below).
// Basketball: what is happening in the game (story.h: a run, a drought, a
// lead change), the last basket, each side's points leader, its rebounds or
// assists leader, the playoff series, the win chance after a big swing. No timeout line: the stopped clock already shows
// the break, and ESPN gives no count of basketball timeouts left.
// Soccer: the latest goal.
//
// Every line is white (football's down turns red in the red zone): a team
// color, even lifted, was hard to read across a room, and the logos and
// abbreviations above already carry the colors.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "espn_parse.h"
#include "game_leaders.h"
#include "panel_text.h"
#include "story.h"

namespace espn {

struct RowLine {
  // Its forms, longest first, one per '\n'-separated piece. One string, not
  // three: the component keeps the last update, these lines included, in
  // internal RAM.
  std::string forms;
  uint32_t rgb{0xFFFFFF};
  uint8_t secs{4};  // how long its turn lasts
  // Whose line it is: 1 ours, 2 theirs, shown as a tag in the team's color
  // with its abbreviation before the words; 3 a tag at each end, ours on the
  // left as on the board; 0 none.
  uint8_t side{0};

  // A shorter form, for when the longer ones don't fit.
  void add_form(const std::string &f) {
    if (f.empty())
      return;
    this->forms += '\n';
    this->forms += f;
  }
  // Form i, "" past the last.
  std::string form(int i) const {
    size_t at = 0;
    for (; i > 0; i--) {
      at = this->forms.find('\n', at);
      if (at == std::string::npos)
        return "";
      at++;
    }
    return this->forms.substr(at, this->forms.find('\n', at) - at);
  }
};

struct RowLines {
  static constexpr uint8_t kMax = 6;
  RowLine line[kMax];
  uint8_t n{0};
  // Bumped when the lead line has news (a new basket, a new down), so the
  // page shows it at once instead of waiting for its turn.
  uint32_t news{0};

  RowLine *add(const std::string &text, uint32_t rgb, uint8_t secs = 4, uint8_t side = 0) {
    if (n >= kMax || text.empty())
      return nullptr;
    RowLine &l = line[n++];
    l = RowLine();
    l.forms = ::esphome::gameday::panel_text(text);
    l.rgb = rgb;
    l.secs = secs;
    l.side = side;
    return &l;
  }
};

// The latest basket the board has seen, held until the next one: ESPN's
// last play moves on with every rebound and foul, so the panel keeps the
// last one that scored. Follows one game; another game starts it over.
struct RowHold {
  std::string event_id;
  std::string basket;  // "McDaniels 3", "Burries FT"
  std::string play;    // the last play worth reading, for the ticker
  int total{-1};       // both scores added, at the last read
  uint8_t basket_side{0}, play_side{0};  // whose: 1 ours, 2 theirs, 0 not known

  // One read of the board game.
  void read(const GameSnapshot &g) {
    if (g.event_id != this->event_id) {
      this->event_id = g.event_id;
      this->basket.clear();
      this->play.clear();
      this->total = -1;
    }
    if (g.sport == Sport::BASKETBALL && g.state == GameState::IN && !g.last_play.empty() &&
        !basketball_junk_play(g) && !timeout_play(g.last_play) && g.last_play != this->play) {
      this->play = g.last_play;
      this->play_side = basketball_play_side(g);
    }
    if (g.sport != Sport::BASKETBALL || g.state != GameState::IN)
      return;
    // A basket the read didn't name (another play came after it): the one
    // held isn't the last any more.
    const int total = g.team_score + g.opp_score;
    if (total != this->total && this->total >= 0)
      this->basket.clear();
    this->total = total;
    if (g.play_value <= 0 || g.play_athlete.empty())
      return;
    std::string who = detail::leader_short_surname(g.play_athlete);
    bool ft = g.play_type.find("Free Throw") != std::string::npos ||
              g.play_type.find("FreeThrow") != std::string::npos;  // the NBA's, college's
    this->basket = who + " " + (ft ? std::string("FT") : std::to_string(g.play_value));
    this->basket_side = g.play_team == g.team_id ? 1 : g.play_team == g.opp_id ? 2 : 0;
  }
};

struct RowOptions {
  bool win_chance{true};  // the Win chance switch (winbar)
  bool last_play{true};   // the Last play switch: the last basket
};

// When the win chance is worth showing: the first number of a game, a
// football touchdown, field goal or turnover, or a move of 10 points or more
// since it last showed, at most once every 3 minutes for a move. On the row
// a showing lasts kShowMs, a turn or two; on the ticker it stays until the
// next play, since a ticker that changes restarts its scroll and the win
// chance is its last part. A steady "WSH 51% to win" every 12 seconds said
// nothing new. Follows one game; another game starts it over.
struct WinSwing {
  static constexpr uint32_t kShowMs = 25 * 1000;
  static constexpr uint32_t kGapMs = 3 * 60 * 1000;
  static constexpr uint32_t kScoreGapMs = 2 * 60 * 1000;  // a touchdown and its try
  static constexpr int kSwing = 10;  // points, on our side's chance
  std::string event_id;
  int shown{-1};  // our side's chance when it last showed, -1 not yet
  std::string play;   // the last play at the last read (its type and text)
  std::string hold;   // the last play when the latest showing began
  int scored_at{-1};  // both scores added when a score last showed it
  uint32_t scored_ms{0};  // and when
  uint32_t start{0};  // millis() when the last showing began
  bool on{false};     // on the row
  bool ticker{false};  // in the ticker: the showing, then until the next play
  // Why the showing that this read began started ("first", "score",
  // "turnover", "swing"), for the log; nullptr when this read began none.
  const char *began{nullptr};

  // One read of the board game (redraws of the same read change nothing).
  // True while the win chance should be on the row and the ticker.
  bool read(const GameSnapshot &g, uint32_t now) {
    if (g.event_id != this->event_id) {
      *this = WinSwing();
      this->event_id = g.event_id;
    }
    const int pct = g.team_win_pct;
    if (pct < 1 || pct > 99)
      return false;
    const bool football = g.sport == Sport::FOOTBALL;
    // The score and the number come from the same last play, so a showing
    // never starts on the number from before it. The try after a touchdown
    // doesn't show it again: ESPN can report it as the touchdown play again
    // with new text, so a score also has to add 3 points or more since the
    // last score that showed it, and come 2 minutes or more after it, in
    // case ESPN's score lags the play (seen live, WSH at NYG 11/12).
    // ESPN's play id when it has one: rewritten words are the same play.
    const std::string key = g.play_id != 0 ? std::to_string(g.play_id) : g.play_type + "|" + g.last_play;
    const bool new_play = !this->play.empty() && key != this->play;
    this->play = key;
    const int total = g.team_score + g.opp_score;
    const bool scored = football && new_play && g.play_value >= 3 &&
                        (this->scored_at < 0 || (total - this->scored_at >= 3 && now - this->scored_ms >= kScoreGapMs));
    if (scored) {
      this->scored_at = total;
      this->scored_ms = now;
    }
    const bool turnover = football && new_play && turnover_play(g.play_type);
    const bool showing = this->on && now - this->start < kShowMs;
    if (!showing)
      this->on = false;
    const bool swing = this->shown >= 0 && std::abs(pct - this->shown) >= kSwing && now - this->start >= kGapMs;
    this->began = nullptr;
    if (this->shown < 0 || scored || turnover || swing) {
      this->on = true;
      this->start = now;
      this->began = this->shown < 0 ? "first" : scored ? "score" : turnover ? "turnover" : "swing";
      this->hold = key;
    }
    // The number it shows is the latest; a move counts from the last one shown.
    if (this->on)
      this->shown = pct;
    else if (key != this->hold)
      this->hold.clear();
    this->ticker = this->on || !this->hold.empty();
    return this->on;
  }

  // ESPN's play types where the ball changes sides on a mistake.
  static bool turnover_play(const std::string &t) {
    // "Fumble Recovery (Opponent)" in the NFL, "Sack Opp Fumble Recovery" in college.
    return t.find("Interception") != std::string::npos || t.find("Fumble Recovery (Opponent)") != std::string::npos ||
           t.find("Opp Fumble Recovery") != std::string::npos || t.find("Fumble Return") != std::string::npos ||
           t.find("Blocked") != std::string::npos;
  }
};

// The win chance as words, "ND 84% to win", for the side ESPN favors.
// Nothing without a number.
inline void add_win_chance(RowLines &r, const GameSnapshot &g, uint8_t secs) {
  if (g.team_win_pct < 1 || g.team_win_pct > 99)
    return;
  bool ours = g.team_win_pct >= 50;
  int pct = ours ? g.team_win_pct : 100 - g.team_win_pct;
  const std::string &abbr = ours ? g.team_abbr : g.opp_abbr;
  std::string num = abbr + " " + std::to_string(pct) + "%";
  RowLine *l = r.add(num + " to win", 0xFFFFFF, secs);
  if (l == nullptr)
    return;
  l->add_form(::esphome::gameday::panel_text(num + " win"));
  l->add_form(::esphome::gameday::panel_text(num));
}

// The last basket, "McDaniels made 3", so its number isn't read as the
// player's points in the game.
inline void add_basket(RowLines &r, const RowHold &hold, uint32_t rgb) {
  if (hold.basket.empty())
    return;
  const size_t sp = hold.basket.rfind(' ');
  const std::string what = hold.basket.substr(sp + 1);
  RowLine *l = r.add(hold.basket.substr(0, sp) + " made " + what, rgb, 4, hold.basket_side);
  if (l != nullptr)
    l->add_form("made a " + what);
}

// A side's points leader, "Nesmith 19 pts".
inline void add_scorer(RowLines &r, uint8_t side, const std::string &scorer, uint32_t rgb) {
  // Under 6 points it says nothing yet.
  const size_t sp = scorer.rfind(' ');
  if (sp == std::string::npos || atoi(scorer.c_str() + sp) < 6)
    return;
  RowLine *l = r.add(scorer + " pts", rgb, 4, side);
  if (l != nullptr)
    l->add_form(::esphome::gameday::panel_text(scorer));
}

// A side's rebounds or assists leader, "Siakam 9 reb".
inline void add_extra(RowLines &r, uint8_t side, const char *extra, uint32_t rgb) {
  if (extra[0] != '\0')
    r.add(extra, rgb, 4, side);
}

// The team stats a line at a time, each side's number by its own tag: "47%
// FG 29%", "3/14 3PT 5/25", "38 reb 22". Each once it has a few behind it.
inline void add_box(RowLines &r, const GameSnapshot &g, uint32_t rgb) {
  const Basketball::Box &a = g.nba.team_box, &b = g.nba.opp_box;
  if (!a.has || !b.has)
    return;
  char buf[32];
  auto pct = [](const Basketball::Box &x) { return (x.fg_made * 200 + x.fg_tried) / (2 * x.fg_tried); };
  if (a.fg_tried >= 8 && b.fg_tried >= 8) {
    snprintf(buf, sizeof(buf), "%d%% FG %d%%", pct(a), pct(b));
    r.add(buf, rgb, 4, 3);
  }
  if (a.three_tried >= 4 && b.three_tried >= 4) {
    snprintf(buf, sizeof(buf), "%u/%u 3PT %u/%u", a.three_made, a.three_tried, b.three_made, b.three_tried);
    RowLine *l = r.add(buf, rgb, 4, 3);
    snprintf(buf, sizeof(buf), "%u threes %u", a.three_made, b.three_made);
    if (l != nullptr)
      l->add_form(buf);
  }
  if (a.rebounds + b.rebounds >= 10) {
    snprintf(buf, sizeof(buf), "%u reb %u", a.rebounds, b.rebounds);
    r.add(buf, rgb, 4, 3);
  }
}

inline void add_story(RowLines &r, const StoryLine &s, uint32_t rgb) {
  RowLine *l = r.add(s.text, rgb, 5, s.side);
  if (l != nullptr && s.brief[0] != '\0')
    l->add_form(s.brief);
}

// Down and distance, "2nd & 17", then "2nd&17" and "2&17" for when the
// clear part of the field strip is narrower (the ball or the line to gain
// near midfield).
inline void add_down(RowLines &r, const GameSnapshot &g, uint32_t rgb) {
  RowLine *l = r.add(g.short_down_distance, rgb, 8);
  if (l == nullptr)
    return;
  std::string tight = l->form(0);
  size_t amp = tight.find(" & ");
  if (amp == std::string::npos)
    return;
  tight.replace(amp, 3, "&");
  l->add_form(tight);
  // "2nd&17" to "2&17": the ordinal's letters after the leading digit.
  if (tight.size() > 3 && isdigit((unsigned char) tight[0]) && isalpha((unsigned char) tight[1]) &&
      isalpha((unsigned char) tight[2]) && tight[3] == '&')
    l->add_form(tight.substr(0, 1) + tight.substr(3));
}

// The row's lines for the board game, into r (filled in place: the
// component's loop stack has no room to spare for a copy). `playing` is a
// game on and not at halftime (gameday.cpp emit_); `hold` has already read
// this game.
inline void fill_row_lines(RowLines &r, const GameSnapshot &g, const RowHold &hold, const RowOptions &o,
                           bool playing, const Story *story = nullptr) {
  r.n = 0;
  if (!g.valid || g.state != GameState::IN)
    return;
  const uint32_t white = 0xFFFFFF;
  switch (g.sport) {
    case Sport::FOOTBALL:
      if (!playing)
        break;
      // Down and distance keeps most of the time; red in the red zone.
      add_down(r, g, g.is_red_zone ? 0xFF3030 : white);
      if (o.win_chance)
        add_win_chance(r, g, 4);
      break;
    case Sport::BASKETBALL: {
      const std::string series = basketball_series(g);
      if (!playing) {
        r.add(series, white);
        break;
      }
      // The best of the game's story leads; the rest comes after the last
      // basket. With two stories up, the leaders give way to them.
      const int told = story != nullptr ? story->count(g) : 0;
      int at = 0;
      for (; at < told && story->line(at).score >= Story::kLead; at++)
        add_story(r, story->line(at), white);
      // Not while the ticker is saying the same basket.
      if (o.last_play && hold.event_id == g.event_id && g.play_value <= 0)
        add_basket(r, hold, white);
      for (; at < told; at++)
        add_story(r, story->line(at), white);
      if (o.win_chance)
        add_win_chance(r, g, 4);
      r.add(series, white);
      add_scorer(r, 1, g.nba.team_scorer, white);
      add_scorer(r, 2, g.nba.opp_scorer, white);
      if (told < 2) {
        add_extra(r, 1, g.nba.team_extra, white);
        add_extra(r, 2, g.nba.opp_extra, white);
      }
      // A quiet stretch: the team stats, unless the ticker has them.
      if (told == 0 && !timeout_play(g.last_play) && !(basketball_junk_play(g) && hold.play.empty()))
        add_box(r, g, white);
      break;
    }
    case Sport::SOCCER:
      r.add(soccer_situation(g), white);
      break;
    default:
      break;
  }
}

inline RowLines row_lines(const GameSnapshot &g, const RowHold &hold, const RowOptions &o, bool playing,
                          const Story *story = nullptr) {
  RowLines r;
  fill_row_lines(r, g, hold, o, playing, story);
  return r;
}

// What counts as news on the row: the held basket, football's down and
// distance, soccer's latest goal. The component bumps RowLines::news when
// it changes.
inline std::string row_news(const GameSnapshot &g, const RowHold &hold) {
  if (g.sport == Sport::BASKETBALL)
    return hold.basket;
  if (g.sport == Sport::FOOTBALL)
    return g.short_down_distance;
  return "";
}

}  // namespace espn
