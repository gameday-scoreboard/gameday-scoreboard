#include "espn_parse.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>

namespace espn {

// A pointer to a literal: no copy in RAM, and one atomic store when a bench
// build swaps it under a read on the worker task.
static std::atomic<const char *> site_root{"https://site.api.espn.com/apis/site/v2/sports/"};

void set_site_root(const char *root) { site_root.store(root, std::memory_order_relaxed); }

// "https://.../sports/football/nfl", with no trailing slash.
static std::string site_base(League league) {
  const LeagueInfo *l = league_info(league);
  return std::string(site_root.load(std::memory_order_relaxed)) + (l != nullptr ? l->path : "football/nfl");
}

const char *league_path(League league) {
  const LeagueInfo *l = league_info(league);
  if (l == nullptr)
    return "";
  const char *slash = strrchr(l->path, '/');
  return slash != nullptr ? slash + 1 : l->path;
}

std::string team_option(const Team &t) {
  const LeagueInfo *l = league_info(t.league);
  return std::string(l != nullptr ? l->prefix : "?") + ": " + t.name;
}

std::string team_url(League league, uint32_t espn_id) {
  return site_base(league) + "/teams/" + std::to_string(espn_id);
}

std::string schedule_url(League league, uint32_t espn_id) {
  // A soccer team's schedule lists past results unless asked for fixtures.
  if (league_sport(league) == Sport::SOCCER)
    return team_url(league, espn_id) + "/schedule?fixture=true";
  return team_url(league, espn_id) + "/schedule";
}

static void civil_from_epoch(int64_t epoch, int &y, int &m, int &d);

std::string team_day_url(League league, uint32_t group, int64_t day_epoch) {
  return team_day_url(league, group, day_epoch, nullptr);
}

static std::string comp_base(League league, const char *comp_slug);

std::string team_day_url(League league, uint32_t group, int64_t day_epoch, const char *comp_slug) {
  std::string url = comp_base(league, comp_slug) + "/scoreboard?";
  int y, m, d;
  civil_from_epoch(day_epoch - 5 * 3600, y, m, d);
  char buf[48];
  if ((league == League::MCBB || league == League::NCAA) && group != 0) {
    snprintf(buf, sizeof(buf), "groups=%u&", (unsigned) group);
    url += buf;
  }
  snprintf(buf, sizeof(buf), "dates=%04d%02d%02d", y, m, d);
  return url + buf;
}

std::string event_url(League league, const std::string &event_id) {
  return site_base(league) + "/scoreboard/" + event_id;
}

// A cup lives next to its league: soccer/eng.1 -> soccer/uefa.champions.
static std::string comp_base(League league, const char *comp_slug) {
  const LeagueInfo *l = league_info(league);
  if (comp_slug == nullptr || l == nullptr)
    return site_base(league);
  const char *slash = strchr(l->path, '/');
  std::string sport = slash != nullptr ? std::string(l->path, slash - l->path + 1) : std::string();
  return site_root.load(std::memory_order_relaxed) + sport + comp_slug;
}

std::string team_url(League league, uint32_t espn_id, const char *comp_slug) {
  return comp_base(league, comp_slug) + "/teams/" + std::to_string(espn_id);
}

std::string event_url(League league, const std::string &event_id, const char *comp_slug) {
  return comp_base(league, comp_slug) + "/scoreboard/" + event_id;
}

// Number of days since 1970-01-01 for a UTC epoch, then split into y/m/d.
// Avoids gmtime_r so the host and device agree regardless of libc quirks.
static void civil_from_epoch(int64_t epoch, int &y, int &m, int &d) {
  int64_t z = epoch / 86400 + 719468;
  if (epoch < 0 && epoch % 86400 != 0)
    z -= 1;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int64_t doe = z - era * 146097;
  int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t yy = yoe + era * 400;
  int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int64_t mp = (5 * doy + 2) / 153;
  d = (int) (doy - (153 * mp + 2) / 5 + 1);
  m = (int) (mp < 10 ? mp + 3 : mp - 9);
  y = (int) (yy + (m <= 2 ? 1 : 0));
}

std::string scoreboard_url(League league, uint32_t group, int64_t kickoff_epoch, bool time_tbd) {
  std::string url = site_base(league) + "/scoreboard";
  int y, m, d;
  // ESPN's date parameter is in US Eastern; shift the UTC kickoff by five
  // hours so late east-coast games stay on the right day. Being off by an
  // hour around a DST change only matters for a game kicking off between
  // 11 PM and midnight Eastern, which does not happen in football. A TBD
  // game's midnight placeholder is exactly that case, so it goes by noon.
  if (time_tbd)
    kickoff_epoch = tbd_day_epoch(kickoff_epoch);
  civil_from_epoch(kickoff_epoch - 5 * 3600, y, m, d);
  char buf[64];
  if (league == League::NCAA)
    snprintf(buf, sizeof(buf), "?groups=%u&dates=%04d%02d%02d", (unsigned) group, y, m, d);
  else
    snprintf(buf, sizeof(buf), "?dates=%04d%02d%02d", y, m, d);
  url += buf;
  return url;
}

std::string scan_url(League league, int64_t now_epoch) {
  std::string url = site_base(league) + "/scoreboard";
  int y, m, d;
  civil_from_epoch(now_epoch - scan_day_shift(league), y, m, d);
  char buf[64];
  // 80 is every FBS game. College basketball without a group lists only the
  // ranked teams' games (17 of 132 on 2/14/2026); 50 is all of Division I.
  if (league == League::NCAA)
    snprintf(buf, sizeof(buf), "?groups=80&limit=300&dates=%04d%02d%02d", y, m, d);
  else if (league == League::MCBB)
    snprintf(buf, sizeof(buf), "?groups=50&limit=300&dates=%04d%02d%02d", y, m, d);
  else
    snprintf(buf, sizeof(buf), "?dates=%04d%02d%02d", y, m, d);
  url += buf;
  return url;
}

// Dark-background variant, shrunk to 64px by ESPN's image resizer. A 500px
// logo PNG is up to 95KB and takes the ESP32 almost two seconds to decode;
// the 64px one is about 3KB and decodes in a few milliseconds.
std::string dark_logo(const std::string &url) {
  if (url.empty())
    return url;
  std::string out = url;
  size_t pos = out.find("/500/");
  if (pos != std::string::npos)
    out.replace(pos, 5, "/500-dark/");
  size_t path = out.find("/i/teamlogos/");
  if (path == std::string::npos)
    return out;
  return "https://a.espncdn.com/combiner/i?img=" + out.substr(path) + "&w=64&h=64";
}

// The same URL the scoreboard gives this team, so the logo cache downloads it
// once. The scoreboard's NFL, MLB, NBA and NHL logos sit under /scoreboard/,
// and that variant is not always the same picture (the Jets').
std::string team_logo_url(League league, uint32_t espn_id, const char *abbr) {
  std::string a = abbr ? abbr : "";
  for (auto &c : a)
    c = (char) tolower((unsigned char) c);
  const LeagueInfo *l = league_info(league);
  if (l == nullptr)
    return "";
  std::string file = l->logo_by_id ? std::to_string(espn_id) : a;
  bool scoreboard = league == League::NFL || league == League::MLB || league == League::NBA || league == League::NHL;
  return dark_logo(std::string("https://a.espncdn.com/i/teamlogos/") + l->logo_dir +
                   (scoreboard ? "/500/scoreboard/" : "/500/") + file + ".png");
}

GameSnapshot team_placeholder(const Team &t) {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::PRE;
  g.team_abbr = t.abbr;
  g.team_id = t.espn_id;
  g.team_logo = team_logo_url(t.league, t.espn_id, t.abbr);
  return g;
}

// Every game read from ESPN has an event id; a final card is POST.
bool is_team_placeholder(const GameSnapshot &g) {
  return g.valid && g.state == GameState::PRE && g.event_id.empty();
}

GameSnapshot final_snapshot(const Final &f) {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::POST;
  g.completed = true;
  g.kickoff_epoch = f.kickoff_epoch;
  g.short_detail = f.detail;
  g.team_id = f.away_id;
  g.opp_id = f.home_id;
  g.team_score = f.away_score;
  g.opp_score = f.home_score;
  g.team_abbr = f.away_abbr;
  g.opp_abbr = f.home_abbr;
  g.team_color = f.away_color;
  g.opp_color = f.home_color;
  g.team_record = f.away_record;
  g.opp_record = f.home_record;
  g.team_logo = team_logo_url((League) f.league, f.away_id, f.away_abbr.c_str());
  g.opp_logo = team_logo_url((League) f.league, f.home_id, f.home_abbr.c_str());
  g.sport = league_sport((League) f.league);
  return g;
}

GameSnapshot pending_snapshot(const Pending &p) {
  GameSnapshot g;
  g.valid = true;
  g.state = GameState::PRE;
  g.event_id = p.event_id;
  g.kickoff_epoch = p.kickoff_epoch;
  g.team_id = p.away_id;
  g.opp_id = p.home_id;
  g.team_abbr = p.away_abbr;
  g.opp_abbr = p.home_abbr;
  g.team_color = p.away_color;
  g.opp_color = p.home_color;
  g.team_record = p.away_record;
  g.opp_record = p.home_record;
  g.tv = p.tv;
  g.team_logo = team_logo_url((League) p.league, p.away_id, p.away_abbr.c_str());
  g.opp_logo = team_logo_url((League) p.league, p.home_id, p.home_abbr.c_str());
  g.sport = league_sport((League) p.league);
  return g;
}

bool parse_team_str(const std::string &json, Schedule &out) { return parse_team(json, out); }

bool parse_upcoming_str(const std::string &json, uint32_t our_team_id, size_t max, std::vector<Upcoming> &out) {
  return parse_upcoming(json, our_team_id, max, out);
}

bool parse_scoreboard_str(const std::string &json, const std::string &event_id, uint32_t our_team_id,
                          GameSnapshot &out) {
  return parse_scoreboard(json, event_id, our_team_id, out);
}

bool parse_team_game_str(const std::string &json, uint32_t our_team_id, Schedule &out, const std::string &skip_event) {
  return parse_team_game(json, our_team_id, skip_event, out) == 1;
}

bool parse_event_str(const std::string &json, Sport sport, uint32_t our_team_id, GameSnapshot &out,
                     const std::string &event_id) {
  return parse_event(json, sport, our_team_id, out, event_id);
}

uint32_t parse_color(const std::string &hex) {
  if (hex.size() != 6)
    return 0xFFFFFF;
  char *end = nullptr;
  unsigned long v = strtoul(hex.c_str(), &end, 16);
  if (end == nullptr || *end != '\0')
    return 0xFFFFFF;
  return (uint32_t) v;
}

static const char *score_word(int delta, bool ours) {
  switch (delta) {
    case 6:
    case 7:
    case 8:
      return ours ? "TOUCHDOWN!" : "TOUCHDOWN";
    case 3:
      return ours ? "FIELD GOAL!" : "FIELD GOAL";
    case 1:
      return ours ? "EXTRA POINT!" : "EXTRA POINT";
    case 2:
      return ours ? "2-POINT!" : "2-POINT";
    default:
      return ours ? "SCORE!" : "SCORE";
  }
}

Splash decide_splash(const GameSnapshot &prev, const GameSnapshot &cur, bool opponent_splashes, bool neutral) {
  if (cur.sport == Sport::BASEBALL)
    return baseball_splash(prev, cur, opponent_splashes, neutral);
  if (cur.sport == Sport::SOCCER)
    return soccer_splash(prev, cur, opponent_splashes, neutral);
  if (cur.sport == Sport::BASKETBALL)
    return basketball_splash(prev, cur, opponent_splashes, neutral);
  if (cur.sport == Sport::HOCKEY)
    return hockey_splash(prev, cur, opponent_splashes, neutral);
  Splash none;
  if (!prev.valid || !cur.valid || prev.event_id != cur.event_id)
    return none;
  if (prev.state == GameState::IN && cur.state == GameState::POST) {
    // ESPN also moves a suspended or canceled game to post, with completed
    // false. Only a completed game has a winner.
    if (!cur.completed)
      return none;
    if (cur.team_score > cur.opp_score)
      return Splash{cur.team_abbr + " WINS!", parse_color(cur.team_color)};
    if (neutral && cur.opp_score > cur.team_score)
      return Splash{cur.opp_abbr + " WINS!", parse_color(cur.opp_color)};
    return none;
  }
  if (prev.state != GameState::IN || cur.state != GameState::IN)
    return none;
  int delta = cur.team_score - prev.team_score;
  if (delta > 0 && neutral)
    return Splash{cur.team_abbr + " " + score_word(delta, false), parse_color(cur.team_color)};
  if (delta > 0)
    return Splash{score_word(delta, true), parse_color(cur.team_color)};
  int odelta = cur.opp_score - prev.opp_score;
  if (odelta > 0 && opponent_splashes)
    return Splash{cur.opp_abbr + " " + score_word(odelta, false), parse_color(cur.opp_color)};
  return none;
}

namespace detail {

void add_part(std::string &out, const std::string &part) {
  if (part.empty())
    return;
  if (!out.empty())
    out += " | ";
  out += part;
}

std::string records_line(const GameSnapshot &s) {
  std::string out;
  if (!s.team_record.empty())
    add_part(out, s.team_abbr + " " + s.team_record);
  if (!s.opp_record.empty())
    add_part(out, s.opp_abbr + " " + s.opp_record);
  return out;
}

void add_odds(std::string &out, const GameSnapshot &s, const TickerOptions &o, const std::string &odds) {
  if (!o.odds)
    return;
  add_part(out, odds);
  if (!s.over_under.empty())
    add_part(out, "O/U " + s.over_under);
  add_part(out, s.tv);
}

void add_odds(std::string &out, const GameSnapshot &s, const TickerOptions &o) { add_odds(out, s, o, s.odds); }

void add_last_play(std::string &out, std::string play) {
  if (play.size() > 160)
    play = play.substr(0, 157) + "...";
  add_part(out, play);
}

}  // namespace detail

using detail::add_last_play;
using detail::add_odds;
using detail::add_part;
using detail::records_line;

std::string no_game_text(bool clock_set, bool live_mode, bool no_game_confirmed, uint32_t misses, bool captive) {
  if (captive)
    return "Wi-Fi needs a sign-in";
  if (!clock_set)
    return "Getting ready";
  if (live_mode)
    return "";
  if (!no_game_confirmed && misses > 0)
    return "Can't reach ESPN";
  return "";
}

std::string status_text(const GameSnapshot &s, const TickerOptions &o, const std::string &kickoff_local) {
  if (s.sport == Sport::BASEBALL)
    return baseball_status_text(s, o, kickoff_local);
  if (s.sport == Sport::SOCCER)
    return soccer_status_text(s, o, kickoff_local);
  if (s.sport == Sport::BASKETBALL)
    return basketball_status_text(s, o, kickoff_local);
  if (s.sport == Sport::HOCKEY)
    return hockey_status_text(s, o, kickoff_local);
  std::string out;
  switch (s.state) {
    case GameState::NOT_FOUND:
      return "No upcoming game";
    case GameState::PRE:
      add_part(out, s.game_name);
      add_part(out, kickoff_local.empty() ? s.short_detail : kickoff_local);
      add_odds(out, s, o);
      if (s.venue != s.game_name)  // "Rose Bowl | ... | Rose Bowl"
        add_part(out, s.venue);
      return out;
    case GameState::POST:
      // A game that ended without being completed says why: Postponed,
      // Canceled or Suspended
      out = s.completed || s.short_detail.empty() ? std::string("Final") : s.short_detail;
      add_part(out, records_line(s));
      return out;
    case GameState::IN:
      break;
  }
  if (s.short_detail.find("Halftime") != std::string::npos) {
    out = "Halftime";
    add_part(out, records_line(s));
    return out;
  }
  if (o.down_distance)
    add_part(out, s.down_distance);
  if (o.clock)
    add_part(out, s.short_detail);
  if (o.last_play) {
    add_last_play(out, s.last_play);
    if (!s.drive.empty())
      add_part(out, "Drive: " + s.drive);
  }
  if (o.odds)
    add_part(out, win_chance(s));
  if (out.empty())
    out = s.short_detail.empty() ? "In Progress" : s.short_detail;
  return out;
}

std::string win_chance(const GameSnapshot &s) {
  if (s.team_win_pct < 1)
    return "";
  // The side ahead in ESPN's model, so the number is always 50 or more.
  bool ours = s.team_win_pct >= 50;
  int pct = ours ? s.team_win_pct : 100 - s.team_win_pct;
  return (ours ? s.team_abbr : s.opp_abbr) + " " + std::to_string(pct) + "% to win";
}

std::string ha_status_text(const GameSnapshot &s, const std::string &kickoff_local) {
  std::string out;
  switch (s.state) {
    case GameState::NOT_FOUND:
      return "No upcoming game";
    case GameState::PRE:
      add_part(out, s.game_name);
      add_part(out, s.team_abbr + " vs " + s.opp_abbr);
      add_part(out, kickoff_local.empty() ? s.short_detail : kickoff_local);
      return out;
    case GameState::POST:
      out = s.completed || s.short_detail.empty() ? std::string("Final") : s.short_detail;
      break;
    case GameState::IN:
      out = s.short_detail.find("Halftime") != std::string::npos ? "Halftime" : "Live";
      break;
  }
  add_part(out, s.team_abbr + " " + std::to_string(s.team_score) + ", " + s.opp_abbr + " " +
                    std::to_string(s.opp_score));
  return out;
}

// Bowls named after their sponsor ("Allstate Sugar Bowl") go by the word
// before "Bowl". Only bowls whose sponsor changes or runs long are listed; one
// not here keeps its full name.
static const char *const kBowls[] = {
    "Rose",       "Sugar",        "Orange",     "Cotton",      "Fiesta",          "Peach",        "Citrus",
    "Gator",      "Sun",          "Liberty",    "Alamo",       "Holiday",         "Texas",        "Music City",
    "Las Vegas",  "Pinstripe",    "Military",   "New Mexico",  "Independence",    "Gasparilla",   "Frisco",
    "Fenway",     "Armed Forces", "Arizona",    "Hawai'i",     "Hawaii",          "Cure",         "New Orleans",
    "Camellia",   "Boca Raton",   "Birmingham", "Bahamas",     "First Responder", "Myrtle Beach",
};

static bool replace_first(std::string &s, const char *from, const char *to) {
  size_t at = s.find(from);
  if (at == std::string::npos)
    return false;
  s.replace(at, strlen(from), to);
  return true;
}

// "FLEX: 3:30 - 4:30pm OR 6 - 8pm ET start": ESPN puts a TBD game's TV
// window in the same notes as game names.
static bool has_time_of_day(const std::string &lower) {
  for (size_t i = 0; i + 2 < lower.size(); i++) {
    if (!isdigit((unsigned char) lower[i]))
      continue;
    size_t j = i + 1;
    while (j < lower.size() && lower[j] == ' ')
      j++;
    if (j + 1 < lower.size() && (lower[j] == 'a' || lower[j] == 'p') && lower[j + 1] == 'm')
      return true;
  }
  return lower.find(" et start") != std::string::npos;
}

std::string short_game_name(const std::string &headline) {
  std::string s = headline;
  std::string lower = s;
  for (char &c : lower)
    c = (char) tolower((unsigned char) c);
  if (has_time_of_day(lower))
    return "";
  // Sponsor tails: "... Presented by AT&T", "LA Bowl Hosted by Gronk"
  for (const char *tail : {" presented by ", " powered by ", " hosted by "}) {
    size_t at = lower.find(tail);
    if (at != std::string::npos) {
      s.resize(at);
      lower.resize(at);
    }
  }
  // A playoff game at a bowl goes by the bowl
  size_t at = s.find(" at the ");
  if (at != std::string::npos && s.find(" Bowl", at) != std::string::npos)
    s = s.substr(at + 8);
  if (!replace_first(s, "College Football Playoff National Championship", "CFP Championship"))
    replace_first(s, "College Football Playoff", "CFP");
  // "NFC Wild Card Playoffs", "AFC Divisional Playoffs"
  static const char kPlayoffs[] = " Playoffs";
  size_t n = sizeof(kPlayoffs) - 1;
  if (s.size() > n && s.compare(s.size() - n, n, kPlayoffs) == 0)
    s.resize(s.size() - n);
  for (const char *bowl : kBowls) {
    std::string name = std::string(bowl) + " Bowl";
    size_t b = s.find(name);
    if (b != std::string::npos && (b == 0 || s[b - 1] == ' ')) {
      s = name;
      break;
    }
  }
  return s;
}

static std::string clock_12h(const struct tm &t) {
  int h = t.tm_hour % 12;
  if (h == 0)
    h = 12;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d %s", h, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
  return buf;
}

std::string kickoff_label(const struct tm &kick_local, const struct tm &now_local, bool time_tbd) {
  static const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  // An ESPTime the panel couldn't convert comes out with month and weekday
  // -1: no kickoff time to show.
  if (kick_local.tm_mon < 0 || kick_local.tm_mon > 11 || kick_local.tm_wday < 0 || kick_local.tm_wday > 6)
    return "";
  int64_t now_day = (int64_t) now_local.tm_year * 366 + now_local.tm_yday;
  int64_t kick_day = (int64_t) kick_local.tm_year * 366 + kick_local.tm_yday;
  int64_t diff = kick_day - now_day;
  std::string when;
  if (diff == 0)
    when = "Today";
  else if (diff == 1)
    when = "Tomorrow";
  else if (diff > 1 && diff < 7)
    when = kDays[kick_local.tm_wday];
  else {
    char buf[16];
    snprintf(buf, sizeof(buf), "%s %d", kMonths[kick_local.tm_mon], kick_local.tm_mday);
    when = buf;
  }
  return when + " " + (time_tbd ? std::string("TBD") : clock_12h(kick_local));
}

static int64_t epoch_from_civil(int y, int mo, int d, int h, int mi, int sec) {
  // days from civil (Howard Hinnant's algorithm)
  int yy = y - (mo <= 2 ? 1 : 0);
  int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
  int64_t yoe = yy - era * 400;
  int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = era * 146097 + doe - 719468;
  return days * 86400 + h * 3600 + mi * 60 + sec;
}

// "2026-09-05T19:30Z" or "2026-09-05T19:30:00Z" (ESPN always sends UTC).
// The field widths keep a run of digits from overflowing an int, and a date
// out of range reads as no date, like garbage does.
int64_t parse_iso8601_z(const std::string &s) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
  int n = sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec);
  if (n < 5 || y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 ||
      sec < 0 || sec > 60)
    return 0;
  return epoch_from_civil(y, mo, d, h, mi, sec);
}

int64_t parse_http_date(const std::string &s) {
  static const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  // "Sat, 17 Oct 2026 23:39:29 GMT": fixed width, so check it by position.
  if (s.size() != 29 || s[3] != ',' || s[4] != ' ' || s[7] != ' ' || s[11] != ' ' || s[16] != ' ' ||
      s[19] != ':' || s[22] != ':' || s.compare(25, 4, " GMT") != 0)
    return 0;
  auto num = [&s](size_t at, size_t len) {
    int v = 0;
    for (size_t i = at; i < at + len; i++) {
      if (s[i] < '0' || s[i] > '9')
        return -1;
      v = v * 10 + (s[i] - '0');
    }
    return v;
  };
  int mo = 0;
  for (int i = 0; i < 12; i++)
    if (s.compare(8, 3, kMonths[i]) == 0)
      mo = i + 1;
  int d = num(5, 2), y = num(12, 4), h = num(17, 2), mi = num(20, 2), sec = num(23, 2);
  if (mo == 0 || d < 1 || d > 31 || y < 2020 || y > 2099 || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 ||
      sec > 60)
    return 0;
  return epoch_from_civil(y, mo, d, h, mi, sec);
}

static std::string period_name(int period) {
  switch (period) {
    case 1:
      return "1st";
    case 2:
      return "2nd";
    case 3:
      return "3rd";
    case 4:
      return "4th";
    case 5:
      return "OT";
    default:
      return period > 5 ? std::to_string(period - 4) + "OT" : "";
  }
}

std::string clock_text(const GameSnapshot &s) {
  if (s.sport == Sport::BASEBALL)
    return baseball_clock_text(s);
  if (s.sport == Sport::SOCCER)
    return soccer_clock_text(s);
  if (s.sport == Sport::BASKETBALL)
    return basketball_clock_text(s);
  if (s.sport == Sport::HOCKEY)
    return hockey_clock_text(s);
  bool has_clock = s.display_clock.find(':') != std::string::npos && s.period > 0;
  bool special = s.short_detail.find(':') == std::string::npos;  // Halftime, End of 3rd, Delayed, Final
  if (!has_clock || special) {
    std::string t = s.short_detail;
    size_t dash = t.find(" - ");
    if (dash != std::string::npos)
      t.replace(dash, 3, " ");
    return t;
  }
  return s.display_clock + " " + period_name(s.period);
}

const char *state_name(GameState s) {
  switch (s) {
    case GameState::PRE:
      return "PRE";
    case GameState::IN:
      return "IN";
    case GameState::POST:
      return "POST";
    default:
      return "NOT_FOUND";
  }
}

}  // namespace espn
