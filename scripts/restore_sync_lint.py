"""YAML values that survive a restart but not a power cut.

ESPHome keeps a restored value (restore_value on a global, number, text or
select; restore_mode RESTORE_* on a switch or light) in RAM and writes it to
flash from a 60 s timer, or at a clean shutdown. A bench "restart" (the
Restart button, esphome upload) is a clean shutdown, so it always finds the
value. An owner who unplugs the panel within the minute loses it. The
firmware's rule since v2026.10.2 is "saved the moment you change them":
the component's own setters call sync_prefs_(), and YAML entities call
id(gameday_ctl).sync_soon() when they change (Scroll Speed, the light).

This lint finds restored YAML values with no sync: an entity whose block has
no sync_soon()/sync_prefs, or a global assigned in a lambda that has none.

  python3 scripts/restore_sync_lint.py <repo dir>   exit 1 when any are found
  python3 scripts/restore_sync_lint.py --selftest
"""

import re
import sys
import tempfile
from pathlib import Path

RESTORES = re.compile(r"^\s*(restore_value:\s*(yes|true|True)|restore_mode:\s*RESTORE\w*)\s*$")
SYNC = re.compile(r"sync_soon\(\)|sync_prefs_?\(\)|global_preferences->sync\(\)")


def indent(s):
    return len(s) - len(s.lstrip(" "))


def item_block(lines, i):
    """The list item ('- ...') that holds line i: (start, end) line indexes."""
    want = indent(lines[i])
    s = i
    while s > 0 and not (lines[s].lstrip().startswith("- ") and indent(lines[s]) < want):
        s -= 1
    base = indent(lines[s])
    e = s + 1
    while e < len(lines) and (not lines[e].strip() or indent(lines[e]) > base):
        e += 1
    return s, e


def section_of(lines, s):
    """The top-level key (globals:, number:, ...) above line s."""
    for k in range(s, -1, -1):
        if re.match(r"^[a-z_]+:\s*$", lines[k]):
            return lines[k].strip().rstrip(":")
    return "?"


def lambdas(text):
    """Every lambda body (block scalar or quoted one-liner) in a YAML file."""
    out = []
    lines = text.splitlines()
    for i, ln in enumerate(lines):
        m = re.match(r"^(\s*)-?\s*lambda:\s*(.*)$", ln)
        if not m:
            continue
        rest = m.group(2).strip()
        if rest.startswith("|") or rest.startswith(">"):
            base = indent(ln)
            j = i + 1
            while j < len(lines) and (not lines[j].strip() or indent(lines[j]) > base):
                j += 1
            out.append("\n".join(lines[i + 1:j]))
        else:
            out.append(rest)
    return out


def check(root):
    """Every restored YAML value under root/firmware with no flash write."""
    files = sorted((root / "firmware").rglob("*.yaml"))
    texts = {p: p.read_text(encoding="utf-8") for p in files}
    all_lambdas = [lb for t in texts.values() for lb in lambdas(t)]
    # A script whose steps sync counts as a sync where it is started.
    sync = SYNC
    syncing_scripts = []
    for t in texts.values():
        lines = t.splitlines()
        for i, ln in enumerate(lines):
            m = re.match(r"^\s*-\s*id:\s*(\w+)\s*$", ln)
            if m and section_of(lines, i) == "script":
                base, e = indent(ln), i + 1
                while e < len(lines) and (not lines[e].strip() or indent(lines[e]) > base):
                    e += 1
                if sync.search("\n".join(lines[i:e])):
                    syncing_scripts.append(m.group(1))
    if syncing_scripts:
        sync = re.compile(sync.pattern + "|" + "|".join(r"id\(" + s + r"\)\.execute\(\)|script\.execute:\s*" + s
                                                         for s in syncing_scripts))
    found = []
    for p, t in texts.items():
        lines = t.splitlines()
        for i, ln in enumerate(lines):
            if not RESTORES.match(ln):
                continue
            s, e = item_block(lines, i)
            block = "\n".join(lines[s:e])
            idm = re.search(r"^\s*-?\s*id:\s*(\w+)", block, re.M)
            eid = idm.group(1) if idm else "?"
            sec = section_of(lines, s)
            if sec == "globals":
                writes = [lb for lb in all_lambdas if re.search(r"id\(" + re.escape(eid) + r"\)\s*=[^=]", lb)]
                ok = bool(writes) and all(sync.search(lb) for lb in writes)
                why = f"{len(writes)} lambda(s) assign it, {sum(1 for lb in writes if not sync.search(lb))} without a sync"
            else:
                ok = bool(sync.search(block))
                why = "its block has no sync_soon()"
            if not ok:
                found.append(f"{p.relative_to(root).as_posix()}:{i + 1}: {sec} {eid} restores its value but {why}; "
                             "a power cut within 60 s of a change loses it")
    return found


SELFTEST = {
    # A restored number that never syncs: caught.
    "bad_entity": ("number:\n  - platform: template\n    id: n1\n    restore_value: yes\n"
                   "    set_action:\n      - lambda: 'x = 1;'\n", 1),
    # The same number syncing in its block: passes.
    "good_entity": ("number:\n  - platform: template\n    id: n1\n    restore_value: yes\n"
                    "    set_action:\n      - lambda: 'id(gameday_ctl).sync_soon();'\n", 0),
    # A restored global set in a lambda with no sync: caught.
    "bad_global": ("globals:\n  - id: g1\n    type: bool\n    restore_value: yes\n"
                   "interval:\n  - interval: 1s\n    then:\n      - lambda: 'id(g1) = true;'\n", 1),
    # The same global, synced through a script started where it is set: passes.
    "good_global": ("globals:\n  - id: g1\n    type: bool\n    restore_value: yes\n"
                    "script:\n  - id: s1\n    then:\n      - lambda: 'id(gameday_ctl).sync_soon();'\n"
                    "interval:\n  - interval: 1s\n    then:\n      - lambda: 'id(g1) = true; id(s1).execute();'\n", 0),
}


def selftest():
    bad = 0
    for name, (yaml, want) in SELFTEST.items():
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "firmware").mkdir()
            (Path(d) / "firmware" / "t.yaml").write_text(yaml, encoding="utf-8")
            got = len(check(Path(d)))
        print(f"{'ok ' if got == want else 'BAD'}  {name}: {got} found, want {want}")
        bad += got != want
    return 1 if bad else 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    found = check(Path(sys.argv[1]))
    for f in found:
        print(f)
    print(f"{len(found)} restored YAML value(s) without a flash write")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
