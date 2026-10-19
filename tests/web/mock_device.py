"""Mock of the scoreboard's web server for the device page tests.

Serves firmware/web/bundle.js and app.css at /0.js and /0.css (where ESPHome's
web_server puts js_include and css_include), the ESPHome /events stream for
the entities the page builds controls from, and the gameday component's
routes with the response shapes from components/gameday/gameday.cpp:

  GET  /gameday/state    the document rebuild_state_() writes
  POST /gameday/set      only the keys in SET_KEYS; 400 when none match
  POST /gameday/action   400 without do=, 404 for a name the firmware does not know;
                         power_ok sets health.power back to "ok"; say keeps its cleaned
                         text in "say" for tests to read (the panel has no such key)
  GET  /gameday/wifi     the saved networks (components/saved_networks), names only
  POST /gameday/wifi     forget=<ssid>; 404 when it is not saved

/?demo loads bundle-demo.js instead, the demo build's page, and
GET/POST /gameday/demo answer as demo.cpp does.

Test hooks: POST /_reset with an optional JSON body
{"state": {...overrides...}, "fail": ["state", "set"], "wifi": [...]} puts the
state back, merges the overrides and makes the named routes answer 500. Two
more fail names drop the connection with no answer, the way a panel that has
gone does: "events" for the event stream (an open one ends within half a
second), "down" for every route but the test hooks. "wifi" replaces the saved
network list; null stands for firmware without the list, whose /gameday/wifi
answers 404. GET /_log lists the /gameday/set, /gameday/action and
/gameday/wifi requests seen since the last reset.

Started by playwright.config.js. Run by hand with `python mock_device.py 8765`
and open http://127.0.0.1:8765/.

The set keys, their limits and the Live auto pick come from
components/gameday/set_request.h. `python mock_device.py --selftest` runs
tests/fixtures/set_cases.json, the table the firmware's host tests also
check, through apply_set, so the mock can't drift from the panel.
"""
import copy
import itertools
import json
import os
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

REPO = Path(__file__).resolve().parents[2]
WEB = Path(os.environ.get("GAMEDAY_WEB", REPO / "firmware" / "web"))

# Same tables the firmware and scripts/build_web.py read.
TEAM_RE = re.compile(r'\{League::([A-Z]+),\s*(\d+),\s*"([^"]*)",\s*"([^"]*)",\s*(\d+)\}')
LEAGUE_RE = re.compile(r'\{League::[A-Z]+,\s*"([a-z]+)",')
TZ_RE = re.compile(r'\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\}')
TEAMS = {}
for lg, tid, abbr, name, _ in TEAM_RE.findall((REPO / "components/gameday/teams.h").read_text(encoding="utf-8")):
    TEAMS[(lg.lower(), int(tid))] = (abbr, name)
# kLeagues order, which is the order the firmware lists live leagues in.
LEAGUES = LEAGUE_RE.findall((REPO / "components/gameday/leagues.h").read_text(encoding="utf-8"))
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
                                             (REPO / "components/gameday/startup.h").read_text(encoding="utf-8"))}
FAV_MAX = int(re.search(r"FAV_MAX = (\d+);", (REPO / "components/gameday/gameday.h").read_text(encoding="utf-8"))[1])
# PanelLayout refuses a count past this (gameday-common.yaml).
MAX_COLS = int(re.search(r"^\s*max_cols: (\d+)", (REPO / "firmware/gameday-common.yaml").read_text(encoding="utf-8"),
                         re.M)[1])
FIXTURE = REPO / "tests/fixtures/set_cases.json"


def team(lg, tid):
    """put_team(): league, id, abbr and name, or an empty object."""
    if (lg, tid) not in TEAMS:
        return {}
    abbr, name = TEAMS[(lg, tid)]
    return {"l": lg, "id": tid, "abbr": abbr, "name": name}


INITIAL = {
    "name": "gameday-2f6a70", "version": "v1.5.4", "setup": True, "panels": 2,
    "team": team("nfl", 6),
    "mode": 0, "rotate": 1, "lockon": 15, "release": 30, "collide": 0,
    "favs": [team("nfl", 6), team("nfl", 34), team("ncaa", 145), {}],
    "favmax": FAV_MAX, "today": False, "live": ["nfl", "ncaa"], "live_auto": True,
    "tz": 1, "tz_name": "US Central", "tz_auto": True,
    "down": True, "count": True, "match": True, "play": True, "odds": True, "opp": True, "bootaddr": True,
    "autoupd": False, "beta": False,
    "msg": "", "msgurl": "", "msgevery": 2,
    "misses": 0, "captive": False, "stale_s": 4, "fallback": False, "recap": False, "espn_kb": 412, "espn_reads": 23,
    # GamedayComponent::health_json_(), added at the end of each read.
    "health": {"up": 7260, "start": "restart", "boots": 2, "fault": "crash", "fault_ago": 90000,
               "heap_kb": 143.2, "block_kb": 40.0, "psram_kb": 4100.5, "rssi": -58, "espn": 200,
               "espn_ago": 9, "version": "1.5.4", "built": 1792285260, "channel": "stable",
               # guard_json_keys() (components/gameday/guard.h)
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
    "next": [{"id": "401772001", "kick": 1792531500, "oi": 21, "oa": "PHI", "on": "Eagles", "home": True,
              "neutral": False, "tv": "FOX"},
             {"id": "401772002", "kick": 1793136300, "oi": 28, "oa": "WSH", "on": "Commanders", "home": False,
              "neutral": False, "tv": "CBS"},
             {"id": "401772003", "kick": 1793741100, "oi": 34, "oa": "HOU", "on": "Texans", "home": True,
              "neutral": False, "tv": "NBC"}],
}

# ESPHome 2026.x state events (detail=all shape) for the entities app.js uses.
ENTITIES = [
    {"id": "switch/Power", "domain": "switch", "name": "Power", "state": "ON", "value": True},
    {"id": "number/Brightness", "domain": "number", "name": "Brightness", "state": "128", "value": "128",
     "min_value": "1", "max_value": "255", "step": "1"},
    {"id": "number/Scroll Speed", "domain": "number", "name": "Scroll Speed", "state": "5", "value": "5",
     "min_value": "1", "max_value": "10", "step": "1"},
    {"id": "select/Select Page", "domain": "select", "name": "Select Page", "state": "Scoreboard",
     "value": "Scoreboard", "option": ["Scoreboard", "Clock"]},
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

STATE = copy.deepcopy(INITIAL)
FAIL = set()
LOG = []
# The saved Wi-Fi networks as GET /gameday/wifi lists them, the one tried first
# first. None: firmware without the list.
WIFI_INITIAL = [{"ssid": "Home", "connected": True}, {"ssid": "iPhone", "connected": False}]
WIFI = {"list": copy.deepcopy(WIFI_INITIAL)}
# The saved team's league byte, which outlives a cleared team and counts in
# Live auto. A fresh panel's is NFL.
TEAM_LEAGUE = ["nfl"]


def reset_state(overrides):
    STATE.clear()
    STATE.update(copy.deepcopy(INITIAL))
    STATE.update(overrides)
    # A null override drops the key, like firmware that doesn't send it.
    for k in [k for k, v in STATE.items() if v is None]:
        del STATE[k]
    TEAM_LEAGUE[0] = (STATE.get("team") or {}).get("l", "nfl")


def atoi(v):
    m = re.match(r"\s*[+-]?\d+", v)
    return int(m.group()) if m else 0


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
    """live_leagues() with a mask of 0: the saved team's league and the favorites'."""
    used = {TEAM_LEAGUE[0]} | {t["l"] for t in STATE["favs"] if t}
    return [lg for lg in LEAGUES if lg in used] or ["nfl"]


def state_doc():
    """rebuild_state_() works the auto leagues out each time from the team and favorites."""
    if STATE.get("live_auto"):
        STATE["live"] = live_auto_leagues()
    return STATE


def apply_set(kv):
    """GamedayComponent::apply_set_ through set_request.h's parse_set."""
    for k, v in kv:
        if k == "team":
            ref = parse_team_ref(v)
            if ref:
                STATE["team"] = team(*ref)
                TEAM_LEAGUE[0] = ref[0]
            elif v == "none":
                STATE["team"] = {}
        elif k == "mode":
            if 0 <= atoi(v) < LIMITS["kModeCount"]:
                STATE["mode"] = atoi(v)
        elif k == "rotate":
            STATE["rotate"] = clamp(atoi(v), "kRotateMinMinutes", "kRotateMaxMinutes")
        elif k == "lockon":
            STATE["lockon"] = clamp(atoi(v), "kLockonMinMinutes", "kLockonMaxMinutes")
        elif k == "release":
            STATE["release"] = clamp(atoi(v), "kReleaseMinSeconds", "kReleaseMaxSeconds")
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
        elif k in ("fav1", "fav2", "fav3", "fav4"):
            ref = parse_team_ref(v)
            if ref is None and v not in ("none", ""):
                continue
            STATE["favs"][int(k[3]) - 1] = team(*ref) if ref else {}
        elif k == "today":
            STATE["today"] = v == "1"
        elif k == "live":
            keys = [lg for lg in LEAGUES if lg in v.split(",")]
            STATE["live_auto"] = not keys
            STATE["live"] = keys
        elif k == "tz":
            i = timezone_index(v)
            if i >= 0:
                STATE["tz"], STATE["tz_name"] = i, TZS[i]
        elif k == "tzauto":
            STATE["tz_auto"] = v == "1"
        elif k in ("down", "count", "match", "play", "odds", "opp", "bootaddr", "autoupd", "beta"):
            STATE[k] = v == "1"
        elif k == "panels":
            if 1 <= atoi(v) <= MAX_COLS:
                STATE["panels"] = atoi(v)
        elif k == "msg":
            STATE["msg"] = message_clean(v, LIMITS["kMsgTextMax"], True)
        elif k == "msgurl":
            if msg_url_ok(v):
                STATE["msgurl"] = v
        elif k == "msgevery":
            if v[:1].isdigit():
                STATE["msgevery"] = min(LIMITS["kMsgEveryMax"], atoi(v))


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
        return value

    bad = 0
    for case in fx["cases"]:
        start = {**fx["base"], **case.get("start", {})}
        team_league = start.pop("team_league", "nfl")
        start["team"] = to_team(start["team"])
        start["favs"] = [to_team(r) for r in start["favs"]]
        reset_state(start)
        if not STATE["team"]:
            TEAM_LEAGUE[0] = team_league
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
        if u.path == "/gameday/wifi":
            if WIFI["list"] is None:
                return self._json(404, {"error": "unknown route"})
            return self._json(200, {"networks": WIFI["list"], "max": 5})
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
            for tick in itertools.count():
                time.sleep(0.5)
                if "down" in FAIL or "events" in FAIL:
                    return
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
        if u.path == "/gameday/set":
            q = {k: v[0] for k, v in pairs.items()}
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
            if q["do"] not in ("refresh", "qc", "power_ok", "say"):
                return self._json(404, {"error": "unknown action"})
            if q["do"] == "power_ok" and STATE.get("health"):
                STATE["health"]["power"] = "ok"
            if q["do"] == "say":
                STATE["say"] = message_clean(q.get("text", ""), LIMITS["kMsgTextMax"], True)
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
        if u.path.split("/")[1] in ("switch", "number", "select", "button", "update"):
            return self._send(200, "text/plain", "")
        self._send(404, "text/plain", "Not Found")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(0 if selftest() else 1)
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
