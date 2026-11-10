#!/usr/bin/env python3
"""Promote dev to beta: name the release on dev, then open the dev-to-beta PR.

    python3 scripts/promote.py              # do it
    python3 scripts/promote.py --dry-run    # say what it would do, change nothing
    python3 scripts/promote.py --selftest

dev stays ready to release (the PR checks in docs-check.yml keep the
CHANGELOG's Unreleased section in its released shape and keep "Coming in the
next release" notes out of the docs), so naming the release is mechanical:

1. Check origin/dev: Unreleased has a summary line and bullets
   (scripts/changelog_check.py) and no docs page has a next-release note.
2. Work out the version the way a beta does (scripts/beta_version.py): the
   newest tag plus one, or YYYY.M.0 in a new month.
3. Open "Release prep: vX.Y.Z" into dev, which renames `## Unreleased` to
   `## vX.Y.Z` and sets `version` in firmware/gameday-common.yaml, wait for
   its checks and merge it.
4. Open "Promote dev to beta for vX.Y.Z". The promotion is never merged
   here: it merges on the repo owner's go, once its checks are green.

Run it again after a failure: a step already done (the prep merged, the
promotion open) is skipped. Needs git and gh signed in with push rights.
"""

import argparse
import datetime
import re
import subprocess
import sys
import tempfile
import time
import zoneinfo
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import beta_version  # noqa: E402
import changelog_check  # noqa: E402

REPO = "gameday-scoreboard/gameday-scoreboard"
YAML = "firmware/gameday-common.yaml"
VERSION_LINE = re.compile(r'^(  version: ")([^"]*)(")$', re.M)
NOTE = "Coming in the next release"
DEV = "origin/dev"  # --dev-ref swaps it for a dry run against an older dev


def run(*cmd, cwd=ROOT, capture=True):
    r = subprocess.run(cmd, cwd=cwd, check=True, text=True, encoding="utf-8",
                       capture_output=capture)
    return r.stdout.strip() if capture else ""


def show(ref, path):
    return run("git", "show", f"{ref}:{path}")


def rename_unreleased(text, version):
    """CHANGELOG text with `## Unreleased` renamed to `## v<version>`."""
    new, n = re.subn(r"^## Unreleased[ \t]*$", f"## v{version}", text, count=1, flags=re.M)
    if n != 1:
        raise SystemExit("CHANGELOG.md has no '## Unreleased' section to release.")
    return new


def set_version(text, version):
    new, n = VERSION_LINE.subn(lambda m: m.group(1) + version + m.group(3), text, count=1)
    if n != 1:
        raise SystemExit(f"No version line in {YAML}.")
    return new


def yaml_version(text):
    m = VERSION_LINE.search(text)
    if not m:
        raise SystemExit(f"No version line in {YAML}.")
    return m.group(2)


def problems_on(changelog, note_hits):
    """What stops origin/dev from being released as it stands."""
    problems = changelog_check.check_unreleased(changelog)
    body = changelog_check.sections(changelog).get("Unreleased", "")
    if not any(l.startswith("- ") for l in body.splitlines()):
        problems.append("There's nothing under '## Unreleased' to release.")
    for hit in note_hits:
        problems.append(f"A '{NOTE}' note is still in the docs: {hit}")
    return problems


def section_text(changelog, heading):
    return changelog_check.sections(changelog).get(heading, "").strip()


def find_pr(base, head):
    out = run("gh", "pr", "list", "-R", REPO, "--base", base, "--head", head,
              "--state", "open", "--json", "number", "--jq", ".[0].number // empty")
    return out or None


def prep(version, dry_run):
    branch = f"release-{version}-prep"
    if dry_run:
        print(f"Would open 'Release prep: v{version}' into dev from {branch}, wait for its checks and merge it.")
        return None
    with tempfile.TemporaryDirectory() as tmp:
        wt = Path(tmp) / "prep"
        run("git", "worktree", "add", "-B", branch, str(wt), DEV)
        try:
            cl = wt / "CHANGELOG.md"
            cl.write_text(rename_unreleased(cl.read_text(encoding="utf-8"), version), encoding="utf-8", newline="\n")
            y = wt / YAML
            y.write_text(set_version(y.read_text(encoding="utf-8"), version), encoding="utf-8", newline="\n")
            run("git", "add", "CHANGELOG.md", YAML, cwd=wt)
            run("git", "commit", "-m", f"Release prep: v{version}", cwd=wt)
            run("git", "push", "-u", "origin", branch, cwd=wt)
        finally:
            run("git", "worktree", "remove", "--force", str(wt))
    body = (f"Names the release on dev: `## Unreleased` becomes `## v{version}` in CHANGELOG.md "
            f"and `version` in {YAML} is `{version}`. Made by scripts/promote.py.\n\n"
            "No owner-visible change, no docs: the release's notes and docs went in with each change.\n")
    url = run("gh", "pr", "create", "-R", REPO, "--base", "dev", "--head", branch,
              "--title", f"Release prep: v{version}", "--body", body)
    number = url.rstrip("/").rsplit("/", 1)[1]
    print(f"Opened {url}; waiting for its checks.")
    # Checks take a few seconds to register; `gh pr checks` fails until one has.
    for _ in range(40):
        time.sleep(15)
        if run("gh", "pr", "view", number, "-R", REPO, "--json", "statusCheckRollup",
               "--jq", ".statusCheckRollup | length") not in ("", "0"):
            break
    run("gh", "pr", "checks", number, "-R", REPO, "--watch", "--fail-fast", "--interval", "30", capture=False)
    run("gh", "pr", "merge", number, "-R", REPO, "--squash")
    print(f"Merged the release prep, #{number}.")
    run("git", "fetch", "origin", "--quiet")
    return number


def promote(version, prep_number, dry_run):
    existing = find_pr("beta", "dev")
    if existing:
        print(f"The promotion is already open: #{existing}.")
        return
    changelog = show(DEV, "CHANGELOG.md")
    notes = section_text(changelog, f"v{version}")
    prep_line = f", with the release prep (#{prep_number})" if prep_number else ""
    body = (f"Promotes dev to beta for v{version}{prep_line}.\n\n{notes}\n\n"
            "Tested: dev's checks are green at the release prep. The beta's hardware sign-off follows this promotion.\n")
    if dry_run:
        print(f"Would open 'Promote dev to beta for v{version}' with this description:\n\n{body}")
        return
    url = run("gh", "pr", "create", "-R", REPO, "--base", "beta", "--head", "dev",
              "--title", f"Promote dev to beta for v{version}", "--body", body)
    print(f"Opened {url}. It merges on the repo owner's go once its checks are green.")


def selftest():
    cl = "# Changelog\n\n## Unreleased\n\nS.\n\n### ✨ New\n\n- 🏒 x\n\n## v2026.11.1\n\nOld.\n"
    out = rename_unreleased(cl, "2026.11.2")
    assert "## v2026.11.2\n\nS." in out and "## Unreleased" not in out
    try:
        rename_unreleased("# Changelog\n\n## v2026.11.1\n", "2026.11.2")
        raise AssertionError("released with no Unreleased section")
    except SystemExit:
        pass
    y = 'substitutions:\n  name: x\n  version: "2026.11.1"\n  comment: "v${version}"\n'
    assert yaml_version(y) == "2026.11.1"
    assert yaml_version(set_version(y, "2026.11.2")) == "2026.11.2"
    assert 'comment: "v${version}"' in set_version(y, "2026.11.2")
    assert problems_on(cl, []) == []
    assert any("nothing" in p for p in problems_on("# Changelog\n\n## Unreleased\n\n## v1.0.0\n", []))
    assert any(NOTE in p for p in problems_on(cl, ["docs/x.md:3"]))
    assert section_text(out, "v2026.11.2").startswith("S.")
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--dev-ref", help="with --dry-run: check this commit in place of origin/dev")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return 0
    if args.dev_ref:
        if not args.dry_run:
            ap.error("--dev-ref only goes with --dry-run")
        global DEV
        DEV = args.dev_ref
    run("git", "fetch", "origin", "--tags", "--quiet")
    changelog = show(DEV, "CHANGELOG.md")
    yaml_v = yaml_version(show(DEV, YAML))
    tags = beta_version.remote_tags()
    now = datetime.datetime.now(zoneinfo.ZoneInfo("America/Chicago"))
    version = "%d.%d.%d" % beta_version.next_version(tags, yaml_v, now.year, now.month)

    prep_number = None
    if f"## v{version}" in changelog.splitlines() and yaml_v == version:
        print(f"dev already names v{version}, so there's no release prep to do.")
    else:
        hits = subprocess.run(["git", "grep", "-n", NOTE, DEV, "--", "docs/wiki/src"],
                              cwd=ROOT, text=True, encoding="utf-8", capture_output=True).stdout.splitlines()
        problems = problems_on(changelog, hits)
        if problems:
            for p in problems:
                print(f"error: {p}")
            print("Fix these on dev first; dev isn't ready to release.")
            return 1
        print(f"dev is ready to release as v{version}.")
        prep_number = prep(version, args.dry_run)
    promote(version, prep_number, args.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
