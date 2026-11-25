// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

// What is happening in a basketball game, for the row under the clock on a
// wide board (row_text.h): a run, a drought, a lead change, the biggest lead.
// It is worked out from the score and the clock of the reads the panel
// already does; nothing more is read from ESPN.
//
// A line shows only when the reads prove it. Two things they can't see:
// - What came before the first read. Until the panel has watched from the
//   start (or one side still has 0), nothing about the whole game is said.
// - The order of the baskets between two reads. A count that depends on it
//   (lead changes, times tied) isn't kept, and "has never led" allows for
//   the worst order.
//
// Pure, with no ESPHome includes, so the host tests build it. It holds no
// heap memory.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "espn_parse.h"
#include "game_leaders.h"
#include "psram.h"

namespace espn {

// Seconds left on the period's clock, "7:38" or "45.3"; -1 when it can't be
// read.
inline int clock_left(const std::string &clock) {
  const char *c = clock.c_str();
  if (!isdigit((unsigned char) c[0]))
    return -1;
  int left = atoi(c);
  const char *colon = strchr(c, ':');
  if (colon != nullptr)
    left = left * 60 + atoi(colon + 1);
  return left >= 0 && left <= 1200 ? left : -1;
}

// How long the period is: halves 20 minutes, overtime 5. ESPN's event doesn't
// say whether quarters are 10 minutes or the NBA's 12; a clock over 10:00
// does, and a panel that missed the NBA's first two minutes of a quarter was
// away long enough to start over (Story::read).
inline int period_length(const GameSnapshot &g, int left) {
  if (g.period > g.nba.regulation)
    return 300;
  if (g.nba.regulation == 2)
    return 1200;
  return left > 600 ? 720 : 600;
}

enum class StoryKind : uint8_t { NONE, RUN, RUN_OVER, DROUGHT, TAKES_LEAD, BIG_LEAD, CLOSE, COMEBACK, NEVER_LED };

struct StoryLine {
  StoryKind kind{StoryKind::NONE};
  uint8_t score{0};   // how much it deserves the row, 0 to 100
  uint16_t value{0};  // what it counts; a change makes it news again
  char text[22]{};    // fits 21 characters, a 128-wide row in the 6 px font
};

class Story {
 public:
  static constexpr int kMarks = 24;
  static constexpr int kShow = 2;          // the lines the row takes
  static constexpr uint8_t kFloor = 30;    // below this a line isn't shown
  static constexpr uint8_t kLead = 70;     // from here it goes before the last basket
  static constexpr uint32_t kFreshMs = 60 * 1000;

  // One read of the board game. A redraw of the same read changes nothing.
  void read(const GameSnapshot &g, uint32_t now_ms) {
    if (!g.valid || g.sport != Sport::BASKETBALL || g.state != GameState::IN)
      return;
    const int64_t id = strtoll(g.event_id.c_str(), nullptr, 10);
    if (id != this->event_) {
      *this = Story();
      this->event_ = id;
    }
    const int left = clock_left(g.display_clock);
    if (left < 0 || g.period < 1)
      return;
    const int t = g.team_score, o = g.opp_score;
    if (!this->started_) {
      this->begin_(g, left);
    } else if (t < this->t_ || o < this->o_ || g.period < this->period_ ||
               (g.period == this->period_ && left > this->secs_ + 30)) {
      // A lower score or an earlier clock is an old copy from ESPN's cache
      // or a correction. The copy passes; a correction that stays ends what
      // the panel knows of the game so far.
      if (this->back_ms_ == 0)
        this->back_ms_ = now_ms == 0 ? 1 : now_ms;
      if (now_ms - this->back_ms_ < kBackMs)
        return;
      this->begin_(g, left);
    } else {
      // Time played since the last read, over a break between periods too.
      int played = this->secs_ - left;
      if (g.period != this->period_)
        played = this->secs_ + period_length(g, left) - left;
      this->back_ms_ = 0;
      if (t - this->t_ + o - this->o_ >= kGapPoints || played > kGapSecs || g.period > this->period_ + 1) {
        // The panel was away (another game on the board, no Wi-Fi).
        this->begin_(g, left);
      } else {
        this->period_ = g.period;
        this->secs_ = left;
        this->step_(g, t, o, this->at_ + played);
      }
    }
    this->final_ = g.period >= g.nba.regulation;
    this->left_ = this->final_ ? left : -1;
    this->rank_(g, now_ms);
  }

  // The lines worth showing now, best first; n of them, at most kShow.
  int count(const GameSnapshot &g) const { return this->is_(g) ? this->n_lines_ : 0; }
  const StoryLine &line(int i) const { return this->lines_[i]; }
  // Changes when the best line has news, so the row jumps to it.
  uint32_t news() const { return this->news_; }
  bool whole_game() const { return this->whole_; }

 private:
  static constexpr int kGapPoints = 9;
  static constexpr int kGapSecs = 90;
  static constexpr uint32_t kBackMs = 90 * 1000;
  // Where the game clock starts counting, so one set back stays above 0.
  static constexpr int kStart = 2000;

  struct Mark {
    uint16_t at;  // seconds played
    uint16_t t, o;
  };

  bool is_(const GameSnapshot &g) const {
    return this->started_ && g.valid && strtoll(g.event_id.c_str(), nullptr, 10) == this->event_;
  }

  // Starts the game's story over from this read.
  void begin_(const GameSnapshot &g, int left) {
    const int64_t id = this->event_;
    const uint32_t news = this->news_;
    *this = Story();
    const int t = g.team_score, o = g.opp_score;
    this->event_ = id;
    this->news_ = news;
    this->started_ = true;
    this->whole_ = t == 0 || o == 0;
    this->t_ = t;
    this->o_ = o;
    this->period_ = g.period;
    this->secs_ = left;
    this->at_ = kStart;
    // With one side on 0, every point so far is the other's, so the lead
    // now is the biggest yet, and that side hasn't scored since the tip.
    this->max_t_ = this->hi_t_ = std::max(0, t - o);
    this->max_o_ = this->hi_o_ = std::max(0, o - t);
    const int tip = g.period == 1 ? kStart - std::max(0, period_length(g, left) - left) : kStart;
    this->last_t_ = t == 0 ? tip : -1;
    this->last_o_ = o == 0 ? tip : -1;
    this->leader_ = t > o ? 1 : o > t ? 2 : 0;
    this->marks_[0] = {(uint16_t) kStart, (uint16_t) t, (uint16_t) o};
    this->n_ = 1;
  }

  void step_(const GameSnapshot &g, int t, int o, int at) {
    const int dt = t - this->t_, dp = o - this->o_;
    this->at_ = at;
    if (dt == 0 && dp == 0)
      return;
    int ended_run = 0;
    if (dt > 0 && dp == 0)
      ended_run = this->unanswered_(2);
    else if (dp > 0 && dt == 0)
      ended_run = this->unanswered_(1);
    // The most either side could have led by between the two reads.
    const int margin = this->t_ - this->o_;
    this->hi_t_ = std::max(this->hi_t_, margin + dt);
    this->hi_o_ = std::max(this->hi_o_, dp - margin);
    if (t - o > this->max_t_) {
      this->max_t_ = t - o;
      this->big_at_ = at;
      this->big_side_ = 1;
    } else if (o - t > this->max_o_) {
      this->max_o_ = o - t;
      this->big_at_ = at;
      this->big_side_ = 2;
    }
    if (dt > 0)
      this->last_t_ = at;
    if (dp > 0)
      this->last_o_ = at;
    if (ended_run >= kRun && g.play_value > 0 && !g.play_athlete.empty() &&
        g.play_team == (dt > 0 ? g.team_id : g.opp_id)) {
      this->over_at_ = at;
      snprintf(this->over_name_, sizeof(this->over_name_), "%s", detail::leader_surname(g.play_athlete).c_str());
    }
    const uint8_t leader = t > o ? 1 : o > t ? 2 : this->leader_;
    if (leader != this->leader_ && this->leader_ != 0)
      this->flip_at_ = at;
    this->leader_ = leader;
    this->t_ = t;
    this->o_ = o;
    if (this->n_ == kMarks) {
      memmove(this->marks_, this->marks_ + 1, sizeof(Mark) * (kMarks - 1));
      this->n_--;
    }
    this->marks_[this->n_++] = {(uint16_t) at, (uint16_t) t, (uint16_t) o};
  }

  // Points side (1 ours, 2 theirs) has scored since the other last did.
  int unanswered_(int side) const {
    const Mark &now = this->marks_[this->n_ - 1];
    int best = 0;
    for (int i = this->n_ - 2; i >= 0; i--) {
      const Mark &m = this->marks_[i];
      if (side == 1 ? m.o != now.o : m.t != now.t)
        break;
      best = side == 1 ? now.t - m.t : now.o - m.o;
    }
    return best;
  }

  // The run a side is on, "14-2": its points (a) and the other's (b). 0 when
  // it isn't on one. The longest stretch that still reads as a run wins.
  int run_(int side, int &b) const {
    b = 0;
    const Mark &now = this->marks_[this->n_ - 1];
    const Mark &was = this->marks_[std::max(0, this->n_ - 2)];
    const int last = side == 1 ? now.t - was.t : now.o - was.o;
    if (this->n_ < 2 || last <= 0 || (side == 1 ? now.o != was.o : now.t != was.t))
      return 0;  // a run is live while its side has the last basket
    int best = 0;
    for (int i = this->n_ - 2; i >= 0; i--) {
      const Mark &m = this->marks_[i];
      if (now.at - m.at > 420)
        break;
      const int a = side == 1 ? now.t - m.t : now.o - m.o;
      const int x = side == 1 ? now.o - m.o : now.t - m.t;
      const bool run = x == 0 ? a >= kRun : x <= 2 ? a >= 10 : x <= 4 ? a >= 14 : false;
      if (run && a - 2 * x > best - 2 * b) {
        best = a;
        b = x;
      }
    }
    return best;
  }

  void put_(StoryKind kind, int score, int value, const char *text) {
    if (score < kFloor)
      return;
    // Keep the best kShow, best first.
    StoryLine s;
    s.kind = kind;
    s.score = (uint8_t) std::min(100, score);
    s.value = (uint16_t) value;
    snprintf(s.text, sizeof(s.text), "%s", text);
    for (int i = 0; i < kShow; i++) {
      if (i < this->n_lines_ && this->lines_[i].score >= s.score)
        continue;
      for (int k = std::min(this->n_lines_, kShow - 1); k > i; k--)
        this->lines_[k] = this->lines_[k - 1];
      this->lines_[i] = s;
      this->n_lines_ = std::min(kShow, this->n_lines_ + 1);
      return;
    }
  }

  void rank_(const GameSnapshot &g, uint32_t now_ms) {
    this->n_lines_ = 0;
    char buf[40];
    const char *us = g.team_abbr.c_str(), *them = g.opp_abbr.c_str();
    const int margin = this->t_ - this->o_, lead = abs(margin);
    const bool late = this->final_ && this->left_ >= 0;
    // Out of reach late: only a bigger run or drought still says anything.
    const bool decided = late && lead >= 20 && this->left_ <= 480;

    for (int side = 1; side <= 2; side++) {
      int b = 0;
      const int a = this->run_(side, b);
      const int since = this->at_ - this->marks_[this->n_ - 1].at;
      if (a > 0 && since <= 120 && !(decided && a < 10)) {
        snprintf(buf, sizeof(buf), "%s on a %d-%d run", side == 1 ? us : them, a, b);
        this->put_(StoryKind::RUN, this->aged_(StoryKind::RUN, side, a * 16 + b, 56 + 2 * (a - b), now_ms), a * 16 + b,
                   buf);
      }
      const int last = side == 1 ? this->last_t_ : this->last_o_;
      const int dry = last < 0 ? 0 : this->at_ - last;
      if (dry >= (decided ? 240 : 120)) {
        snprintf(buf, sizeof(buf), "%s scoreless %d:%02d", side == 1 ? us : them, dry / 60, dry % 60);
        this->put_(StoryKind::DROUGHT, this->aged_(StoryKind::DROUGHT, side, dry / 60, 44 + dry / 8, now_ms), dry / 60,
                   buf);
      }
    }
    if (this->over_at_ >= 0 && this->at_ - this->over_at_ <= 30) {
      snprintf(buf, sizeof(buf), "%s ends the run", this->over_name_);
      // 21 characters fit the row.
      if (strlen(buf) > 21)
        snprintf(buf, sizeof(buf), "%s ends run", this->over_name_);
      if (strlen(buf) <= 21)
        this->put_(StoryKind::RUN_OVER, 72, this->over_at_, buf);
    }
    if (this->flip_at_ >= 0 && this->at_ - this->flip_at_ <= 45 && margin != 0) {
      snprintf(buf, sizeof(buf), "%s takes the lead", margin > 0 ? us : them);
      this->put_(StoryKind::TAKES_LEAD, late ? 95 : 74, this->flip_at_, buf);
    }
    if (late && this->left_ <= 180 && lead >= 1 && lead <= 3)
      this->put_(StoryKind::CLOSE, 78, lead, "One-possession game");
    if (this->whole_) {
      // Only while it is the biggest whatever the order between reads.
      const int big = this->big_side_ == 1 ? this->max_t_ : this->max_o_;
      const int top = this->big_side_ == 1 ? this->hi_t_ : this->hi_o_;
      if (this->big_side_ != 0 && big >= 10 && big >= top && big >= this->max_t_ && big >= this->max_o_ && lead == big && this->at_ - this->big_at_ <= 60) {
        snprintf(buf, sizeof(buf), "Biggest lead, %d", big);
        this->put_(StoryKind::BIG_LEAD, this->aged_(StoryKind::BIG_LEAD, this->big_side_, big, 58, now_ms), big, buf);
      }
      for (int side = 1; side <= 2; side++) {
        const int down = side == 1 ? this->max_o_ : this->max_t_;
        const int now = side == 1 ? -margin : margin;
        if (down >= 12 && now <= down / 2 && now <= 8) {
          snprintf(buf, sizeof(buf), "%s was down %d", side == 1 ? us : them, down);
          this->put_(StoryKind::COMEBACK, this->aged_(StoryKind::COMEBACK, side, down, 64, now_ms), down, buf);
        }
        const int hi = side == 1 ? this->hi_t_ : this->hi_o_;
        if (hi <= 0 && this->half_over_(g) && lead > 0) {
          snprintf(buf, sizeof(buf), "%s has never led", side == 1 ? us : them);
          this->put_(StoryKind::NEVER_LED, this->aged_(StoryKind::NEVER_LED, side, side, 46, now_ms), side, buf);
        }
      }
    }
    const uint32_t key = this->n_lines_ == 0 || this->lines_[0].score < kLead
                             ? 0
                             : ((uint32_t) this->lines_[0].kind << 16) | this->lines_[0].value;
    if (key != this->news_key_) {
      this->news_key_ = key;
      if (key != 0)
        this->news_++;
    }
  }

  static bool half_over_(const GameSnapshot &g) { return g.period > std::max(1, (int) g.nba.regulation / 2); }

  // A line that has been up a minute without changing gives way: it comes
  // back when what it counts changes.
  int aged_(StoryKind kind, int side, int value, int score, uint32_t now_ms) {
    Seen &s = this->seen_[(int) kind * 2 + side - 1];
    if (s.ms == 0 || s.value != (uint16_t) value) {
      s.value = (uint16_t) value;
      s.ms = now_ms == 0 ? 1 : now_ms;
    }
    return now_ms - s.ms > kFreshMs ? score - 30 : score;
  }

  static constexpr int kRun = 6;

  struct Seen {
    uint32_t ms{0};
    uint16_t value{0};
  };

  int64_t event_{0};
  bool started_{false}, whole_{false}, final_{false};
  uint8_t leader_{0}, big_side_{0};
  uint32_t back_ms_{0};
  int period_{0}, secs_{0};
  int t_{0}, o_{0}, at_{0}, left_{-1};
  int max_t_{0}, max_o_{0};  // the biggest lead seen
  int hi_t_{0}, hi_o_{0};    // the biggest that could have been between reads
  int last_t_{-1}, last_o_{-1};
  int big_at_{-1}, flip_at_{-1}, over_at_{-1};
  char over_name_[22]{};
  Mark marks_[kMarks]{};
  int n_{0};
  Seen seen_[18];
  StoryLine lines_[kShow];
  int n_lines_{0};
  uint32_t news_{0}, news_key_{0};
};

// The stories of the last few basketball games the panel read, so a game
// that leaves the board and comes back keeps its own. In PSRAM, from the
// first basketball read on.
class StoryBook {
 public:
  static constexpr int kGames = 4;

  void read(const GameSnapshot &g, uint32_t now_ms) {
    if (!g.valid || g.sport != Sport::BASKETBALL || g.state != GameState::IN || g.event_id.empty())
      return;
    if (this->slots_ == nullptr) {
      this->slots_ = static_cast<Slot *>(psram_alloc(sizeof(Slot) * kGames));
      for (int i = 0; i < kGames; i++)
        new (&this->slots_[i]) Slot();
    }
    const int64_t id = strtoll(g.event_id.c_str(), nullptr, 10);
    Slot *to = nullptr;
    for (int i = 0; i < kGames; i++) {
      if (this->slots_[i].event == id) {
        to = &this->slots_[i];
        break;
      }
    }
    if (to == nullptr) {
      to = &this->slots_[0];
      for (int i = 1; i < kGames; i++)
        if (this->slots_[i].used < to->used)
          to = &this->slots_[i];
      *to = Slot();
      to->event = id;
    }
    to->used = ++this->turn_;
    to->story.read(g, now_ms);
  }

  // nullptr when the panel has read nothing of the game.
  const Story *find(const GameSnapshot &g) const {
    if (this->slots_ == nullptr || g.sport != Sport::BASKETBALL)
      return nullptr;
    const int64_t id = strtoll(g.event_id.c_str(), nullptr, 10);
    for (int i = 0; i < kGames; i++)
      if (this->slots_[i].used != 0 && this->slots_[i].event == id)
        return &this->slots_[i].story;
    return nullptr;
  }

 private:
  struct Slot {
    int64_t event{0};
    uint32_t used{0};
    Story story;
  };
  Slot *slots_{nullptr};
  uint32_t turn_{0};
};

}  // namespace espn
