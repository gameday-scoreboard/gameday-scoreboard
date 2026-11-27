// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The Game Day card: a QR code for the store (tt_gdcard_qr holds the
// address) beside a few words on getting a scoreboard, between games every
// kGamedayCardEveryMin minutes for one page's time (message.h kMsgPageMs). Its switch is in the Between
// games list (cards.h Card::GAMEDAY), off unless the build turns it on.
// Drawn by gd_gameday_card in firmware/pages/gameday-live.yaml.
// Pure, no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>

namespace espn {

constexpr int kGamedayCardEveryMin = 20;
// On a single 64 px panel the words and the code take turns: the words
// first for this long, then the code for the rest of the page.
constexpr uint32_t kGamedayCardWordsMs = 4000;
constexpr uint32_t kGamedayCardAccent = 0xF5B301;

// One line of the card's words, centered in its 64 px: its font's height
// (8, 12 or 16), its top and whether it is in the accent color (else white).
struct GamedayCardLine {
  const char *s;
  uint8_t px, y;
  bool accent;
};
constexpr int kGamedayCardWordings = 4;
constexpr int kGamedayCardLinesMax = 6;

// Each showing takes the next wording, so the same line doesn't come up
// every time. Every one says what it is and where to get one.
inline const GamedayCardLine *gameday_card_lines(int wording, int &count) {
  static const GamedayCardLine buy[] = {{"BUY ONE OF", 12, 2, true},   {"THESE AT", 12, 14, true},
                                        {"gameday-", 12, 30, false},   {"scoreboard", 12, 42, false},
                                        {".com", 12, 52, false}};
  static const GamedayCardLine want[] = {{"WANT THIS", 12, 1, true},   {"SCOREBOARD", 12, 13, true},
                                         {"AT HOME?", 12, 25, true},   {"gameday-", 8, 40, false},
                                         {"scoreboard", 8, 48, false}, {".com", 8, 56, false}};
  static const GamedayCardLine like[] = {{"LIKE THIS", 12, 1, true},   {"BOARD?", 12, 13, true},
                                         {"GET ONE AT", 12, 25, false}, {"gameday-", 8, 40, false},
                                         {"scoreboard", 8, 48, false}, {".com", 8, 56, false}};
  static const GamedayCardLine own[] = {{"OWN THIS", 16, 4, true},    {"SCOREBOARD", 12, 21, true},
                                        {"gameday-", 8, 40, false},   {"scoreboard", 8, 48, false},
                                        {".com", 8, 56, false}};
  switch (((wording % kGamedayCardWordings) + kGamedayCardWordings) % kGamedayCardWordings) {
    case 1:
      count = 6;
      return want;
    case 2:
      count = 6;
      return like;
    case 3:
      count = 5;
      return own;
    default:
      count = 5;
      return buy;
  }
}

}  // namespace espn
