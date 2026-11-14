#!/usr/bin/env python3
"""The JSON type of every key in the device API, tests/fixtures/api/types.json.

The fixtures in tests/fixtures/api/ name every key the panel sends; types.json
says what each one holds, so the companion apps' tests can check a value's
type and not only its name. tests/test_api.cpp fails when a fixture's value
has another type than types.json gives it, and when the firmware's health and
wifi objects do.

  python3 scripts/api_types.py seed            print types.json from the fixtures
  python3 scripts/api_types.py check <panel>   compare a live panel with types.json
  python3 scripts/api_types.py --selftest

Types: bool, int (a whole number), number (any number), string, object, array,
and null. A key that can hold more than one has a list, like ["int", "null"].
Paths read "game.s", "favs[].abbr" and "favs[]" for an array's elements, the
notation test_api.cpp uses.
"""
import json
import sys
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
API = REPO / "tests/fixtures/api"
TYPES = API / "types.json"
# The documents with keys of their own; set_params.json and actions.json are name lists.
DOCS = {"state": ("state.json", "/gameday/state"), "wifi": ("wifi.json", "/gameday/wifi"),
        "settings": ("settings.json", "/gameday/settings"), "crash": ("crash.json", "/gameday/crash")}


def type_of(v):
    if isinstance(v, bool):
        return "bool"
    if isinstance(v, int):
        return "int"
    if isinstance(v, float):
        return "number"
    if isinstance(v, str):
        return "string"
    if isinstance(v, dict):
        return "object"
    if isinstance(v, list):
        return "array"
    return "null"


def walk(v, prefix, out):
    """Every key path in a document with the types found there."""
    if isinstance(v, dict):
        for k, x in v.items():
            path = f"{prefix}.{k}" if prefix else k
            out.setdefault(path, set()).add(type_of(x))
            walk_value(x, path, out)


def walk_value(x, path, out):
    if isinstance(x, list):
        for e in x:
            out.setdefault(path + "[]", set()).add(type_of(e))
            walk_value(e, path + "[]", out) if isinstance(e, list) else walk(e, path + "[]", out)
    else:
        walk(x, path, out)


def fits(want, got):
    if isinstance(want, list):
        return any(fits(w, got) for w in want)
    return got == want or (want == "number" and got == "int")


def seed():
    out = {}
    for name, (fixture, _) in DOCS.items():
        found = {}
        walk(json.loads((API / fixture).read_text(encoding="utf-8")), "", found)
        table = {}
        for path, types in sorted(found.items()):
            table[path] = types.pop() if len(types) == 1 else sorted(types)
        # An array empty in the fixture keeps the element type written by hand.
        if TYPES.exists():
            saved = json.loads(TYPES.read_text(encoding="utf-8")).get(name, {})
            for path, t in saved.items():
                if path.endswith("[]") and path not in table and table.get(path[:-2]) == "array":
                    table[path] = t
        out[name] = dict(sorted(table.items()))
    return out


def compare(table, doc, where):
    """Problems with a document against one table: a type that differs, a key the table lacks."""
    found, bad = {}, []
    walk(doc, "", found)
    for path, types in sorted(found.items()):
        want = table.get(path)
        if want is None:
            bad.append(f"{where}: {path} isn't in types.json")
            continue
        for t in sorted(types):
            if not fits(want, t):
                bad.append(f"{where}: {path} is {t}, types.json says {want}")
    return bad


def check(panel):
    table = json.loads(TYPES.read_text(encoding="utf-8"))
    bad = []
    for name, (_, route) in DOCS.items():
        try:
            with urllib.request.urlopen(f"http://{panel}{route}", timeout=10) as r:
                doc = json.loads(r.read())
        except Exception as e:
            bad.append(f"{route}: {e}")
            continue
        bad += compare(table[name], doc, route)
    for b in bad:
        print("FAIL", b)
    print(f"{panel}: {len(bad)} problems")
    return not bad


def selftest():
    table = seed()
    bad = []
    for name, (fixture, _) in DOCS.items():
        bad += compare(table[name], json.loads((API / fixture).read_text(encoding="utf-8")), fixture)
    if TYPES.exists():
        saved = json.loads(TYPES.read_text(encoding="utf-8"))
        for name in DOCS:
            for path, ts in seed()[name].items():
                for t in ts if isinstance(ts, list) else [ts]:
                    if path in saved.get(name, {}) and not fits(saved[name][path], t):
                        bad.append(f"types.json: {name} {path} is {saved[name][path]}, the fixture holds {t}")
    probe = compare({"a": "number", "b": "int", "c[]": "string", "c": "array"}, {"a": 1, "b": 1.5, "c": ["x", 2]},
                    "probe")
    if probe != ["probe: b is number, types.json says int", "probe: c[] is int, types.json says string"]:
        bad.append(f"compare() missed a wrong type: {probe}")
    for b in bad:
        print("FAIL", b)
    print(f"api types selftest: {len(bad)} failures")
    return not bad


if __name__ == "__main__":
    if sys.argv[1:] == ["seed"]:
        print(json.dumps(seed(), indent=2))
    elif len(sys.argv) == 3 and sys.argv[1] == "check":
        sys.exit(0 if check(sys.argv[2]) else 1)
    elif sys.argv[1:] == ["--selftest"]:
        sys.exit(0 if selftest() else 1)
    else:
        print(__doc__)
        sys.exit(2)
