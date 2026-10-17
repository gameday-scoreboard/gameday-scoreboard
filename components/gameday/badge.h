// The abbreviation badge that stands in for a team logo: a rounded square in
// the team's color with the abbreviation on it. The rules are the companion
// app's (TeamBadgeStyle.swift), so a panel and a phone showing the same team
// match: the ring is the team's second color unless it is the fill, and the
// text is black on a light fill, white on anything else. The panel is black,
// so a near-black fill with no ring (or a near-black ring) gets a neutral
// gray ring instead, where the app draws its outline. Pure, with no ESPHome
// includes, so the host tests compile it directly.
#pragma once

#include <cstdint>

namespace espn {

struct BadgeStyle {
  uint32_t fill;
  uint32_t ring;   // meaningful when has_ring
  bool has_ring;
  bool dark_text;  // black text, else white
};

// Relative luminance in thousandths, 0 black to 1000 white: sRGB weights
// with no gamma, the same as the app's.
inline int badge_luminance(uint32_t rgb) {
  int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return (2126 * r + 7152 * g + 722 * b) / 2550;
}

constexpr int kBadgeDarkTextLum = 600;   // lighter fills take black text
constexpr int kBadgeNearBlackLum = 120;  // fills this dark need the gray ring
constexpr uint32_t kBadgeNeutralRing = 0x808080;

inline BadgeStyle badge_style(uint32_t fill, uint32_t alt) {
  BadgeStyle s{fill, alt, alt != fill, badge_luminance(fill) > kBadgeDarkTextLum};
  bool fill_dark = badge_luminance(fill) < kBadgeNearBlackLum;
  bool ring_dark = s.has_ring && badge_luminance(alt) < kBadgeNearBlackLum;
  if (fill_dark && (!s.has_ring || ring_dark)) {
    s.ring = kBadgeNeutralRing;
    s.has_ring = true;
  }
  return s;
}

}  // namespace espn
