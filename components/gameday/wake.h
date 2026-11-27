// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Game-day wake and After the game. Wake turns a panel that is off back on a
// set time before a game of the owner's team or one of their favorites
// starts. After the game says what the panel does once that game's final has
// been up for the league's linger: stay on the scoreboard (as before), turn
// off, or show the clock until the next game is near. The night schedule
// (night.h) still has the last word on brightness, so a panel woken inside
// its hours stays dim or dark until the game is live. Off out of the box.
// Pure, with no ESPHome includes, so the host tests compile it directly.
#pragma once

#include <cstdint>
#include <string>

namespace espn {

constexpr int kWakeLeadDefault = 15;
constexpr int kWakeLeadMin = 5;
constexpr int kWakeLeadMax = 120;
// ESPN can keep a game "pre" well past its listed start (a rain delay), so a
// kickoff stays the one the wake is for this long after it.
static const int64_t kWakeLateS = 3 * 3600;

// What the panel does between games. SCOREBOARD is 0 so a saved spare byte
// keeps today's behavior; the page and the apps list only the ones this
// firmware has. SMART is the Smart board page (firmware/pages/smart_board.yaml),
// EFFECTS the Effects page (firmware/pages/effects.yaml) and FLIGHTS the
// Flights page (firmware/pages/flights.yaml) and WEATHER the Weather page
// (firmware/pages/weather.yaml) until the next game is near.
enum class After : uint8_t { SCOREBOARD = 0, OFF = 1, CLOCK = 2, SMART = 3, EFFECTS = 4, FLIGHTS = 5, WEATHER = 6 };
static const uint8_t kAfterCount = 7;

inline const char *after_word(uint8_t a) {
  switch ((After) a) {
    case After::OFF:
      return "off";
    case After::CLOCK:
      return "clock";
    case After::SMART:
      return "smartboard";
    case After::EFFECTS:
      return "effects";
    case After::FLIGHTS:
      return "flights";
    case After::WEATHER:
      return "weather";
    default:
      return "scoreboard";
  }
}

// "off" -> 1; -1 for a word this firmware doesn't have
inline int after_parse(const std::string &v) {
  for (uint8_t a = 0; a < kAfterCount; a++)
    if (v == after_word(a))
      return a;
  return -1;
}

inline int clamp_wake_lead(int minutes) {
  return minutes < kWakeLeadMin ? kWakeLeadMin : minutes > kWakeLeadMax ? kWakeLeadMax : minutes;
}

// Kept in flash. Its size is fixed: a blob that changes size fails to load
// and resets every setting in it, so later settings take the spare bytes.
struct WakePrefs {
  uint8_t on;     // 1 = wake before a game
  uint8_t lead;   // minutes before the start
  uint8_t after;  // After
  uint8_t spare[5];
} __attribute__((packed));
static_assert(sizeof(WakePrefs) == 8,
              "WakePrefs is saved: a new size needs a new preference key or a migration, not a new number");

inline WakePrefs wake_defaults() {
  WakePrefs p{};
  p.lead = kWakeLeadDefault;
  return p;
}

// A blob from flash that can't be right (a later firmware's, or damage)
// goes back to the defaults field by field.
inline void wake_sanitize(WakePrefs &p) {
  p.on = p.on ? 1 : 0;
  if (p.lead < kWakeLeadMin || p.lead > kWakeLeadMax)
    p.lead = kWakeLeadDefault;
  if (p.after >= kAfterCount)
    p.after = (uint8_t) After::SCOREBOARD;
}

// True from lead minutes before the kickoff until kWakeLateS after it.
inline bool wake_window(int64_t kick, int64_t now, uint8_t lead) {
  return kick > 0 && now >= kick - (int64_t) lead * 60 && now < kick + kWakeLateS;
}

// What the wake remembers between ticks. Kept in RTC memory by the
// component, so a restart (the nightly update) doesn't light a panel that
// After the game turned off, or wake it twice for one game.
struct WakeState {
  int64_t woke_for{0};   // the kickoff the panel last woke for
  int64_t ended_at{0};   // when the owner's game stopped being live, 0 when none is ending
  uint32_t linger_s{0};  // how long that final stays up
  bool was_live{false};
  bool asleep{false};  // After the game turned the panel off and nothing has turned it on since
  // A remote button that follows something (a team, a playlist, Live games)
  // or asks for the Scoreboard ended this stretch between games: the clock
  // or Smart board stays down until a game of the owner's is live again,
  // so it comes back after that game's final.
  bool held{false};
};

struct WakeInputs {
  bool clock_valid{false};
  bool has_team{false};  // a team or favorite is picked
  int64_t now{0};
  int64_t kick{0};  // the soonest start of a game of the owner's still ahead, 0 for none
  bool owner_live{false};
  uint32_t linger_s{0};  // the league's linger, for a game that ends now
};

enum class WakeStep : uint8_t { NONE, WAKE, SLEEP };

// One tick. WAKE turns Power on (the panel only does it when Power is off),
// SLEEP turns it off. Each fires once: once per kickoff, once per final.
inline WakeStep wake_step(const WakePrefs &p, WakeState &s, const WakeInputs &in) {
  if (!in.clock_valid || !in.has_team) {
    s.was_live = false;
    s.ended_at = 0;
    return WakeStep::NONE;
  }
  if (in.owner_live) {
    s.was_live = true;
    s.ended_at = 0;
    s.held = false;
    // A panel After the game put to sleep comes back for a game that went
    // live with no start time to wake for (a TBD start, a restart mid-game).
    if (p.on && s.asleep) {
      s.asleep = false;
      return WakeStep::WAKE;
    }
    return WakeStep::NONE;
  }
  if (s.was_live) {
    s.was_live = false;
    s.ended_at = in.now;
    s.linger_s = in.linger_s;
  }
  const bool near = wake_window(in.kick, in.now, p.lead);
  if (p.on && near && s.woke_for != in.kick) {
    s.woke_for = in.kick;
    s.asleep = false;
    return WakeStep::WAKE;
  }
  if (s.ended_at != 0 && in.now - s.ended_at >= (int64_t) s.linger_s) {
    s.ended_at = 0;
    // The next game is already near: that is game time, not between games.
    if (!near && p.after == (uint8_t) After::OFF && !s.asleep) {
      s.asleep = true;
      return WakeStep::SLEEP;
    }
  }
  return WakeStep::NONE;
}

// Between games: no game of the owner's live, no final still up, and the
// next start not near.
inline bool wake_between(const WakePrefs &p, const WakeState &s, const WakeInputs &in) {
  return in.clock_valid && in.has_team && !in.owner_live && !s.was_live && s.ended_at == 0 &&
         !wake_window(in.kick, in.now, p.lead);
}

// The clock, Smart board or an effect between games, unless a remote press
// or a pick ended it.
inline bool wake_page_between(const WakePrefs &p, const WakeState &s, const WakeInputs &in) {
  return !s.held && wake_between(p, s, in);
}

// After the game is Clock and it is between games.
inline bool wake_clock(const WakePrefs &p, const WakeState &s, const WakeInputs &in) {
  return p.after == (uint8_t) After::CLOCK && wake_page_between(p, s, in);
}

// After the game is Smart board and it is between games.
inline bool wake_smart(const WakePrefs &p, const WakeState &s, const WakeInputs &in) {
  return p.after == (uint8_t) After::SMART && wake_page_between(p, s, in);
}

// After the game is Effects and it is between games.
inline bool wake_effects(const WakePrefs &p, const WakeState &s, const WakeInputs &in) {
  return p.after == (uint8_t) After::EFFECTS && wake_page_between(p, s, in);
}

// After the game is Flights and it is between games.
inline bool wake_flights(const WakePrefs &p, const WakeState &s, const WakeInputs &in) {
  return p.after == (uint8_t) After::FLIGHTS && wake_page_between(p, s, in);
}

// After the game is Weather and it is between games.
inline bool wake_weather(const WakePrefs &p, const WakeState &s, const WakeInputs &in) {
  return p.after == (uint8_t) After::WEATHER && wake_page_between(p, s, in);
}

// For the state document's wake_now and the log: "asleep" (After the game
// turned the panel off), "clock" (the clock between games), "smartboard"
// (the Smart board between games), "effects" (an effect between games),
// "flights" (the Flights page between games), "weather" (the Weather page
// between games), else "".
inline const char *wake_word(bool asleep, bool clock, bool smart = false, bool effects = false,
                             bool flights = false, bool weather = false) {
  return asleep    ? "asleep"
         : clock   ? "clock"
         : smart   ? "smartboard"
         : effects ? "effects"
         : flights ? "flights"
         : weather ? "weather"
                   : "";
}

// Effects between games: whether the effect should be up now. Each of these
// takes the screen from it, and the effect comes back when it ends: the
// panel off, the night schedule's screen-off, Wi-Fi lost or wanting a
// sign-in, the board's trouble line (no internet, ESPN not answering), the
// heat or power mark, a banner, and a turn of the message card or of the
// countdown screen (the owner's own countdown, the weather).
struct FxBetween {
  bool between{false};      // wake_effects, and not a Live games board on another game
  bool board_ready{false};  // the first scoreboard since the start is up
  bool power_on{false};
  bool screen_on{false};  // not the night schedule's screen-off
  bool wifi{false};
  bool trouble{false};    // a Wi-Fi sign-in, no internet, ESPN misses
  bool mark{false};       // the heat or power mark
  bool banner{false};
  bool card_turn{false};  // the message card or the countdown screen has the board
};

inline bool fx_between_want(const FxBetween &f) {
  return f.between && f.board_ready && f.power_on && f.screen_on && f.wifi && !f.trouble && !f.mark && !f.banner &&
         !f.card_turn;
}

}  // namespace espn
