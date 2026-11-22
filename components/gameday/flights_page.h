// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// What the Flights page (firmware/pages/flights.yaml) draws: a message, the
// card for one plane, or the list of the nearest four, as text for its
// labels. The component works out the inputs from its last read; this file
// only turns them into words, so the host tests cover every screen.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "flights.h"

// Built for size, as flights.h is.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("Os")
#endif

namespace espn {

// Why the last read failed, for the state document and the page's message.
enum class FlErr : uint8_t { NONE, NET, LIMIT, HTTP };

inline const char *fl_err_word(FlErr e) {
  switch (e) {
    case FlErr::NET:
      return "net";
    case FlErr::LIMIT:
      return "limit";
    case FlErr::HTTP:
      return "http";
    default:
      return "none";
  }
}

// The owner's receiver is read this often, adsb.lol every 60 s, or every
// 30 s with the owner's own key (a free key is 1,000 reads a day: about
// 8 hours on screen, as 500 a day at 60 s is without one).
constexpr uint32_t kFlReceiverMs = 10 * 1000;
constexpr uint32_t kFlLolMs = 60 * 1000;
constexpr uint32_t kFlLolKeyMs = 30 * 1000;
// Failed reads of the receiver in a row before adsb.lol takes over. While
// it has, the receiver is tried again this often, and takes back over the
// first time it answers.
constexpr uint8_t kFlReceiverFails = 2;
constexpr uint32_t kFlReceiverRetryMs = 60 * 1000;
// A read older than this isn't drawn: the page says it is looking, or that
// it can't reach the data.
constexpr uint32_t kFlStaleMs = 150 * 1000;
// Routes: one lookup per 15 s at most, only for the card's plane, kept for
// 3 hours (1 hour for a callsign adsbdb doesn't know).
constexpr uint32_t kFlRouteGapMs = 15 * 1000;
constexpr uint32_t kFlRouteHitMs = 3 * 3600 * 1000;
constexpr uint32_t kFlRouteMissMs = 3600 * 1000;
constexpr size_t kFlRouteCache = 16;
// A daily limit's wait is kept up to a day.
constexpr uint32_t kFlLimitMaxMs = 24 * 3600 * 1000;
// With more than one plane, the card and the list of the nearest take turns
// this long each. A plane about to pass overhead keeps the card.
constexpr uint32_t kFlTurnMs = 8000;
constexpr size_t kFlListRows = 4;

enum class FlScreen : uint8_t { MESSAGE, CARD, LIST };

struct FlView {
  FlScreen screen{FlScreen::MESSAGE};
  // MESSAGE: a title and the line under it.
  char title[24]{};
  char sub[48]{};
  // CARD
  char call[12]{};
  bool emergency{false};
  char airline[16]{};  // two panels only; "" when unknown or no room
  char line2[24]{};    // the route, else the registration
  bool route{false};   // line2 is a route, drawn in gold
  char type[24]{};
  char alt[32]{};    // two panels: the altitude and the speed together
  char speed[16]{};  // one panel: the speed on its own line
  char where[24]{};  // "2.4 mi SW", or "Overhead in 40 s"
  int8_t vdir{0};    // 1 climbing, -1 descending, 0 level or overhead
  bool overhead{false};
  // LIST: a row per plane; one panel puts the altitude and distance on a
  // second row under the callsign.
  uint8_t rows{0};
  char row[kFlListRows][24]{};
  char row2[kFlListRows][16]{};
  bool row_emergency[kFlListRows]{};
};

struct FlPageIn {
  bool home{false};   // the weather town is found
  bool fresh{false};  // the last good read is recent enough to draw
  FlErr err{FlErr::NONE};
  uint32_t wait_s{0};      // left on adsb.lol's wait after a refusal
  const char *radius{""};  // "10 mi"
  bool metric{false};
  bool wide{true};       // two panels, 128 wide
  bool list_turn{false};  // the list's turn, when there is more than one plane
};

// "12,400" or "3,780", the unit left off; "Ground"; "" when unknown.
inline const char *fl_alt_short(char *out, size_t n, const Plane &p, bool metric) {
  if (n == 0)
    return out;
  out[0] = '\0';
  if (p.ground)
    snprintf(out, n, "Ground");
  else if (p.alt_ft != kAltUnknown)
    fl_grouped(out, n, metric ? fl_ft_to_m(p.alt_ft) : (long) p.alt_ft);
  return out;
}

// "2.4mi", for the narrow list.
inline const char *fl_dist_short(char *out, size_t n, float km, bool metric) {
  char d[16];
  dist_text(d, sizeof(d), km, metric);
  size_t at = 0;
  for (const char *c = d; *c && at + 1 < n; c++)
    if (*c != ' ')
      out[at++] = *c;
  if (n > 0)
    out[at] = '\0';
  return out;
}

inline void fl_message(FlView &v, const char *title, const char *sub) {
  v.screen = FlScreen::MESSAGE;
  snprintf(v.title, sizeof(v.title), "%s", title);
  snprintf(v.sub, sizeof(v.sub), "%s", sub);
}

// The page for now. sky's planes are where they are now (moved on from the
// read), nearest first as the read had them; card is the card's plane, -1
// for none; route its route when one fits, else nullptr.
inline void fl_compose(const FlPageIn &in, const Sky &sky, int card, const Route *route, FlView &v) {
  v = FlView{};
  if (!in.home) {
    fl_message(v, "Flights", "Set your town on the device page");
    return;
  }
  if (in.err == FlErr::LIMIT && in.wait_s > 0) {
    char sub[48];
    if (in.wait_s >= 3600)
      snprintf(sub, sizeof(sub), "Daily limit, back in %uh", (unsigned) ((in.wait_s + 3599) / 3600));
    else
      snprintf(sub, sizeof(sub), "Back in %u min", (unsigned) ((in.wait_s + 59) / 60));
    fl_message(v, "Flight data paused", sub);
    return;
  }
  if (!in.fresh) {
    if (in.err == FlErr::NET || in.err == FlErr::HTTP)
      fl_message(v, "Flights", "Can't reach flight data");
    else
      fl_message(v, "Flights", "Looking for planes");
    return;
  }
  if (sky.n == 0 || card < 0 || card >= sky.n) {
    char sub[48];
    snprintf(sub, sizeof(sub), "No planes within %s", in.radius);
    fl_message(v, "Flights", sub);
    return;
  }

  const Plane &p = sky.p[card];
  Cpa c = plane_cpa(p);
  v.overhead = cpa_overhead(c);
  if (in.list_turn && sky.n > 1 && !v.overhead) {
    v.screen = FlScreen::LIST;
    v.rows = (uint8_t) (sky.n < kFlListRows ? sky.n : kFlListRows);
    for (uint8_t i = 0; i < v.rows; i++) {
      const Plane &q = sky.p[i];
      char alt[16], dist[16];
      fl_alt_short(alt, sizeof(alt), q, in.metric);
      v.row_emergency[i] = q.emergency;
      if (in.wide) {
        dist_text(dist, sizeof(dist), q.dist_km, in.metric);
        snprintf(v.row[i], sizeof(v.row[i]), "%-8.8s%-7s%6s", plane_label(q), alt, dist);
      } else {
        fl_dist_short(dist, sizeof(dist), q.dist_km, in.metric);
        snprintf(v.row[i], sizeof(v.row[i]), "%.8s", plane_label(q));
        snprintf(v.row2[i], sizeof(v.row2[i]), "%s %s", alt, dist);
      }
    }
    return;
  }

  v.screen = FlScreen::CARD;
  snprintf(v.call, sizeof(v.call), "%s", plane_label(p));
  v.emergency = p.emergency;
  const char *airline = p.call[0] ? airline_short(p.call) : nullptr;
  if (in.wide && airline != nullptr) {
    // Right of the callsign (8 px a character) in the 5 px font, cut to fit.
    int room = (128 - 8 * (int) strlen(v.call) - 4) / 5;
    if (room > 0)
      snprintf(v.airline, sizeof(v.airline), "%.*s", room, airline);
  }
  if (route != nullptr) {
    snprintf(v.line2, sizeof(v.line2), "%s > %s", route->from, route->to);
    v.route = true;
  } else if (p.call[0] && p.reg[0]) {
    snprintf(v.line2, sizeof(v.line2), "%s", p.reg);
  }
  plane_type_name(v.type, sizeof(v.type), p.type, in.wide);
  char alt[16], speed[16];
  alt_text(alt, sizeof(alt), p, in.metric);
  speed_text(speed, sizeof(speed), p.ground ? NAN : p.gs_kt, in.metric);
  if (in.wide) {
    snprintf(v.alt, sizeof(v.alt), "%s%s%s", alt, alt[0] && speed[0] ? "  " : "", speed);
  } else {
    snprintf(v.alt, sizeof(v.alt), "%s", alt);
    snprintf(v.speed, sizeof(v.speed), "%s", speed);
  }
  if (v.overhead) {
    unsigned secs = (unsigned) lroundf(c.secs);
    snprintf(v.where, sizeof(v.where), in.wide ? "Overhead in %u s" : "Overhead %us", secs);
  } else {
    where_text(v.where, sizeof(v.where), p, in.metric);
    v.vdir = (int8_t) (p.ground ? 0 : vrate_dir(p.vrate_fpm));
  }
}

// Set by firmware/pages/flights.yaml around a page change the panel makes
// itself (After the game), so the page select's callback doesn't take it
// for somebody picking Flights.
struct FlFlags {
  bool own_change{false};
};
inline FlFlags &fl_flags() {
  static FlFlags f;
  return f;
}

}  // namespace espn

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
