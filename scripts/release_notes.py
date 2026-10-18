#!/usr/bin/env python3
"""Print the CHANGELOG.md section for one version, or render the site's changelog page.

    python scripts/release_notes.py v0.2.2            # full section body
    python scripts/release_notes.py v0.2.2 --summary  # one line for the firmware manifest
    python scripts/release_notes.py --page site/changelog/index.html [--dates releases.json] [--date v0.2.2=2026-10-20]

Exits non-zero when the section is missing, which is what makes a tag without
release notes fail CI.

--page fills the <!-- RELEASES --> marker in that file (a copy of
docs/site/changelog/index.html) with every release, newest first. --dates is
`gh release list --json tagName,publishedAt`; --date adds one the list doesn't
have yet, like the tag being released in this run.
"""

import html
import json
import re
import sys
from datetime import date, datetime
from pathlib import Path
from zoneinfo import ZoneInfo

CHANGELOG = Path(__file__).resolve().parent.parent / "CHANGELOG.md"
HEADING = re.compile(r"^## (v\d+(?:\.\d+)+)\s*$", re.M)
MARKER = "<!-- RELEASES -->"
# Release dates are shown in the business's own time zone, so an evening
# release isn't dated the next day.
ZONE = ZoneInfo("America/Chicago")


def section(version: str) -> str:
    version = version if version.startswith("v") else "v" + version
    text = CHANGELOG.read_text(encoding="utf-8")
    m = re.search(rf"^## {re.escape(version)}\s*$(.*?)(?=^## |\Z)", text, re.M | re.S)
    if not m or not m.group(1).strip():
        raise SystemExit(f"CHANGELOG.md has no '## {version}' section. Add one before tagging.")
    return m.group(1).strip() + "\n"


def summary(body: str, limit: int = 240) -> str:
    """Bullets joined into one line with " | " (the page splits on it), trimmed to fit an ESPHome text field."""
    bullets = [ln[2:].strip() for ln in body.splitlines() if ln.startswith("- ")]
    out = " | ".join(bullets) if bullets else body.strip().splitlines()[0]
    return out if len(out) <= limit else out[: limit - 3].rstrip() + "..."


def releases() -> list[tuple[str, str]]:
    """(version, body) for every section, newest version first."""
    text = CHANGELOG.read_text(encoding="utf-8")
    heads = list(HEADING.finditer(text))
    out = []
    for i, m in enumerate(heads):
        end = heads[i + 1].start() if i + 1 < len(heads) else len(text)
        body = text[m.end():end].strip()
        if body:
            out.append((m.group(1), body))
    return sorted(out, key=lambda r: [int(n) for n in r[0][1:].split(".")], reverse=True)


def headline(body: str) -> str:
    """The section's one-line summary: a plain line above the bullets if it has one, else the first bullet's first sentence."""
    lead = []
    for ln in body.splitlines():
        if ln.startswith("- "):
            break
        if ln.strip():
            lead.append(ln.strip())
    if lead:
        return " ".join(lead)
    first = next((ln[2:].strip() for ln in body.splitlines() if ln.startswith("- ")), "")
    m = re.match(r"(.+?[.!?])\s+(?=[A-Z\"])", first)
    return m.group(1) if m else first


def release_dates(path: str | None, extra: list[str]) -> dict[str, date]:
    dates = {}
    if path:
        for r in json.loads(Path(path).read_text(encoding="utf-8")):
            when = datetime.fromisoformat(r["publishedAt"].replace("Z", "+00:00"))
            dates[r["tagName"]] = when.astimezone(ZONE).date()
    for item in extra:
        tag, _, day = item.partition("=")
        dates[tag] = date.fromisoformat(day)
    return dates


def render(dates: dict[str, date]) -> str:
    parts = []
    for i, (version, body) in enumerate(releases()):
        bullets = [ln[2:].strip() for ln in body.splitlines() if ln.startswith("- ")]
        line = headline(body)
        when = dates.get(version)
        meta = f'<p class="when">{when:%B} {when.day}, {when.year}</p>' if when else ""
        card = [f'<section class="card" id="{version}">',
                f'  <h2><a href="#{version}">{version}</a></h2>{meta}',
                f'  <p class="sum">{html.escape(line, quote=False)}</p>']
        if bullets and bullets != [line]:
            n = len(bullets)
            items = "\n".join(f"      <li>{html.escape(b, quote=False)}</li>" for b in bullets)
            card += [f'  <details{" open" if i == 0 else ""}>',
                     f'    <summary>{"The change" if n == 1 else f"All {n} changes"}</summary>',
                     f"    <ul>\n{items}\n    </ul>",
                     "  </details>"]
        card.append("</section>")
        parts.append("\n".join(card))
    return "\n\n".join(parts)


def page(out: str, dates: dict[str, date]) -> None:
    path = Path(out)
    text = path.read_text(encoding="utf-8")
    if MARKER not in text:
        raise SystemExit(f"{out} has no {MARKER} marker to fill.")
    path.write_text(text.replace(MARKER, render(dates)), encoding="utf-8", newline="\n")
    print(f"wrote {out} ({len(releases())} releases)")


def option(name: str) -> list[str]:
    return [sys.argv[i + 1] for i, a in enumerate(sys.argv[:-1]) if a == name]


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == "--page":
        dates_file = option("--dates")
        page(sys.argv[2], release_dates(dates_file[0] if dates_file else None, option("--date")))
        raise SystemExit(0)
    body = section(sys.argv[1])
    print(summary(body) if "--summary" in sys.argv else body, end="")
