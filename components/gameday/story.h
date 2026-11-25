// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
#pragma once

// What is happening in a basketball game, for the two rows under the clock on
// a wide board (story_rows.h): a run, a drought, a lead change, the biggest
// lead, and the detail behind it. It is worked out from the score, the clock
// and the box score of the reads the panel already does; nothing more is read
// from ESPN.
//
// A line shows only when the reads prove it. Two things they can't see:
// - What came before the first read. Until the panel has watched from the
//   start (or one side still has 0), nothing about the whole game is said.
// - The order of the baskets between two reads. A count that depends on it
//   (lead changes, times tied) isn't kept, and "has never led" allows for
//   the worst order.
//
// ESPN's box score can trail its score by a read or two (11/24: after about a
// third of score changes, for about 10 s). A shooting detail ("7 of 13
// shooting") is the box score now less the box score when the stretch began,
// so it is said only while both are known to be in step with the score:
// - The points the box adds up to are the score. Missed shots don't show in
//   the score, so a shot ESPN's last play tells of has to be in the box too.
//   Both sides' at once: a side that hasn't scored lately agrees with any
//   old copy of its box.
// - Nothing was missed between the basket the stretch began with and the
//   read that saw it.
// Until then the detail is one the score alone proves ("Outscored 9-0", "Was
// down 6, now up 4"), or who scored, when every basket was named. A basket is
// a play's only when it was the whole change in the score, and the play
// hadn't been counted for another.
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
#include "play_pair.h"
#include "psram.h"

namespace espn {

// Seconds left on the period's clock, "7:38" or "45.3"; -1 when it can't be
// read.
inline int clock_left(const std::string &clock) {
  const char *c = clock.c_str();
  if (!isdigit((unsigned char) c[0]))
    return -1;
  int left = std::min(1201, atoi(c));
  const char *colon = strchr(c, ':');
  if (colon != nullptr)
    left = left * 60 + std::min(1201, std::max(0, atoi(colon + 1)));
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

enum class StoryKind : uint8_t {
  NONE,
  RUN,
  RUN_OVER,
  DROUGHT,
  TAKES_LEAD,
  BIG_LEAD,
  CLOSE,
  COMEBACK,
  NEVER_LED,
  HOT_HAND,
  PERIOD
};

struct StoryLine {
  StoryKind kind{StoryKind::NONE};
  uint8_t score{0};   // how much it deserves the row, 0 to 100
  uint16_t value{0};  // what it counts; a change makes it news again
  uint8_t side{0};    // whose it is: 1 ours, 2 theirs
  char text[40]{};    // row 3 after the team's name
  char detail[56]{};  // row 4; "" for none, and the team's leader goes there
};

// One side's shots so far, from the box score.
struct Shots {
  uint8_t fm{0}, fa{0}, tm{0}, ta{0}, lm{0}, la{0};  // field goals, threes, free throws: made, tried
  int points() const { return 2 * this->fm + this->tm + this->lm; }
  int tried() const { return this->fa + this->la; }
  int missed() const { return this->fa - this->fm + this->la - this->lm; }
};

class Story {
 public:
  static constexpr int kMarks = 24;
  static constexpr int kShow = 2;          // the lines the row takes
  static constexpr uint8_t kFloor = 30;    // below this a line isn't shown
  static constexpr uint8_t kLead = 70;     // from here it goes before the last basket
  static constexpr uint32_t kFreshMs = 60 * 1000;
  static constexpr uint32_t kPlayMs = 45 * 1000;  // how long a play stays up

  // One read of the board game. A redraw of the same read changes nothing.
  void read(const GameSnapshot &g, uint32_t now_ms) {
    if (!g.valid || g.sport != Sport::BASKETBALL || g.state != GameState::IN)
      return;
    const int64_t id = strtoll(g.event_id.c_str(), nullptr, 10);
    if (id != this->event_) {
      this->reset_();
      this->event_ = id;
    }
    const int left = clock_left(g.display_clock);
    if (left < 0 || g.period < 1 || g.period > 99)
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
      if (t - this->t_ + o - this->o_ >= kGapPoints || played > kGapSecs || g.period - 1 > this->period_) {
        // The panel was away (another game on the board, no Wi-Fi).
        this->begin_(g, left);
      } else {
        const bool next = g.period != this->period_;
        if (next)
          this->period_starts_(g, left);
        this->period_ = g.period;
        this->secs_ = left;
        this->step_(g, t, o, this->at_ + played);
      }
    }
    this->late_name_(g);
    this->box_(g, now_ms);
    this->hold_play_(g, now_ms);
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
  // Out of reach: 20 points or more with 8 minutes or less to play.
  bool decided(const GameSnapshot &g) const { return this->is_(g) && this->decided_(); }
  // The last play worth showing (play_pair.h), "three" over "Baker Jr. from 25
  // feet". nullptr for none, once it is kPlayMs old, or when a basket that
  // had no play of its own came after it.
  const StoryLine *play(const GameSnapshot &g) const {
    return this->is_(g) && this->play_.side != 0 ? &this->play_ : nullptr;
  }
  uint32_t play_news() const { return this->play_news_; }
  // The most a side has led by, 0 when the reads can't tell.
  int led_by(int side) const {
    if (!this->whole_)
      return 0;
    const int max = side == 1 ? this->max_t_ : this->max_o_, could = side == 1 ? this->hi_t_ : this->hi_o_;
    return max >= could ? max : 0;
  }
  // The other side has never led: hi_ allows for the worst order.
  bool never_trailed(int side) const { return this->whole_ && (side == 1 ? this->hi_o_ : this->hi_t_) <= 0; }
  // The box score and the score agree, as far as the reads tell.
  bool in_step(const GameSnapshot &g, int side) const { return this->is_(g) && this->now_ok_[side - 1]; }

 private:
  static constexpr int kGapPoints = 9;
  static constexpr int kGapSecs = 90;
  static constexpr uint32_t kBackMs = 90 * 1000;
  // Where the game clock starts counting, so one set back stays above 0.
  static constexpr int kStart = 2000;

  struct Mark {
    uint16_t at;  // seconds played
    uint16_t t, o;
    Shots shots[2];  // ours, theirs, when the score became this
    uint8_t known;   // bit 0 ours, bit 1 theirs: shots is certain
    uint8_t side;    // whose basket it was, 0 when both scored or it isn't known
    uint32_t who;    // the player's name, hashed; 0 for nobody named
    char name[18];
  };

  bool is_(const GameSnapshot &g) const {
    return this->started_ && g.valid && strtoll(g.event_id.c_str(), nullptr, 10) == this->event_;
  }

  // In place: `*this = Story()` puts 1.3 KB on the loop task's stack.
  void reset_() { new (this) Story(); }

  // Starts the game's story over from this read.
  void begin_(const GameSnapshot &g, int left) {
    const int64_t id = this->event_;
    const uint32_t news = this->news_, play_news = this->play_news_;
    this->reset_();
    const int t = g.team_score, o = g.opp_score;
    this->reg_ = g.nba.regulation;
    this->event_ = id;
    this->news_ = news;
    this->play_news_ = play_news;
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
    this->big_score_[0] = this->big_score_[1] = {(uint16_t) t, (uint16_t) o};
    const int tip = g.period == 1 ? kStart - std::max(0, period_length(g, left) - left) : kStart;
    this->last_t_ = t == 0 ? tip : -1;
    this->last_o_ = o == 0 ? tip : -1;
    this->leader_ = t > o ? 1 : o > t ? 2 : 0;
    this->trail_[0] = std::max(0, o - t);
    this->trail_[1] = std::max(0, t - o);
    this->used_key_ = play_key_(g);  // its points are in this score
    this->last_opp_[0] = t == 0 ? 0 : -1;
    this->last_opp_[1] = o == 0 ? 0 : -1;
    this->start_t_ = this->start_o_ = 0;
    this->start_ok_ = this->whole_ && g.period == 1;
    this->marks_[0] = {(uint16_t) kStart, (uint16_t) t, (uint16_t) o, {}, 0, 0, 0, {}};
    this->n_ = 1;
    // A side with no points has only missed, and all of it since the tip.
    for (int i = 0; i < 2; i++)
      this->dry_ok_[i] = (i == 0 ? t : o) == 0 && g.period == 1;
    if (t == 0 && o == 0 && g.period == 1)
      this->marks_[0].known = 3;
  }

  // What tells one play from the next. ESPN's id; its words when it has none.
  static int64_t play_key_(const GameSnapshot &g) {
    if (g.play_id != 0)
      return g.play_id;
    uint32_t h = 2166136261u;
    for (char c : g.last_play)
      h = (h ^ (uint8_t) c) * 16777619u;
    return g.last_play.empty() ? 0 : (int64_t) h + 1;
  }

  // A period began: the score and the box it began with. Known when the read
  // at the end of the last one had 0:00, or this one has the full clock.
  void period_starts_(const GameSnapshot &g, int left) {
    this->start_ok_ = this->secs_ == 0 || left == period_length(g, left);
    this->start_t_ = this->secs_ == 0 ? this->t_ : g.team_score;
    this->start_o_ = this->secs_ == 0 ? this->o_ : g.opp_score;
    if (this->secs_ == 0 && left == period_length(g, left) && (this->t_ != g.team_score || this->o_ != g.opp_score))
      this->start_ok_ = false;  // a basket the two reads disagree on
    this->start_box_ok_ = this->start_ok_ && this->end_ok_ && this->end_t_ == this->start_t_ && this->end_o_ == this->start_o_;
    for (int i = 0; i < 2; i++)
      this->start_box_[i] = this->end_box_[i];
    this->end_ok_ = false;
  }

  bool decided_() const {
    return this->final_ && this->left_ >= 0 && this->left_ <= 480 && abs(this->t_ - this->o_) >= 20;
  }

  // Keeps the last play worth showing through the plays that aren't.
  void hold_play_(const GameSnapshot &g, uint32_t now_ms) {
    const int64_t key = play_key_(g);
    const int total = g.team_score + g.opp_score, before = this->held_before_;
    this->held_before_ = total;
    if (key != this->held_key_) {
      // ESPN's play can come a read before its points.
      if (this->held_owed_ > 0 && total == this->held_total_ + this->held_owed_) {
        this->held_total_ = total;
        this->held_owed_ = 0;
      }
      if (total != this->held_total_ || now_ms - this->held_ms_ > kPlayMs)
        this->play_ = StoryLine();
    }
    // A basket ESPN has no player for yet waits: the words come a read later.
    PlayPair p;
    if (key == 0 || !play_pair(g, p) || p.detail[0] == '\0') {
      if (key == this->held_key_)
        this->held_total_ = total;
      return;
    }
    if (key != this->held_key_) {
      this->held_key_ = key;
      this->held_ms_ = now_ms;
      this->held_owed_ = p.basket && total == before ? g.play_value : 0;
      this->play_news_++;
    } else if (now_ms - this->held_ms_ > kPlayMs) {
      this->play_ = StoryLine();
      return;
    }
    if (total != before)
      this->held_owed_ = 0;
    this->held_total_ = total;
    this->play_.side = p.side;
    snprintf(this->play_.text, sizeof(this->play_.text), "%s", p.what);
    snprintf(this->play_.detail, sizeof(this->play_.detail), "%s", p.detail);
  }

  // The box score of this read: whether it is in step, and the shots the
  // newest mark began with.
  void box_(const GameSnapshot &g, uint32_t now_ms) {
    const Basketball::Box *box[2] = {&g.nba.team_box, &g.nba.opp_box};
    const int score[2] = {g.team_score, g.opp_score};
    Shots now[2];
    for (int i = 0; i < 2; i++)
      now[i] = {box[i]->fg_made, box[i]->fg_tried, box[i]->three_made, box[i]->three_tried, box[i]->ft_made,
                box[i]->ft_tried};
    const int64_t key = play_key_(g);
    if (key != this->play_key_seen_) {
      bool shot = false;
      const int by = play_shooter(g, shot);
      // A new shot has to be in the box; a rebound or a block too, unless
      // the play before was its shot.
      if (by != 0 && (shot || by != this->shooter_) && this->play_key_seen_ != 0 &&
          now[by - 1].tried() <= this->tried_[by - 1]) {
        this->owed_[by - 1] = this->tried_[by - 1] + 1;
        this->owed_ms_[by - 1] = now_ms;
      }
      this->shooter_ = by;
      this->play_key_seen_ = key;
    }
    Mark &m = this->marks_[this->n_ - 1];
    for (int i = 0; i < 2; i++) {
      if (now[i].tried() >= this->owed_[i] || now_ms - this->owed_ms_[i] > kOwedMs)
        this->owed_[i] = 0;
      this->tried_[i] = now[i].tried();
      const bool none = !box[i]->has && score[i] == 0 && this->whole_;  // no list before the first shot
      this->now_ok_[i] = none || (box[i]->has_ft && now[i].points() == score[i] && this->owed_[i] == 0);
    }
    // Both sides': a side that hasn't scored lately agrees with any old copy.
    if (!this->now_ok_[0] || !this->now_ok_[1] || m.t != score[0] || m.o != score[1]) {
      this->now_ok_[0] = this->now_ok_[1] = false;
      return;
    }
    for (int i = 0; i < 2; i++) {
      this->now_[i] = now[i];
      const bool clean = this->pre_ok_[i] && now[i].missed() == this->pre_[i].missed();
      if ((m.known >> i & 1) == 0 && clean) {
        m.shots[i] = now[i];
        m.known |= 1 << i;
      }
      // A drought begins with the side's own last basket.
      const int scored = i == 0 ? this->last_t_ : this->last_o_;
      if (!this->dry_ok_[i] && scored == m.at && clean) {
        this->dry_[i] = now[i];
        this->dry_ok_[i] = true;
      }
      this->good_[i] = now[i];
      this->good_ok_[i] = true;
    }
    // What the next period begins with, and this one began with.
    if (this->secs_ == 0) {
      this->end_ok_ = true;
      this->end_t_ = score[0];
      this->end_o_ = score[1];
      for (int i = 0; i < 2; i++)
        this->end_box_[i] = now[i];
    } else if (!this->start_box_ok_ && this->start_ok_ && this->secs_ == period_length(g, this->secs_) &&
               score[0] == this->start_t_ && score[1] == this->start_o_) {
      this->start_box_ok_ = true;
      for (int i = 0; i < 2; i++)
        this->start_box_[i] = now[i];
    }
  }

  // "7 of 13 shooting" for the run since mark `from`, "" when the reads can't
  // prove it.
  void run_detail_(int side, int from, int points, char (&out)[56]) const {
    out[0] = '\0';
    const int i = side - 1;
    const Mark &m = this->marks_[from];
    if ((m.known >> i & 1) == 0 || !this->now_ok_[i])
      return;
    const Shots &n = this->now_[i], &w = m.shots[i];
    if (n.points() - w.points() != points || n.fa < w.fa || n.la < w.la || n.fm < w.fm)
      return;
    if (n.fa > w.fa)
      snprintf(out, sizeof(out), "%d of %d shooting", n.fm - w.fm, n.fa - w.fa);
    else if (n.la > w.la)
      snprintf(out, sizeof(out), "%d of %d free throws", n.lm - w.lm, n.la - w.la);
  }

  // Who scored a run's points, "Langsy 8 of the 10", when every basket of
  // the side's since mark `from` was one player's and named.
  void share_(int side, int from, int points, char (&out)[56]) const {
    out[0] = '\0';
    uint32_t ids[8]{};
    int sum[8]{}, who_mark[8]{}, n = 0, seen = 0;
    for (int i = from + 1; i < this->n_; i++) {
      const Mark &m = this->marks_[i], &was = this->marks_[i - 1];
      if (m.side == 3 - side)
        continue;
      if (m.side != side || m.who == 0)
        return;
      const int got = side == 1 ? m.t - was.t : m.o - was.o;
      int k = 0;
      while (k < n && ids[k] != m.who)
        k++;
      if (k == n) {
        if (n == 8)
          return;
        ids[n++] = m.who;
        who_mark[k] = i;
      }
      sum[k] += got;
      seen += got;
    }
    if (seen != points || n == 0)
      return;
    int best = 0;
    for (int k = 1; k < n; k++)
      if (sum[k] > sum[best])
        best = k;
    if (n == 1)
      fit_row(out, std::string("All ") + std::to_string(points) + " by " + this->marks_[who_mark[0]].name);
    else if (sum[best] * 2 >= points)
      fit_row(out, std::string(this->marks_[who_mark[best]].name) + " " + std::to_string(sum[best]) + " of the " +
                       std::to_string(points));
    else if (n >= 3)
      snprintf(out, sizeof(out), "%d different scorers", n);
  }

  // What the run did to the margin, "Was down 6, now up 4".
  void swing_(int side, int from, char (&out)[56]) const {
    const Mark &m = this->marks_[from];
    const int was = side == 1 ? m.t - m.o : m.o - m.t, now = side == 1 ? this->t_ - this->o_ : this->o_ - this->t_;
    char a[16], b[16];
    if (was == 0)
      snprintf(a, sizeof(a), "Was tied");
    else if (was < 0)
      snprintf(a, sizeof(a), "Was down %d", -was);
    else
      snprintf(a, sizeof(a), "Lead was %d", was);
    if (now == 0)
      snprintf(b, sizeof(b), "now tied");
    else
      snprintf(b, sizeof(b), was > 0 ? "now %d" : now > 0 ? "now up %d" : "now down %d", abs(now));
    snprintf(out, sizeof(out), "%s, %s", a, b);
  }

  // What the score says of a drought with no shots to count: how the other
  // side did, or the last basket. Never a team's leader, which would read
  // against it.
  void dry_score_(int side, char (&out)[56]) const {
    const int i = side - 1, now = side == 1 ? this->o_ : this->t_;
    out[0] = '\0';
    if (this->last_opp_[i] >= 0 && now > this->last_opp_[i])
      snprintf(out, sizeof(out), "Outscored %d-0", now - this->last_opp_[i]);
    else if (this->last_what_[i][0] != '\0')
      snprintf(out, sizeof(out), "%s", this->last_what_[i]);
    else if (this->last_opp_[i] >= 0 && now == this->last_opp_[i])
      snprintf(out, sizeof(out), "Neither team has scored");
    else if (this->last_period_[i] == this->period_)
      snprintf(out, sizeof(out), "None since %d:%02d", this->last_secs_[i] / 60, this->last_secs_[i] % 60);
    else if (this->last_period_[i] > this->reg_)
      snprintf(out, sizeof(out), "None since overtime");
    else if (this->last_period_[i] > 0 && this->reg_ == 2)
      snprintf(out, sizeof(out), "None since the 1st half");
    else if (this->last_period_[i] > 0)
      snprintf(out, sizeof(out), "None since Q%d", this->last_period_[i]);
  }

  // "0 of 6, 0 of 5 on 3s" since the side last scored.
  void dry_detail_(int side, char (&out)[56]) const {
    out[0] = '\0';
    const int i = side - 1;
    if (!this->dry_ok_[i] || !this->now_ok_[i])
      return;
    const Shots &n = this->now_[i], &w = this->dry_[i];
    if (n.points() != w.points() || n.fm != w.fm || n.fa < w.fa || n.ta < w.ta)
      return;
    const int shots = n.fa - w.fa, threes = n.ta - w.ta;
    if (shots > 0 && threes == shots)
      snprintf(out, sizeof(out), "0 of %d on 3s", threes);
    else if (shots > 0 && threes > 0)
      snprintf(out, sizeof(out), "0 of %d, 0 of %d on 3s", shots, threes);
    else if (shots > 0)
      snprintf(out, sizeof(out), "0 of %d shooting", shots);
    else if (n.la > w.la)
      snprintf(out, sizeof(out), "0 of %d free throws", n.la - w.la);
  }

  void step_(const GameSnapshot &g, int t, int o, int at) {
    const int dt = t - this->t_, dp = o - this->o_, was_at = this->at_;
    this->at_ = at;
    if (dt == 0 && dp == 0)
      return;
    const int margin = this->t_ - this->o_, now_margin = t - o;
    int ended_run = 0;
    if (dt > 0 && dp == 0)
      ended_run = this->unanswered_(2);
    else if (dp > 0 && dt == 0)
      ended_run = this->unanswered_(1);
    const bool first[2] = {this->hi_t_ <= 0, this->hi_o_ <= 0};  // hasn't led yet
    // The most either side could have led by between the two reads.
    this->hi_t_ = std::max(this->hi_t_, margin + dt);
    this->hi_o_ = std::max(this->hi_o_, dp - margin);
    if (now_margin > this->max_t_) {
      this->max_t_ = now_margin;
      this->big_at_ = at;
      this->big_side_ = 1;
      this->big_score_[0] = {(uint16_t) t, (uint16_t) o};
    } else if (-now_margin > this->max_o_) {
      this->max_o_ = -now_margin;
      this->big_at_ = at;
      this->big_side_ = 2;
      this->big_score_[1] = {(uint16_t) t, (uint16_t) o};
    }
    // How long each had gone without one, at least: to the read before.
    const int dry[2] = {this->last_t_ < 0 ? 0 : was_at - this->last_t_, this->last_o_ < 0 ? 0 : was_at - this->last_o_};
    if (dt > 0) {
      this->last_t_ = at;
      this->dry_ok_[0] = false;
      this->last_opp_[0] = dp == 0 ? o : -1;  // theirs in the same read could have come after
      this->last_what_[0][0] = '\0';
      this->last_period_[0] = g.period;
      this->last_secs_[0] = this->secs_;
    }
    if (dp > 0) {
      this->last_o_ = at;
      this->dry_ok_[1] = false;
      this->last_opp_[1] = dt == 0 ? t : -1;
      this->last_what_[1][0] = '\0';
      this->last_period_[1] = g.period;
      this->last_secs_[1] = this->secs_;
    }
    // Who it was, only when that one basket is the whole change: with two
    // between reads, the last play isn't the one that ended the run. A play
    // that went with another change doesn't count again.
    PlayPair play;
    const int64_t key = play_key_(g);
    const bool fresh = key != 0 && key != this->used_key_;
    this->used_key_ = key;
    const bool one = fresh && g.play_value == dt + dp && (dt == 0 || dp == 0) && play_pair(g, play) && play.basket &&
                     play.side == (dt > 0 ? 1 : 2);
    if (one)
      fit_row(this->last_what_[dt > 0 ? 0 : 1], "Last: " + play.who_what(),
              play.who[0] != '\0' ? std::string("Last: ") + play.who : "");
    if (ended_run >= kRun) {
      this->over_at_ = at;
      this->over_side_ = dt > 0 ? 1 : 2;
      this->over_run_ = ended_run;
      this->over_dry_ = dry[dt > 0 ? 0 : 1];
      snprintf(this->over_who_, sizeof(this->over_who_), "%s", one ? play.who : "");
      snprintf(this->over_what_, sizeof(this->over_what_), "%s", one ? play.what : "");
    }
    // How far behind a side has been since it last led.
    if (now_margin != 0)
      this->trail_[now_margin > 0 ? 1 : 0] = std::max(this->trail_[now_margin > 0 ? 1 : 0], abs(now_margin));
    const uint8_t leader = now_margin > 0 ? 1 : now_margin < 0 ? 2 : this->leader_;
    if (leader != this->leader_ && this->leader_ != 0) {
      this->flip_at_ = at;
      this->flip_first_ = this->whole_ && first[leader - 1];
      this->flip_down_ = this->trail_[leader - 1];
      this->trail_[leader - 1] = 0;
      this->flip_what_[0] = '\0';
      if (one)
        fit_row(this->flip_what_, play.who_what(), play.who);
    }
    this->leader_ = leader;
    // Since when the side in front has been in front: the first read it was.
    // Not known where the panel came in late, or a lead change between two
    // reads can't be ruled out.
    if (now_margin == 0) {
      this->ahead_at_ = -1;
      this->tie_score_ = {(uint16_t) t, (uint16_t) o};
      this->tie_ok_ = true;
    } else {
      const bool same = margin != 0 && (margin > 0) == (now_margin > 0);
      const int low = abs(margin) - (now_margin > 0 ? dp : dt);
      if (!same) {
        this->ahead_at_ = at;
        this->ahead_period_ = g.period;
        this->ahead_secs_ = this->secs_;
        this->ahead_flip_ = this->flip_at_ == at;
      } else if (low <= 0) {
        this->ahead_at_ = -1;
      }
    }
    this->t_ = t;
    this->o_ = o;
    if (this->n_ == kMarks) {
      memmove(this->marks_, this->marks_ + 1, sizeof(Mark) * (kMarks - 1));
      this->n_--;
    }
    this->marks_[this->n_++] = {(uint16_t) at, (uint16_t) t, (uint16_t) o, {}, 0, 0, 0, {}};
    for (int i = 0; i < 2; i++) {
      this->pre_[i] = this->good_[i];
      this->pre_ok_[i] = this->good_ok_[i];
    }
    // The player of the basket, when there is just the one. ESPN can send
    // the play before it has the name: the next reads of it fill it in.
    Mark &m = this->marks_[this->n_ - 1];
    m.side = dt > 0 && dp > 0 ? 0 : dt > 0 ? 1 : 2;
    this->unnamed_key_ = 0;
    if (one && play.who[0] != '\0')
      name_mark_(m, play);
    else if (one)
      this->unnamed_key_ = key;
  }

  static void name_mark_(Mark &m, const PlayPair &play) {
    m.who = play.who_id == 0 ? 1 : play.who_id;
    snprintf(m.name, sizeof(m.name), "%s", play.who);
  }

  // The newest basket's player, once ESPN has the name.
  void late_name_(const GameSnapshot &g) {
    if (this->unnamed_key_ == 0 || play_key_(g) != this->unnamed_key_)
      return;
    PlayPair play;
    if (!play_pair(g, play) || play.who[0] == '\0' || !play.basket)
      return;
    Mark &m = this->marks_[this->n_ - 1];
    if (m.side != 0 && m.who == 0)
      name_mark_(m, play);
    // Its name on the lines that were waiting for it.
    if (m.side != 0)
      fit_row(this->last_what_[m.side - 1], "Last: " + play.who_what(), std::string("Last: ") + play.who);
    if (this->over_at_ == m.at && this->over_side_ == m.side) {
      snprintf(this->over_who_, sizeof(this->over_who_), "%s", play.who);
      snprintf(this->over_what_, sizeof(this->over_what_), "%s", play.what);
    }
    if (this->flip_at_ == m.at)
      fit_row(this->flip_what_, play.who_what(), play.who);
    this->unnamed_key_ = 0;
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
  int run_(int side, int &b, int &from) const {
    b = 0;
    from = 0;
    const Mark &now = this->marks_[this->n_ - 1];
    const Mark &was = this->marks_[std::max(0, this->n_ - 2)];
    const int last = side == 1 ? now.t - was.t : now.o - was.o;
    if (this->n_ < 2 || last <= 0 || (side == 1 ? now.o != was.o : now.t != was.t))
      return 0;  // a run is live while its side has the last basket
    int best = 0;
    for (int i = this->n_ - 2; i >= 0; i--) {
      const Mark &m = this->marks_[i];
      if (now.at - m.at > 600)
        break;
      const int a = side == 1 ? now.t - m.t : now.o - m.o;
      const int x = side == 1 ? now.o - m.o : now.t - m.t;
      const bool run = x == 0 ? a >= kRun : x <= 2 ? a >= 10 : x <= 4 ? a >= 14 : false;
      if (run && a - 2 * x > best - 2 * b) {
        best = a;
        b = x;
        from = i;
      }
    }
    return best;
  }

  // "21-12 since then": both sides' points since a score the panel read.
  struct Pair {
    uint16_t t, o;
  };
  void since_(int side, const Pair &from, const char *what, char (&out)[56]) const {
    const int us = this->t_ - from.t, them = this->o_ - from.o;
    snprintf(out, sizeof(out), "%d-%d %s", side == 1 ? us : them, side == 1 ? them : us, what);
  }

  // A player with most of the side's last points, "Langsy has 8" over "of
  // their last 10 points": every one of them named. 0 for none.
  int hot_(int side, char (&text)[40], char (&more)[56]) const {
    uint32_t ids[8]{};
    int sum[8]{}, at[8]{}, n = 0, total = 0, best_mark = -1, best = 0, best_of = 0, last = -1;
    for (int i = this->n_ - 1; i >= 1; i--) {
      const Mark &m = this->marks_[i], &was = this->marks_[i - 1];
      if (m.side == 3 - side)
        continue;
      if (m.side != side || m.who == 0)
        break;
      if (last < 0) {
        last = m.at;
        if (this->at_ - last > 120)
          return 0;
      }
      total += side == 1 ? m.t - was.t : m.o - was.o;
      int k = 0;
      while (k < n && ids[k] != m.who)
        k++;
      if (k == n) {
        if (n == 8)
          break;
        ids[n++] = m.who;
        at[k] = i;
      }
      sum[k] += side == 1 ? m.t - was.t : m.o - was.o;
      if (total > 14)
        break;
      if (sum[k] >= 8 && sum[k] * 10 >= total * 7 && sum[k] >= best) {
        best = sum[k];
        best_of = total;
        best_mark = at[k];
      }
    }
    if (best_mark < 0)
      return 0;
    snprintf(text, sizeof(text), "%s has %d", this->marks_[best_mark].name, best);
    snprintf(more, sizeof(more), "of their last %d points", best_of);
    return best * 32 + best_of;
  }

  // The period's score so far, "24-11 this half" over "10 of 14 shooting":
  // from the second period on, two minutes in, when a side is 8 up in it.
  bool period_line_(int side, const GameSnapshot &g, char (&text)[40], char (&more)[56]) const {
    if (!this->start_ok_ || this->period_ < 2 || this->period_ > g.nba.regulation)
      return false;
    const int us = (side == 1 ? this->t_ - this->start_t_ : this->o_ - this->start_o_);
    const int them = (side == 1 ? this->o_ - this->start_o_ : this->t_ - this->start_t_);
    if (us - them < 8 || period_length(g, this->secs_) - this->secs_ < 120)
      return false;
    snprintf(text, sizeof(text), "%d-%d this %s", us, them, g.nba.regulation == 2 ? "half" : "quarter");
    more[0] = '\0';
    const int i = side - 1, down = side == 1 ? this->start_o_ - this->start_t_ : this->start_t_ - this->start_o_;
    if (this->start_box_ok_ && this->now_ok_[i]) {
      const Shots &n = this->now_[i], &w = this->start_box_[i];
      if (n.points() - w.points() == us && n.fa > w.fa)
        snprintf(more, sizeof(more), "%d of %d shooting", n.fm - w.fm, n.fa - w.fa);
    }
    if (more[0] == '\0') {
      const char *when = g.nba.regulation == 2 ? "at halftime" : this->period_ == 2 ? "after Q1" : this->period_ == 3 ? "at halftime" : "after Q3";
      if (down == 0)
        snprintf(more, sizeof(more), "Tied %s", when);
      else
        snprintf(more, sizeof(more), "%s %d %s", down > 0 ? "Down" : "Up", abs(down), when);
    }
    return true;
  }

  void put_(StoryKind kind, int side, int score, int value, const char *text, const char *detail = "") {
    if (score < kFloor)
      return;
    // Keep the best kShow, best first.
    StoryLine s;
    s.kind = kind;
    s.score = (uint8_t) std::min(100, score);
    s.value = (uint16_t) value;
    s.side = (uint8_t) side;
    snprintf(s.text, sizeof(s.text), "%s", text);
    snprintf(s.detail, sizeof(s.detail), "%s", detail);
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
    char buf[40], more[56];
    const int margin = this->t_ - this->o_, lead = abs(margin);
    const bool late = this->final_ && this->left_ >= 0;
    // Out of reach late: only a bigger run or drought still says anything.
    const bool decided = this->decided_();

    int run_side = 0;
    for (int side = 1; side <= 2; side++) {
      int b = 0, from = 0;
      const int a = this->run_(side, b, from);
      const int since = this->at_ - this->marks_[this->n_ - 1].at;
      if (a > 0 && since <= 120 && !(decided && a < 10)) {
        run_side = side;
        // "a 12-2 run", "an 8-0 run"
        const bool an = a == 8 || a == 11 || a == 18;
        snprintf(buf, sizeof(buf), "on %s %d-%d run", an ? "an" : "a", a, b);
        this->run_detail_(side, from, a, more);
        if (more[0] == '\0')
          this->share_(side, from, a, more);
        if (more[0] == '\0')
          this->swing_(side, from, more);
        this->put_(StoryKind::RUN, side, this->aged_(StoryKind::RUN, side, a * 16 + b, 56 + 2 * (a - b), now_ms),
                   a * 16 + b, buf, more);
      }
      const int last = side == 1 ? this->last_t_ : this->last_o_;
      const int dry = last < 0 ? 0 : this->at_ - last;
      if (dry >= (decided ? 240 : 120)) {
        snprintf(buf, sizeof(buf), "scoreless %d:%02d", dry / 60, dry % 60);
        this->dry_detail_(side, more);
        if (more[0] == '\0')
          this->dry_score_(side, more);
        this->put_(StoryKind::DROUGHT, side, this->aged_(StoryKind::DROUGHT, side, dry / 60, 44 + dry / 8, now_ms),
                   dry / 60, buf, more);
      }
    }
    if (this->over_at_ >= 0 && this->at_ - this->over_at_ <= 30) {
      // "Williford ends the run" (or "ends run") over what it was, when that
      // fits beside the team's name; else the run's size over both.
      const std::string &abbr = this->over_side_ == 1 ? g.team_abbr : g.opp_abbr;
      std::string named = this->over_who_;
      if (!named.empty()) {
        const size_t used = abbr.size() + 1 + detail::glyph_count(named);
        named += used + 13 <= kRowChars ? " ends the run" : " ends run";
      }
      more[0] = '\0';
      if (!named.empty() && abbr.size() + 1 + detail::glyph_count(named) <= kRowChars && named.size() < sizeof(buf)) {
        snprintf(buf, sizeof(buf), "%s", named.c_str());
        snprintf(more, sizeof(more), "%s", this->over_what_);
        more[0] = (char) toupper((unsigned char) more[0]);
      } else {
        const int r = this->over_run_;
        snprintf(buf, sizeof(buf), "ends %s %d-0 run", r == 8 || r == 11 || r == 18 ? "an" : "a", r);
        if (!named.empty())
          fit_row(more, std::string(this->over_who_) + " " + this->over_what_, this->over_who_);
      }
      if (more[0] == '\0' && this->over_dry_ >= 60)
        snprintf(more, sizeof(more), "First in over %d minute%s", this->over_dry_ / 60, this->over_dry_ < 120 ? "" : "s");
      this->put_(StoryKind::RUN_OVER, this->over_side_, 72, this->over_at_, buf, more);
    }
    if (this->flip_at_ >= 0 && this->at_ - this->flip_at_ <= 45 && margin != 0) {
      const int side = margin > 0 ? 1 : 2;
      more[0] = '\0';
      if (this->leader_ == side && this->flip_what_[0] != '\0')
        snprintf(more, sizeof(more), "%s", this->flip_what_);
      else if (this->leader_ == side && this->flip_first_)
        snprintf(more, sizeof(more), "First lead of the game");
      else if (this->leader_ == side && this->flip_down_ > 0)
        snprintf(more, sizeof(more), "Were down %d", this->flip_down_);
      this->put_(StoryKind::TAKES_LEAD, side, late ? 95 : 74, this->flip_at_, "takes the lead", more);
    }
    // Not while the lead change itself is on the row.
    if (late && this->left_ > 0 && this->left_ <= 180 && lead >= 1 && lead <= 3 &&
        !(this->flip_at_ >= 0 && this->at_ - this->flip_at_ <= 45)) {
      const int side = margin > 0 ? 1 : 2;
      buf[0] = more[0] = '\0';
      if (this->never_trailed(side)) {
        snprintf(buf, sizeof(buf), "has never trailed");
        snprintf(more, sizeof(more), "Led by %d at most", side == 1 ? this->max_t_ : this->max_o_);
        if (this->led_by(side) == 0)
          more[0] = '\0';
      } else if (this->ahead_at_ >= 0) {
        // "ahead since 4:10", "ahead since Q3"; the row says by how many.
        if (this->ahead_period_ == this->period_)
          snprintf(buf, sizeof(buf), "ahead since %d:%02d", this->ahead_secs_ / 60, this->ahead_secs_ % 60);
        else if (this->ahead_period_ > g.nba.regulation)
          snprintf(buf, sizeof(buf), "ahead since OT");
        else if (g.nba.regulation == 2)
          snprintf(buf, sizeof(buf), "ahead since the 1st half");
        else
          snprintf(buf, sizeof(buf), "ahead since Q%d", this->ahead_period_);
        if (this->ahead_flip_ && this->flip_what_[0] != '\0')
          snprintf(more, sizeof(more), "%s", this->flip_what_);
        else if (this->ahead_flip_ && this->flip_down_ > 0)
          snprintf(more, sizeof(more), "Were down %d", this->flip_down_);
      }
      if (buf[0] != '\0')
        this->put_(StoryKind::CLOSE, side, 72, this->ahead_at_ < 0 ? 1 : this->ahead_at_ + 2, buf, more);
    }
    if (this->whole_) {
      // Only while it is the biggest whatever the order between reads.
      const int big = this->big_side_ == 1 ? this->max_t_ : this->max_o_;
      const int top = this->big_side_ == 1 ? this->hi_t_ : this->hi_o_;
      if (this->big_side_ != 0 && big >= 10 && big >= top && big >= this->max_t_ && big >= this->max_o_ &&
          lead == big && this->at_ - this->big_at_ <= 60) {
        snprintf(buf, sizeof(buf), "biggest lead, %d", big);
        more[0] = '\0';
        if (this->never_trailed(this->big_side_))
          snprintf(more, sizeof(more), "Has never trailed");
        else if (this->tie_ok_)
          this->since_(this->big_side_, this->tie_score_, "since it was tied", more);
        else if (this->flip_down_ > 0 && this->leader_ == this->big_side_)
          snprintf(more, sizeof(more), "Were down %d", this->flip_down_);
        this->put_(StoryKind::BIG_LEAD, this->big_side_,
                   this->aged_(StoryKind::BIG_LEAD, this->big_side_, big, 58, now_ms), big, buf, more);
      }
      for (int side = 1; side <= 2; side++) {
        const int down = side == 1 ? this->max_o_ : this->max_t_;
        const int now = side == 1 ? -margin : margin;
        if (down >= 12 && now <= down / 2 && now <= 8) {
          snprintf(buf, sizeof(buf), "was down %d", down);
          this->since_(side, this->big_score_[2 - side], "since then", more);
          this->put_(StoryKind::COMEBACK, side, this->aged_(StoryKind::COMEBACK, side, down, 64, now_ms), down, buf,
                     more);
        }
        const int hi = side == 1 ? this->hi_t_ : this->hi_o_;
        if (hi <= 0 && this->half_over_(g) && lead > 0) {
          more[0] = '\0';
          if (this->led_by(3 - side) > 0)
            snprintf(more, sizeof(more), "Down as many as %d", this->led_by(3 - side));
          this->put_(StoryKind::NEVER_LED, side, this->aged_(StoryKind::NEVER_LED, side, side, 46, now_ms), side,
                     "has never led", more);
        }
      }
    }
    for (int side = 1; side <= 2; side++) {
      const int hot = side == run_side ? 0 : this->hot_(side, buf, more);
      if (hot != 0)
        this->put_(StoryKind::HOT_HAND, side, this->aged_(StoryKind::HOT_HAND, side, hot, 50, now_ms), hot, buf, more);
      if (this->period_line_(side, g, buf, more))
        this->put_(StoryKind::PERIOD, side, 36, 0, buf, more);
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
  static constexpr uint32_t kOwedMs = 40 * 1000;

  struct Seen {
    uint32_t ms{0};
    uint16_t value{0};
  };

  int64_t event_{0};
  bool started_{false}, whole_{false}, final_{false};
  uint8_t leader_{0}, big_side_{0}, over_side_{0};
  uint32_t back_ms_{0};
  int period_{0}, secs_{0};
  int t_{0}, o_{0}, at_{0}, left_{-1};
  int max_t_{0}, max_o_{0};  // the biggest lead seen
  int hi_t_{0}, hi_o_{0};    // the biggest that could have been between reads
  int last_t_{-1}, last_o_{-1};
  int last_opp_[2]{-1, -1};      // theirs at the side's last basket, -1 when unsure
  int last_period_[2]{}, last_secs_[2]{};
  char last_what_[2][40]{};      // "Last: Williford layup"
  int over_dry_{0};
  int trail_[2]{}, flip_down_{0};
  bool flip_first_{false}, ahead_flip_{false}, tie_ok_{false};
  int ahead_at_{-1}, ahead_period_{0}, ahead_secs_{0};
  Pair big_score_[2]{}, tie_score_{};
  int reg_{4};
  int start_t_{0}, start_o_{0}, end_t_{0}, end_o_{0};
  bool start_ok_{false}, start_box_ok_{false}, end_ok_{false};
  Shots start_box_[2], end_box_[2];
  int64_t used_key_{0}, unnamed_key_{0};
  int big_at_{-1}, flip_at_{-1}, over_at_{-1};
  int over_run_{0};
  char over_who_[40]{}, over_what_[24]{};  // the basket that ended the last run
  char flip_what_[56]{};                   // the basket that took the lead
  // The box score: this read's, the last that was in step, the one that was
  // before the newest mark, and the one when each side last scored (box_).
  Shots now_[2], good_[2], pre_[2], dry_[2];
  bool now_ok_[2]{}, good_ok_[2]{}, pre_ok_[2]{}, dry_ok_[2]{};
  int tried_[2]{}, owed_[2]{};
  uint32_t owed_ms_[2]{};
  int shooter_{0};
  int64_t play_key_seen_{0};
  Mark marks_[kMarks]{};
  int n_{0};
  Seen seen_[22];
  StoryLine lines_[kShow];
  StoryLine play_;
  int64_t held_key_{0};
  uint32_t held_ms_{0}, play_news_{0};
  int held_total_{-1}, held_before_{-1}, held_owed_{0};
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
      new (to) Slot();
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
