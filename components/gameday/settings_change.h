// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
// A settings change and the reads around it: which read goes first after
// the change, which read in flight it stops, and which answers still count.
// The apps and the device page send a change as several quick posts, so the
// board must show the new pick within seconds whatever order and gap they
// land in. Pure logic, no ESPHome includes, so the host tests cover it.
#pragma once

#include <cstdint>

namespace espn {

// How long after a change the board's reads go ahead of the side reads
// (the season schedule, news, Top 25, the message list, the weather). A
// season read is up to 1.9 MB and 10 s, and it used to start right after a
// change, between the first favorite's read and the rest. The window keeps
// a board that keeps reading from holding them off for good.
constexpr uint32_t kBoardFirstMs = 30000;

// Whether the side reads wait: a change is recent and the board's next read
// is due now. changed_ms is when the change came, 0 for none.
inline bool board_first(uint32_t now, uint32_t changed_ms, bool board_due) {
  return changed_ms != 0 && board_due && now - changed_ms < kBoardFirstMs;
}

// Whether the game leaders read for the board's game (a summary of up to
// 600 KB, about 5 s) waits: a change is recent and a favorite on the list
// has never been read. It goes ahead of the mode's next read otherwise, and
// after three quick adds it held the third team (the live one) back 5 s.
inline bool leaders_wait(uint32_t now, uint32_t changed_ms, bool unread_favorite) {
  return changed_ms != 0 && unread_favorite && now - changed_ms < kBoardFirstMs;
}

// What the job in flight reads, as far as a change cares.
enum class JobKind : uint8_t {
  BOARD,     // a scan, a game, a team or a day for the board
  FAV_TEAM,  // one favorite's team and game read (Favorite teams)
  SEASON,    // a team's season schedule, for the schedule and recap cards
  SIDE,      // the rest: small, and the same whatever the board shows
};

// Whether a change stops the job in flight: its worker stops at its next
// request (the season read mid-body) and the main loop drops the answer.
// board_changed: the change cleared the board (a new mode, favorites,
// playlist or leagues), not only a take-over or a favorite Live games reads
// on its next scan. fav_still_listed: the job is a favorite's read and the
// team is still a favorite on Favorite teams, so its card stays and the read
// is the one the panel would make next anyway. The season read stops for any
// change, since the board's read goes first, and is read again afterwards.
inline bool job_moot_after_change(JobKind kind, bool board_changed, bool fav_still_listed) {
  switch (kind) {
    case JobKind::BOARD:
      return board_changed;
    case JobKind::FAV_TEAM:
      return board_changed && !fav_still_listed;
    case JobKind::SEASON:
      return true;
    case JobKind::SIDE:
      return false;
  }
  return false;
}

// Whether the answer to a favorite's read still counts after the board
// changed under it: the change didn't stop it, the panel is still on
// Favorite teams, and the team is still a favorite (it may have moved in the
// list). Three quick adds used to throw away the first team's read twice.
inline bool fav_answer_counts(bool generation_changed, bool moot, bool fav_mode, bool still_listed) {
  if (!generation_changed)
    return true;
  return !moot && fav_mode && still_listed;
}

// Whether a season read that ended without a schedule counts as a failure,
// which waits before the next try. One a change stopped is read again on the
// card's next turn, once the board has its game.
inline bool season_read_failed(bool ok, bool cut) { return !ok && !cut; }

// Favorites take over, after a job ends: when take-over was turned on, or
// the favorites changed, while the job ran (asks_at_start differs from
// asks_now), the scan it asked for runs now. The job's own answer set the
// next read for itself (a scan's kickoff rescan, up to three hours in a quiet
// night), which would have left a favorite that is live now off the board.
inline bool take_scan_now_after_job(uint32_t asks_at_start, uint32_t asks_now, bool take_over_on) {
  return take_over_on && asks_at_start != asks_now;
}

// sport=none in a post that also sets a mode other than Favorite teams
// (mode=5&sport=none): it stops the sport row without switching back to
// Favorite teams, which stopping a row on its own does. post_mode is the
// post's mode, -1 for none.
inline bool sport_stop_keeps_mode(int post_mode, uint8_t favorites_mode) {
  return post_mode >= 0 && post_mode != (int) favorites_mode;
}

}  // namespace espn
