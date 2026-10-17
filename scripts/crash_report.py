#!/usr/bin/env python3
"""Read a panel's saved crash report and decode its backtrace.

The panel keeps the last crash, watchdog or brownout restart in flash
(components/crash_report) and serves it at GET /gameday/crash. This prints
it and runs addr2line on the addresses, using the ELF from the GitHub
release of the version the panel was running (firmware-<variant>.zip has it),
downloaded once into ~/.cache/gameday-scoreboard/elf.

    python3 scripts/crash_report.py 192.168.1.104
    python3 scripts/crash_report.py 192.168.1.104 --elf firmware/.esphome/build/gameday-scoreboard75/.pioenvs/gameday-scoreboard75/firmware.elf
    python3 scripts/crash_report.py --file crash.json    # a report saved from the device page or a customer
    python3 scripts/crash_report.py 192.168.1.104 --clear
    python3 scripts/crash_report.py --selftest

A dev, beta or local build reports the version it was branched from, so the
release ELF won't match it. The script compares the build time the panel
saved with the release's and stops with a note when they differ; pass that
build's own ELF with --elf (a CI Build artifact has it next to the bins).
"""

import argparse
import datetime
import glob
import io
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

REPO = "gameday-scoreboard/gameday-scoreboard"
CACHE = Path.home() / ".cache" / "gameday-scoreboard" / "elf"
# A release build is made by CI a few minutes before the release is published.
BUILD_SLACK_S = 3 * 3600
ADDR2LINE_NAMES = ["xtensa-esp32s3-elf-addr2line", "xtensa-esp-elf-addr2line"]
ADDR2LINE_GLOBS = [
    "~/.cache/esphome/idf/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line",
    "~/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line",
    "~/.platformio/packages/toolchain-xtensa-esp*/bin/xtensa-esp32s3-elf-addr2line",
]
# The crash handler's lines. A report captured by a different build than the
# one that saved it uses lowercase labels for its addresses.
ADDR_RE = re.compile(r"^\s*(PC|BT\d+):\s*0x([0-9A-Fa-f]{8})")
FOREIGN_ADDR_RE = re.compile(r"^\s*(pc|bt\d+):\s*0x([0-9A-Fa-f]{8})")
FOREIGN_MARK = "Captured by a different firmware build"


def addresses(report, foreign_ok=False):
    """[(label, '0x...')] for the PC and backtrace lines, in order."""
    out = []
    for line in report.splitlines():
        m = ADDR_RE.match(line) or (foreign_ok and FOREIGN_ADDR_RE.match(line))
        if m:
            out.append((m.group(1), "0x" + m.group(2).upper()))
    return out


def is_foreign(report):
    return FOREIGN_MARK in report


def asset_name(variant):
    return f"firmware-{variant}.zip"


def build_matches(build_time, published):
    """Whether a panel's build time fits a release published at `published` (Unix time)."""
    return build_time > 0 and abs(published - build_time) <= BUILD_SLACK_S


def fmt_time(t):
    if not t:
        return "unknown (no clock yet)"
    return datetime.datetime.fromtimestamp(t).strftime("%Y-%m-%d %-I:%M %p")


def fetch(host):
    with urllib.request.urlopen(f"http://{host}/gameday/crash", timeout=10) as r:
        return json.load(r)


def clear(host):
    req = urllib.request.Request(f"http://{host}/gameday/crash?clear=1", data=b"", method="POST")
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.status == 200


def release_published(version):
    url = f"https://api.github.com/repos/{REPO}/releases/tags/v{version}"
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            rel = json.load(r)
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise
    stamp = rel.get("published_at") or rel.get("created_at")
    return int(datetime.datetime.fromisoformat(stamp.replace("Z", "+00:00")).timestamp())


def release_elf(version, variant):
    dest = CACHE / version / f"{variant}.elf"
    if dest.exists():
        return dest
    url = f"https://github.com/{REPO}/releases/download/v{version}/{asset_name(variant)}"
    print(f"Downloading {url}")
    with urllib.request.urlopen(url, timeout=120) as r:
        data = r.read()
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        elves = [n for n in z.namelist() if n.endswith(".elf")]
        if not elves:
            raise SystemExit(f"{asset_name(variant)} in v{version} has no ELF")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(z.read(elves[0]))
    return dest


def find_addr2line(given):
    if given:
        return given
    for name in ADDR2LINE_NAMES:
        path = shutil.which(name)
        if path:
            return path
    for pattern in ADDR2LINE_GLOBS:
        hits = sorted(glob.glob(os.path.expanduser(pattern)))
        if hits:
            return hits[-1]
    raise SystemExit("No xtensa addr2line found; compile any panel firmware once, or pass --addr2line")


def decode(addr2line, elf, addrs):
    out = subprocess.run([addr2line, "-pfiaC", "-e", str(elf)] + [a for _, a in addrs],
                         capture_output=True, text=True, check=True).stdout
    return out


def selftest():
    report = (
        "*** CRASH DETECTED ON PREVIOUS BOOT ***\n"
        "  Reason: Fault - LoadProhibited (cause 28)\n"
        "  Crashed core: 0\n"
        "  PC:  0x40055EBF  (fault location)\n"
        "  EXCVADDR: 0x00000008  (faulting address)\n"
        "  BT0: 0x40386CDF  (backtrace)\n"
        "  BT1: 0x421736DD  (backtrace)\n"
        "  Other core (1) backtrace:\n"
        "  BT2: 0x4038665D  (backtrace)\n"
    )
    got = addresses(report)
    assert got == [("PC", "0x40055EBF"), ("BT0", "0x40386CDF"), ("BT1", "0x421736DD"),
                   ("BT2", "0x4038665D")], got
    assert not is_foreign(report)
    foreign = (
        "*** CRASH DETECTED ON PREVIOUS BOOT ***\n"
        "  Reason: Abort\n"
        "  Crashed core: 1\n"
        "  Captured by a different firmware build; addresses belong to that build's ELF\n"
        "  pc: 0x40055ebf\n"
        "  bt0: 0x40386cdf\n"
    )
    assert is_foreign(foreign)
    assert addresses(foreign) == [], addresses(foreign)
    assert addresses(foreign, foreign_ok=True) == [("pc", "0x40055EBF"), ("bt0", "0x40386CDF")]
    assert asset_name("scoreboard75") == "firmware-scoreboard75.zip"
    assert build_matches(1760000000, 1760000000 + 600)
    assert not build_matches(1760000000, 1760000000 + 86400)
    assert not build_matches(0, 1760000000)
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", nargs="?", help="the panel's IP or name")
    ap.add_argument("--file", help="read the report from a saved /gameday/crash JSON instead")
    ap.add_argument("--elf", help="decode with this ELF instead of the release's")
    ap.add_argument("--addr2line", help="path to xtensa-esp32s3-elf-addr2line")
    ap.add_argument("--clear", action="store_true", help="empty the panel's saved report after printing it")
    ap.add_argument("--selftest", action="store_true", help="check the parsing only, no network")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return 0
    if not args.host and not args.file:
        ap.error("give a panel or --file")

    rec = json.loads(Path(args.file).read_text()) if args.file else fetch(args.host)
    if not rec.get("count"):
        print("No crash saved.")
        return 0
    print(f"Restarts since cleared: {rec['count']}")
    print(f"Last: {rec.get('reason')} on v{rec.get('version')} ({rec.get('variant') or 'variant unknown'}), "
          f"clock {fmt_time(rec.get('time'))}, firmware built {fmt_time(rec.get('build_time'))}")
    report = rec.get("report", "")
    print(report.rstrip() or "(no backtrace: a brownout, or the crash handler had nothing)")

    foreign = is_foreign(report)
    addrs = addresses(report, foreign_ok=foreign)
    if addrs:
        elf = args.elf
        if foreign and not elf:
            print("\nThis report came from a different build than the one that saved it (an update that "
                  "rolled back); pass that build's ELF with --elf to decode it.")
        elif not elf:
            version, variant = rec.get("version"), rec.get("variant")
            published = release_published(version) if version else None
            if not variant or published is None:
                print(f"\nNo release v{version} for {variant or 'this build'}; pass the build's ELF with --elf.")
            elif not build_matches(rec.get("build_time", 0), published):
                print(f"\nThis panel's firmware was built {fmt_time(rec.get('build_time'))}, but v{version} was "
                      f"released {fmt_time(published)}: not the release build. Pass its ELF with --elf.")
            else:
                elf = release_elf(version, variant)
        if elf:
            print(f"\nDecoded with {elf}:")
            print(decode(find_addr2line(args.addr2line), elf, addrs).rstrip())

    if args.clear and args.host:
        print("\nCleared." if clear(args.host) else "\nClear failed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
