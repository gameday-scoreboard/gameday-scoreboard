// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// The abbreviation badge that stands in for a team logo: a rounded square in
// the team's color with the abbreviation on it. The rules are the companion
// app's (TeamBadgeStyle.swift), so a panel and a phone showing the same team
// match: the ring is the team's second color unless it is the fill, and the
// text is black on a light fill, white on anything else. The panel is black,
// so a near-black fill with no ring (or a near-black ring) gets a neutral
// gray ring instead, where the app draws its outline. The panel itself draws
// only the letters (badge_text below). Pure, with no ESPHome includes, so the
// host tests compile it directly.
#pragma once

#include <cstdint>
#include <string>

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

// ESPN's hex color with no # as RGB, or 0 (black) when it is missing or not
// a color.
inline uint32_t badge_rgb(const std::string &hex) {
  if (hex.size() != 6)
    return 0;
  uint32_t v = 0;
  for (char c : hex) {
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0)
      return 0;
    v = (v << 4) | (uint32_t) d;
  }
  return v;
}

// On the panel the badge is the abbreviation alone, in the team's color on
// the black logo slot. How bright a letter looks on the LEDs follows its
// brightest channel, not luminance: full blue reads fine, while dark green
// 005730 (luminance 257) is dimmer than maroon. A color whose brightest
// channel is under half (navy 0C2340 is 64, maroon 7A0019 is 122) barely
// lights the LEDs, so the letters take the team's second color, or white
// when that is dark too or ESPN has no color.
constexpr int kBadgeTextMinPeak = 128;
constexpr uint32_t kBadgeNoColorText = 0xFFFFFF;

inline int badge_peak(uint32_t rgb) {
  int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return r > g ? (r > b ? r : b) : (g > b ? g : b);
}

inline uint32_t badge_text(uint32_t color, uint32_t alt) {
  if (badge_peak(color) >= kBadgeTextMinPeak)
    return color;
  if (badge_peak(alt) >= kBadgeTextMinPeak)
    return alt;
  return kBadgeNoColorText;
}

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
