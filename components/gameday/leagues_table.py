"""leagues.h's kLeagues table, read once for every Python file that needs it:
__init__.py, scripts/build_teams.py, scripts/build_web.py and
tests/web/mock_device.py. No ESPHome imports, so the scripts load it by path.

A row the pattern can't read stops the caller instead of dropping the league.
"""

from pathlib import Path
import re

LEAGUES_H = Path(__file__).resolve().parent / "leagues.h"

# LeagueInfo's fields, in order.
_ROW_RE = re.compile(
    r'\{League::(\w+),\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*Sport::(\w+),'
    r"\s*(true|false),\s*(true|false),\s*(\d+)\}"
)


def leagues(text=None):
    """kLeagues rows in order, each a dict: league (the enum name), key, path,
    prefix, logo_dir, sport, season_list, logo_by_id, linger_min."""
    if text is None:
        text = LEAGUES_H.read_text(encoding="utf-8")
    start = text.find("kLeagues[] = {")
    end = text.find("\n};", start)
    if start < 0 or end < 0:
        raise ValueError("leagues.h has no kLeagues table")
    body = "\n".join(ln for ln in text[start:end].splitlines() if not ln.lstrip().startswith("//"))
    rows = []
    for m in _ROW_RE.finditer(body):
        lg, key, path, prefix, logo_dir, sport, season_list, by_id, linger = m.groups()
        rows.append(
            {
                "league": lg,
                "key": key,
                "path": path,
                "prefix": prefix,
                "logo_dir": logo_dir,
                "sport": sport,
                "season_list": season_list == "true",
                "logo_by_id": by_id == "true",
                "linger_min": int(linger),
            }
        )
    if not rows or len(rows) != body.count("{League::"):
        raise ValueError(f"leagues.h: read {len(rows)} of {body.count('{League::')} kLeagues rows; check the field order")
    return rows
