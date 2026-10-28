// Which celebration a score gets, and the owner's Celebrations setting.
// Pure, no ESPHome includes: the host tests run decide_celebration() over
// every sport and both sides. The animations themselves are celebrate_fx.h.
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
};

inline bool celeb_animated(CelebKind k) { return k >= CelebKind::TOUCHDOWN; }

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

inline bool celeb_same_game(const GameSnapshot &prev, const GameSnapshot &cur) {
  return prev.valid && cur.valid && !cur.event_id.empty() && prev.event_id == cur.event_id;
}

// The celebration for this poll. splash is decide_splash's answer for it,
// which already knows about Opponent scores too and the neutral modes; the
// one score decide_splash stays quiet on, a basketball basket, is read here
// from the score delta (a three in one read, the only basket worth a show).
// Opponent scores (not neutral) are always MUTED when they show at all.
inline CelebPlan decide_celebration(const GameSnapshot &prev, const GameSnapshot &cur, const Splash &splash,
                                    CelebLevel level, bool neutral) {
  CelebPlan none;
  if (level == CelebLevel::OFF)
    return none;
  const bool same = celeb_same_game(prev, cur);
  const int delta = same ? cur.team_score - prev.team_score : 0;
  const int odelta = same ? cur.opp_score - prev.opp_score : 0;
  const bool live = same && prev.state == GameState::IN && cur.state == GameState::IN;

  if (splash.text.empty()) {
    // Basketball's three. decide_splash leaves baskets alone; Full celebrates
    // exactly three points in one read for our side (either side when neutral).
    if (level != CelebLevel::FULL || !live || cur.sport != Sport::BASKETBALL)
      return none;
    bool ours = delta == 3 && odelta == 0;
    bool theirs = neutral && odelta == 3 && delta == 0;
    if (!ours && !theirs)
      return none;
    CelebPlan p;
    p.kind = CelebKind::THREE;
    p.color = celeb_bright(parse_color(ours ? cur.team_color : cur.opp_color));
    p.alt = celeb_alt_for(p.color);
    p.text = neutral ? (ours ? cur.team_abbr : cur.opp_abbr) + " THREE!" : "THREE!";
    p.duration_ms = kCelebThreeMs;
    return p;
  }

  const bool win = splash.text.find("WINS") != std::string::npos;
  // Whose splash: a win is the side with more points; a score is the side
  // whose points moved (both can move in one read; the splash picked ours).
  bool ours;
  if (win)
    ours = cur.team_score > cur.opp_score;
  else
    ours = delta > 0 || odelta <= 0;
  CelebPlan p;
  p.text = splash.text;
  p.color = celeb_bright(splash.color);
  p.alt = celeb_alt_for(p.color);

  if (!ours && !neutral) {
    p.kind = CelebKind::MUTED;
    p.color = celeb_dim(p.color);
    p.duration_ms = kCelebMutedMs;
    return p;
  }
  if (level == CelebLevel::SUBTLE) {
    p.kind = CelebKind::FLASH;
    p.duration_ms = kCelebFlashMs;
    return p;
  }
  if (win) {
    p.kind = CelebKind::WIN;
    // The board's order, our side first; neutral boards read the same way.
    p.line2 = std::to_string(cur.team_score) + "-" + std::to_string(cur.opp_score);
    p.duration_ms = kCelebWinMs;
    return p;
  }
  const int pts = ours ? delta : odelta;
  switch (cur.sport) {
    case Sport::FOOTBALL:
      if (pts >= 6 && pts <= 8) {
        p.kind = CelebKind::TOUCHDOWN;
        p.duration_ms = kCelebTouchdownMs;
      } else {
        p.kind = CelebKind::WAVE;
        p.duration_ms = kCelebWaveMs;
      }
      return p;
    case Sport::BASEBALL:
      if (splash.text.find("HOME RUN") != std::string::npos) {
        p.kind = CelebKind::HOME_RUN;
        p.duration_ms = kCelebHomeRunMs;
      } else {
        p.kind = CelebKind::WAVE;
        p.duration_ms = kCelebWaveMs;
      }
      return p;
    case Sport::SOCCER:
      p.kind = CelebKind::GOAL_NET;
      p.duration_ms = kCelebGoalMs;
      return p;
    case Sport::HOCKEY:
      p.kind = CelebKind::GOAL_LAMP;
      p.duration_ms = kCelebGoalMs;
      return p;
    case Sport::BASKETBALL:
    default:
      // Nothing but the win splashes here; a stray text gets the flash.
      p.kind = CelebKind::FLASH;
      p.duration_ms = kCelebFlashMs;
      return p;
  }
}

// Where the words go, so the animation leaves them room. The page uses the
// same answer for its label; the host renderer for its own text.
struct CelebTextPlace {
  int y;         // top of the first line
  int font;      // 16, 12 or 8: the Spleen size
  int line2_y;   // top of the second line (the win), -1 when none
};
inline CelebTextPlace celeb_text_place(CelebKind kind, int w, int h) {
  const bool wide = w >= 128;
  CelebTextPlace t{0, wide ? 16 : 12, -1};
  const int lh = t.font;  // Spleen's line height equals its size
  switch (kind) {
    case CelebKind::WIN:
      t.y = h / 2 - lh - 1;
      t.line2_y = h / 2 + 1;
      break;
    case CelebKind::TOUCHDOWN:
      t.y = wide ? 10 : 8;  // the goalposts stand under it
      break;
    case CelebKind::HOME_RUN:
      t.y = wide ? 4 : 2;  // the ball peaks under it and leaves high on the right
      break;
    case CelebKind::THREE:
      t.y = h - lh - (wide ? 14 : 10);  // the ball flies over it to the hoop
      break;
    default:
      t.y = (h - lh) / 2;
      break;
  }
  return t;
}

}  // namespace espn
