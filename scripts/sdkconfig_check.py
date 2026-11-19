"""Every ESP-IDF option our YAML sets still takes effect.

The YAML sets raw ESP-IDF options under esp32: framework: sdkconfig_options
(gameday-common.yaml, the controller files). ESPHome passes them to ESP-IDF
as written and nothing checks them: when ESP-IDF renames or drops an option,
or a dependency turns it off, the build still passes and the panel quietly
loses the memory or Wi-Fi setting the option was there for. ESPHome itself
hit this with renamed keys (esphome#16522).

After a build, this reads the sdkconfig ESP-IDF generated and fails, naming
the option, when one we set is:
  missing     no line for it at all (ESP-IDF no longer has it, or something
              it depends on is off)
  different   there, with another value than ours
  deprecated  only in the "Deprecated options for backward compatibility"
              part: ESP-IDF renamed it and maps the old name for now. The
              new name is in ESP-IDF's sdkconfig.rename files.
  not passed  ESPHome's own list of options it handed ESP-IDF
              (sdkconfig.<name>.esphomeinternal) lacks it or has another
              value

CI's Build job runs it for every board, so an ESPHome pin bump PR runs it.

  python3 scripts/sdkconfig_check.py <top-level yaml> <build dir>
      e.g. firmware/gameday.yaml firmware/.esphome/build/gameday-moonhub75
  python3 scripts/sdkconfig_check.py --selftest
"""

import re
import sys
import tempfile
from pathlib import Path

INCLUDE = re.compile(r"!include\s+([\w./-]+\.ya?ml)\b")
BLOCK = re.compile(r"^(\s*)sdkconfig_options:\s*(#.*)?$")
OPTION = re.compile(r"^\s*(\w+):\s*(.*?)\s*$")
SET = re.compile(r"^(CONFIG_\w+)=(.*)$")
UNSET = re.compile(r"^# (CONFIG_\w+) is not set$")
DEPRECATED = "# Deprecated options for backward compatibility"


def indent(s):
    return len(s) - len(s.lstrip(" "))


def norm(v):
    """One spelling per value: y, n, or the text without quotes."""
    v = v.strip()
    if len(v) >= 2 and v[0] == v[-1] and v[0] in "\"'":
        v = v[1:-1]
    low = v.lower()
    if low in ("y", "yes", "true", "on"):
        return "y"
    if low in ("n", "no", "false", "off"):
        return "n"
    return v


def strip_comment(v):
    """Drop a trailing # comment that isn't inside quotes."""
    q = None
    for i, c in enumerate(v):
        if q:
            q = None if c == q else q
        elif c in "\"'":
            q = c
        elif c == "#" and (i == 0 or v[i - 1] in " \t"):
            return v[:i].rstrip()
    return v


def yaml_files(top):
    """The top-level YAML and every local file it !includes, in order."""
    seen, order, todo = set(), [], [Path(top)]
    while todo:
        f = todo.pop(0).resolve()
        if f in seen:
            continue
        seen.add(f)
        if not f.is_file():
            raise SystemExit(f"error: {f} is included but doesn't exist")
        order.append(f)
        for line in f.read_text(encoding="utf-8").splitlines():
            if line.lstrip().startswith("#"):
                continue
            for m in INCLUDE.finditer(line):
                todo.append(f.parent / m.group(1))
    return order


def wanted(top):
    """{option: (value, 'file:line')} from every sdkconfig_options block, and
    a list of errors for an option set twice with different values."""
    want, errors = {}, []
    root = Path(top).resolve().parent
    for f in yaml_files(top):
        lines = f.read_text(encoding="utf-8").splitlines()
        i = 0
        while i < len(lines):
            m = BLOCK.match(lines[i])
            i += 1
            if not m:
                continue
            base = len(m.group(1))
            while i < len(lines):
                line = lines[i]
                if line.strip() and not line.lstrip().startswith("#") and indent(line) <= base:
                    break
                o = OPTION.match(line)
                if o and not line.lstrip().startswith("#"):
                    key, val = o.group(1), norm(strip_comment(o.group(2)))
                    try:
                        where = f"{f.relative_to(root)}:{i + 1}"
                    except ValueError:
                        where = f"{f}:{i + 1}"
                    if key in want and want[key][0] != val:
                        errors.append(f"{key}: set to {want[key][0]} at {want[key][1]} and to {val} at {where}")
                    want.setdefault(key, (val, where))
                i += 1
    return want, errors


def read_sdkconfig(path):
    """({option: value} before the deprecated part, the same for after it)."""
    main, old = {}, {}
    part = main
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line == DEPRECATED:
            part = old
            continue
        m = SET.match(line)
        if m:
            part[m.group(1)] = norm(m.group(2))
            continue
        m = UNSET.match(line)
        if m:
            part[m.group(1)] = "n"
    return main, old


def generated_files(build):
    """(ESP-IDF's sdkconfig, ESPHome's list of options or None) in a build dir."""
    build = Path(build)
    if not build.is_dir():
        raise SystemExit(f"error: no build folder at {build}; build the firmware first")
    finals = [p for p in sorted(build.glob("sdkconfig.*"))
              if p.suffix not in (".esphomeinternal", ".old", ".defaults")]
    if len(finals) != 1:
        names = ", ".join(p.name for p in finals) or "none"
        raise SystemExit(f"error: want one generated sdkconfig.<name> in {build}, found {names}")
    internal = list(build.glob("sdkconfig.*.esphomeinternal")) or list(build.glob("sdkconfig.defaults"))
    return finals[0], (internal[0] if internal else None)


def check(top, build):
    """Problems, one line each, naming the option."""
    want, problems = wanted(top)
    if not want:
        return [f"no sdkconfig_options found from {top}; the check would pass on nothing"]
    final, internal = generated_files(build)
    main, old = read_sdkconfig(final)
    passed = read_sdkconfig(internal)[0] if internal else None
    for key, (val, where) in sorted(want.items()):
        if passed is not None and passed.get(key) != val:
            got = passed.get(key, "nothing")
            problems.append(f"{key} not passed: we set {val} ({where}), ESPHome handed ESP-IDF {got} ({internal.name})")
        if key in main:
            if main[key] != val:
                problems.append(f"{key} different: we set {val} ({where}), the build has {main[key]}")
        elif key in old:
            problems.append(f"{key} deprecated: ESP-IDF renamed it; set the new name from its sdkconfig.rename files ({where})")
        else:
            problems.append(f"{key} missing: the build has no such option ({where}); ESP-IDF dropped or renamed it, or something it needs is off")
    return problems


TOP = """\
packages:
  controller: !include controllers/board.yaml
  common: !include common.yaml
"""
BOARD = """\
esp32:
  framework:
    sdkconfig_options:
      CONFIG_CACHE_LINE_64B: y
psram:
  mode: octal
"""
COMMON = """\
esp32:
  framework:
    advanced:
      loop_task_stack_size: 16384
    sdkconfig_options:
      CONFIG_ON: y
      # A comment in the block
      CONFIG_OFF: n
      CONFIG_DELAY: "1000"  # trailing comment
esphome:
  name: x
"""
FINAL = """\
CONFIG_CACHE_LINE_64B=y
CONFIG_ON=y
# CONFIG_OFF is not set
CONFIG_DELAY=1000
CONFIG_NEW_NAME=y

# Deprecated options for backward compatibility
CONFIG_OLD_NAME=y
# End of deprecated options
"""
INTERNAL = """\
CONFIG_CACHE_LINE_64B=y
CONFIG_ON=y
CONFIG_OFF=n
CONFIG_DELAY=1000
CONFIG_OLD_NAME=y
CONFIG_GAMEDAY_MADE_UP=y
"""

# name: (text added to COMMON's sdkconfig_options, FINAL edit, want problems
# that each contain these words)
SELFTEST = {
    "today's options pass": ("", None, []),
    "a made-up option fails with its name": (
        "      CONFIG_GAMEDAY_MADE_UP: y\n", None, ["CONFIG_GAMEDAY_MADE_UP missing"]),
    "a renamed option fails as deprecated": (
        "      CONFIG_OLD_NAME: y\n", None, ["CONFIG_OLD_NAME deprecated"]),
    "a value Kconfig changed fails": (
        "", ("CONFIG_DELAY=1000", "CONFIG_DELAY=5000"), ["CONFIG_DELAY different"]),
    "an option turned off by a dependency fails": (
        "", ("CONFIG_ON=y\n", ""), ["CONFIG_ON missing"]),
    "an option ESPHome didn't pass fails": (
        "      CONFIG_NEW_NAME: y\n", None, ["CONFIG_NEW_NAME not passed"]),
}


def selftest():
    bad = 0
    for name, (extra, edit, want) in SELFTEST.items():
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "controllers").mkdir()
            (d / "top.yaml").write_text(TOP, encoding="utf-8")
            (d / "controllers" / "board.yaml").write_text(BOARD, encoding="utf-8")
            common = COMMON.replace("esphome:\n", extra + "esphome:\n")
            (d / "common.yaml").write_text(common, encoding="utf-8")
            build = d / "build"
            build.mkdir()
            final = FINAL if not edit else FINAL.replace(*edit)
            (build / "sdkconfig.x").write_text(final, encoding="utf-8")
            (build / "sdkconfig.x.esphomeinternal").write_text(INTERNAL, encoding="utf-8")
            got = check(d / "top.yaml", build)
        ok = len(got) == len(want) and all(any(w in g for g in got) for w in want)
        print(f"{'ok ' if ok else 'BAD'}  {name}: {got or 'no problems'}")
        bad += not ok
    # The block parser reads all four options from the two files.
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / "controllers").mkdir()
        (d / "top.yaml").write_text(TOP, encoding="utf-8")
        (d / "controllers" / "board.yaml").write_text(BOARD, encoding="utf-8")
        (d / "common.yaml").write_text(COMMON, encoding="utf-8")
        got = {k: v for k, (v, _) in wanted(d / "top.yaml")[0].items()}
    want = {"CONFIG_CACHE_LINE_64B": "y", "CONFIG_ON": "y", "CONFIG_OFF": "n", "CONFIG_DELAY": "1000"}
    print(f"{'ok ' if got == want else 'BAD'}  reads every option from the included files: {got}")
    bad += got != want
    return 1 if bad else 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    top, build = sys.argv[1], sys.argv[2]
    problems = check(top, build)
    want, _ = wanted(top)
    if problems:
        for p in problems:
            print(f"::error file={top}::{p}")
        print(f"{len(problems)} problem(s) with the {len(want)} ESP-IDF options {top} sets")
        return 1
    for key, (val, where) in sorted(want.items()):
        print(f"ok  {key}={val} ({where})")
    print(f"All {len(want)} ESP-IDF options {top} sets are in the build as set")
    return 0


if __name__ == "__main__":
    sys.exit(main())
