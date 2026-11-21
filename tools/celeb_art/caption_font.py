"""Writes components/gameday/celeb_caption_font.h: the printable ASCII
glyphs of firmware/fonts/spleen-5x8.bdf as one byte a row, for the words
celeb_caption.h draws over a big play's art (a scorer's name, a minute).

Usage: python3 caption_font.py [out .h]"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def bdf(path):
    glyphs, code, rows, inb = {}, None, [], False
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("ENCODING"):
            code = int(line.split()[1])
        elif line.startswith("BITMAP"):
            inb, rows = True, []
        elif line.startswith("ENDCHAR"):
            if code is not None:
                glyphs[code] = rows
            inb = False
        elif inb:
            rows.append(int(line.strip(), 16))
    return glyphs


def main():
    dst = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "components" / "gameday" / "celeb_caption_font.h"
    glyphs = bdf(ROOT / "firmware" / "fonts" / "spleen-5x8.bdf")
    rows = []
    for code in range(32, 127):
        g = (glyphs.get(code, []) + [0] * 8)[:8]
        rows.append("    " + ", ".join(f"0x{b:02X}" for b in g) + f",  // {chr(code)!r}")
    text = "\n".join([
        "// Made by tools/celeb_art/caption_font.py from firmware/fonts/spleen-5x8.bdf",
        "// (Spleen, BSD 2-clause); do not edit. ASCII 32 to 126, eight rows a",
        "// glyph, the leftmost pixel in the top bit.",
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace espn {",
        "",
        "inline constexpr uint8_t kCaptionFont5x8[95][8] = {",
        *rows,
        "};",
        "",
        "}  // namespace espn",
        "",
    ])
    dst.write_text(text)
    print(f"95 glyphs -> {dst}")


if __name__ == "__main__":
    main()
