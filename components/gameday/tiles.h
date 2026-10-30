// The owner's "Games on the board" setting for the multi-game board
// (multi_board.h): its values, how many tiles they give on a display, and
// the grid those tiles make. No parser includes, so set_request.h and the
// device API tests take it as it is.
#pragma once

#include <cstdint>
#include <string>

namespace espn {

// The owner's "Games on the board" (Prefs8.spare[0]; 0 is a panel from
// before the setting, so ONE keeps today's board).
enum class Tiles : uint8_t { ONE = 0, TWO = 1, FOUR = 2, AUTO = 3 };
constexpr int kTilesMax = 4;

inline const char *tiles_name(uint8_t v) {
  switch ((Tiles) v) {
    case Tiles::TWO:
      return "2";
    case Tiles::FOUR:
      return "4";
    case Tiles::AUTO:
      return "auto";
    default:
      return "1";
  }
}

// The set API's tiles=1|2|4|auto. Anything else is refused.
inline bool parse_tiles(const std::string &v, uint8_t &out) {
  if (v == "1") {
    out = (uint8_t) Tiles::ONE;
  } else if (v == "2") {
    out = (uint8_t) Tiles::TWO;
  } else if (v == "4") {
    out = (uint8_t) Tiles::FOUR;
  } else if (v == "auto") {
    out = (uint8_t) Tiles::AUTO;
  } else {
    return false;
  }
  return true;
}

// The grid for count tiles on a display width_px wide. Two games sit side
// by side; four are two by two on one panel and four across on two.
struct TileGrid {
  int cols{1}, rows{1};
  int tile_w{0}, tile_h{64};
};

inline TileGrid tile_grid(int count, int width_px) {
  TileGrid g;
  if (count >= 4) {
    g.cols = width_px >= 256 ? 4 : 2;
    g.rows = width_px >= 256 ? 1 : 2;
  } else if (count >= 2) {
    g.cols = 2;
  }
  g.tile_w = width_px / g.cols;
  g.tile_h = 64 / g.rows;
  return g;
}

// How many tiles the board draws. A 64-wide panel never tiles. Auto shows
// two games with two live, four with three or more (the fourth tile takes a
// final or a game to come), and one big game with one live. A fixed 2 or 4
// with only one game on hand is the big board too: it says more.
inline int tile_count(uint8_t setting, int width_px, int live_n, int total_n) {
  if (width_px < 128 || setting == (uint8_t) Tiles::ONE || setting > (uint8_t) Tiles::AUTO)
    return 1;
  if (total_n <= 1)
    return 1;
  if (setting == (uint8_t) Tiles::AUTO) {
    if (live_n <= 1)
      return 1;
    return live_n >= 3 ? 4 : 2;
  }
  return setting == (uint8_t) Tiles::TWO ? 2 : 4;
}

}  // namespace espn
