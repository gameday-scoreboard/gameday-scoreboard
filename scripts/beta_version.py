#!/usr/bin/env python3
"""Print the version a beta build of this commit goes out under.

A beta is the next release's number plus "-beta.<run>": the newest release
tag (vYYYY.M.P) with the patch plus one, or YYYY.M.0 when the month has
moved on since that tag (US Central time, like the release dates). When
`version` in firmware/gameday-common.yaml is already ahead of that (the
release prep PR set it), the beta takes the YAML's number instead. So no
one edits `version` on dev for a beta; the YAML stays at the last release
until the prep PR sets the next one.

    python3 scripts/beta_version.py <run number>
    python3 scripts/beta_version.py --selftest

Reads the tags from origin with `git ls-remote`, so a shallow checkout works.
"""

import argparse
import datetime
import re
import subprocess
import sys
import zoneinfo

YAML = "firmware/gameday-common.yaml"
TAG = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")


def parse(version):
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", version)
    if not m:
        raise ValueError(f"version '{version}' isn't YYYY.M.P")
    return tuple(int(x) for x in m.groups())


def next_version(tags, yaml_version, year, month):
    releases = [tuple(int(x) for x in m.groups()) for m in map(TAG.match, tags) if m]
    yaml = parse(yaml_version)
    if not releases:
        return yaml
    y, m, p = max(releases)
    nxt = (year, month, 0) if (year, month) > (y, m) else (y, m, p + 1)
    return max(nxt, yaml)


def remote_tags():
    out = subprocess.run(["git", "ls-remote", "--tags", "--refs", "origin", "refs/tags/v*"],
                         check=True, capture_output=True, text=True).stdout
    return [line.split("refs/tags/", 1)[1] for line in out.splitlines() if "refs/tags/" in line]


def yaml_version():
    with open(YAML, encoding="utf-8") as f:
        for line in f:
            m = re.match(r'^  version: "(.*)"$', line)
            if m:
                return m.group(1)
    raise SystemExit(f"no version line in {YAML}")


def selftest():
    tags = ["v1.5.5", "v2026.10.0", "v2026.10.2", "v2026.10.1", "v2026.10.2-rc1", "beta-firmware"]
    # After a release the next beta is the next patch, not the shipped number.
    assert next_version(tags, "2026.10.2", 2026, 10) == (2026, 10, 3)
    # The prep PR already set the next number: the beta carries it.
    assert next_version(tags, "2026.10.3", 2026, 10) == (2026, 10, 3)
    assert next_version(tags, "2026.11.0", 2026, 10) == (2026, 11, 0)
    # A new month starts at .0, and a new year too.
    assert next_version(tags, "2026.10.2", 2026, 11) == (2026, 11, 0)
    assert next_version(["v2026.12.4"], "2026.12.4", 2027, 1) == (2027, 1, 0)
    # A release tagged in the new month counts up from there.
    assert next_version(tags + ["v2026.11.0"], "2026.11.0", 2026, 11) == (2026, 11, 1)
    # Months compare as numbers, not text (10 after 9).
    assert next_version(["v2026.9.3"], "2026.9.3", 2026, 10) == (2026, 10, 0)
    # No release tag at all: the YAML's number.
    assert next_version([], "2026.10.0", 2026, 10) == (2026, 10, 0)
    try:
        next_version(tags, "2026.10", 2026, 10)
        raise AssertionError("two-part version accepted")
    except ValueError:
        pass
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("run", nargs="?", help="the beta's build number (GITHUB_RUN_NUMBER)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return 0
    if not args.run:
        ap.error("give the run number")
    now = datetime.datetime.now(zoneinfo.ZoneInfo("America/Chicago"))
    v = next_version(remote_tags(), yaml_version(), now.year, now.month)
    print("%d.%d.%d-beta.%s" % (*v, args.run))
    return 0


if __name__ == "__main__":
    sys.exit(main())
