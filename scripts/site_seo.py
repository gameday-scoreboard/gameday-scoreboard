#!/usr/bin/env python3
"""Check the site's search and sharing tags, and add what a deploy builds.

check <dir>: every page (an index.html outside firmware/) has its own title
and description, a canonical URL that matches where it lives, Open Graph and
Twitter card tags, a share image that exists in the site, and JSON-LD that
parses. A page with <meta name="robots" content="noindex"> (a redirect, a
test-board installer) is skipped here and left out of the sitemap. CI runs it
on docs/site.

build <dir>: check, then add the FAQPage JSON-LD to support/ from the page's
own cards, and write sitemap.xml and robots.txt. Both site deploys run it on
the assembled site, after docs/site is copied in.

    python3 scripts/site_seo.py check docs/site
    python3 scripts/site_seo.py build site
    python3 scripts/site_seo.py --selftest
"""

import json
import os
import re
import sys
import tempfile
from html.parser import HTMLParser

BASE = "https://install.gameday-scoreboard.com"
FAQ_PAGE = "support/index.html"
REQUIRED_PROPS = ["og:title", "og:description", "og:url", "og:image", "og:type"]
REQUIRED_NAMES = ["description", "twitter:card", "twitter:image"]


class Head(HTMLParser):
    """The tags check() reads, from one page."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.title = ""
        self.meta = {}
        self.canonical = None
        self.ld = []
        self._in = None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "title":
            self._in = "title"
        elif tag == "meta":
            key = a.get("property") or a.get("name")
            if key:
                self.meta[key] = a.get("content", "")
        elif tag == "link" and a.get("rel") == "canonical":
            self.canonical = a.get("href")
        elif tag == "script" and a.get("type") == "application/ld+json":
            self._in = "ld"
            self.ld.append("")

    def handle_endtag(self, tag):
        if tag in ("title", "script"):
            self._in = None

    def handle_data(self, data):
        if self._in == "title":
            self.title += data
        elif self._in == "ld":
            self.ld[-1] += data


class Cards(HTMLParser):
    """Each <div class="card">'s h2 and the text of the rest of it."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.cards = []
        self._depth = 0
        self._in_h2 = False

    def handle_starttag(self, tag, attrs):
        if tag == "div":
            if self._depth:
                self._depth += 1
            elif "card" in (dict(attrs).get("class") or "").split():
                self._depth = 1
                self.cards.append(["", ""])
        elif self._depth and tag == "h2":
            self._in_h2 = True
        elif self._depth and tag in ("p", "li"):
            self.cards[-1][1] += " "

    def handle_endtag(self, tag):
        if tag == "div" and self._depth:
            self._depth -= 1
        elif tag == "h2":
            self._in_h2 = False

    def handle_data(self, data):
        if not self._depth:
            return
        self.cards[-1][0 if self._in_h2 else 1] += data


def squash(s):
    return re.sub(r"\s+", " ", s).strip()


def noindex(path):
    with open(path, encoding="utf-8") as f:
        head = Head()
        head.feed(f.read())
    return "noindex" in head.meta.get("robots", "")


def pages(site):
    """(path under the site, URL) for every page search engines may list, home first."""
    found = []
    for root, dirs, files in os.walk(site):
        dirs[:] = sorted(d for d in dirs if not (root == site and d == "firmware") and not d.startswith("."))
        if "index.html" in files and not noindex(os.path.join(root, "index.html")):
            rel = os.path.relpath(os.path.join(root, "index.html"), site).replace(os.sep, "/")
            url = BASE + "/" + rel[: -len("index.html")]
            found.append((rel, url))
    return sorted(found, key=lambda p: (p[0] != "index.html", p[0]))


def check(site):
    errors = []
    seen_titles, seen_descs = {}, {}
    for rel, url in pages(site):
        with open(os.path.join(site, rel), encoding="utf-8") as f:
            head = Head()
            head.feed(f.read())
        title = squash(head.title)
        desc = head.meta.get("description", "")
        if not title:
            errors.append(f"{rel}: no <title>")
        elif title in seen_titles:
            errors.append(f"{rel}: same title as {seen_titles[title]}")
        else:
            seen_titles[title] = rel
        if desc in seen_descs:
            errors.append(f"{rel}: same description as {seen_descs[desc]}")
        if desc:
            seen_descs[desc] = rel
        if desc and not 50 <= len(desc) <= 160:
            errors.append(f"{rel}: description is {len(desc)} characters, keep it 50 to 160")
        for key in REQUIRED_NAMES + REQUIRED_PROPS:
            if not head.meta.get(key):
                errors.append(f"{rel}: no {key}")
        if head.canonical != url:
            errors.append(f"{rel}: canonical is {head.canonical}, should be {url}")
        if head.meta.get("og:url") and head.meta["og:url"] != url:
            errors.append(f"{rel}: og:url is {head.meta['og:url']}, should be {url}")
        for key in ("og:image", "twitter:image"):
            img = head.meta.get(key, "")
            if img and not (img.startswith(BASE + "/") and os.path.isfile(os.path.join(site, img[len(BASE) + 1 :]))):
                errors.append(f"{rel}: {key} {img} isn't a file on this site")
        for i, block in enumerate(head.ld):
            try:
                json.loads(block)
            except ValueError as e:
                errors.append(f"{rel}: JSON-LD block {i + 1} doesn't parse: {e}")
    return errors


def faq(html):
    """The FAQPage JSON-LD for a page's cards, as a <script> tag."""
    cards = Cards()
    cards.feed(html)
    entities = [
        {"@type": "Question", "name": squash(q), "acceptedAnswer": {"@type": "Answer", "text": squash(a)}}
        for q, a in cards.cards
        if squash(q) and squash(a)
    ]
    ld = {"@context": "https://schema.org", "@type": "FAQPage", "mainEntity": entities}
    # "</" inside a script element would end it early.
    body = json.dumps(ld, ensure_ascii=False, indent=2).replace("</", "<\\/")
    return f'<script type="application/ld+json">\n{body}\n</script>\n'


def build(site):
    errors = check(site)
    if errors:
        return errors
    path = os.path.join(site, FAQ_PAGE)
    if os.path.isfile(path):
        with open(path, encoding="utf-8") as f:
            html = f.read()
        with open(path, "w", encoding="utf-8") as f:
            f.write(html.replace("</head>", faq(html) + "</head>", 1))
    urls = "".join(f"  <url><loc>{url}</loc></url>\n" for _, url in pages(site))
    with open(os.path.join(site, "sitemap.xml"), "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        f.write(f'<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n{urls}</urlset>\n')
    # The firmware files are for panels and the installer, not search results.
    with open(os.path.join(site, "robots.txt"), "w", encoding="utf-8") as f:
        f.write(f"User-agent: *\nDisallow: /firmware/\n\nSitemap: {BASE}/sitemap.xml\n")
    return []


def page(path, title, desc, extra=""):
    url = BASE + "/" + path[: -len("index.html")]
    return f"""<!doctype html><html><head><title>{title}</title>
<meta name="description" content="{desc}"><link rel="canonical" href="{url}">
<meta property="og:title" content="{title}"><meta property="og:description" content="{desc}">
<meta property="og:url" content="{url}"><meta property="og:type" content="website">
<meta property="og:image" content="{BASE}/media/og.jpg"><meta name="twitter:card" content="summary_large_image">
<meta name="twitter:image" content="{BASE}/media/og.jpg">{extra}</head><body><main>
<div class="card"><h2>Lines on the panel?</h2><p>Unplug it for <strong>10 seconds</strong>.</p>
<p class="note">Still there? Email <a href="mailto:x@y">support</a>.</p></div>
<div class="card"><h2>Outside?</h2><ol><li>Indoors only.</li><li>Keep it dry.</li></ol></div>
</main></body></html>"""


def selftest():
    with tempfile.TemporaryDirectory() as site:
        desc = "A description long enough to pass the fifty character floor, page "
        for rel in ["index.html", "support/index.html", "install/diy/index.html", "firmware/x/index.html"]:
            os.makedirs(os.path.dirname(os.path.join(site, rel)) or site, exist_ok=True)
            with open(os.path.join(site, rel), "w", encoding="utf-8") as f:
                f.write(page(rel, "Page " + rel, desc + rel))
        os.makedirs(os.path.join(site, "media"))
        open(os.path.join(site, "media/og.jpg"), "wb").close()
        # A noindex page needs none of the tags and stays out of the sitemap.
        os.makedirs(os.path.join(site, "old"))
        with open(os.path.join(site, "old/index.html"), "w", encoding="utf-8") as f:
            f.write('<!doctype html><html><head><meta name="robots" content="noindex"></head></html>')
        assert [p for p, _ in pages(site)] == ["index.html", "install/diy/index.html", "support/index.html"]
        assert check(site) == [], check(site)
        assert build(site) == []

        with open(os.path.join(site, "support/index.html"), encoding="utf-8") as f:
            head = Head()
            head.feed(f.read())
        ld = json.loads(head.ld[0])
        assert ld["@type"] == "FAQPage"
        assert [q["name"] for q in ld["mainEntity"]] == ["Lines on the panel?", "Outside?"]
        assert ld["mainEntity"][0]["acceptedAnswer"]["text"] == "Unplug it for 10 seconds. Still there? Email support."
        assert ld["mainEntity"][1]["acceptedAnswer"]["text"] == "Indoors only. Keep it dry."
        with open(os.path.join(site, "sitemap.xml"), encoding="utf-8") as f:
            sitemap = f.read()
        assert f"<loc>{BASE}/</loc>" in sitemap and f"<loc>{BASE}/install/diy/</loc>" in sitemap
        assert "firmware" not in sitemap and "/old/" not in sitemap
        with open(os.path.join(site, "robots.txt"), encoding="utf-8") as f:
            assert f"Sitemap: {BASE}/sitemap.xml" in f.read()

        # A copied page, a wrong canonical, a missing image and broken JSON-LD all fail.
        with open(os.path.join(site, "install/diy/index.html"), "w", encoding="utf-8") as f:
            f.write(page("index.html", "Page index.html", desc + "index.html", '<script type="application/ld+json">{</script>'))
        os.remove(os.path.join(site, "media/og.jpg"))
        errors = "\n".join(check(site))
        for want in ["same title", "same description", "canonical is", "og:url is", "isn't a file", "doesn't parse"]:
            assert want in errors, (want, errors)
    print("selftest ok")


def main(argv):
    if argv[1:] == ["--selftest"]:
        selftest()
        return 0
    if len(argv) != 3 or argv[1] not in ("check", "build"):
        print(__doc__, file=sys.stderr)
        return 2
    errors = (check if argv[1] == "check" else build)(argv[2])
    for e in errors:
        print(f"::error::{e}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
