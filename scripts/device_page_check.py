"""The page a firmware image serves at / is our device page.

ESPHome's web_server builds that page. With js_include it loads our bundle
from /0.js, but ESPHome can embed its own page instead and drop ours from the
image (2026.11.0 began to, for a Wi-Fi config with only an ap:), and the build
stays green while an owner opening the panel's address gets ESPHome's page. It
can also add a script from another host, which an owner's browser waits for
when there's no internet.

The page is in the image as plain text, so this reads it there and fails when:
  no page      nothing in the image loads /0.js
  two pages    more than one does, so which one / serves isn't known
  no style     the page doesn't load /0.css
  other host   the page loads something that isn't on the panel

CI's Build job runs it for every board.

  python3 scripts/device_page_check.py <image.bin>
      e.g. firmware/.esphome/build/gameday-moonhub75/build/firmware.bin
  python3 scripts/device_page_check.py --selftest
"""

import re
import sys
from pathlib import Path

PAGE = re.compile(rb"<!DOCTYPE html>(?:(?!<!DOCTYPE html>)[\x09\x0a\x0d\x20-\x7e])*?</html>", re.I)
LOADS = re.compile(r"""\b(?:src|href)\s*=\s*["']?([^"'\s>]+)""", re.I)


def check(image):
    """(our page or None, problems) for an image's bytes."""
    pages = [m.group().decode("ascii") for m in PAGE.finditer(image)]
    ours = [p for p in pages if "/0.js" in LOADS.findall(p)]
    if not ours:
        return None, ["no page: nothing in the image loads /0.js, so / is ESPHome's own page "
                      "(web_server's local and js_url in firmware/gameday-common.yaml)"]
    if len(ours) > 1:
        return None, [f"two pages: {len(ours)} pages in the image load /0.js"]
    loads = LOADS.findall(ours[0])
    problems = []
    if "/0.css" not in loads:
        problems.append("no style: the page doesn't load /0.css")
    problems += [f"other host: the page loads {u}" for u in loads
                 if u.startswith("//") or re.match(r"[a-z][a-z0-9+.-]*:", u, re.I) and not u.startswith("data:")]
    return ours[0], problems


HEAD = b"<!DOCTYPE html><html><head><meta charset=UTF-8><link rel=icon href=data:>"
CSS = b"<link rel=stylesheet href=/0.css>"
JS = b"</head><body><script type=module src=/0.js></script><esp-app></esp-app>"
END = b"</body></html>"
OURS = HEAD + CSS + JS + END
HOSTED = HEAD + CSS + JS + b'<script src="https://oi.esphome.io/v3/www.js"></script>' + END
OTHER = b"<!DOCTYPE html><html><body>setup</body></html>"
# The route check in web_server.cpp keeps these in every image.
ROUTES = b"\xe9\x00/0.js\x00/0.css\x00"

# name: (image, want problems that each start with these words)
SELFTEST = {
    "our page passes": (ROUTES + b"\x00" + OURS + b"\x00", []),
    "our page beside another one passes": (ROUTES + OTHER + b"\x00" + OURS, []),
    "an embedded page, ours dropped, fails": (ROUTES + b"\x1f\x8b\x08\x00", ["no page"]),
    "only another page fails": (ROUTES + OTHER, ["no page"]),
    "ESPHome's hosted script fails": (ROUTES + HOSTED, ["other host"]),
    "a script with no scheme fails": (HEAD + CSS + JS + b"<script src=//x.io/a.js></script>" + END, ["other host"]),
    "a page without our style fails": (HEAD + JS + END, ["no style"]),
    "two pages fail": (OURS + b"\x00" + OURS, ["two pages"]),
}


def selftest():
    bad = 0
    for name, (image, want) in SELFTEST.items():
        got = check(image)[1]
        ok = len(got) == len(want) and all(g.startswith(w) for g, w in zip(got, want))
        print(f"{'ok ' if ok else 'BAD'}  {name}: {got or 'no problems'}")
        bad += not ok
    return 1 if bad else 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    page, problems = check(Path(sys.argv[1]).read_bytes())
    for p in problems:
        print(f"::error::{sys.argv[1]}: {p}")
    if problems:
        return 1
    print(f"/ is our device page ({len(page)} bytes): {page}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
