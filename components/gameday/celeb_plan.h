// The celebration a play gets, as data: the owner's Celebrations setting,
// the kinds of show, the plan the component hands the page, and the color
// helpers every sport's picker shares. Pure, no ESPHome includes.
// celebrate.h decides scores and wins; the big plays each sport reads on its
// own are in celeb_<sport>.h, which build on this header alone.
#pragma once
#include <cstdint>
#include <string>

#include "espn_parse.h"

namespace espn {

// The owner's setting. Off: no full-screen splash at all, the board just
// updates. Subtle: a short solid flash in the team's color with white words,
// the look before fireworks. Full: the animations in celebrate_fx.h.
enum class CelebLevel : uint8_t { OFF = 0, SUBTLE = 1, FULL = 2 };

// Saved as one byte where 0 is Full, so a panel saved before the setting
// existed keeps celebrating.
inline CelebLevel celeb_level_from_stored(uint8_t v) {
  return v == 1 ? CelebLevel::OFF : v == 2 ? CelebLevel::SUBTLE : CelebLevel::FULL;
}
inline uint8_t celeb_level_to_stored(CelebLevel l) {
  return l == CelebLevel::OFF ? 1 : l == CelebLevel::SUBTLE ? 2 : 0;
}
inline const char *celeb_level_name(CelebLevel l) {
  return l == CelebLevel::OFF ? "off" : l == CelebLevel::SUBTLE ? "subtle" : "full";
}

enum class CelebKind : uint8_t {
  NONE = 0,
  FLASH,      // solid team color, white words (Subtle, and Full's tiny scores)
  MUTED,      // the other side: the same, dimmed, shorter
  TOUCHDOWN,  // goalposts, fireworks in the team's colors
  WAVE,       // field goal, extra point, two points, a safety, runs: a light sweep
  HOME_RUN,   // the ball arcs out of the park and bursts
  GOAL_NET,   // soccer: the net ripples where the ball hit
  GOAL_LAMP,  // hockey: the red goal lamp spins
  THREE,      // basketball, three points in one read: the ball drops through the hoop
  WIN,        // the victory screen: the name, the final score, confetti
  // The big plays: frame-by-frame pixel art (celeb_art.h), one kind a play,
  // picked by celeb_<sport>.h and drawn from celeb_art_index.h. New plays go
  // at the end, so every kind from DUNK on is art.
  DUNK,       // basketball: the ball slams through the rim, the rim shakes
  ALLEY_OOP,  // basketball: the lob arcs high to the rim, then the finish
  HAT_TRICK,  // hockey: the goal lamp, then hats rain down onto the ice
  GRAND_SLAM,  // baseball: the ball sails out, four runners cross the plate, a bigger burst
  WALK_OFF,    // baseball: the team's color floods up from the bottom, the dugout empties; the win follows
  BRACE,      // soccer, a scorer's second goal: the net ripples, the name, two balls
  SOCCER_HAT_TRICK,  // soccer, the same player's third: three balls
  PENALTY,    // soccer: the ball on the spot, the keeper dives the wrong way
  LATE_GOAL,  // soccer, a go-ahead goal from 85': the net ripples, the minute flashes
};

inline bool celeb_animated(CelebKind k) { return k >= CelebKind::TOUCHDOWN; }
// The pixel-art kinds draw their own title, so the page's words sit off the
// board for them.
inline bool celeb_is_art(CelebKind k) { return k >= CelebKind::DUNK; }

struct CelebPlan {
  CelebKind kind{CelebKind::NONE};
  uint32_t color{0xFFFFFF};  // the celebrating team's color, lifted for the panel
  uint32_t alt{0xFFFFFF};    // its second color (white or gold when ESPN gives none)
  std::string text;          // the words, "TOUCHDOWN!" or "KC WINS!"
  std::string line2;         // the win's final score, "24-17"; empty otherwise
  uint32_t duration_ms{0};
  bool operator==(const CelebPlan &o) const {
    return kind == o.kind && color == o.color && alt == o.alt && text == o.text && line2 == o.line2 &&
           duration_ms == o.duration_ms;
  }
};

// How long each one holds the board. The win stays longest: it is the final,
// and the line score follows it.
constexpr uint32_t kCelebFlashMs = 2500;
constexpr uint32_t kCelebMutedMs = 2000;
constexpr uint32_t kCelebWaveMs = 3000;
constexpr uint32_t kCelebTouchdownMs = 5000;
constexpr uint32_t kCelebHomeRunMs = 4500;
constexpr uint32_t kCelebGoalMs = 4500;
constexpr uint32_t kCelebThreeMs = 3000;
constexpr uint32_t kCelebWinMs = 10000;
// The big plays: each art's frames / fps, so the last frame holds to the end.
constexpr uint32_t kCelebDunkMs = 2500;
constexpr uint32_t kCelebAlleyOopMs = 3500;
constexpr uint32_t kCelebHatTrickMs = 4500;  // 54 frames
constexpr uint32_t kCelebGrandSlamMs = 5000;
constexpr uint32_t kCelebWalkOffMs = 4500;

// Dark team colors vanish on an LED panel: lift the brightest channel to 0xE0.
inline uint32_t celeb_bright(uint32_t c) {
  uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  uint32_t m = r > g ? (r > b ? r : b) : (g > b ? g : b);
  if (m == 0)
    return 0xFFFFFF;
  if (m < 0xE0) {
    r = r * 0xE0 / m;
    g = g * 0xE0 / m;
    b = b * 0xE0 / m;
  }
  return (r << 16) | (g << 8) | b;
}

// Half brightness, for the other side's muted flash.
inline uint32_t celeb_dim(uint32_t c) { return ((c >> 1) & 0x7F7F7F); }

// Gold when the team's color is itself near white, else white: the second
// color for sparks and confetti next to the team's own.
inline uint32_t celeb_alt_for(uint32_t color) {
  uint32_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
  return (r > 0xC0 && g > 0xC0 && b > 0xC0) ? 0xFFD000 : 0xFFFFFF;
}

// Whether s holds w, ignoring case (w in lower case): ESPN writes the same
// play type as "Dunk Shot" and "dunk shot".
inline bool celeb_has_word(const std::string &s, const char *w) {
  std::string low;
  low.reserve(s.size());
  for (char c : s)
    low += (char) ((c >= 'A' && c <= 'Z') ? c + 32 : c);
  return low.find(w) != std::string::npos;
}

inline bool celeb_same_game(const GameSnapshot &prev, const GameSnapshot &cur) {
  return prev.valid && cur.valid && !cur.event_id.empty() && prev.event_id == cur.event_id;
}

}  // namespace espn
