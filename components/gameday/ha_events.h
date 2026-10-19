#pragma once

// Home Assistant's score and final events (gameday-common.yaml, on_score and
// on_final). They follow the game Home Assistant's Game Status follows, and
// fire for both sides whatever the board's splash settings: basketball only
// splashes the win, and the other side's splash can be off, but a light show
// in the living room is the owner's call in their own automation.

#include <algorithm>
#include <string>
#include <vector>

#include "espn_parse.h"

namespace espn {

// One event for Home Assistant. A score names the side that scored as team;
// a final names the panel's own side (the team on the left of a My team or
// Favorites board) as team, with the result from its side.
struct HaEvent {
  std::string team, opponent;
  std::string play;  // "touchdown", "field_goal", "run", "goal"...; "final" for a final
  int points{0};     // a score: the points between two reads
  int team_score{0}, opponent_score{0};
  std::string sport;   // "football", "baseball", "basketball", "hockey", "soccer"
  std::string result;  // a final: "win", "loss" or "draw" for team
  std::string winner;  // a final: the winner's abbreviation, "" on a draw
  bool my_team{false};  // a score: the owner's team scored; a final: team is the owner's team
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

// Remembers the last read of the games Home Assistant followed most
// recently, so Favorites alternating between two live games, or Live games
// rotating, still sees each score once when its game comes back: late, if
// the score came while another game was up. Eight covers a full Sunday
// slot of NFL games. A game read for the first time fires nothing: the
// panel just started or picked it up.
class HaTracker {
 public:
  // cur is the game Home Assistant follows now; neutral is true in the Live
  // modes, where neither side is the owner's team. Returns the events since
  // the last read of the same game, the panel's side first, and remembers it.
  std::vector<HaEvent> update(const GameSnapshot &cur, bool neutral) {
    std::vector<HaEvent> out;
    if (!cur.valid || cur.event_id.empty() || cur.state == GameState::NOT_FOUND)
      return out;
    Seen *s = this->find_(cur.event_id);
    if (s == nullptr) {
      this->remember_(cur);
      return out;
    }
    // The same game read from the other side (two favorites playing each
    // other) keeps its scores straight.
    bool same_side = s->team_abbr == cur.team_abbr;
    int was_team = same_side ? s->team_score : s->opp_score;
    int was_opp = same_side ? s->opp_score : s->team_score;
    bool was_live = s->state == GameState::IN;
    // A score on the last play shows up with the final, so a game that went
    // from live to over still checks the scores first.
    if (was_live && (cur.state == GameState::IN || cur.state == GameState::POST)) {
      if (cur.team_score > was_team)
        out.push_back(score_(cur, true, cur.team_score - was_team, neutral));
      if (cur.opp_score > was_opp)
        out.push_back(score_(cur, false, cur.opp_score - was_opp, neutral));
    }
    // ESPN also moves a suspended or canceled game to post, with completed
    // false. Only a completed game has a final.
    if (was_live && cur.state == GameState::POST && cur.completed)
      out.push_back(final_(cur, neutral));
    s->team_abbr = cur.team_abbr;
    s->state = cur.state;
    s->team_score = cur.team_score;
    s->opp_score = cur.opp_score;
    return out;
  }

 private:
  struct Seen {
    std::string event_id;
    std::string team_abbr;  // the side team_score is
    GameState state{GameState::NOT_FOUND};
    int team_score{0}, opp_score{0};
  };
  static constexpr size_t kGames = 8;

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

  void remember_(const GameSnapshot &cur) {
    if (this->seen_.size() >= kGames)
      this->seen_.erase(this->seen_.begin());
    Seen s;
    s.event_id = cur.event_id;
    s.team_abbr = cur.team_abbr;
    s.state = cur.state;
    s.team_score = cur.team_score;
    s.opp_score = cur.opp_score;
    this->seen_.push_back(s);
  }

  static HaEvent score_(const GameSnapshot &cur, bool ours, int points, bool neutral) {
    HaEvent e;
    e.team = ours ? cur.team_abbr : cur.opp_abbr;
    e.opponent = ours ? cur.opp_abbr : cur.team_abbr;
    e.play = ha_play(cur, points);
    e.points = points;
    e.team_score = ours ? cur.team_score : cur.opp_score;
    e.opponent_score = ours ? cur.opp_score : cur.team_score;
    e.sport = ha_sport(cur.sport);
    e.my_team = ours && !neutral;
    return e;
  }

  static HaEvent final_(const GameSnapshot &cur, bool neutral) {
    HaEvent e;
    e.team = cur.team_abbr;
    e.opponent = cur.opp_abbr;
    e.play = "final";
    e.team_score = cur.team_score;
    e.opponent_score = cur.opp_score;
    e.sport = ha_sport(cur.sport);
    e.my_team = !neutral;
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
