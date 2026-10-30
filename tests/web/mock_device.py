"""Mock of the scoreboard's web server for the device page tests.

Serves firmware/web/bundle.js and app.css at /0.js and /0.css (where ESPHome's
web_server puts js_include and css_include), the ESPHome /events stream for
the entities the page builds controls from, and the gameday component's
routes with the response shapes from components/gameday/gameday.cpp:

  GET  /gameday/state    the document rebuild_state_() writes
  POST /gameday/set      only the keys in SET_KEYS, from a form body or the URL's query
                         (the body first, as ESPHome reads them); 400 when none match.
                         A URL past 512 bytes answers 414 and a body past 1024 bytes
                         400, as the panel's server does
  POST /gameday/action   400 without do=, 404 for a name the firmware does not know;
                         say keeps its cleaned text in "say" for tests to read (the
                         panel has no such key); night_skip turns a "dim" or "off"
                         night_now to "skipped" and night_resume a "skipped" one back
                         to "dim" or "off"
  GET  /gameday/wifi     the saved networks (components/saved_networks), names only
  POST /gameday/wifi     forget=<ssid>; 404 when it is not saved
  GET  /gameday/settings the settings backup (components/gameday/settings_doc.h)
  POST /gameday/settings a backup as a JSON body: checked whole, then applied through
                         apply_set; 400 with "error" when refused
  GET  /gameday/picture  ?slot=N, a kept picture's file as uploaded; 404 for an empty slot
  POST /gameday/picture  a PNG or GIF body kept in the first free slot, or delete=N;
                         400 with "error" when refused (picture.h)

/?demo loads bundle-demo.js instead, the demo build's page, and
GET/POST /gameday/demo answer as demo.cpp does.

Test hooks: POST /_reset with an optional JSON body
{"state": {...overrides...}, "fail": ["state", "set"], "wifi": [...]} puts the
state back, merges the overrides and makes the named routes answer 500. Two
more fail names drop the connection with no answer, the way a panel that has
gone does: "events" for the event stream (an open one ends within half a
second), "down" for every route but the test hooks. "wifi" replaces the saved
network list; null stands for firmware without the list, whose /gameday/wifi
answers 404. "settings" is firmware without the backup: /gameday/settings
answers 404. GET /_log lists the /gameday/set, /gameday/action,
/gameday/wifi and /gameday/settings requests seen since the last reset, and
the entity posts (/select/Effect/set and the like), which change the entity
and go out on every open /events stream as the panel's do.

Started by playwright.config.js. Run by hand with `python mock_device.py 8765`
and open http://127.0.0.1:8765/.

The set keys, their limits and the Live auto pick come from
components/gameday/set_request.h. `python mock_device.py --selftest` runs
tests/fixtures/set_cases.json, the table the firmware's host tests also
check, through apply_set, so the mock can't drift from the panel.
"""
import copy
import datetime
import itertools
import json
import os
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

REPO = Path(__file__).resolve().parents[2]
WEB = Path(os.environ.get("GAMEDAY_WEB", REPO / "firmware" / "web"))

# Same tables the firmware and scripts/build_web.py read.
TEAM_RE = re.compile(r'\{League::([A-Z]+),\s*(\d+),\s*"([^"]*)",\s*"([^"]*)",\s*(\d+)\}')
TZ_RE = re.compile(r'\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\}')
TEAMS = {}
for lg, tid, abbr, name, _ in TEAM_RE.findall((REPO / "components/gameday/teams.h").read_text(encoding="utf-8")):
    TEAMS[(lg.lower(), int(tid))] = (abbr, name)
# kLeagues order, which is the order the firmware lists live leagues in.
sys.path.insert(0, str(REPO / "components" / "gameday"))
from leagues_table import leagues  # noqa: E402

LEAGUES = [row["key"] for row in leagues()]
LEAGUE_SPORT = {row["key"]: row["sport"].lower() for row in leagues()}
# leagues.h kSportKeys: the sport rows' words, in Sport order.
SPORT_KEYS = tuple(re.findall(r'"(\w+)"', re.search(r"kSportKeys\[kSportCount\] = \{(.*?)\};",
                                                    (REPO / "components/gameday/leagues.h").read_text(encoding="utf-8"),
                                                    re.S).group(1)))
TZ_TEXT = "\n".join(ln for ln in (REPO / "components/gameday/timezones.h").read_text(encoding="utf-8").splitlines()
                    if not ln.lstrip().startswith("//"))
TZ_ROWS = TZ_RE.findall(TZ_TEXT)
TZS = [name for name, _posix, _iana in TZ_ROWS]

# /gameday/set's keys in the order the firmware applies them, and its limits.
SET_H = (REPO / "components/gameday/set_request.h").read_text(encoding="utf-8")
SET_KEYS = tuple(re.findall(r'"(\w+)"', re.search(r"kSetKeys\[\] = \{(.*?)\};", SET_H, re.S).group(1)))
MESSAGE_H = (REPO / "components/gameday/message.h").read_text(encoding="utf-8")
LIMITS = {k: int(v) for k, v in re.findall(r"(?:constexpr|static const) (?:int|size_t) (k\w+) = (\d+);",
                                             SET_H + MESSAGE_H +
                                             (REPO / "components/gameday/startup.h").read_text(encoding="utf-8") +
                                             (REPO / "components/gameday/playlists.h").read_text(encoding="utf-8") +
                                             (REPO / "components/gameday/hook.h").read_text(encoding="utf-8") +
                                             (REPO / "components/gameday/weather.h").read_text(encoding="utf-8") +
                                             (REPO / "components/gameday/wake.h").read_text(encoding="utf-8"))}
# After the game's choices (wake.h after_word), in no particular order.
AFTER = tuple(re.findall(r'return "(\w+)";', re.search(r"after_word\(.*?\n}\n",
                         (REPO / "components/gameday/wake.h").read_text(encoding="utf-8"), re.S).group(0)))
FAV_MAX = int(re.search(r"FAV_MAX = (\d+);", (REPO / "components/gameday/gameday.h").read_text(encoding="utf-8"))[1])
# PanelLayout refuses a count past this (gameday-common.yaml).
MAX_COLS = int(re.search(r"^\s*max_cols: (\d+)", (REPO / "firmware/gameday-common.yaml").read_text(encoding="utf-8"),
                         re.M)[1])
FIXTURE = REPO / "tests/fixtures/set_cases.json"
COUNTDOWN_NAME_MAX = int(re.search(r"kCountdownNameMax = (\d+);",
                                   (REPO / "components/gameday/countdown.h").read_text(encoding="utf-8"))[1])
# The Between games list (components/gameday/cards.h kCards): id, name, a new
# panel's choice and the hint while the card has nothing to show.
CARDS = [(m[1], m[2], m[3] == "true", m[4]) for m in re.finditer(
    r'\{Card::\w+,\s*"(\w+)",\s*"([^"]*)",\s*(true|false),\s*"([^"]*)"\}',
    (REPO / "components/gameday/cards.h").read_text(encoding="utf-8"))]
# Cards that kept the switch they had before the list, by id: that state key
# is their truth (GamedayComponent::card_on).
OWN_SWITCH_CARDS = {"countdown": "countdown", "top25": "top25", "news": "news", "gameleaders": "gameleaders",
                    "weather": "wxon"}
# The settings backup's key types and export order (settings_doc.h).
SETTINGS_H = (REPO / "components/gameday/settings_doc.h").read_text(encoding="utf-8")


def _settings_list(name):
    return tuple(re.findall(r'"(\w+)"', re.search(name + r"\[\] = \{(.*?)\};", SETTINGS_H, re.S).group(1)))


SETTINGS_TYPES = {k: t for t in ("Bool", "Int", "Text", "List") for k in _settings_list("k" + t)}
SETTINGS_ORDER = _settings_list("kKeys")
SETTINGS_SCHEMA = int(re.search(r"kSettingsSchema = (\d+);", SETTINGS_H)[1])
SETTINGS_MAX = int(re.search(r"kSettingsMaxBytes = (\d+);", SETTINGS_H)[1])
# The panel's web server: ESP-IDF's URL cap (CONFIG_HTTPD_MAX_URI_LEN) and
# the form body cap (CONFIG_HTTPD_MAX_REQ_HDR_LEN, as ESPHome sets it).
URI_MAX = 512
FORM_MAX = 1024


def team(lg, tid):
    """put_team(): league, id, abbr and name, or an empty object."""
    if (lg, tid) not in TEAMS:
        return {}
    abbr, name = TEAMS[(lg, tid)]
    return {"l": lg, "id": tid, "abbr": abbr, "name": name}


INITIAL = {
    "name": "gameday-2f6a70", "version": "v1.5.4", "setup": True, "panels": 2,
    "mode": 4, "rotate": 1, "lockon": 15, "release": 30, "collide": 0,
    "favs": [team("nfl", 6), team("nfl", 34), team("ncaa", 145), {}],
    "favmax": FAV_MAX, "lists": [], "list": 0, "listmax": LIMITS["kListMax"], "sports": [], "sport": "", "sched": 0,
    # The WizMote's buttons (set_request.h RemoteTarget), each on its own favorite until set.
    "remote": [{"v": "fav"}, {"v": "fav"}, {"v": "fav"}, {"v": "fav"}], "today": False, "jump": False, "banner": True, "favtake": False, "live": ["nfl", "ncaa"], "live_auto": True,
    "tz": 1, "tz_name": "US Central", "tz_auto": True,
    "down": True, "count": True, "match": True, "play": True, "odds": True, "leaders": True, "opp": True, "best": True, "tiles": "1", "bootaddr": True,
    "autoupd": False, "beta": False, "countdown": True, "top25": True, "news": False, "cdname": "", "cddate": "",
    "gameleaders": True,
    # cards.h: the Between games list, rebuilt by state_doc() from these choices.
    "cards": [{"id": cid, "on": on} for cid, _name, on, _hint in CARDS],
    "msg": "", "msgurl": "", "msgevery": 2, "nick": "",
    # The picture card (picture.h): what each kept file is, and how big the next upload can be.
    "pics": [], "picroom": 65536,
    # The game events (ha_events.h, hook.h): the webhook and the reminder's lead.
    "hook": "", "pregame": 15,
    # The weather card (weather.h): the town and the switch; wx once a town is set.
    "town": "", "wxon": False,
    # night.h: the schedule, and what it is doing now ("", "dim", "off", "game", "skipped")
    "night": False, "nightstart": "23:00", "nightend": "07:00", "nightscreen": True, "nightlevel": 16, "night_now": "",
    # wake.h: Game-day wake, After the game, and what it is doing now ("", "asleep", "clock")
    "wake": False, "wakelead": 15, "after": "scoreboard", "wake_now": "",
    "misses": 0, "captive": False, "stale_s": 4, "fallback": False, "recap": False, "espn_kb": 412, "espn_reads": 23,
    # GamedayComponent::health_json_(), added at the end of each read.
    "health": {"up": 7260, "start": "restart", "boots": 2, "fault": "crash", "fault_ago": 90000,
               "heap_kb": 143.2, "block_kb": 40.0, "psram_kb": 4100.5, "rssi": -58, "espn": 200,
               "espn_ago": 9, "version": "1.5.4", "built": 1792285260, "channel": "stable",
               # guard_json_keys() (components/gameday/guard.h): heat 1 while the
               # thermometer shows, power "dipped" while the plug does
               "temp": 46.5, "heat": 0, "brownouts": 0, "power": "ok"},
    # Spliced in live by the firmware (wifi_history.h): an hour up, one drop.
    "wifi": {"rssi": -61, "step": 300, "hist": [-58, -59, -60, None, -64, -62, -61, -61, -60, -62, -61, -61],
             "weak": False, "drops": 1, "day": 1, "causes": {"signal": 1}, "last": "signal", "last_ago": 2400,
             "last_down": 42},
    "game": {"s": "PRE", "l": "nfl", "ta": "DAL", "ti": 6, "ts": 0, "oa": "PHI", "oi": 21, "os": 0,
             "tr": "1-0", "or": "0-1", "p": 0, "tt": 3, "ot": 3, "rz": False, "tv": "FOX",
             "venue": "AT&T Stadium", "odds": "DAL -3", "ou": "47.5", "detail": "Sun 3:25 PM",
             "kick": 1792531500, "lp": "", "dd": "", "c": "", "d": "", "k": "Sun 3:25 PM",
             "tc": "041E42", "oc": "004C54", "id": "401772001"},
    "status": "DAL vs PHI | Sun 3:25 PM | on FOX",
    "splash": "", "splash_color": "000000",
    # Favorite teams: one row per favorite, its next game (rebuild_state_).
    "shown": 1, "locked": False,
    "next": [{"slot": 1, "t": team("nfl", 6), "id": "401772001", "s": "PRE", "kick": 1792531500, "oi": 21, "oa": "PHI",
              "ts": 0, "os": 0, "tv": "FOX", "detail": "Sun 3:25 PM", "done": False},
             {"slot": 2, "t": team("nfl", 34), "id": "401772002", "s": "PRE", "kick": 1793136300, "oi": 28, "oa": "WSH",
              "ts": 0, "os": 0, "tv": "CBS", "detail": "Sun 12:00 PM", "done": False},
             {"slot": 3, "t": team("ncaa", 145), "id": "401772003", "s": "PRE", "kick": 1793741100, "oi": 99, "oa": "LSU",
              "ts": 0, "os": 0, "tv": "ESPN", "detail": "Sat 6:30 PM", "done": False}],
}

# ESPHome 2026.x state events (detail=all shape) for the entities app.js uses.
ENTITIES = [
    {"id": "switch/Power", "domain": "switch", "name": "Power", "state": "ON", "value": True},
    {"id": "number/Brightness", "domain": "number", "name": "Brightness", "state": "128", "value": "128",
     "min_value": "1", "max_value": "255", "step": "1"},
    {"id": "number/Scroll Speed", "domain": "number", "name": "Scroll Speed", "state": "5", "value": "5",
     "min_value": "1", "max_value": "10", "step": "1"},
    {"id": "select/Select Page", "domain": "select", "name": "Select Page", "state": "Scoreboard",
     "value": "Scoreboard", "option": ["Scoreboard", "Clock", "Effects"]},
    # Effects mode (firmware/pages/effects.yaml): wled_fx's entities, shown
    # while Select Page is on Effects.
    {"id": "select/Effect", "domain": "select", "name": "Effect", "state": "Matrix", "value": "Matrix",
     "option": ["Matrix", "Metaballs", "DNA", "Fireworks", "Snow Fall"]},
    {"id": "select/Effect Palette", "domain": "select", "name": "Effect Palette", "state": "Default",
     "value": "Default", "option": ["Default", "Rainbow", "Ocean", "Fire"]},
    {"id": "number/Effect Speed", "domain": "number", "name": "Effect Speed", "state": "128", "value": "128",
     "min_value": "0", "max_value": "255", "step": "1"},
    {"id": "number/Effect Intensity", "domain": "number", "name": "Effect Intensity", "state": "128",
     "value": "128", "min_value": "0", "max_value": "255", "step": "1"},
    {"id": "text_sensor/Game Status", "domain": "text_sensor", "name": "Game Status",
     "state": INITIAL["status"], "value": INITIAL["status"]},
    {"id": "update/Firmware", "domain": "update", "name": "Firmware", "state": "NO UPDATE", "value": "1.5.4",
     "current_version": "1.5.4", "summary": "", "release_url": ""},
    {"id": "text_sensor/IP", "domain": "text_sensor", "name": "IP", "state": "10.10.10.97", "value": "10.10.10.97"},
    {"id": "button/Refresh Now", "domain": "button", "name": "Refresh Now", "state": ""},
    {"id": "button/Reboot", "domain": "button", "name": "Reboot", "state": ""},
    {"id": "button/Reset Wi-Fi", "domain": "button", "name": "Reset Wi-Fi", "state": ""},
    {"id": "button/Factory Reset", "domain": "button", "name": "Factory Reset", "state": ""},
]
FIRMWARE = next(e for e in ENTITIES if e["name"] == "Firmware")
ENTITIES_INITIAL = copy.deepcopy(ENTITIES)
# The control actions ESPHome's web server takes for each domain the page posts to.
ENTITY_ACTIONS = {"switch": ("turn_on", "turn_off", "toggle"), "number": ("set",), "select": ("set",),
                  "button": ("press",), "update": ("install",)}


def entity_post_ok(path):
    """POST /{domain}/{entity name}/{action}: only an entity in ENTITIES and an action
    its domain takes, so a renamed entity or a misspelled URL fails the tests."""
    parts = path.split("/")
    if len(parts) != 4 or parts[1] not in ENTITY_ACTIONS or parts[3] not in ENTITY_ACTIONS[parts[1]]:
        return False
    return any(e["domain"] == parts[1] and e["name"] == unquote(parts[2]) for e in ENTITIES)


# Entity states changed since the mock started, in order; each open /events
# stream sends the ones it hasn't yet, as the panel pushes a change to every
# listener.
PUSHED = []


def entity_post(path, q):
    """A control post the way ESPHome's web server applies it: the switch turns,
    the number takes value=, the select takes option= from its options. True when
    something changed; a value the entity doesn't take changes nothing."""
    domain, name, action = path.split("/")[1:]
    e = next(x for x in ENTITIES if x["domain"] == domain and x["name"] == unquote(name))
    if domain == "switch":
        on = action == "turn_on" or (action == "toggle" and e["state"] != "ON")
        e["state"], e["value"] = ("ON", True) if on else ("OFF", False)
    elif domain == "number":
        m = re.fullmatch(r"-?\d+(\.\d+)?", q.get("value", ""))
        if not m or not float(e["min_value"]) <= float(m.group()) <= float(e["max_value"]):
            return False
        e["state"] = e["value"] = m.group()
    elif domain == "select":
        if q.get("option") not in e["option"]:
            return False
        e["state"] = e["value"] = q["option"]
    else:
        return True
    PUSHED.append(copy.deepcopy(e))
    return True

STATE = copy.deepcopy(INITIAL)
FAIL = set()
LOG = []
# The saved Wi-Fi networks as GET /gameday/wifi lists them, the one tried first
# first. None: firmware without the list.
WIFI_INITIAL = [{"ssid": "Home", "connected": True}, {"ssid": "iPhone", "connected": False}]
WIFI = {"list": copy.deepcopy(WIFI_INITIAL)}
# Live games' own list (the live mask), kept apart from the state's live list,
# which shows every league instead while no favorite is picked.
LIVE_LIST = [[]]


PIC_FILES = {}  # slot (1 to 4) -> the bytes uploaded
PIC_MAX = 64 * 1024


def add_picture(raw):
    """PictureCard::add(): the first free slot, with the checks the page can hit
    in tests (pic_check() itself is in the host tests)."""
    if not raw:
        return 400, {"error": "That file is empty."}
    if len(raw) > PIC_MAX:
        return 400, {"error": "That file is too big; a picture can be up to 64 KB."}
    if raw[:8] == b"\x89PNG\r\n\x1a\n" and raw[12:16] == b"IHDR":
        kind, w, h, frames = "png", int.from_bytes(raw[16:20], "big"), int.from_bytes(raw[20:24], "big"), 1
    elif raw[:6] in (b"GIF87a", b"GIF89a"):
        kind, w, h = "gif", int.from_bytes(raw[6:8], "little"), int.from_bytes(raw[8:10], "little")
        frames = max(1, raw.count(b"\x21\xf9\x04"))
    else:
        return 400, {"error": "Pictures can be PNG or GIF files."}
    if w > 128 or h > 64:
        return 400, {"error": "Pictures can be up to 128 by 64 pixels."}
    free = [s for s in range(1, 5) if s not in PIC_FILES]
    if not free:
        return 400, {"error": "The panel keeps 4 pictures. Delete one to add another."}
    slot = free[0]
    PIC_FILES[slot] = raw
    STATE["pics"] = [p for p in STATE["pics"] if p["s"] != slot] + [
        {"s": slot, "k": kind, "w": w, "h": h, "f": frames, "b": len(raw), "id": 1000 + len(LOG)}]
    STATE["pics"].sort(key=lambda p: p["s"])
    STATE["picroom"] = 0 if len(PIC_FILES) == 4 else PIC_MAX
    return 200, {"ok": True, "slot": slot}


def reset_state(overrides):
    PIC_FILES.clear()
    STATE.clear()
    STATE.update(copy.deepcopy(INITIAL))
    STATE.update(overrides)
    # A null override drops the key, like firmware that doesn't send it.
    for k in [k for k, v in STATE.items() if v is None]:
        del STATE[k]
    LIVE_LIST[0] = [] if STATE.get("live_auto", True) else list(STATE.get("live", []))


def atoi(v):
    m = re.match(r"\s*[+-]?\d+", v)
    return int(m.group()) if m else 0


def whole_int(v):
    # set_request.h parse_whole_int: an optional minus and up to six digits, nothing else
    return re.fullmatch(r"-?[0-9]{1,6}", v) is not None


def clamp(v, lo, hi):
    return min(LIMITS[hi], max(LIMITS[lo], v))


# message.h's emoji(): left out of a message rather than drawn as '?'.
EMOJI = re.compile("[\U0001F000-\U0001FAFF\u2600-\u27BF\u2B00-\u2BFF\uFE00-\uFE0F\u200D\u20E3\U000E0000-\U000E007F]")


def message_clean(v, cap, lines):
    """message_clean() in components/gameday/message.h."""
    v = EMOJI.sub("", v).replace("\t", " ")
    v = "".join(c for c in v if c == "\n" or (ord(c) >= 0x20 and ord(c) != 0x7F))
    out = ("\n" if lines else " ").join(ln.strip(" ") for ln in v.split("\n") if ln.strip(" "))
    out = re.sub(r"(?<=[ \n]) +", "", out)
    raw = out.encode("utf-8")
    if len(raw) > cap:
        out = raw[:cap].decode("utf-8", "ignore").rstrip(" \n")
    return out


def msg_url_ok(v):
    """msg_url_ok() in components/gameday/message.h."""
    if not v:
        return True
    rest = v[7:] if v.startswith("http://") else v[8:] if v.startswith("https://") else ""
    return len(v) <= LIMITS["kMsgUrlMax"] and bool(rest) and all(0x20 < ord(c) < 0x7F for c in v)


def hook_url_ok(v):
    """hook_url_ok() in components/gameday/hook.h: plain http only."""
    if not v:
        return True
    if len(v) > LIMITS["kHookUrlMax"] or not v.startswith("http://") or len(v) == 7 or v[7] in "/?#":
        return False
    return all(0x20 < ord(c) < 0x7F for c in v)


def parse_team_ref(v):
    """parse_team_ref() plus known_team(): (league, id) of a team in teams.h, else None."""
    lg, colon, tid = v.partition(":")
    m = re.match(r"\s*\d+", tid)
    ref = (lg, int(m.group())) if colon and lg in LEAGUES and m else None
    return ref if ref in TEAMS else None


def timezone_index(v):
    if v.isdigit():
        return int(v) if int(v) < len(TZ_ROWS) else -1
    return next((i for i, (name, _posix, iana) in enumerate(TZ_ROWS) if v in (name, iana)), -1) if v else -1


def live_auto_leagues():
    """live_leagues() with a mask of 0: the favorites' leagues, NFL with none."""
    used = {t["l"] for t in STATE["favs"] if t}
    return [lg for lg in LEAGUES if lg in used] or ["nfl"]


def no_team_default():
    """set_request.h no_team_default(): Favorites with none set."""
    return STATE["mode"] == 4 and not any(STATE["favs"])


def sport_rows():
    """sport_rows_(): a row per sport among the favorites, with two sports or more."""
    have = {LEAGUE_SPORT.get(t["l"]) for t in STATE["favs"] if t}
    rows = [k for k in SPORT_KEYS if k in have]
    return rows if len(rows) >= 2 else []


def state_doc():
    """rebuild_state_() works the leagues out each time: every league with no
    favorite picked (no_team_leagues()), else the auto leagues from the favorites.
    The sport rows come from the favorites too."""
    if "sport" in STATE:
        STATE["sports"] = sport_rows()
        if STATE["sport"] not in STATE["sports"]:
            STATE["sport"] = ""
    if no_team_default():
        STATE["live"] = list(LEAGUES)
    elif STATE.get("live_auto"):
        STATE["live"] = live_auto_leagues()
    else:
        STATE["live"] = list(LIVE_LIST[0])
    if "cards" in STATE:
        on = cards_on()
        STATE["cards"] = []
        for cid, name, _default, hint in CARDS:
            STATE["cards"].append({"id": cid, "name": name, "on": on[cid], "ok": card_ok(cid), "hint": hint})
    STATE["lists"] = [norm_list(pl) for pl in STATE["lists"]]
    STATE.setdefault("sched", 0)
    STATE["remote"] = [{"v": b.get("v", "fav"), "gone": remote_gone(i + 1, b.get("v", "fav"))}
                       for i, b in enumerate((STATE.get("remote") or [{}] * 4)[:4])]
    return STATE


def cards_on():
    """card_on() for each card: its own key for the cards that kept one, else the list."""
    listed = {c["id"]: c["on"] for c in STATE.get("cards", []) if "id" in c}
    key = OWN_SWITCH_CARDS.get
    return {cid: STATE[key(cid)] if key(cid) in STATE else listed.get(cid, default)
            for cid, _name, default, _hint in CARDS}


def card_ok(cid):
    """card_ok_(): the custom countdown needs a day, the message card text or a
    list, Top 25 a college football or men's basketball team (top25_slots_)."""
    if cid == "custom":
        return bool(STATE.get("cddate"))
    if cid == "message":
        return bool(STATE.get("msg") or STATE.get("msgurl"))
    if cid == "picture":
        return bool(STATE.get("pics"))
    if cid == "weather":
        return bool((STATE.get("wx") or {}).get("place"))
    if cid == "top25":
        leagues = {t["l"] for t in STATE["favs"] if t} | (set(STATE.get("live", [])) if STATE["mode"] == 5 else set())
        return not no_team_default() and bool(leagues & {"ncaa", "mcbb"})
    return True


def set_cards(v):
    """cards= (cards.h parse_cards): the cards named, on for 1 and off for 0;
    an id this firmware lacks, or a value other than 0 or 1, is left out."""
    on = cards_on()
    for pair in v.split(","):
        cid, _colon, val = pair.partition(":")
        if cid in on and val in ("0", "1"):
            on[cid] = val == "1"
    for cid, key in OWN_SWITCH_CARDS.items():
        if key in STATE:
            STATE[key] = on[cid]
    STATE["cards"] = [{"id": cid, "on": on[cid]} for cid, _name, _default, _hint in CARDS]


def fav_refs():
    return [(t["l"], t["id"]) if t else None for t in STATE["favs"]]


def remap_lists(before):
    """remap_lists_(): each playlist keeps its teams wherever the favorites moved them."""
    after = fav_refs()
    for pl in STATE["lists"]:
        teams = [before[i] for i in pl["f"] if i < len(before) and before[i]]
        pl["f"] = [i for i, r in enumerate(after) if r and r in teams]
    # A sport row the favorites have no row for now stops.
    if STATE.get("sport") and STATE["sport"] not in sport_rows():
        STATE["sport"] = ""


def remote_parse(v):
    """remote_parse(): the text of a button's target, canonical, or None when the panel refuses it."""
    if v in ("fav", "all", "none"):
        return v
    if v.startswith("team:"):
        ref = parse_team_ref(v[5:])
        return f"team:{ref[0]}:{ref[1]}" if ref else None
    if v.startswith("sport:"):
        return v if v[6:] in SPORT_KEYS else None
    if v.startswith("list:") and len(v) > 5:
        return "list:" + v[5:].encode("utf-8")[:16].decode("utf-8", "ignore")
    return None


def remote_gone(button, v):
    """remote_resolve().gone: the button's team, playlist or sport row is gone, so it does its default."""
    if v.startswith("team:"):
        ref = parse_team_ref(v[5:])
        return ref not in fav_refs()
    if v.startswith("list:"):
        return v[5:] not in [pl["n"] for pl in STATE["lists"]]
    if v.startswith("sport:"):
        return v[6:] not in sport_rows()
    return False


def list_time(v):
    """list_parse_time(): "11:00" -> "HH:MM" as the state writes it, None for anything else."""
    m = re.fullmatch(r"(\d{1,2}):(\d{2})", v)
    if not m or int(m[1]) > 23 or int(m[2]) > 59:
        return None
    return f"{int(m[1]):02d}:{m[2]}"


def norm_list(pl):
    """A playlist with every option the state document writes (a test's override may leave them out)."""
    out = {"n": pl["n"], "f": pl["f"], "days": pl.get("days", ""), "start": pl.get("start", "00:00"),
           "end": pl.get("end", "00:00"), "only": bool(pl.get("only"))}
    if "today" in pl:
        out["today"] = pl["today"]
    return out


def list_opt_token(tok, opt):
    """list_opt_token(): one @ token of a playlist's teams into its options."""
    if tok.startswith("@d:"):
        opt["days"] = "".join(d for d in "0123456" if d in tok[3:])
    elif tok.startswith("@t:"):
        start, dash, end = tok[3:].partition("-")
        if dash and list_time(start) and list_time(end):
            opt["start"], opt["end"] = list_time(start), list_time(end)
    elif tok == "@only":
        opt["only"] = True
    elif tok in ("@today:1", "@today:0"):
        opt["today"] = tok == "@today:1"


def list_opt_text(pl):
    """list_opt_text(): a playlist's options back as their tokens."""
    pl = norm_list(pl)
    out = ""
    if pl["days"]:
        out += ",@d:" + pl["days"]
        if pl["start"] != pl["end"]:
            out += f",@t:{pl['start']}-{pl['end']}"
    if pl["only"]:
        out += ",@only"
    if "today" in pl:
        out += ",@today:1" if pl["today"] else ",@today:0"
    return out


def parse_lists(v):
    """parse_lists() in components/gameday/playlists.h, with list_mask() against the favorites."""
    out = []
    for part in v.split(";") if v else []:
        if len(out) >= LIMITS["kListMax"]:
            break
        name, _eq, refs = part.partition("=")
        name = message_clean(name.replace("=", ""), LIMITS["kListNameMax"], False)
        if not name:
            continue
        teams, opt = [], {}
        for r in refs.split(",") if refs else []:
            if r.startswith("@"):
                list_opt_token(r, opt)
                continue
            ref = parse_team_ref(r)
            if ref and ref not in teams and len(teams) < FAV_MAX:
                teams.append(ref)
        out.append(norm_list({"n": name, "f": [i for i, r in enumerate(fav_refs()) if r and r in teams], **opt}))
    return out


def night_time(v):
    """night_parse_time() in components/gameday/night.h, as the state document writes it."""
    m = re.fullmatch(r"(\d{1,2}):(\d{2})", v)
    if not m or int(m[1]) > 23 or int(m[2]) > 59:
        return None
    return f"{int(m[1]):02d}:{m[2]}"


def wx_lookup(town):
    """The lookup and first forecast read, answered at once. A town starting
    with "nowhere" is one Open-Meteo doesn't know."""
    if not town:
        STATE.pop("wx", None)
        return
    if town.lower().startswith("nowhere"):
        STATE["wx"] = {"place": "", "lat": 0, "lon": 0, "lookup": 200, "read": -1}
        return
    place = "Coppell, Texas" if town == "75019" else f"{town}, Texas"
    STATE["wx"] = {"place": place, "lat": 32.95, "lon": -97.02, "lookup": 200, "read": 200, "read_s": 60,
                   "temp": 71, "hi": 81, "lo": 61, "code": 3, "cond": "Cloudy", "night": False,
                   "days": [{"d": "Thu", "hi": 80, "lo": 66, "code": 61}, {"d": "Fri", "hi": 75, "lo": 67, "code": 2}]}


def apply_set(kv):
    """GamedayComponent::apply_set_ through set_request.h's parse_set."""
    hours = (STATE["nightstart"], STATE["nightend"])
    # apply_ops_: list= with lists= names the playlist playing after the change.
    with_lists = any(k == "lists" for k, _ in kv)
    keep_list = next((atoi(v) for k, v in kv if k == "list" and v[:1].isdigit() and atoi(v) <= LIMITS["kListMax"]), -1)
    # A sport row picked takes over list= in the same request.
    sport_pick = any(k == "sport" and v in SPORT_KEYS for k, v in kv)
    for k, v in kv:
        before = fav_refs()
        if k == "mode":
            # read_mode(): My team's 0 is Favorites.
            if whole_int(v) and 0 <= int(v) < LIMITS["kModeCount"]:
                STATE["mode"] = int(v) or 4
        elif k == "rotate":
            if whole_int(v):
                STATE["rotate"] = clamp(int(v), "kRotateMinMinutes", "kRotateMaxMinutes")
        elif k == "lockon":
            if whole_int(v):
                STATE["lockon"] = clamp(int(v), "kLockonMinMinutes", "kLockonMaxMinutes")
        elif k == "release":
            if whole_int(v):
                STATE["release"] = clamp(int(v), "kReleaseMinSeconds", "kReleaseMaxSeconds")
        elif k == "collide":
            STATE["collide"] = 1 if v == "1" else 0
        elif k == "favs":
            refs = []
            for part in v.split(",") if v else []:
                if len(refs) >= FAV_MAX:
                    break
                ref = parse_team_ref(part)
                if ref and ref not in refs:
                    refs.append(ref)
            # set_favorites_(): the list in order, then empty slots up to the four remote slots.
            STATE["favs"] = [team(*r) for r in refs] + [{}] * (4 - len(refs))
            remap_lists(before)
        elif k in ("fav1", "fav2", "fav3", "fav4"):
            ref = parse_team_ref(v)
            if ref is None and v not in ("none", ""):
                continue
            STATE["favs"][int(k[3]) - 1] = team(*ref) if ref else {}
            remap_lists(before)
        elif k == "lists":
            # set_lists_(): the playlist being played stays chosen by its name.
            active = STATE["lists"][STATE["list"] - 1]["n"] if STATE["list"] else ""
            STATE["lists"] = parse_lists(v)
            STATE["list"] = next((i + 1 for i, pl in enumerate(STATE["lists"]) if active and pl["n"] == active), 0)
            if 0 <= keep_list <= len(STATE["lists"]):
                STATE["list"] = keep_list
                STATE["sport"] = ""
        elif k == "list":
            # set_active_list_() plays it in Favorite teams; with lists= set_lists_() took it,
            # and with a sport row picked set_sport_row_() does.
            if not with_lists and not sport_pick and v[:1].isdigit() and atoi(v) <= len(STATE["lists"]):
                STATE["list"] = atoi(v)
                STATE["sport"] = ""
                STATE["mode"] = 4
        elif k == "sport":
            # set_sport_row_() plays a row the favorites give; none stops the one
            # playing unless list= says what plays.
            if (v in sport_rows()) if v != "none" else (keep_list < 0 and STATE.get("sport")):
                STATE["list"] = 0
                STATE["sport"] = "" if v == "none" else v
                STATE["mode"] = 4
        elif k in ("today", "jump", "banner", "favtake"):
            STATE[k] = v == "1"
        elif k == "live":
            keys = [lg for lg in LEAGUES if lg in v.split(",")]
            STATE["live_auto"] = not keys
            LIVE_LIST[0] = keys
        elif k == "tz":
            i = timezone_index(v)
            if i >= 0:
                STATE["tz"], STATE["tz_name"] = i, TZS[i]
        elif k == "tzauto":
            STATE["tz_auto"] = v == "1"
        elif k in ("down", "count", "match", "play", "odds", "leaders", "opp", "best", "bootaddr", "autoupd", "beta"):
            STATE[k] = v == "1"
        elif k == "tiles":
            if v in ("1", "2", "4", "auto"):
                STATE["tiles"] = v
        elif k == "panels":
            if 1 <= atoi(v) <= MAX_COLS:
                STATE["panels"] = atoi(v)
        elif k in ("countdown", "top25", "news", "gameleaders"):
            STATE[k] = v == "1"
        elif k == "cards":
            if "cards" in STATE:
                set_cards(v)
        elif k == "cdname":
            STATE["cdname"] = countdown_name(v)
        elif k == "cddate":
            day = countdown_date(v)
            if day is not None:
                STATE["cddate"] = day
        elif k == "msg":
            STATE["msg"] = message_clean(v, LIMITS["kMsgTextMax"], True)
        elif k == "town":
            # set_town(): a new town turns the card on and is looked up; the
            # same town again leaves the switch alone.
            town = message_clean(v.replace("\ufffd", ""), LIMITS["kTownMax"], False)
            if town != STATE.get("town", ""):
                STATE["town"], STATE["wxon"] = town, bool(town)
                wx_lookup(town)
        elif k == "wxon":
            STATE["wxon"] = v == "1"
        elif k == "nick":
            # utf8_only(): parse_qs turns bytes that aren't UTF-8 into U+FFFD; the firmware drops them.
            STATE["nick"] = message_clean(v.replace("\ufffd", ""), LIMITS["kNickMax"], False)
        elif k == "msgurl":
            if msg_url_ok(v):
                STATE["msgurl"] = v
        elif k == "msgevery":
            if v[:1].isdigit():
                STATE["msgevery"] = min(LIMITS["kMsgEveryMax"], atoi(v))
        elif re.fullmatch(r"remote[1-4]", k):
            # set_remote_(): a playlist's name as the playlists keep it.
            v = remote_parse(v)
            if v is None:
                continue
            if v.startswith("list:"):
                name = message_clean(v[5:].replace(";", "").replace("=", ""), LIMITS["kListNameMax"], False)
                v = "list:" + name if name else "fav"
            STATE["remote"][int(k[6]) - 1] = {"v": v}
        elif k == "hook":
            if hook_url_ok(v):
                STATE["hook"] = v
        elif k == "pregame":
            if whole_int(v):
                STATE["pregame"] = max(0, min(LIMITS["kPregameMax"], int(v)))
        elif k in ("night", "nightscreen"):
            STATE[k] = v == "1"
        elif k in ("nightstart", "nightend"):
            if night_time(v):
                STATE[k] = night_time(v)
        elif k == "nightlevel":
            STATE["nightlevel"] = min(255, max(1, atoi(v)))
        elif k == "wake":
            STATE["wake"] = v == "1"
        elif k == "wakelead":
            if whole_int(v):
                STATE["wakelead"] = clamp(int(v), "kWakeLeadMin", "kWakeLeadMax")
        elif k == "after":
            if v in AFTER:
                STATE["after"] = v
    # The same start and end keeps the hours it had (night_keep_hours).
    if STATE["nightstart"] == STATE["nightend"]:
        STATE["nightstart"], STATE["nightend"] = hours


def entity(name):
    return next(e for e in ENTITIES if e["name"] == name)


def team_text(t):
    return f"{t['l']}:{t['id']}" if t and t.get("id") else ""


def settings_doc():
    """settings_from_state(): the backup GET /gameday/settings writes."""
    s = state_doc()
    out = {"schema": SETTINGS_SCHEMA, "version": s["version"], "name": s["name"]}
    for k in SETTINGS_ORDER:
        if k == "favs":
            favs = [team_text(t) for t in s["favs"]]
            while favs and not favs[-1]:
                favs.pop()
            out["favs"] = favs
        elif k == "lists":
            # The state's teams are places in favs; the backup names them.
            out["lists"] = []
            for pl in s.get("lists", []):
                e = {"n": pl["n"], "t": [team_text(s["favs"][i]) for i in pl["f"] if team_text(s["favs"][i])]}
                # Its options, only what is set (settings_from_state).
                if pl.get("days"):
                    e["days"] = pl["days"]
                    if pl["start"] != pl["end"]:
                        e["start"], e["end"] = pl["start"], pl["end"]
                if pl.get("only"):
                    e["only"] = True
                if "today" in pl:
                    e["today"] = pl["today"]
                out["lists"].append(e)
        elif k == "live":
            # Live games' own list, not the state's (every league with no favorite picked).
            out["live"] = list(LIVE_LIST[0])
        elif k == "tz":
            out["tz"] = s["tz_name"]
        elif k == "tzauto":
            out["tzauto"] = s["tz_auto"]
        elif k == "sport":
            if "sport" in s:
                out["sport"] = s["sport"] or "none"
        elif k == "cards":
            if "cards" in s:
                out["cards"] = {c["id"]: c["on"] for c in s["cards"]}
        elif re.fullmatch(r"remote[1-4]", k):
            out[k] = s["remote"][int(k[6]) - 1]["v"]
        elif k in s:
            out[k] = s[k]
    if s.get("panels"):
        out["panels"] = s["panels"]
    out["brightness"] = int(entity("Brightness")["value"])
    out["scroll"] = int(entity("Scroll Speed")["value"])
    return out


def import_settings(doc):
    """parse_settings() and apply_settings_(): (code, reply). Nothing changes on a 400."""
    def fail(why):
        return 400, {"error": why}

    def is_int(v):
        return isinstance(v, int) and not isinstance(v, bool)

    def team_ref(v):
        """settings_team(): 1 known, 0 empty, -1 unknown here, -2 not a team."""
        if v in ("", "none"):
            return 0
        lg, colon, tid = v.partition(":")
        m = re.match(r"\s*\d+", tid)
        if not colon or not lg or not m or int(m.group()) == 0:
            return -2
        return 1 if parse_team_ref(v) else -1

    if not isinstance(doc, dict) or not is_int(doc.get("schema")) or doc["schema"] < 1:
        return fail("Not a settings backup.")
    if doc["schema"] > SETTINGS_SCHEMA:
        return fail("This backup is from newer firmware. Update the panel, then restore it.")
    kv, skipped, kept, favs, numbers = [], [], [], None, {}
    # The nickname and layout come back only onto the panel the file is from.
    same_panel = isinstance(doc.get("name"), str) and doc["name"] == STATE.get("name")
    ranges = {"rotate": ("kRotateMinMinutes", "kRotateMaxMinutes"), "lockon": ("kLockonMinMinutes", "kLockonMaxMinutes"),
              "release": ("kReleaseMinSeconds", "kReleaseMaxSeconds"),
              "wakelead": ("kWakeLeadMin", "kWakeLeadMax")}
    for k in SET_KEYS:
        if k not in doc or doc[k] is None:
            continue
        v, kind = doc[k], SETTINGS_TYPES.get(k)
        if k == "lists":
            # The PLAYLISTS case: into lists= text, unknown teams, nameless
            # playlists and any past kListMax left out and listed.
            bad = 'lists should be playlists like {"n": "Football", "t": ["nfl:6"]}.'
            if not isinstance(v, list):
                return fail(bad)
            parts = []
            for e in v:
                if not isinstance(e, dict) or not isinstance(e.get("n"), str) or not isinstance(e.get("t"), list):
                    return fail(bad)
                name = message_clean(e["n"].replace(";", "").replace("=", ""), LIMITS["kListNameMax"], False)
                refs = []
                for t in e["t"]:
                    if not isinstance(t, str):
                        return fail(bad)
                    r = team_ref(t)
                    if r in (-2, 0):
                        return fail("lists should list teams like nfl:6.")
                    if r == -1:
                        skipped.append(f"lists={name}:{t}")
                        continue
                    refs.append(t)
                opt = {}
                if e.get("days") is not None:
                    if not isinstance(e["days"], str):
                        return fail('lists days should be digits like "06" (0 Sunday to 6 Saturday).')
                    list_opt_token("@d:" + e["days"], opt)
                if e.get("start") is not None or e.get("end") is not None:
                    if not isinstance(e.get("start"), str) or not isinstance(e.get("end"), str):
                        return fail('lists start and end should be times like "11:00".')
                    list_opt_token("@t:" + e["start"] + "-" + e["end"], opt)
                    if "start" not in opt and (e["start"], e["end"]) not in (("00:00", "00:00"), ("0:00", "0:00")):
                        skipped.append(f"lists={name}:{e['start']}-{e['end']}")
                for b in ("only", "today"):
                    if e.get(b) is not None:
                        if not isinstance(e[b], bool):
                            return fail(f"lists {b} should be true or false.")
                        if b == "only" and e[b] or b == "today":
                            opt[b] = e[b]
                if not name or len(parts) == LIMITS["kListMax"]:
                    skipped.append("lists=" + name)
                    continue
                parts.append(name + "=" + ",".join(refs) + list_opt_text({"n": name, "f": [], **opt}))
            kv.append(("lists", ";".join(parts)))
            continue
        if k == "cards":
            # The CARDS case: into cards= text, a card this firmware lacks left out and listed.
            bad = 'cards should be cards like {"next": true}.'
            if not isinstance(v, dict) or any(not isinstance(e, bool) for e in v.values()):
                return fail(bad)
            ids = [cid for cid, _name, _default, _hint in CARDS]
            skipped += ["cards=" + cid for cid in v if cid not in ids]
            pairs = [f"{cid}:{1 if on else 0}" for cid, on in v.items() if cid in ids]
            if pairs:
                kv.append(("cards", ",".join(pairs)))
            continue
        if kind == "Bool":
            if not isinstance(v, bool):
                return fail(k + " should be true or false.")
            if k == "beta" and "beta" not in STATE or k == "autoupd" and "autoupd" not in STATE:
                skipped.append(k)
                continue
            kv.append((k, "1" if v else "0"))
        elif kind == "Int":
            if not is_int(v):
                return fail(k + " should be a whole number.")
            lo, hi = {"mode": (0, LIMITS["kModeCount"] - 1), "panels": (1, MAX_COLS), "collide": (0, 1),
                      "msgevery": (0, LIMITS["kMsgEveryMax"]), "nightlevel": (1, 255),
                      "pregame": (0, LIMITS["kPregameMax"]),
                      "list": (0, LIMITS["kListMax"])}.get(k, (None, None))
            if k in ranges:
                lo, hi = LIMITS[ranges[k][0]], LIMITS[ranges[k][1]]
            # Out of range is left out and listed (newer firmware may take more).
            if lo is not None and not lo <= v <= hi:
                skipped.append(f"{k}={v}")
                continue
            if k == "panels" and not same_panel:
                if v != STATE.get("panels"):
                    kept.append(k)
                continue
            kv.append((k, str(v)))
        elif kind == "Text":
            if not isinstance(v, str):
                return fail(k + " should be text.")
            if re.fullmatch(r"fav[1-4]", k):
                r = team_ref(v)
                if r == -2:
                    return fail(k + " should be a team like nfl:6, or none.")
                if r == -1:
                    skipped.append(f"{k}={v}")
                    continue
                kv.append((k, v if r == 1 else "none"))
            elif (k == "tz" and timezone_index(v) < 0) or (k == "msgurl" and not msg_url_ok(v)) or (
                    k == "hook" and not hook_url_ok(v)) or (k == "sport" and v != "none" and v not in SPORT_KEYS) or (
                    k == "after" and v not in AFTER) or (
                    re.fullmatch(r"remote[1-4]", k) and remote_parse(v) is None):
                skipped.append(f"{k}={v}")
            elif k == "nick" and not same_panel:
                if v != STATE.get("nick", ""):
                    kept.append(k)
            elif k in ("nightstart", "nightend") and not night_time(v):
                return fail(k + " isn't a value the panel takes.")
            else:
                kv.append((k, v))
        elif kind == "List":
            if not isinstance(v, list):
                return fail(k + " should be a list.")
            if any(not isinstance(e, str) for e in v):
                return fail(k + " should be a list of text.")
            if k == "live":
                skipped += [f"live={e}" for e in v if e not in LEAGUES]
                kv.append(("live", ",".join(e for e in v if e in LEAGUES)))
                continue
            favs = []
            for e in v:
                if len(favs) == FAV_MAX:
                    if e not in ("", "none"):
                        skipped.append("favs=" + e)
                    continue
                r = team_ref(e)
                if r == -2:
                    return fail("favs should list teams like nfl:6.")
                if r == -1:
                    skipped.append("favs=" + e)
                ref = parse_team_ref(e) if r == 1 else None
                if ref and ref in favs:
                    return fail(f"favs lists {e} twice.")
                favs.append(ref)
        else:
            skipped.append(k)
    # The same start and end: the end is left out (settings_doc.h).
    hours = dict(p for p in kv if p[0] in ("nightstart", "nightend"))
    if len(hours) == 2 and night_time(hours["nightstart"]) == night_time(hours["nightend"]):
        kv = [p for p in kv if p[0] != "nightend"]
        skipped.append("nightend=" + night_time(hours["nightend"]))
    for name, ent, lo, hi in (("brightness", "Brightness", 1, 255), ("scroll", "Scroll Speed", 1, 10)):
        if name in doc and doc[name] is not None:
            if not is_int(doc[name]):
                return fail(name + " should be a whole number.")
            if not lo <= doc[name] <= hi:
                skipped.append(f"{name}={doc[name]}")
                continue
            numbers[ent] = doc[name]
    known = set(SET_KEYS) | {"schema", "version", "name", "brightness", "scroll"}
    skipped += [k for k in doc if k not in known]
    restart = any(k == "panels" and int(v) != STATE.get("panels") for k, v in kv)
    # Applied in kSetKeys order; favs slot by slot, keeping empty slots.
    apply_set([p for p in kv if SET_KEYS.index(p[0]) < SET_KEYS.index("favs")])
    if favs is not None:
        STATE["favs"] = [team(*r) if r else {} for r in favs] + [{}] * (4 - len(favs))
    apply_set([p for p in kv if SET_KEYS.index(p[0]) > SET_KEYS.index("favs")])
    for ent, value in numbers.items():
        entity(ent)["value"] = entity(ent)["state"] = str(value)
    return 200, {"ok": True, "restart": restart, "skipped": skipped, "kept": kept}


def countdown_name(v):
    """countdown.h's countdown_name(): trimmed, no control characters or emoji, at most kCountdownNameMax bytes."""
    v = "".join(c for c in EMOJI.sub("", v) if c >= " " and c != "\x7f")
    raw = re.sub(r" {2,}", " ", v).strip(" ").encode("utf-8")
    return raw[:COUNTDOWN_NAME_MAX].decode("utf-8", "ignore").rstrip(" ")


def countdown_date(v):
    """countdown.h's parse_countdown_date(): "" for none, the day as given, or None when refused."""
    if v in ("", "none"):
        return ""
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", v):
        return None
    try:
        day = datetime.date.fromisoformat(v)
    except ValueError:
        return None
    return v if 2020 <= day.year <= 2099 else None


def selftest():
    """Runs tests/fixtures/set_cases.json through apply_set and state_doc."""
    fx = json.loads(FIXTURE.read_text(encoding="utf-8"))

    def to_team(ref):
        return team(*parse_team_ref(ref)) if ref else {}

    def compact(key, value):
        if key == "team":
            return f'{value["l"]}:{value["id"]}' if value else ""
        if key == "favs":
            return [compact("team", t) for t in value]
        if key == "cards":
            # set_cases.json lists the cards as the backup does: {"next": true, ...}.
            return {c["id"]: c["on"] for c in value}
        return value

    bad = 0
    for case in fx["cases"]:
        start = {**fx["base"], **case.get("start", {})}
        start["favs"] = [to_team(r) for r in start["favs"]]
        if "cards" in start:
            start["cards"] = [{"id": cid, "on": on} for cid, on in start["cards"].items()]
        reset_state(start)
        kv = [(k, v) for key in SET_KEYS for k, v in case["set"] if k == key]
        status = 200 if kv else 400
        if kv:
            apply_set(kv)
        got = state_doc()
        if status != case.get("status", 200):
            bad += 1
            print(f"FAIL {case['name']}: status {status}")
        for key, want in case["expect"].items():
            have = compact(key, got.get(key))
            if json.dumps(have) != json.dumps(want):
                bad += 1
                print(f"FAIL {case['name']}: {key} is {json.dumps(have)}, expected {json.dumps(want)}")
    reset_state({})
    print(f"{len(fx['cases'])} set cases, {bad} failures")

    # The export of the API fixture's state is the API fixture's backup, as in
    # tests/test_settings.cpp, and the backup comes back with nothing left out.
    api = REPO / "tests/fixtures/api"
    fixture_state = json.loads((api / "state.json").read_text(encoding="utf-8"))
    want = json.loads((api / "settings.json").read_text(encoding="utf-8"))
    reset_state(fixture_state)
    STATE["live"] = fixture_state["live"]
    LIVE_LIST[0] = []
    entity("Brightness")["value"], entity("Scroll Speed")["value"] = str(want["brightness"]), str(want["scroll"])
    have = settings_doc()
    if have != want or list(have) != list(want):
        bad += 1
        print("FAIL the export of fixtures/api/state.json isn't fixtures/api/settings.json:\n  "
              + json.dumps(have) + "\n  " + json.dumps(want))
    code, reply = import_settings(want)
    if code != 200 or reply["skipped"] or settings_doc() != want:
        bad += 1
        print(f"FAIL the settings fixture didn't come back whole: {code} {json.dumps(reply)}")
    for doc in ({"schema": 1, "mode": "4"}, {"schema": 2}, {"schema": 1, "favs": ["nfl:6", "nfl:6"]},
                {"schema": 1, "brightness": 40, "scroll": "5"}):
        before = json.dumps(STATE, sort_keys=True)
        code, _ = import_settings(doc)
        if code != 400 or json.dumps(STATE, sort_keys=True) != before:
            bad += 1
            print(f"FAIL {json.dumps(doc)} wasn't refused whole")
    reset_state({})
    for e in ENTITIES:
        e.update(next(x for x in ENTITIES_INITIAL if x["id"] == e["id"]))
    print(f"settings backup: {bad} failures in all")

    for path, want in (("/button/Reset%20Wi-Fi/press", True), ("/switch/Power/turn_off", True),
                       ("/number/Scroll%20Speed/set", True), ("/update/Firmware/install", True),
                       ("/button/Reset%20WiFi/press", False), ("/button/Reboot/turn_on", False),
                       ("/switch/Brightness/turn_on", False), ("/text_sensor/IP/set", False)):
        if entity_post_ok(path) != want:
            bad += 1
            print(f"FAIL POST {path} should {'pass' if want else 'get a 404'}")
    # The names the mock takes are the firmware's, so renaming one there fails here.
    yaml = (REPO / "firmware/gameday-common.yaml").read_text(encoding="utf-8")
    yaml += (REPO / "firmware/pages/effects.yaml").read_text(encoding="utf-8")
    for e in ENTITIES:
        if e["domain"] in ENTITY_ACTIONS and f'name: "{e["name"]}"' not in yaml:
            bad += 1
            print(f"FAIL {e['id']} isn't an entity in firmware/gameday-common.yaml or pages/effects.yaml")
    # Control posts change the entity and are pushed to the event stream.
    entity_post("/select/Select%20Page/set", {"option": "Effects"})
    entity_post("/number/Effect%20Speed/set", {"value": "40"})
    if entity("Select Page")["value"] != "Effects" or entity("Effect Speed")["value"] != "40" or len(PUSHED) != 2:
        bad += 1
        print("FAIL an entity post didn't change the entity or wasn't pushed")
    if entity_post("/select/Effect/set", {"option": "Lava Lamp"}) or entity_post("/number/Effect%20Speed/set", {"value": "300"}):
        bad += 1
        print("FAIL an option or value the entity doesn't take was applied")
    for e in ENTITIES:
        e.update(copy.deepcopy(next(x for x in ENTITIES_INITIAL if x["id"] == e["id"])))
    PUSHED.clear()
    print(f"entity URLs: {bad} failures in all")
    return bad == 0


# The demo build's /gameday/demo (components/gameday/demo.cpp), with the
# stock matchups and made-up teams from demo.h.
DEMO_KEYS = ("do", "kind", "pro_home", "pro_away", "college_home", "college_away", "madeup")
DEMO_INITIAL = {"kind": "off", "madeup": False,
                "picks": {"pro_home": ("nfl", 6), "pro_away": ("nfl", 21),
                          "college_home": ("ncaa", 194), "college_away": ("ncaa", 130)}}
DEMO = copy.deepcopy(DEMO_INITIAL)
MADE_UP = {"pro": ({"abbr": "HBR", "name": "Harbor City Hawks"}, {"abbr": "IRN", "name": "Iron Valley Miners"}),
           "college": ({"abbr": "SUM", "name": "Summit State Peaks"}, {"abbr": "CPT", "name": "Capitol Tech Comets"})}


def demo_doc():
    doc = {"kind": DEMO["kind"], "playing": DEMO["kind"] != "off", "madeup": DEMO["madeup"]}
    for kind in ("pro", "college"):
        side = {}
        for end in ("home", "away"):
            lg, tid = DEMO["picks"][kind + "_" + end]
            abbr, name = TEAMS[(lg, tid)]
            side[end] = {"l": lg, "id": tid, "abbr": abbr, "name": name}
        side["madeup_home"], side["madeup_away"] = MADE_UP[kind]
        doc[kind] = side
    return doc


def apply_demo(q):
    for k in ("pro_home", "pro_away", "college_home", "college_away"):
        if k in q:
            lg, _, tid = q[k].partition(":")
            if (lg, int(tid or 0)) in TEAMS:
                DEMO["picks"][k] = (lg, int(tid))
    if "madeup" in q:
        DEMO["madeup"] = q["madeup"] == "1"
    if q.get("do") == "start":
        DEMO["kind"] = "college" if q.get("kind") == "college" else "pro"
    elif q.get("do") == "stop":
        DEMO["kind"] = "off"


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, ctype, body):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def _json(self, code, doc):
        self._send(code, "application/json", json.dumps(doc))

    def _gone(self, path):
        """True when the panel is set to not answer this path; the connection closes unanswered."""
        if path in ("/_reset", "/_log"):
            return False
        if "down" in FAIL or ("events" in FAIL and path == "/events"):
            self.close_connection = True
            return True
        return False

    def do_GET(self):
        u = urlparse(self.path)
        if self._gone(u.path):
            return
        if u.path == "/":
            js = "/0.js?demo" if "demo" in parse_qs(u.query, keep_blank_values=True) else "/0.js"
            html = ("<!DOCTYPE html><html><head><meta charset=UTF-8><link rel=icon href=data:>"
                    "<link rel=stylesheet href=/0.css></head><body><script src=" + js + "></script></body></html>")
            return self._send(200, "text/html", html)
        if u.path == "/0.js":
            bundle = "bundle-demo.js" if "demo" in parse_qs(u.query, keep_blank_values=True) else "bundle.js"
            return self._send(200, "text/javascript", (WEB / bundle).read_bytes())
        if u.path == "/gameday/demo":
            return self._json(200, demo_doc())
        if u.path == "/0.css":
            return self._send(200, "text/css", (WEB / "app.css").read_bytes())
        if u.path == "/gameday/state":
            if "state" in FAIL:
                return self._send(500, "text/plain", "")
            return self._json(200, state_doc())
        if u.path == "/update/Firmware":
            return self._json(200, FIRMWARE)
        if u.path == "/gameday/settings":
            if "settings" in FAIL:
                return self._json(404, {"error": "unknown route"})
            return self._json(200, settings_doc())
        if u.path == "/gameday/wifi":
            if WIFI["list"] is None:
                return self._json(404, {"error": "unknown route"})
            return self._json(200, {"networks": WIFI["list"], "max": 5})
        if u.path == "/gameday/picture":
            slot = atoi(parse_qs(u.query).get("slot", ["0"])[0])
            if slot not in PIC_FILES:
                return self._json(404, {"error": "no picture there"})
            kind = next(p["k"] for p in STATE["pics"] if p["s"] == slot)
            return self._send(200, "image/" + kind, PIC_FILES[slot])
        if u.path == "/events":
            return self._events()
        if u.path == "/_log":
            return self._json(200, LOG)
        self._send(404, "text/plain", "Not Found")

    def _events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        try:
            ping = {"title": "Game Day Scoreboard", "comment": STATE["version"]}
            self.wfile.write(("event: ping\ndata: " + json.dumps(ping) + "\n\n").encode())
            for e in ENTITIES:
                self.wfile.write(("event: state\ndata: " + json.dumps(e) + "\n\n").encode())
            self.wfile.flush()
            sent = len(PUSHED)
            for tick in itertools.count():
                time.sleep(0.5)
                if "down" in FAIL or "events" in FAIL:
                    return
                # A reset empties the list under an open stream
                sent = min(sent, len(PUSHED))
                for e in PUSHED[sent:]:
                    self.wfile.write(("event: state\ndata: " + json.dumps(e) + "\n\n").encode())
                sent = len(PUSHED)
                if tick % 10 == 9:
                    self.wfile.write(b"event: ping\ndata: \n\n")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            return

    def do_POST(self):
        u = urlparse(self.path)
        if self._gone(u.path):
            return
        pairs = parse_qs(u.query, keep_blank_values=True)
        if u.path == "/_reset":
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
            LOG.clear()
            reset_state(body.get("state") or {})
            FAIL.clear()
            FAIL.update(body.get("fail") or [])
            DEMO.clear()
            DEMO.update(copy.deepcopy(DEMO_INITIAL))
            WIFI["list"] = body["wifi"] if "wifi" in body else copy.deepcopy(WIFI_INITIAL)
            for e in ENTITIES:
                e.update(copy.deepcopy(next(x for x in ENTITIES_INITIAL if x["id"] == e["id"])))
            PUSHED.clear()
            return self._json(200, {"ok": True})
        if u.path == "/gameday/wifi":
            q = {k: v[0] for k, v in pairs.items()}
            LOG.append({"path": u.path, "q": q})
            if WIFI["list"] is None:
                return self._json(404, {"error": "unknown route"})
            if not q.get("forget"):
                return self._json(400, {"error": "forget is required"})
            keep = [n for n in WIFI["list"] if n["ssid"] != q["forget"]]
            if len(keep) == len(WIFI["list"]):
                return self._json(404, {"error": "not a saved network"})
            WIFI["list"] = keep
            return self._json(200, {"ok": True})
        if u.path == "/gameday/picture":
            n = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(n)
            q = {k: v[0] for k, v in pairs.items()}
            LOG.append({"path": u.path, "q": q, "bytes": n, "type": self.headers.get("Content-Type", "")})
            if "delete" in q:
                slot = atoi(q["delete"])
                if PIC_FILES.pop(slot, None) is None:
                    return self._json(404, {"error": "no picture there"})
                STATE["pics"] = [p for p in STATE["pics"] if p["s"] != slot]
                STATE["picroom"] = PIC_MAX
                return self._json(200, {"ok": True})
            return self._json(*add_picture(raw))
        if u.path == "/gameday/settings":
            n = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(n)
            LOG.append({"path": u.path, "body": raw.decode("utf-8", "replace")})
            if "settings" in FAIL:
                return self._json(404, {"error": "unknown route"})
            if n > SETTINGS_MAX:
                return self._json(400, {"error": "That file is too big to be a settings backup."})
            try:
                doc = json.loads(raw)
            except ValueError:
                return self._json(400, {"error": "Not a settings backup."})
            return self._json(*import_settings(doc))
        if u.path == "/gameday/set":
            if len(self.path) > URI_MAX:
                return self._json(414, {"error": "URI too long"})
            n = int(self.headers.get("Content-Length") or 0)
            if n > FORM_MAX:
                return self._json(400, {"error": "Request size is too big"})
            body = parse_qs(self.rfile.read(n).decode("utf-8", "replace"), keep_blank_values=True)
            q = {k: v[0] for k, v in pairs.items()}
            q.update({k: v[0] for k, v in body.items()})
            LOG.append({"path": u.path, "q": q})
            if "set" in FAIL:
                return self._send(500, "text/plain", "")
            kv = [(k, q[k]) for k in SET_KEYS if k in q]
            if not kv:
                return self._json(400, {"error": "nothing to set"})
            apply_set(kv)
            return self._json(200, {"ok": True})
        if u.path == "/gameday/action":
            q = {k: v[0] for k, v in pairs.items()}
            LOG.append({"path": u.path, "q": q})
            if not q.get("do"):
                return self._json(400, {"error": "do is required"})
            if q["do"] not in ("refresh", "qc", "say", "regulatory", "night_skip", "night_resume", "event_test"):
                return self._json(404, {"error": "unknown action"})
            if q["do"] == "say":
                STATE["say"] = message_clean(q.get("text", ""), LIMITS["kMsgTextMax"], True)
            if q["do"] == "night_skip" and STATE.get("night_now") in ("dim", "off"):
                STATE["night_now"] = "skipped"
            if q["do"] == "night_resume" and STATE.get("night_now") == "skipped":
                STATE["night_now"] = "dim" if STATE.get("nightscreen") else "off"
            return self._json(200, {"ok": True})
        if u.path == "/gameday/demo":
            q = {k: v[0] for k, v in pairs.items()}
            LOG.append({"path": u.path, "q": q})
            if not any(k in q for k in DEMO_KEYS):
                return self._json(400, {"error": "nothing to do"})
            apply_demo(q)
            return self._json(200, {"ok": True})
        if u.path.startswith("/gameday/"):
            return self._json(404, {"error": "unknown route"})
        if entity_post_ok(u.path):
            q = {k: v[0] for k, v in pairs.items()}
            LOG.append({"path": u.path, "q": q})
            if not entity_post(u.path, q):
                return self._send(400, "text/plain", "")
            return self._send(200, "text/plain", "")
        self._send(404, "text/plain", "Not Found")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(0 if selftest() else 1)
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
