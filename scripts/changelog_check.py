#!/usr/bin/env python3
"""Check that CHANGELOG.md's Unreleased section is ready to ship as it stands.

    python3 scripts/changelog_check.py                       # the file as it is
    python3 scripts/changelog_check.py --base origin/dev     # also: released sections unchanged
    python3 scripts/changelog_check.py --base origin/dev --need-change
    python3 scripts/changelog_check.py --selftest

dev stays release-ready, so the promotion only renames `## Unreleased` to the
version (scripts/promote.py). That works when every PR leaves the section in
its released shape:

- `## Unreleased`, when there is one, is the first `##` heading.
- With any bullets, its first line is a plain summary line under 240
  characters (the update summary on the device page), then the groups
  `### ⚠️ Heads up`, `### ✨ New` and `### 🐛 Fixes`, in that order.
- Each bullet sits in a group and starts with an emoji.

With --base, every released `## vX.Y.Z` section in the base must be the same
in this file: a released section stays as it shipped. A version section counts
as released once its `vX.Y.Z` tag exists; until then (prepped, on beta, not
shipped) it can still take a line. --need-change also
fails when the Unreleased section is the same as the base's, for a PR that
changes the firmware and so owes the section a line.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

CHANGELOG = Path(__file__).resolve().parent.parent / "CHANGELOG.md"
SECTION = re.compile(r"^## (.+?)\s*$", re.M)
GROUPS = ["### ⚠️ Heads up", "### ✨ New", "### 🐛 Fixes"]
SUMMARY_MAX = 240


def sections(text):
    """{heading: body} for every `## ` heading, in file order."""
    marks = list(SECTION.finditer(text))
    out = {}
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
        out[m.group(1)] = text[m.end():end].strip("\n")
    return out


def check_unreleased(text):
    """Problems with the Unreleased section, as a list of messages."""
    secs = sections(text)
    if "Unreleased" not in secs:
        return []
    problems = []
    if next(iter(secs)) != "Unreleased":
        problems.append("`## Unreleased` has to be the first `##` heading, above the released versions.")
    lines = [l.rstrip() for l in secs["Unreleased"].splitlines()]
    body = [l for l in lines if l.strip()]
    if not any(l.startswith("- ") for l in body):
        return problems
    first = body[0]
    has_summary = not (first.startswith("#") or first.startswith("- "))
    if not has_summary:
        problems.append("`## Unreleased` starts with a plain summary line, one sentence or two, above the groups.")
    elif len(first) > SUMMARY_MAX:
        problems.append(f"The summary line under `## Unreleased` is {len(first)} characters; keep it under {SUMMARY_MAX}.")
    group = None
    seen = []
    for l in body[1 if has_summary else 0:]:
        if l.startswith("### "):
            if l not in GROUPS:
                problems.append(f"'{l}' isn't one of the groups: {', '.join(GROUPS)}.")
                continue
            if seen and GROUPS.index(l) <= GROUPS.index(seen[-1]):
                problems.append(f"'{l}' is out of order or repeated; the groups go {', '.join(GROUPS)}.")
            seen.append(l)
            group = l
        elif l.startswith("- "):
            if group is None:
                problems.append(f"Bullet outside a group: {l[:60]}")
            elif l[2:3].isascii():
                problems.append(f"Bullet doesn't start with an emoji: {l[:60]}")
        elif group is not None:
            problems.append(f"Only bullets go under a group: {l[:60]}")
        else:
            problems.append(f"One summary line only, then the groups: {l[:60]}")
    return problems


def check_against(base_text, text, need_change, tags=None):
    """tags: the release tags that exist. None treats every version section
    as released."""
    problems = []
    base, head = sections(base_text), sections(text)
    pending_changed = False
    for heading, body in base.items():
        if heading == "Unreleased" or not heading.startswith("v"):
            continue
        if heading not in head:
            problems.append(f"The released section `## {heading}` is gone.")
        elif tags is not None and heading not in tags:
            pending_changed = pending_changed or head[heading] != body
        elif head[heading] != body:
            problems.append(f"The released section `## {heading}` changed. A released section stays as it shipped: put the change under `## Unreleased`.")
    if need_change and head.get("Unreleased", "") == base.get("Unreleased", "") and not pending_changed:
        problems.append("This PR changes the firmware but adds nothing under `## Unreleased`.")
    return problems


def release_tags():
    out = subprocess.run(["git", "tag", "--list", "v*"], check=True, capture_output=True, text=True).stdout
    return set(out.split())


def git_show(ref):
    return subprocess.run(["git", "show", f"{ref}:CHANGELOG.md"], check=True,
                          capture_output=True, text=True, encoding="utf-8").stdout


def selftest():
    head = "# Changelog\n\nIntro.\n\n"
    v1 = "## v2026.11.1\n\nSummary.\n\n### ✨ New\n\n- 🗓️ Old thing\n"
    good = head + "## Unreleased\n\nBig plays come to hockey.\n\n### ✨ New\n\n- 🏒 Hat tricks\n\n### 🐛 Fixes\n\n- ⭐ Favorites\n\n" + v1
    assert check_unreleased(good) == [], check_unreleased(good)
    assert check_unreleased(head + v1) == []
    # An empty Unreleased (headings only) is fine.
    assert check_unreleased(head + "## Unreleased\n\n" + v1) == []
    no_summary = good.replace("Big plays come to hockey.\n\n", "")
    assert [p for p in check_unreleased(no_summary)] == [
        "`## Unreleased` starts with a plain summary line, one sentence or two, above the groups."]
    long = good.replace("Big plays come to hockey.", "x" * 241)
    assert any("241" in p for p in check_unreleased(long))
    order = good.replace("### ✨ New\n\n- 🏒 Hat tricks\n\n### 🐛 Fixes\n\n- ⭐ Favorites",
                         "### 🐛 Fixes\n\n- ⭐ Favorites\n\n### ✨ New\n\n- 🏒 Hat tricks")
    assert any("order" in p for p in check_unreleased(order))
    assert any("emoji" in p for p in check_unreleased(good.replace("- 🏒 Hat", "- Hat")))
    assert any("groups" in p for p in check_unreleased(good.replace("### ✨ New", "### New stuff")))
    below = head + v1 + "\n## Unreleased\n\nS.\n\n### ✨ New\n\n- 🏒 x\n"
    assert any("first" in p for p in check_unreleased(below))
    # A released section stays as it shipped.
    assert check_against(good, good, False) == []
    edited = good.replace("- 🗓️ Old thing", "- 🗓️ Old thing\n- 🏒 Snuck in")
    assert any("v2026.11.1" in p for p in check_against(good, edited, False))
    # The prep renames Unreleased to a new version: allowed.
    renamed = good.replace("## Unreleased", "## v2026.11.2")
    assert check_against(good, renamed, False) == []
    # A firmware PR owes the section a line.
    assert check_against(good, good, True)
    more = good.replace("- ⭐ Favorites", "- ⭐ Favorites\n- 🏷️ Logos")
    assert check_against(good, more, True) == []
    assert check_against(head + v1, good, True) == []
    # A version that isn't tagged yet (prepped, on beta) can still take a line,
    # and that line counts as the PR's changelog change.
    assert check_against(good, edited, True, tags=set()) == []
    assert any("v2026.11.1" in p for p in check_against(good, edited, False, tags={"v2026.11.1"}))
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--base", help="git ref to compare with, like origin/dev")
    ap.add_argument("--need-change", action="store_true",
                    help="fail when the Unreleased section is the same as the base's")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return 0
    text = CHANGELOG.read_text(encoding="utf-8")
    problems = check_unreleased(text)
    if args.base:
        problems += check_against(git_show(args.base), text, args.need_change, release_tags())
    for p in problems:
        print(f"::error file=CHANGELOG.md::{p}")
    if not problems:
        print("CHANGELOG.md is ready to release as it stands.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
