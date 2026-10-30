// Pure ESPN parsing and game logic. No ESPHome includes so the host test
// binary can compile this file directly.
#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "psram.h"
#include "rankings.h"
#include "teams.h"

namespace espn {

enum class GameState : uint8_t { NOT_FOUND, PRE, IN, POST };

// What the team endpoint tells us about the next game.
struct Schedule {
  bool valid{false};
  uint8_t league{0};  // League as uint8_t; set for live-mode games
  std::string event_id;
  int64_t kickoff_epoch{0};
  bool time_tbd{false};  // no kickoff time yet: kickoff_epoch is ESPN's midnight Eastern placeholder
  uint32_t group{0};
  std::string team_color;
  std::string team_record;
  const char *comp_slug{nullptr};  // a kCups slug when the game is in that cup, nullptr for the league
  bool next_final{false};          // the team endpoint's next event is already over (ESPN lags)
  // The other side of event_id, so its logo can download early. 0 when the
  // team endpoint did not say.
  uint32_t opp_id{0};
  std::string opp_abbr;
};

enum class Half : uint8_t { NONE, TOP, MID, BOTTOM, END };

// Baseball's live state. Zero or -1 when the document has none (pre-game,
// finals, between half innings).
struct Baseball {
  static constexpr uint8_t FIRST = 1, SECOND = 2, THIRD = 4;
  int8_t balls{-1}, strikes{-1}, outs{-1};
  uint8_t bases{0};  // FIRST | SECOND | THIRD
  Half half{Half::NONE};
  int team_hits{0}, opp_hits{0}, team_errors{0}, opp_errors{0};
  std::string batter, pitcher;               // "A. Riley", while a half inning is on
  std::string team_probable, opp_probable;   // starting pitchers, pre-game
  std::string series;                        // "LAD lead series 2-1", postseason
  std::string play_type;                     // last play's type: "Home Run", "Ball"
};

// One goal from a soccer event's details. Names are plain ASCII (the panel
// fonts have no accents) with the initial dropped: "Saka", "Alan Patrick".
struct SoccerGoal {
  std::string clock;  // "52'", "45'+3'"
  std::string name;
  bool ours{false};  // counts for our side; an own goal counts for the side it helped
  bool own_goal{false}, penalty{false};
};

// Soccer's goals and cards (the event's details), form, shootout, match
// stats and three-way odds. Empty, zero or -1 when the document has none.
struct Soccer {
  std::string status;             // ESPN's status name: "STATUS_HALFTIME", "STATUS_FINAL_PEN"
  std::vector<SoccerGoal> goals;  // in match order
  int team_reds{0}, opp_reds{0}, team_yellows{0}, opp_yellows{0};
  std::string last_event;                      // newest goal or card: "85' Yellow Card: R. Garro (COR)"
  std::string team_form, opp_form;             // last five results, oldest first: "WWWWL"
  int team_shootout{-1}, opp_shootout{-1};     // penalty shootout goals
  std::string team_line, draw_line, opp_line;  // moneyline before kickoff: "-275", "+425", "+700"
  std::string team_possession, opp_possession;  // percent: "58.1"
  int team_shots{-1}, opp_shots{-1}, team_on_target{-1}, opp_on_target{-1};
  std::string note;  // "UEFA Champions League, League Phase"
};

// Where a basketball game is, from ESPN's status name.
enum class Phase : uint8_t { OTHER, PLAY, END_PERIOD, HALFTIME };

// Basketball (NBA, WNBA, men's college). The series fields are set only for
// a playoff game.
struct Basketball {
  uint8_t regulation{4};  // periods before overtime: 4 quarters, or 2 halves in college
  Phase phase{Phase::OTHER};
  std::string series;                  // ESPN's line: "ATL leads series 1-0"
  int8_t team_wins{-1}, opp_wins{-1};  // series wins so far, -1 outside a series
};

// Hockey's extras. ESPN's period 5 is the shootout in a regular-season or
// preseason game and 2OT in the playoffs.
struct Hockey {
  bool playoffs{false};  // season type 3
  std::string series;    // "CAR leads series 2-0", playoffs
};

// One scoreboard event, resolved to "us" and "them".
constexpr int16_t kNoWeatherF = INT16_MIN;

struct GameSnapshot {
  bool valid{false};
  GameState state{GameState::NOT_FOUND};
  std::string event_id;
  int64_t kickoff_epoch{0};
  bool time_tbd{false};  // no kickoff time yet (see Schedule::time_tbd)
  bool completed{false};
  std::string short_detail;
  std::string display_clock;
  int period{0};
  std::string team_abbr, opp_abbr;
  uint32_t team_id{0}, opp_id{0};
  int team_score{0}, opp_score{0};
  std::string team_record, opp_record;
  std::string team_color, opp_color;
  std::string team_logo, opp_logo;
  bool team_winner{false};
  int possession{0};
  int team_timeouts{0}, opp_timeouts{0};
  std::string last_play, down_distance;
  std::string short_down_distance;  // "3rd & 10"
  bool is_red_zone{false};
  // Football: how the drive went ("4 plays, 58 yards, 1:36"), only while the
  // last play is a touchdown or field goal, and our side's chance to win
  // after the last play (1 to 99), -1 when ESPN gives none.
  std::string drive;
  int8_t team_win_pct{-1};
  std::string odds, over_under, tv, venue;
  // The last play's details, for the scoring log (score_log.h): ESPN's play
  // type ("Rushing Touchdown", "Field Goal Good"), its points, its yards, the
  // side's team id and the first player named ("J. Grier").
  std::string play_type, play_athlete;
  int8_t play_value{0};
  int16_t play_yards{0};
  uint32_t play_team{0};
  // Each side's points by period, quarter or inning, for the line score
  // (line_score.h). A period past kLinesMax adds into the last one.
  static constexpr uint8_t kLinesMax = 16;
  uint8_t team_lines[kLinesMax]{}, opp_lines[kLinesMax]{};
  uint8_t team_line_count{0}, opp_line_count{0};
  std::string headline;  // ESPN's story line on a final, "" when it has none
  // Each side's season leader before the start, ready for the pre-game
  // ticker: "Prescott 1,381 yds 9 TD", "Machado 30 HR 91 RBI", "Allen 21.0
  // PPG", "McDavid 9 pts". "" when ESPN lists none, and once the game starts.
  std::string team_leader, opp_leader;
  // Before the start: the regular-season week (football, 0 when ESPN gives
  // none) and the forecast at an outdoor venue ("76F Cloudy", "" indoors or
  // when the event has none).
  uint8_t week{0};
  std::string weather;
  // The same forecast for the board (weather.h): its temperature in whole
  // degrees F (kNoWeatherF without one) and AccuWeather's icon number (0
  // for none). Before the start of a sport played outside, the venue's place
  // for a forecast of the panel's own when ESPN has none ("Evanston, IL"),
  // and whether it is indoors: 1, 0, or -1 when ESPN doesn't say.
  int16_t weather_f{kNoWeatherF};
  uint8_t weather_accu{0};
  int8_t venue_indoor{-1};
  std::string venue_place;
  // Other sports. Football leaves these at their defaults.
  Sport sport{Sport::FOOTBALL};
  bool team_home{false};
  Baseball mlb;
  Soccer soc;
  Basketball nba;  // every basketball league
  Hockey nhl;
  std::string game_name;  // "Rose Bowl", "Super Bowl LX"; empty for most games
};

// One future game from the team's schedule endpoint, for the "Up next" list.
struct Upcoming {
  std::string event_id;
  int64_t kickoff_epoch{0};
  bool time_tbd{false};  // no kickoff time yet (see Schedule::time_tbd)
  uint32_t opp_id{0};
  std::string opp_abbr, opp_name;
  bool home{false};     // we are the home side
  bool neutral{false};  // neutral site
  std::string tv;
};

// A game found by scanning a day's scoreboard (live-game modes).
struct LiveGame {
  uint8_t league{0};
  std::string event_id;
  uint32_t group{0};        // home team's conference, for the follow-up polls
  uint32_t away_id{0};      // shown on the left, like a broadcast
  uint32_t home_id{0};      // for its logo, fetched as soon as the game is picked
  std::string away_abbr, home_abbr;
  // Where the game stands, for the best-game pick (best_game.h).
  int16_t away_score{0}, home_score{0};
  uint8_t period{0};
  int32_t clock_s{-1};  // ESPN's status clock: left in the period, or soccer's elapsed
  uint8_t away_rank{99}, home_rank{99};  // AP rank, 99 when unranked
};

// A finished game from a day's scoreboard, for the finals rotation (recap.h).
// Kept small, since a few dozen of them stay in RAM: short strings only,
// and the logos are worked out from the ids when the card goes up.
struct Final {
  uint8_t league{0};
  uint8_t rank{99};  // the better AP rank of the two sides, 99 when neither is ranked
  int64_t kickoff_epoch{0};
  std::string detail;  // "Final", "Final/OT"
  uint32_t away_id{0}, home_id{0};
  int away_score{0}, home_score{0};
  std::string away_abbr, home_abbr;
  std::string away_color, home_color;
  std::string away_record, home_record;
};

// The scoreboard card for a final, the away side as "team" like a broadcast.
GameSnapshot final_snapshot(const Final &f);

// A game later today from a day's scoreboard, for the rotation's "Later
// today" cards (recap.h). As small as a Final: short strings and ids, the
// logos worked out from the ids when the card goes up.
struct Pending {
  uint8_t league{0};
  uint8_t rank{99};  // the better AP rank of the two sides, 99 when neither is ranked
  int64_t kickoff_epoch{0};
  std::string event_id;
  uint32_t away_id{0}, home_id{0};
  std::string away_abbr, home_abbr;
  std::string away_color, home_color;
  std::string away_record, home_record;
  std::string tv;  // the first network ESPN lists, "" when it lists none
};

// One scan keeps at most this many of a league's games later today: the
// most a loop of the rotation shows.
static const size_t kPendingKeep = 8;

// A scan's lists live in PSRAM on the panel. With no team picked it reads
// every league, and a college Saturday ends with 80 or more finals at about
// 200 bytes each: one 25 KB block, more than internal RAM has in one piece.
using LiveList = PsVector<LiveGame>;
using FinalList = PsVector<Final>;
using PendingList = PsVector<Pending>;

// Ranked games first (best rank on either side), then by kickoff.
inline bool pending_before(const Pending &a, const Pending &b) {
  if (a.rank != b.rank)
    return a.rank < b.rank;
  return a.kickoff_epoch < b.kickoff_epoch;
}

// The pregame scoreboard card for a game later today, away side as "team".
GameSnapshot pending_snapshot(const Pending &p);

struct TickerOptions {
  bool clock{true};
  bool down_distance{true};   // football
  bool baseball_count{true};  // count, outs, pitcher and batter
  bool soccer_stats{true};    // goals, cards, possession and shots
  bool last_play{true};
  bool odds{true};
  bool leaders{true};  // each side's season leader before the start
  // Both records before the start, for a board too narrow to show them
  // under the logos (one 64-wide panel, pages/gameday-live.yaml).
  bool records{false};
  // The forecast as the panel says it before the start ("12C Clear" for an
  // owner on Celsius, or the panel's own forecast when the event has none,
  // weather.h). "" says the event's own.
  std::string weather;
};

struct Splash {
  std::string text;
  uint32_t color{0};
};

const char *league_path(League league);  // the last part of the ESPN path: "nfl"
std::string team_option(const Team &t);  // "NFL: Dallas Cowboys", the Home Assistant option
std::string team_url(League league, uint32_t espn_id);
std::string schedule_url(League league, uint32_t espn_id);  // the season's games, ~200KB
// time_tbd: kickoff_epoch is a TBD game's midnight Eastern placeholder.
std::string scoreboard_url(League league, uint32_t group, int64_t kickoff_epoch, bool time_tbd = false);
// Every game of the day for one league. top25: men's college basketball
// reads only ESPN's Top 25 slate (the no-team panel), not all of Division I.
std::string scan_url(League league, int64_t now_epoch, bool top25 = false);
// How far behind UTC the day scan_url() reads turns over: 4 AM Eastern, for
// every league. ESPN lists a game under the Eastern date it started on, keeps
// it off the next date while it is on, and rejects date ranges. West Coast
// games run past midnight Eastern (a 10:30 PM college kickoff ends near
// 2 AM), and no league starts a game between midnight and 6 AM Eastern.
inline int64_t scan_day_shift(League) { return 9 * 3600; }
// One game on its own, 8 to 20 KB, where the day's scoreboard it sits in is
// up to 260 KB (NFL) or 750 KB (college). Every league polls this.
std::string event_url(League league, const std::string &event_id);
// One day's scoreboard for finding a team's next game (Eastern date of
// day_epoch). College football and men's college basketball read the team's
// conference only (130 KB for the SEC's Saturday, 835 KB for every FBS game).
std::string team_day_url(League league, uint32_t group, int64_t day_epoch);
// The same in a cup of the league (kCups): soccer/uefa.champions/... A null
// slug gives the league's own URL.
std::string team_url(League league, uint32_t espn_id, const char *comp_slug);
std::string event_url(League league, const std::string &event_id, const char *comp_slug);
std::string team_day_url(League league, uint32_t group, int64_t day_epoch, const char *comp_slug);
std::string dark_logo(const std::string &url);
std::string team_logo_url(League league, uint32_t espn_id, const char *abbr);  // for a team with no game loaded yet
// The news feed's latest headlines (news.h): the team's with an id, else the
// league's. 15 to 170 KB.
std::string news_url(League league, uint32_t team_id);

bool parse_team_str(const std::string &json, Schedule &out);
// Up to `max` games not yet started, in date order, from the schedule endpoint.
bool parse_upcoming_str(const std::string &json, uint32_t our_team_id, size_t max, std::vector<Upcoming> &out);
bool parse_scoreboard_str(const std::string &json, const std::string &event_id, uint32_t our_team_id,
                          GameSnapshot &out);
// The team's next game that is not over yet in a day's scoreboard
// (team_day_url), into out.event_id, out.kickoff_epoch and out.time_tbd.
bool parse_team_game_str(const std::string &json, uint32_t our_team_id, Schedule &out,
                         const std::string &skip_event = "");
// The single-event document from event_url(). With event_id, a document for
// some other game reads as a failure.
bool parse_event_str(const std::string &json, Sport sport, uint32_t our_team_id, GameSnapshot &out,
                     const std::string &event_id = "");
// parse_news into a std::string, for the host tests.
int parse_news_str(const std::string &json, std::string &list);

// neutral = nobody is "our" team (live-game modes): both sides splash with their abbreviation.
Splash decide_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral = false);
uint32_t parse_color(const std::string &hex);
std::string status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local);
// "KC 72% to win" for the side ESPN's model favors, "" when it gives no
// number (football's last play only).
std::string win_chance(const GameSnapshot &s);
// Whether ESPN has said the board has no game, for no_game_text. In Favorites
// (favs > 0) only once every favorite's team read has answered (`read` of
// them): one answered favorite says nothing of the others.
inline bool no_game_said(size_t favs, size_t read, bool team_said) { return favs > 0 ? read == favs : team_said; }
// How long after joining Wi-Fi a panel with no clock says "Getting ready".
// A time server answers in a second or two, and ESPN's Date header is read
// 5 s after joining; past this, neither has answered.
constexpr uint32_t kClockGraceMs = 30 * 1000;
// No clock kClockGraceMs after joining Wi-Fi: neither the time server nor
// ESPN answered, so the panel has no internet. since_join_ms is 0 off Wi-Fi.
inline bool clock_late(bool clock_set, uint32_t since_join_ms) {
  return !clock_set && since_join_ms >= kClockGraceMs;
}
// The board and ticker with no game on the board, in place of the page's
// "No upcoming game": "Getting ready" until the clock is set (nothing can
// be read from ESPN before it), "No internet" once that has taken
// kClockGraceMs since joining Wi-Fi, "Can't reach ESPN" while reads fail and
// ESPN has never said this team has no game. Empty for the default, and in
// a live mode, which has its own words. captive (captive.h: the Wi-Fi answered
// with a sign-in page and ESPN has not answered since) wins over all of it,
// live modes included, since nothing can be read until it ends.
std::string no_game_text(bool clock_set, bool live_mode, bool no_game_confirmed, uint32_t misses,
                         bool captive = false, uint32_t since_join_ms = 0);
// Bench only. Every ESPN URL starts with this root; a bench build points
// it at a host that does not exist to make ESPN unreachable without
// touching the network. Pass a string literal: only the pointer is kept,
// and the worker task reads it while the main loop may set it.
void set_site_root(const char *root);
// The same for the core API, which only the Top 25 card reads (rankings.h).
void set_core_root(const char *root);
// The league's AP poll, this week's.
std::string rankings_url(League league);
// Home Assistant's Game Status: the matchup, the state and the score, and
// nothing that changes every play, so the recorder logs a line when the game
// starts, scores or ends rather than every poll.
std::string ha_status_text(const GameSnapshot &s, const std::string &kickoff_local);
// ESPN's game headline ("College Football Playoff Quarterfinal at the Rose
// Bowl Presented by Prudential") cut down to what people call the game
// ("Rose Bowl"). ESPN has no short form of it. Empty for a note that is a
// kickoff window, not a name.
std::string short_game_name(const std::string &headline);
// The local calendar day of a broken-down time, as a number that changes at
// local midnight and goes up by one a day across New Year too. kickoff_label
// counts "Today" and "Tomorrow" in these, so a board drawn on an earlier one
// has a stale label. tm_year * 366 + tm_yday was off by one or two from
// Dec 31 to Jan 1, so a New Year's Day game seen on Dec 31 read "Fri" rather
// than "Tomorrow".
inline int64_t local_day(const struct tm &t) {
  const int64_t y = (int64_t) t.tm_year + 1899;  // the full year before this one
  return (int64_t) t.tm_year * 365 + y / 4 - y / 100 + y / 400 + t.tm_yday;
}
// "Sat 3:30 PM", or "Sat TBD" with time_tbd, when ESPN has no kickoff time
// yet (then kick_local is from tbd_day_epoch).
std::string kickoff_label(const struct tm &kick_local, const struct tm &now_local, bool time_tbd = false);
// ESPN lists a game with no kickoff time yet (timeValid false: college games
// a week or two out, TBD playoff slots) at midnight Eastern of its day. That
// instant is the evening before in Central and Pacific time, so anything that
// needs the game's day uses noon Eastern instead: the same calendar day from
// Hawaii to central Europe.
inline int64_t tbd_day_epoch(int64_t placeholder) { return placeholder + 12 * 3600; }
int64_t parse_iso8601_z(const std::string &s);
// An HTTP Date header, "Sat, 17 Oct 2026 23:39:29 GMT" (the only form a
// server may send), as a UTC epoch. 0 when it is anything else or outside
// 2020 to 2099.
int64_t parse_http_date(const std::string &s);
const char *state_name(GameState s);
// "0:42 2nd" from the raw clock and period. ESPN's pre-formatted shortDetail
// lags those fields by a poll or two, so it is only used for the states that
// have no clock (Halftime, End of 3rd, Delayed).
std::string clock_text(const GameSnapshot &s);

// The team endpoint does not say which league answered, so a freshly parsed
// Schedule carries league 0 (NFL). Favorites and the live modes choose the
// follow-up scoreboard endpoint from Schedule::league, so it must be stamped
// from the team the schedule was fetched for.
inline void adopt_league(Schedule &s, League league) { s.league = (uint8_t) league; }
// A cup's team endpoint does not name the cup either.
inline void adopt_comp(Schedule &s, const char *comp_slug) { s.comp_slug = comp_slug; }

// Per-sport text and splashes. status_text, clock_text and decide_splash hand
// any snapshot that is not football to these (sport_baseball.cpp).
std::string baseball_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local);
std::string baseball_clock_text(const GameSnapshot &s);
Splash baseball_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral);
// The count while a half inning is on ("2-1"), else "".
std::string baseball_count(const GameSnapshot &s);
// The latest play is a home run.
bool baseball_home_run(const GameSnapshot &s);
// Hockey (sport_hockey.cpp).
std::string hockey_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local);
std::string hockey_clock_text(const GameSnapshot &s);
Splash hockey_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral);

// Soccer (sport_soccer.cpp).
std::string soccer_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local);
std::string soccer_clock_text(const GameSnapshot &s);
Splash soccer_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral);
// The latest goal for the 128-wide situation row, at most `max` characters:
// "52' Saka", "55' Hany OG", "52' Saka (P)". "" before the first goal.
std::string soccer_situation(const GameSnapshot &s, size_t max = 12);
// A W-D-L record for the label under a logo (7 characters wide): as is when
// it fits, else the league points ("38 pts").
std::string soccer_board_record(const std::string &wdl);
// Plain ASCII for the panel fonts: accents dropped ("Guimarães" -> "Guimaraes").
std::string ascii_fold(const std::string &utf8);
// One club's team reads, the league's first and then each cup's: the index of
// the one to follow, -1 when none names a game. The earliest kickoff wins. A
// game that kicked off over four hours ago, or done_event (a game the panel
// saw finish), only counts when no read names anything else.
int pick_schedule(const std::vector<Schedule> &comps, int64_t now_epoch, const std::string &done_event);
// Basketball (sport_basketball.cpp). The JSON filter and extras parse are
// declared in espn_parse_impl.h, next to ArduinoJson.
std::string basketball_status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local);
std::string basketball_clock_text(const GameSnapshot &s);
Splash basketball_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral);
// The playoff series in at most 12 characters for the situation row
// ("NY lead 2-1", "Series 2-2"), else "".
std::string basketball_series(const GameSnapshot &s);

namespace detail {
// Ticker helpers shared by the sport files.
void add_part(std::string &out, const std::string &part);
std::string records_line(const GameSnapshot &s);
// Both records as one pre-game part, ours first: "DAL 2-3, GB 3-2". A side
// with no record is left out; "" when neither has one.
std::string pregame_records(const GameSnapshot &s);
// A football leader for the ticker from ESPN's short name and stat line:
// "D. Prescott" + "129/192, 1381 YDS, 9 TD, 3 INT" gives "Prescott 1,381 yds
// 9 TD". "" when the line has no yards.
std::string leader_line(const std::string &short_name, const std::string &display_value);
// Another sport's leader from ESPN's short name, the stat ("35.0", "42") and
// its unit: "Antetokounmpo 35.0 PPG". "" when the stat isn't a number above 0.
std::string stat_leader(const std::string &short_name, const std::string &value, const char *unit);
// The pre-game ticker's first part: the game's name ("Rose Bowl", "NLCS -
// Game 2"), else the week ("Week 8"), else "".
std::string pregame_name(const GameSnapshot &s);
// The pre-game ticker's last part: the forecast and the venue, "76F Cloudy at
// Hard Rock Stadium". A venue that repeats the game's name ("Rose Bowl") is
// left out.
std::string venue_line(const GameSnapshot &s, const TickerOptions &o = TickerOptions());
// The pre-game betting parts when the ticker shows odds: the line, the
// over/under and the TV listing. Soccer builds its own line.
void add_odds(std::string &out, const GameSnapshot &s, const TickerOptions &o);
void add_odds(std::string &out, const GameSnapshot &s, const TickerOptions &o, const std::string &odds);
// Both sides' season leaders before the start, when the ticker shows them.
void add_leaders(std::string &out, const GameSnapshot &s, const TickerOptions &o);
// The last play, cut to 160 characters so one long description doesn't fill
// the ticker.
void add_last_play(std::string &out, std::string play);
}  // namespace detail

// Does this cycle need to re-read the team endpoint, or only re-poll the game
// it already knows about? `force` is the Refresh Now button. It has to be its
// own flag: clearing the fetch timestamp does not force anything, because a
// still valid schedule with a zero timestamp reads as "age = millis()", which
// is under the interval for the first six hours after a restart. Unsigned
// subtraction keeps the age right across the millis() wrap.
inline bool schedule_due(bool have_schedule, bool force, uint32_t now, uint32_t fetched_ms, uint32_t interval) {
  return force || !have_schedule || (now - fetched_ms) >= interval;
}

// ESPN's team endpoint names a team's next game as soon as the current one
// starts (MIL and LAD on 2026-10-12), so a read during a game skips it.
// `today` is the team's first unfinished game on the day's scoreboard
// (parse_team_game). True when it comes before the team endpoint's game, or
// the team endpoint named none: it is the game on now.
inline bool today_game_first(const Schedule &team, const Schedule &today) {
  if (today.event_id.empty() || today.event_id == team.event_id)
    return false;
  return team.event_id.empty() || today.kickoff_epoch < team.kickoff_epoch;
}

// The NFL's team endpoint keeps naming a game through its final (NE @ CHI on
// 2026-10-22, while a college team's had moved on from a game still in
// overtime), so an NFL read never needs the day's scoreboard to find the game
// on now. That read is the whole NFL day, ~256 KB on a Sunday.
inline bool team_names_game_on_now(League league) { return league == League::NFL; }

// How long a live mode waits after a scan that found nothing in progress:
// until the day's next kickoff, but at least min_ms (the game is due and
// not under way yet) and at most max_ms (kickoffs move, and the day the scan
// reads rolls over). With no kickoff left (next_kickoff 0) it waits max_ms,
// and with no clock min_ms.
inline uint32_t no_live_rescan_ms(int64_t now_epoch, int64_t next_kickoff, uint32_t min_ms, uint32_t max_ms) {
  if (now_epoch < 1000000000)
    return min_ms;
  if (next_kickoff == 0)
    return max_ms;
  int64_t ms = (next_kickoff - now_epoch) * 1000;
  if (ms < (int64_t) min_ms)
    return min_ms;
  if (ms > (int64_t) max_ms)
    return max_ms;
  return (uint32_t) ms;
}

}  // namespace espn

// Streaming entry points live in the header because they are templates on
// the reader type (a std::string on the host, an HTTP container on device).
#include "espn_parse_impl.h"
