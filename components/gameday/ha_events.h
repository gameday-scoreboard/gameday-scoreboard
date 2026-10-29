#pragma once

// The game events the panel sends Home Assistant (esphome.gameday_*) and the
// owner's webhook: the reminder before the start, the start, every score, a
// change of lead, the end of each period and the final. They follow the game
// Home Assistant's Game Status follows, and fire for both sides whatever the
// board's splash settings: basketball only splashes the win, and the other
// side's splash can be off, but a light show in the living room is the
// owner's call in their own automation.
//
// Pure, no ESPHome includes, so the host tests drive it with snapshots.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "espn_parse.h"
#include "leagues.h"

namespace espn {

// TEST is the device page's "Send a test event": a made-up score, so an
// automation can be tried without waiting for a game.
enum class HaKind : uint8_t { PREGAME, START, SCORE, LEAD, PERIOD, FINAL, TEST };

// The kind's word in the payload and in esphome.gameday_<word>.
inline const char *ha_kind_name(HaKind k) {
  switch (k) {
    case HaKind::PREGAME:
      return "pregame";
    case HaKind::START:
      return "start";
    case HaKind::SCORE:
      return "score";
    case HaKind::LEAD:
      return "lead_change";
    case HaKind::PERIOD:
      return "period";
    case HaKind::TEST:
      return "test";
    default:
      return "final";
  }
}

// One event. team is the side the event is about: the side that scored, the
// side that leads now, or the panel's own side (left on the board) for the
// rest, and the scores are team's first.
struct HaEvent {
  HaKind kind{HaKind::SCORE};
  std::string event_id;  // ESPN's id for the game
  std::string league;    // "nfl", "mlb"...; "" when the panel can't tell
  std::string team, opponent;
  uint32_t team_rgb{0xFFFFFF}, opponent_rgb{0xFFFFFF};  // ESPN's colors, white when it has none
  std::string play;  // a score: "touchdown", "field_goal", "run", "goal"...; "final" for a final
  int points{0};     // a score: the points between two reads
  int team_score{0}, opponent_score{0};
  std::string sport;   // "football", "baseball", "basketball", "hockey", "soccer"
  std::string result;  // a final: "win", "loss" or "draw" for team
  std::string winner;  // a final: the winner's abbreviation, "" on a draw
  // A lead change: "took_lead" from a tie, "lead_change" from the other side,
  // "tied" when the scores come level. leader is "" when level.
  std::string change, leader, previous_leader;
  int period{0};            // a period: the one that ended
  std::string period_name;  // "1st quarter", "top of the 3rd"... the words for it
  int minutes{0};           // the reminder: minutes to the start
  // team is the owner's team (a score: the owner's team scored). Always
  // false in Live games, where neither side is the owner's.
  bool my_team{false};
};

inline const char *ha_sport(Sport s) {
  switch (s) {
    case Sport::BASEBALL:
      return "baseball";
    case Sport::BASKETBALL:
      return "basketball";
    case Sport::HOCKEY:
      return "hockey";
    case Sport::SOCCER:
      return "soccer";
    default:
      return "football";
  }
}

// What the points were. Football reads them like the splash does: 6 to 8 is a
// touchdown with or without the try, 2 is a two-point try or a safety.
// Basketball's points between two reads can be several baskets, so it says
// only "score".
inline const char *ha_play(const GameSnapshot &cur, int points) {
  switch (cur.sport) {
    case Sport::BASEBALL:
      return baseball_home_run(cur) ? "home_run" : "run";
    case Sport::HOCKEY:
    case Sport::SOCCER:
      return "goal";
    case Sport::BASKETBALL:
      return "score";
    default:
      break;
  }
  switch (points) {
    case 6:
    case 7:
    case 8:
      return "touchdown";
    case 3:
      return "field_goal";
    case 1:
      return "extra_point";
    case 2:
      return "two_point";
    default:
      return "score";
  }
}

// ESPN's "002a5c" (a leading # allowed), white when it isn't six hex digits.
inline uint32_t ha_rgb(const std::string &hex) {
  size_t i = !hex.empty() && hex[0] == '#' ? 1 : 0;
  if (hex.size() != i + 6)
    return 0xFFFFFF;
  uint32_t v = 0;
  for (; i < hex.size(); i++) {
    char c = hex[i];
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (d < 0)
      return 0xFFFFFF;
    v = v << 4 | (uint32_t) d;
  }
  return v;
}

// "0,42,92", the form a Home Assistant template splits into rgb_color.
inline std::string ha_rgb_text(uint32_t rgb) {
  char b[16];
  snprintf(b, sizeof(b), "%u,%u,%u", (unsigned) (rgb >> 16 & 0xFF), (unsigned) (rgb >> 8 & 0xFF),
           (unsigned) (rgb & 0xFF));
  return b;
}

inline std::string ha_hex_text(uint32_t rgb) {
  char b[8];
  snprintf(b, sizeof(b), "#%06X", (unsigned) (rgb & 0xFFFFFF));
  return b;
}

inline std::string ha_ordinal(int n) {
  int t = n % 100;
  const char *suf = (t >= 11 && t <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
  return std::to_string(n) + suf;
}

// The words for period p of the game: "1st quarter", "2nd half", "3rd
// period", "5th inning", "overtime", "2nd overtime".
inline std::string ha_period_name(const GameSnapshot &g, int p) {
  int regulation = 4;
  const char *word = "quarter";
  switch (g.sport) {
    case Sport::BASEBALL:
      return ha_ordinal(p) + " inning";
    case Sport::HOCKEY:
      regulation = 3;
      word = "period";
      break;
    case Sport::SOCCER:
      regulation = 2;
      word = "half";
      break;
    case Sport::BASKETBALL:
      regulation = g.nba.regulation;
      word = regulation == 2 ? "half" : "quarter";
      break;
    default:
      break;
  }
  if (p <= regulation)
    return ha_ordinal(p) + " " + word;
  int ot = p - regulation;
  return ot == 1 ? "overtime" : ha_ordinal(ot) + " overtime";
}

// The game is between periods: halftime, an intermission, "End of 3rd", the
// end of an inning. The period the break follows is g.period.
inline bool ha_at_break(const GameSnapshot &g) {
  if (g.state != GameState::IN)
    return false;
  switch (g.sport) {
    case Sport::BASEBALL:
      return g.mlb.half == Half::END;
    case Sport::BASKETBALL:
      return g.nba.phase == Phase::END_PERIOD || g.nba.phase == Phase::HALFTIME;
    case Sport::SOCCER:
      return g.soc.status == "STATUS_HALFTIME" || g.soc.status == "STATUS_END_PERIOD";
    default:
      return g.short_detail.rfind("End of", 0) == 0 || g.short_detail.find("Halftime") != std::string::npos;
  }
}

// Home Assistant's Period sensor: where a live game is, in words. "3rd
// quarter", "Halftime", "End of 1st", "Top of the 5th", "Final"; "" before
// the start and with no game.
inline std::string ha_period_text(const GameSnapshot &g) {
  if (!g.valid)
    return "";
  if (g.state == GameState::POST)
    return g.completed ? "Final" : g.short_detail;
  if (g.state != GameState::IN || g.period <= 0)
    return "";
  if (g.sport == Sport::BASEBALL) {
    const char *half = g.mlb.half == Half::TOP      ? "Top"
                       : g.mlb.half == Half::BOTTOM ? "Bottom"
                       : g.mlb.half == Half::MID    ? "Middle"
                       : g.mlb.half == Half::END    ? "End"
                                                    : nullptr;
    return half != nullptr ? std::string(half) + " of the " + ha_ordinal(g.period) : ha_period_name(g, g.period);
  }
  if (g.sport == Sport::SOCCER && g.soc.status == "STATUS_HALFTIME")
    return "Halftime";
  if (g.sport == Sport::BASKETBALL && g.nba.phase == Phase::HALFTIME)
    return "Halftime";
  if (ha_at_break(g) && !g.short_detail.empty())
    return g.short_detail;
  return ha_period_name(g, g.period);
}

// The Game Clock sensor: ESPN's clock while the game is on ("4:32", soccer's
// "67'"), "" between periods, in baseball and when it isn't live.
inline std::string ha_clock_text(const GameSnapshot &g) {
  if (!g.valid || g.state != GameState::IN || g.sport == Sport::BASEBALL || ha_at_break(g))
    return "";
  return g.display_clock;
}

// Each event's fields, the same for Home Assistant's event data and the
// webhook's JSON, in the order the docs list them. Every value is text, as
// Home Assistant's event data from ESPHome always is.
using HaFields = std::vector<std::pair<const char *, std::string>>;

inline HaFields ha_event_fields(const HaEvent &e) {
  HaFields f;
  f.reserve(20);
  f.emplace_back("kind", ha_kind_name(e.kind));
  f.emplace_back("league", e.league);
  f.emplace_back("sport", e.sport);
  f.emplace_back("team", e.team);
  f.emplace_back("opponent", e.opponent);
  f.emplace_back("team_score", std::to_string(e.team_score));
  f.emplace_back("opponent_score", std::to_string(e.opponent_score));
  f.emplace_back("my_team", e.my_team ? "true" : "false");
  f.emplace_back("team_color", ha_hex_text(e.team_rgb));
  f.emplace_back("team_rgb", ha_rgb_text(e.team_rgb));
  f.emplace_back("opponent_color", ha_hex_text(e.opponent_rgb));
  f.emplace_back("opponent_rgb", ha_rgb_text(e.opponent_rgb));
  f.emplace_back("game_id", e.event_id);
  switch (e.kind) {
    case HaKind::PREGAME:
      f.emplace_back("minutes", std::to_string(e.minutes));
      break;
    case HaKind::SCORE:
    case HaKind::TEST:
      f.emplace_back("play", e.play);
      f.emplace_back("points", std::to_string(e.points));
      break;
    case HaKind::LEAD:
      f.emplace_back("change", e.change);
      f.emplace_back("leader", e.leader);
      f.emplace_back("previous_leader", e.previous_leader);
      break;
    case HaKind::PERIOD:
      f.emplace_back("period", std::to_string(e.period));
      f.emplace_back("period_name", e.period_name);
      break;
    case HaKind::FINAL:
      f.emplace_back("result", e.result);
      f.emplace_back("winner", e.winner);
      break;
    default:
      break;
  }
  return f;
}

// A JSON string's contents: quotes, backslashes and control characters
// escaped. Team abbreviations and the panel's words are ASCII, but an
// owner's panel name may not be.
inline void ha_json_escape(std::string &out, const std::string &s) {
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += (char) c;
    } else if (c < 0x20) {
      char b[8];
      snprintf(b, sizeof(b), "\\u%04x", c);
      out += b;
    } else {
      out += (char) c;
    }
  }
}

// The webhook's body: {"event":"gameday_score","panel":"gameday-2f6a70",...}
// with the fields above, every value a string like Home Assistant's.
inline std::string ha_webhook_json(const HaEvent &e, const std::string &panel) {
  std::string out = "{\"event\":\"gameday_";
  out += ha_kind_name(e.kind);
  out += "\",\"panel\":\"";
  ha_json_escape(out, panel);
  out += '"';
  for (const auto &kv : ha_event_fields(e)) {
    out += ",\"";
    out += kv.first;
    out += "\":\"";
    ha_json_escape(out, kv.second);
    out += '"';
  }
  out += '}';
  return out;
}

// What the tracker needs besides the game.
struct HaContext {
  bool neutral{false};        // Live games: neither side is the owner's
  std::string league;         // the game's league key, "" when unknown
  int64_t now_epoch{0};       // the wall clock, 0 before it is set (no reminder then)
  int pregame_minutes{0};     // the reminder's lead, 0 for none
  uint32_t mono_s{0};         // seconds since boot, for how long a read that went back has held
};

// A read that goes back (a lower score, an earlier state or period) is
// usually an old copy of the document from ESPN's cache, and is ignored. One
// that holds this long is a real correction, like a score taken off on
// review, and becomes the new last read without an event.
constexpr uint32_t kHaStepBackHoldS = 90;

// Remembers the last read of the games Home Assistant followed most
// recently, so Favorites alternating between two live games, or Live games
// rotating, still sees each score once when its game comes back: late, if
// the score came while another game was up. Eight covers a full Sunday
// slot of NFL games. A game read for the first time fires nothing: the
// panel just started or picked it up, so nothing repeats after a restart.
class HaTracker {
 public:
  // The events since the last read of the same game, in the order they
  // happened, and remembers it. Call it with every read and every so often
  // with no new read: the reminder before the start goes by the clock.
  std::vector<HaEvent> update(const GameSnapshot &cur, const HaContext &ctx) {
    std::vector<HaEvent> out;
    if (!cur.valid || cur.event_id.empty() || cur.state == GameState::NOT_FOUND)
      return out;
    Seen *s = this->find_(cur.event_id);
    if (s == nullptr) {
      this->remember_(cur, ctx);
      return out;
    }
    // The same game read from the other side (two favorites playing each
    // other) keeps its scores straight.
    bool same_side = s->team_abbr == cur.team_abbr;
    int was_team = same_side ? s->team_score : s->opp_score;
    int was_opp = same_side ? s->opp_score : s->team_score;

    // An old copy of the game: hold everything until it either goes away
    // or has held long enough to be the truth.
    bool back = rank(cur.state) < rank(s->state) || cur.team_score < was_team || cur.opp_score < was_opp ||
                (cur.state == GameState::IN && s->state == GameState::IN && cur.period < s->period);
    if (back) {
      if (!s->back_since_set) {
        s->back_since_set = true;
        s->back_since = ctx.mono_s;
      }
      if (ctx.mono_s - s->back_since < kHaStepBackHoldS)
        return out;
      this->store_(*s, cur);
      s->period_done = std::min<int>(s->period_done, cur.period);
      s->back_since_set = false;  // the next step back waits its own 90 s
      return out;
    }
    s->back_since_set = false;

    if (cur.state == GameState::PRE) {
      if (!s->pregame_known) {
        pregame_decide_(*s, cur, ctx);  // the first read with a clock settles it, and fires nothing
      } else if (!s->pregame_done && ctx.pregame_minutes > 0) {
        int64_t left = minutes_left_(cur, ctx);
        if (left >= 0 && left <= ctx.pregame_minutes) {
          HaEvent e = base_(cur, true, ctx);
          e.kind = HaKind::PREGAME;
          e.minutes = (int) left;
          out.push_back(e);
          s->pregame_done = true;
        }
      }
      this->store_(*s, cur);
      return out;
    }

    bool was_pre = s->state == GameState::PRE;
    bool was_live = s->state == GameState::IN || was_pre;
    if (was_pre && cur.state == GameState::IN) {
      HaEvent e = base_(cur, true, ctx);
      e.kind = HaKind::START;
      out.push_back(e);
    }
    // A score on the last play shows up with the final, so a game that went
    // from live to over still checks the scores first. A game first seen
    // before the start and next seen over (the panel was off) has no live
    // part to report.
    bool scored_live = was_live && !(was_pre && cur.state == GameState::POST);
    if (scored_live) {
      if (cur.team_score > was_team)
        out.push_back(score_(cur, true, cur.team_score - was_team, ctx));
      if (cur.opp_score > was_opp)
        out.push_back(score_(cur, false, cur.opp_score - was_opp, ctx));
      int lead_was = sign(was_team - was_opp), lead_now = sign(cur.team_score - cur.opp_score);
      if (lead_now != lead_was)
        out.push_back(lead_(cur, lead_was, lead_now, ctx));
    }
    // The end of a period: the break after it, or, when the panel missed a
    // break or two, every period over by the time the next is under way, in
    // order. A final ends the last one itself.
    if (cur.state == GameState::IN && !was_pre) {
      int last = ha_at_break(cur) ? cur.period : cur.period - 1;
      for (int ended = s->period_done + 1; ended <= last; ended++) {
        HaEvent e = base_(cur, true, ctx);
        e.kind = HaKind::PERIOD;
        e.period = ended;
        e.period_name = ha_period_name(cur, ended);
        out.push_back(e);
        s->period_done = ended;
      }
    }
    if (cur.state == GameState::IN && was_pre)
      s->period_done = ha_at_break(cur) ? cur.period : cur.period - 1;
    // ESPN also moves a suspended or canceled game to post, with completed
    // false. Only a completed game has a final.
    if (s->state == GameState::IN && cur.state == GameState::POST && cur.completed)
      out.push_back(final_(cur, ctx));
    this->store_(*s, cur);
    return out;
  }

  // Shorthand for the tests and for reads with nothing but the side.
  std::vector<HaEvent> update(const GameSnapshot &cur, bool neutral) {
    HaContext ctx;
    ctx.neutral = neutral;
    return this->update(cur, ctx);
  }

 private:
  struct Seen {
    std::string event_id;
    std::string team_abbr;  // the side team_score is
    GameState state{GameState::NOT_FOUND};
    int team_score{0}, opp_score{0};
    int period{0};
    int period_done{0};  // the last period whose end has been reported
    bool pregame_done{false};
    bool pregame_known{false};  // whether pregame_done has been settled by a read with the clock
    bool back_since_set{false};
    uint32_t back_since{0};  // mono_s when reads started going back
  };
  static constexpr size_t kGames = 8;

  static int rank(GameState s) { return s == GameState::PRE ? 0 : s == GameState::IN ? 1 : 2; }
  static int sign(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

  // Minutes to the start, rounded up; -1 when the start is unknown or past.
  static int64_t minutes_left_(const GameSnapshot &cur, const HaContext &ctx) {
    if (ctx.now_epoch <= 0 || cur.kickoff_epoch <= 0 || cur.time_tbd)
      return -1;
    int64_t s = cur.kickoff_epoch - ctx.now_epoch;
    if (s < 0)
      return -1;
    return (s + 59) / 60;
  }

  // Moves the game to the back, so the one forgotten first is the one
  // followed least recently.
  Seen *find_(const std::string &event_id) {
    for (size_t i = 0; i < this->seen_.size(); i++) {
      if (this->seen_[i].event_id != event_id)
        continue;
      std::rotate(this->seen_.begin() + i, this->seen_.begin() + i + 1, this->seen_.end());
      return &this->seen_.back();
    }
    return nullptr;
  }

  static void store_(Seen &s, const GameSnapshot &cur) {
    s.team_abbr = cur.team_abbr;
    s.state = cur.state;
    s.team_score = cur.team_score;
    s.opp_score = cur.opp_score;
    s.period = cur.period;
  }

  void remember_(const GameSnapshot &cur, const HaContext &ctx) {
    if (this->seen_.size() >= kGames)
      this->seen_.erase(this->seen_.begin());
    Seen s;
    s.event_id = cur.event_id;
    store_(s, cur);
    // Whatever already happened stays unreported: a period already over, a
    // reminder already due.
    s.period_done = cur.state != GameState::IN ? 0 : ha_at_break(cur) ? cur.period : cur.period - 1;
    s.pregame_done = s.pregame_known = cur.state != GameState::PRE;
    if (!s.pregame_known)
      pregame_decide_(s, cur, ctx);
    this->seen_.push_back(s);
  }

  // Whether the reminder is still to come, settled once by the first read
  // with the clock: a game already inside its window when the panel picks
  // it up has had its reminder (before a restart, say) and gets no other.
  // Before the clock is set there is no telling, so the decision waits: a
  // panel turned on ten minutes before the start must not send one when
  // its clock arrives a tick later.
  static void pregame_decide_(Seen &s, const GameSnapshot &cur, const HaContext &ctx) {
    if (ctx.now_epoch <= 0)
      return;
    int64_t left = minutes_left_(cur, ctx);
    s.pregame_done = left >= 0 && left <= ctx.pregame_minutes;
    s.pregame_known = true;
  }

  // An event about one side of the game, ours (left on the board) or theirs.
  static HaEvent base_(const GameSnapshot &cur, bool ours, const HaContext &ctx) {
    HaEvent e;
    e.event_id = cur.event_id;
    e.league = ctx.league;
    e.sport = ha_sport(cur.sport);
    e.team = ours ? cur.team_abbr : cur.opp_abbr;
    e.opponent = ours ? cur.opp_abbr : cur.team_abbr;
    e.team_rgb = ha_rgb(ours ? cur.team_color : cur.opp_color);
    e.opponent_rgb = ha_rgb(ours ? cur.opp_color : cur.team_color);
    e.team_score = ours ? cur.team_score : cur.opp_score;
    e.opponent_score = ours ? cur.opp_score : cur.team_score;
    e.my_team = ours && !ctx.neutral;
    return e;
  }

  static HaEvent score_(const GameSnapshot &cur, bool ours, int points, const HaContext &ctx) {
    HaEvent e = base_(cur, ours, ctx);
    e.kind = HaKind::SCORE;
    e.play = ha_play(cur, points);
    e.points = points;
    return e;
  }

  // lead_was and lead_now: 1 we lead, -1 they lead, 0 level.
  static HaEvent lead_(const GameSnapshot &cur, int lead_was, int lead_now, const HaContext &ctx) {
    HaEvent e = base_(cur, lead_now >= 0, ctx);
    e.kind = HaKind::LEAD;
    e.leader = lead_now > 0 ? cur.team_abbr : lead_now < 0 ? cur.opp_abbr : "";
    e.previous_leader = lead_was > 0 ? cur.team_abbr : lead_was < 0 ? cur.opp_abbr : "";
    e.change = lead_now == 0 ? "tied" : lead_was == 0 ? "took_lead" : "lead_change";
    return e;
  }

  static HaEvent final_(const GameSnapshot &cur, const HaContext &ctx) {
    HaEvent e = base_(cur, true, ctx);
    e.kind = HaKind::FINAL;
    e.play = "final";
    e.my_team = !ctx.neutral;
    // A shootout decides a level soccer game, as on the board.
    int us = cur.team_score, them = cur.opp_score;
    if (us == them && cur.sport == Sport::SOCCER && cur.soc.team_shootout >= 0 && cur.soc.opp_shootout >= 0) {
      us = cur.soc.team_shootout;
      them = cur.soc.opp_shootout;
    }
    e.result = us > them ? "win" : us < them ? "loss" : "draw";
    e.winner = us > them ? cur.team_abbr : us < them ? cur.opp_abbr : "";
    return e;
  }

  std::vector<Seen> seen_;  // followed least recently first
};

}  // namespace espn
