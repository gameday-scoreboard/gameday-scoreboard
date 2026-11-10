// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// Soccer's big plays: a brace or hat trick, a penalty and a late goal. All
// four are goals, so they read from the goal list the panel already keeps
// (ESPN's details, Soccer::goals) the moment the score moves. Pure, no
// ESPHome includes: the host tests run it over recorded goals.
#pragma once
#include <cstdint>
#include <string>

#include "celeb_plan.h"
#include "espn_parse.h"

namespace espn {

// How long each holds the board: the length of its frames
// (tools/celeb_art/soccer_frames.py) and a beat of the caption after.
constexpr uint32_t kCelebBraceMs = 5000;
constexpr uint32_t kCelebPenaltyMs = 5000;
constexpr uint32_t kCelebLateGoalMs = 5000;

enum class SoccerPlay : uint8_t { NONE = 0, BRACE, HAT_TRICK, PENALTY, LATE_GOAL };

struct SoccerBigPlay {
  SoccerPlay play{SoccerPlay::NONE};
  bool ours{true};     // whose goal: our side, or the other (neutral modes only)
  std::string words;   // the caption: the scorer for a brace or hat trick, the minute for a late goal
};

// The minute a goal went in, from ESPN's clock: "89'" is 89, "90'+7'" is 97,
// "45'+2'" is 47. 0 when the clock says nothing.
inline int soccer_goal_minute(const std::string &clock) {
  int base = 0, added = 0, *n = &base;
  bool any = false;
  for (char c : clock) {
    if (c >= '0' && c <= '9') {
      *n = *n * 10 + (c - '0');
      any = true;
    } else if (c == '+') {
      n = &added;
    }
  }
  return any ? base + added : 0;
}

// The minute as the caption shows it, the way the clock row writes it
// without the inner mark: "89'", "90+3'".
inline std::string soccer_minute_words(const std::string &clock) {
  std::string out;
  for (char c : clock)
    if ((c >= '0' && c <= '9') || c == '+')
      out += c;
  return out.empty() ? out : out + "'";
}

// Late: the last five minutes of normal time and its stoppage (period 2), or
// of extra time (period 4). A first-half stoppage goal ("45'+3'") never is.
// With no period in the read (0), the minute alone decides, as normal time.
inline bool soccer_late_minute(int minute, int period) {
  if (period == 4)
    return minute >= 115;
  if (period == 2 || period == 0)
    return minute >= 85 && minute < 105;
  return false;
}

// The scorer as the caption shows the name: capitals, the last name when the
// whole name is longer than max.
inline std::string soccer_caption_name(const std::string &name, size_t max) {
  std::string n = name;
  if (n.size() > max) {
    size_t sp = n.rfind(' ');
    if (sp != std::string::npos)
      n = n.substr(sp + 1);
  }
  if (n.size() > max)
    n = n.substr(0, max);
  for (char &c : n)
    if (c >= 'a' && c <= 'z')
      c = (char) (c - 32);
  return n;
}

// How many goals in the list count for this side.
inline int soccer_side_goals(const Soccer &s, bool ours) {
  int n = 0;
  for (const auto &g : s.goals)
    if (g.ours == ours)
      n++;
  return n;
}

// The big play for this read, if the read brought one: the side's score
// went up AND its newest goal is in the list (ESPN's details can trail the
// score by a read; then it is a plain goal, never a late show of the play).
// Ours always; the other side's only when neutral and opp_splashes. When a
// goal is more than one of these, the rarest wins: hat trick, late goal,
// brace, penalty.
inline SoccerBigPlay soccer_big_play(const GameSnapshot &prev, const GameSnapshot &cur, bool neutral,
                                     bool opp_splashes) {
  SoccerBigPlay none;
  if (!prev.valid || !cur.valid || cur.sport != Sport::SOCCER || cur.event_id.empty() ||
      prev.event_id != cur.event_id || prev.state != GameState::IN || cur.state != GameState::IN)
    return none;
  if (cur.period >= 5)
    return none;  // the shootout has its own board; its kicks are not goals
  const bool ours_up = cur.team_score > prev.team_score;
  const bool theirs_up = cur.opp_score > prev.opp_score;
  bool ours;
  if (ours_up)
    ours = true;
  else if (theirs_up && neutral && opp_splashes)
    ours = false;
  else
    return none;
  if (soccer_side_goals(cur.soc, ours) <= soccer_side_goals(prev.soc, ours))
    return none;
  const SoccerGoal *goal = nullptr;
  for (const auto &g : cur.soc.goals)
    if (g.ours == ours)
      goal = &g;
  if (goal == nullptr)
    return none;

  SoccerBigPlay p;
  p.ours = ours;
  int same = 0;
  if (!goal->own_goal && !goal->name.empty())
    for (const auto &g : cur.soc.goals)
      if (g.ours == ours && !g.own_goal && g.name == goal->name)
        same++;
  const int us = ours ? cur.team_score : cur.opp_score, them = ours ? cur.opp_score : cur.team_score;
  const int minute = soccer_goal_minute(goal->clock);
  const bool late = us == them + 1 && minute > 0 && soccer_late_minute(minute, cur.period);
  if (same == 3) {
    p.play = SoccerPlay::HAT_TRICK;
    p.words = soccer_caption_name(goal->name, 20);
  } else if (late) {
    p.play = SoccerPlay::LATE_GOAL;
    p.words = soccer_minute_words(goal->clock);
  } else if (same == 2) {
    p.play = SoccerPlay::BRACE;
    p.words = soccer_caption_name(goal->name, 20);
  } else if (goal->penalty && !goal->own_goal) {
    p.play = SoccerPlay::PENALTY;
  } else {
    return none;
  }
  return p;
}

// The plan for decide_celebration: called only on Full with Big plays on.
// NONE when the read is a plain goal (the net ripple, as before).
inline CelebPlan big_play_soccer(const GameSnapshot &prev, const GameSnapshot &cur, const Splash &splash,
                                 bool neutral, bool opp_splashes) {
  (void) splash;
  CelebPlan plan;
  const SoccerBigPlay b = soccer_big_play(prev, cur, neutral, opp_splashes);
  switch (b.play) {
    case SoccerPlay::BRACE:
      plan.kind = CelebKind::BRACE;
      plan.text = "BRACE!";
      plan.duration_ms = kCelebBraceMs;
      break;
    case SoccerPlay::HAT_TRICK:
      plan.kind = CelebKind::SOCCER_HAT_TRICK;
      plan.text = "HAT TRICK!";
      plan.duration_ms = kCelebBraceMs;
      break;
    case SoccerPlay::PENALTY:
      plan.kind = CelebKind::PENALTY;
      plan.text = "PENALTY!";
      plan.duration_ms = kCelebPenaltyMs;
      break;
    case SoccerPlay::LATE_GOAL:
      plan.kind = CelebKind::LATE_GOAL;
      plan.text = "LATE GOAL!";
      plan.duration_ms = kCelebLateGoalMs;
      break;
    default:
      return plan;
  }
  plan.line2 = b.words;
  plan.color = celeb_bright(parse_color(b.ours ? cur.team_color : cur.opp_color));
  plan.alt = celeb_alt_for(plan.color);
  return plan;
}

}  // namespace espn
