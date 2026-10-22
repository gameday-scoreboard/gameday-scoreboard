#pragma once

// The scoring log for the halftime and final tickers: each score the panel
// saw, as it went by, "Q1 7:12 Lamb 12 yd pass, DAL 7-0". ESPN's event
// document carries only the last play, so a score is caught from the change
// in the score between two reads, and described from the last play when
// that play is the score. A read that missed the play still logs the score
// ("Q2 DAL TD, DAL 14-7"). Basketball (every basket scores) and soccer (the
// ticker already lists the goals) keep no log. Pure, for the host tests.

#include <algorithm>
#include <string>
#include <vector>

#include "espn_parse.h"
#include "line_score.h"

namespace espn {

namespace detail {

// "J. Grier" -> "Grier", "O. Mabson II" -> "Mabson II", "G.Kittle" -> "Kittle"
inline std::string last_name(const std::string &short_name) {
  size_t dot = short_name.find('.');
  std::string out = short_name;
  if (dot != std::string::npos && dot <= 2)
    out = short_name.substr(dot + 1);
  size_t start = out.find_first_not_of(' ');
  return start == std::string::npos ? "" : ascii_fold(out.substr(start));
}

// The receiver of a pass, from ESPN's play text: "B.Purdy pass short left to
// G.Kittle for 11 yards" or "#17 J.Kohl pass complete short right to #4
// J.Resar caught at ...". "" when the text has no "pass ... to".
inline std::string pass_target(const std::string &text) {
  size_t pass = text.find(" pass ");
  if (pass == std::string::npos)
    return "";
  size_t to = text.find(" to ", pass);
  if (to == std::string::npos)
    return "";
  size_t at = to + 4;
  if (at < text.size() && text[at] == '#') {
    at = text.find(' ', at);
    if (at == std::string::npos)
      return "";
    at++;
  }
  size_t end = text.find_first_of(" ,", at);
  std::string name = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
  // A "to the MNE00" yard line is not a name
  if (name.empty() || name == "the")
    return "";
  return last_name(name);
}

inline bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

// "Grier 28 yd run", "Kittle 11 yd pass", "Spetic 48 yd FG", "Safety", or ""
// when the last play does not say how the points came.
inline std::string football_score_play(const GameSnapshot &g) {
  const std::string &t = g.play_type;
  std::string who = last_name(g.play_athlete);
  std::string kind;
  if (has(t, "Field Goal")) {
    kind = "FG";
  } else if (has(t, "Safety")) {
    return "Safety";
  } else if (has(t, "Touchdown")) {
    if (has(t, "Passing")) {
      std::string to = pass_target(g.last_play);
      if (!to.empty())
        who = to;
      kind = "pass";
    } else if (has(t, "Rushing")) {
      kind = "run";
    } else if (has(t, "Interception")) {
      kind = "INT return";
    } else if (has(t, "Fumble")) {
      kind = "fumble return";
    } else if (has(t, "Punt")) {
      kind = "punt return";
    } else if (has(t, "Kickoff")) {
      kind = "kickoff return";
    } else {
      kind = "TD";
    }
  } else {
    return "";
  }
  if (who.empty())
    return kind == "FG" ? "FG" : "TD";
  if (g.play_yards > 0 && kind != "TD")
    return who + " " + std::to_string(g.play_yards) + " yd " + kind;
  return who + " " + kind;
}

// "Mesa Jr. homered to right center, Caminero scored": ESPN's play text
// without the distance in brackets or the closing full stop, at most 60
// characters.
inline std::string baseball_score_play(const GameSnapshot &g) {
  std::string out;
  int depth = 0;
  for (char c : g.last_play) {
    if (c == '(') {
      depth++;
      continue;
    }
    if (c == ')') {
      depth = std::max(0, depth - 1);
      continue;
    }
    if (depth > 0)
      continue;
    if (c == ' ' && (out.empty() || out.back() == ' '))
      continue;
    out += c;
  }
  // "right center , Caminero" after the brackets came out
  std::string tidy;
  for (size_t i = 0; i < out.size(); i++) {
    if (out[i] == ' ' && i + 1 < out.size() && (out[i + 1] == ',' || out[i + 1] == '.'))
      continue;
    tidy += out[i];
  }
  while (!tidy.empty() && (tidy.back() == '.' || tidy.back() == ' '))
    tidy.pop_back();
  tidy = ascii_fold(tidy);
  if (tidy.size() > 60)
    tidy = tidy.substr(0, 57) + "...";
  return tidy;
}

// "Q1", "OT", "2OT"; hockey "P1", "OT", "SO"; baseball "T6", "B6".
inline std::string score_period(const GameSnapshot &g) {
  int p = g.period;
  if (g.sport == Sport::BASEBALL) {
    // ESPN moves to "Mid 6th" as the third out is made; a run in that read
    // came in the top half.
    bool bottom = g.short_detail.rfind("Bot", 0) == 0 || g.short_detail.rfind("End", 0) == 0;
    return (bottom ? "B" : "T") + std::to_string(p);
  }
  uint8_t reg = g.sport == Sport::HOCKEY ? 3 : 4;
  if (p >= 1 && p <= reg)
    return (g.sport == Sport::HOCKEY ? "P" : "Q") + std::to_string(p);
  if (p == reg + 1)
    return "OT";
  if (g.sport == Sport::HOCKEY && !g.nhl.playoffs && p == 5)
    return "SO";
  if (p > reg)
    return std::to_string(p - reg) + "OT";
  return "";
}

}  // namespace detail

class ScoreLog {
 public:
  // Takes one read of the board's game. A read repeats on every redraw; only
  // a change in the score adds a line. The first read of a game logs
  // nothing: the panel just picked it up and did not see the scores before.
  void update(const GameSnapshot &cur) {
    if (!cur.valid || cur.event_id.empty() || cur.sport == Sport::BASKETBALL || cur.sport == Sport::SOCCER)
      return;
    if (cur.state != GameState::IN && cur.state != GameState::POST)
      return;
    Game *g = this->find_(cur.event_id);
    if (g == nullptr) {
      // A final read for the first time (the finals rotation, a game that
      // ended before the panel started) has no log to keep.
      if (cur.state != GameState::IN)
        return;
      if (this->games_.size() >= kGames)
        this->games_.erase(this->games_.begin());
      Game fresh;
      fresh.event_id = cur.event_id;
      fresh.team_abbr = cur.team_abbr;
      fresh.team_score = cur.team_score;
      fresh.opp_score = cur.opp_score;
      this->games_.push_back(fresh);
      return;
    }
    // The same game read from the other side (two favorites playing each
    // other) keeps its scores straight.
    bool same_side = g->team_abbr == cur.team_abbr;
    int was_team = same_side ? g->team_score : g->opp_score;
    int was_opp = same_side ? g->opp_score : g->team_score;
    bool both = cur.team_score > was_team && cur.opp_score > was_opp;
    if (cur.team_score > was_team)
      this->add_(*g, cur, true, cur.team_score - was_team, both);
    if (cur.opp_score > was_opp)
      this->add_(*g, cur, false, cur.opp_score - was_opp, both);
    g->team_abbr = cur.team_abbr;
    g->team_score = cur.team_score;
    g->opp_score = cur.opp_score;
  }

  // The game's lines, oldest first: "Q1 7:12 Lamb 12 yd pass, DAL 7-0".
  std::vector<std::string> lines(const std::string &event_id) const {
    std::vector<std::string> out;
    for (const auto &g : this->games_) {
      if (g.event_id != event_id)
        continue;
      for (const auto &e : g.entries)
        out.push_back(e.what + ", " + e.abbr + " " + std::to_string(e.score) + "-" + std::to_string(e.other));
    }
    return out;
  }

  static constexpr size_t kLines = 12;  // a game's lines kept, the latest
  static constexpr size_t kGames = 2;   // the board's game and the one before it

 private:
  struct Entry {
    std::string what;  // "Q1 7:12 Lamb 12 yd pass"
    std::string abbr;  // the side that scored
    int score{0}, other{0};
    bool try_due{false};  // football: a touchdown whose extra point or two-point try is still to come
  };
  struct Game {
    std::string event_id;
    std::string team_abbr;  // the side team_score is
    int team_score{0}, opp_score{0};
    std::vector<Entry> entries;
  };

  Game *find_(const std::string &event_id) {
    for (size_t i = 0; i < this->games_.size(); i++) {
      if (this->games_[i].event_id != event_id)
        continue;
      std::rotate(this->games_.begin() + i, this->games_.begin() + i + 1, this->games_.end());
      return &this->games_.back();
    }
    return nullptr;
  }

  // both: the two sides scored between reads, so the last play can only
  // describe one of them and describes neither.
  void add_(Game &g, const GameSnapshot &cur, bool ours, int points, bool both) {
    const std::string &abbr = ours ? cur.team_abbr : cur.opp_abbr;
    int score = ours ? cur.team_score : cur.opp_score;
    int other = ours ? cur.opp_score : cur.team_score;
    uint32_t side_id = ours ? cur.team_id : cur.opp_id;
    // The last play is this score when it scored and, if ESPN says whose
    // play it was, it was this side's.
    bool play_is_it = !both && cur.play_value > 0 &&
                      (cur.play_team == 0 || cur.play_team == side_id ||
                       (cur.play_team != cur.team_id && cur.play_team != cur.opp_id));
    bool safety = play_is_it && detail::has(cur.play_type, "Safety");
    if (cur.sport == Sport::FOOTBALL && (points == 1 || points == 2) && !safety && !g.entries.empty()) {
      // The try after a touchdown finishes that touchdown's line
      Entry &last = g.entries.back();
      if (last.try_due && last.abbr == abbr && last.other == other) {
        last.score = score;
        last.try_due = false;
        return;
      }
    }
    Entry e;
    e.abbr = abbr;
    e.score = score;
    e.other = other;
    std::string what;
    std::string clock;
    switch (cur.sport) {
      case Sport::FOOTBALL:
        if (play_is_it)
          what = detail::football_score_play(cur);
        if (what.empty())
          what = abbr + (points >= 6 ? " TD" : points == 3 ? " FG" : points == 2 ? " safety" : " scores");
        e.try_due = points == 6;
        clock = cur.display_clock;
        break;
      case Sport::HOCKEY: {
        if (detail::score_period(cur) == "SO") {
          what = abbr + " wins shootout";
          break;
        }
        std::string who = play_is_it ? detail::last_name(cur.play_athlete) : "";
        what = who.empty() ? abbr + " goal" : who + " goal";
        clock = cur.display_clock;
        break;
      }
      case Sport::BASEBALL:
        if (play_is_it)
          what = detail::baseball_score_play(cur);
        if (what.empty())
          what = abbr + " " + std::to_string(points) + (points == 1 ? " run" : " runs");
        break;
      default:
        what = abbr + " scores";
        break;
    }
    std::string period = detail::score_period(cur);
    e.what = period;
    if (!clock.empty() && clock != "0:00" && clock != "0.0")
      e.what += (e.what.empty() ? "" : " ") + clock;
    e.what += (e.what.empty() ? "" : " ") + what;
    g.entries.push_back(std::move(e));
    if (g.entries.size() > kLines)
      g.entries.erase(g.entries.begin());
  }

  std::vector<Game> games_;  // followed least recently first
};

// The halftime or final ticker with the headline (a final only) and the
// scoring log after its first part: "Final | <headline> | Q1 ... | records".
inline std::string with_score_log(const std::string &status, const std::string &headline,
                                  const std::vector<std::string> &log) {
  std::string extra;
  if (!headline.empty())
    detail::add_part(extra, ascii_fold(headline));
  for (const auto &l : log)
    detail::add_part(extra, l);
  if (extra.empty())
    return status;
  size_t bar = status.find(" | ");
  if (bar == std::string::npos)
    return status.empty() ? extra : status + " | " + extra;
  return status.substr(0, bar) + " | " + extra + status.substr(bar);
}

}  // namespace espn
