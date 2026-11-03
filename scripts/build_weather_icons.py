#!/usr/bin/env python3
"""Draw the weather card's icons (firmware/images/wx-*.png): eight skies in
16x16 for today and 8x8 for the next two days. Pixel maps below, one letter
per LED; run it after changing one and commit the PNGs.
"""

from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parent.parent / "firmware" / "images"

PALETTE = {
    ".": None,
    "Y": (255, 200, 40),    # sun
    "y": (255, 240, 170),   # moon
    "W": (235, 235, 235),   # cloud, snow
    "G": (150, 150, 160),   # darker cloud, fog
    "B": (70, 140, 255),    # rain
    "D": (110, 110, 125),   # storm cloud
}

ICONS16 = {
    "sun": [
        "................",
        ".......Y........",
        "...Y...Y...Y....",
        "....Y.....Y.....",
        ".....YYYYY......",
        "....YYYYYYY.....",
        "...YYYYYYYYY....",
        ".YYYYYYYYYYYYY..",
        "...YYYYYYYYY....",
        "....YYYYYYY.....",
        ".....YYYYY......",
        "....Y.....Y.....",
        "...Y...Y...Y....",
        ".......Y........",
        "................",
        "................",
    ],
    "moon": [
        "................",
        ".....yyyy.......",
        "....yyyyyy......",
        "...yyyyy........",
        "..yyyyy.........",
        "..yyyy..........",
        "..yyyy..........",
        "..yyyy..........",
        "..yyyy..........",
        "..yyyyy.........",
        "...yyyyy........",
        "....yyyyyy....y.",
        ".....yyyyyyyyy..",
        "................",
        "................",
        "................",
    ],
    "part": [
        "................",
        "...Y............",
        ".Y..YYY..Y......",
        "...YYYYY........",
        "..YYYYYYY.......",
        ".YYYYYYYY.......",
        "..YYYYYWWWW.....",
        "...YYWWWWWWW....",
        "...WWWWWWWWWW...",
        "..WWWWWWWWWWWW..",
        ".WWWWWWWWWWWWWW.",
        ".WWWWWWWWWWWWWW.",
        "..WWWWWWWWWWWW..",
        "................",
        "................",
        "................",
    ],
    "cloud": [
        "................",
        "................",
        "................",
        "......WWWW......",
        ".....WWWWWW.....",
        "....WWWWWWWW....",
        "...WWWWWWWWWW...",
        "..WWWWWWWWWWWW..",
        ".WWWWWWWWWWWWWW.",
        ".WWWWWWWWWWWWWW.",
        ".WWWWWWWWWWWWWW.",
        "..WWWWWWWWWWWW..",
        "................",
        "................",
        "................",
        "................",
    ],
    "fog": [
        "................",
        "................",
        "................",
        "..GGGGGGGGGGG...",
        "................",
        "....GGGGGGGGGGG.",
        "................",
        ".GGGGGGGGGGGG...",
        "................",
        "...GGGGGGGGGGGG.",
        "................",
        "..GGGGGGGGGG....",
        "................",
        "................",
        "................",
        "................",
    ],
    "rain": [
        "................",
        "......GGGG......",
        ".....GGGGGG.....",
        "....GGGGGGGG....",
        "..GGGGGGGGGGGG..",
        ".GGGGGGGGGGGGGG.",
        ".GGGGGGGGGGGGGG.",
        "..GGGGGGGGGGGG..",
        "................",
        "...B...B...B....",
        "..B...B...B.....",
        "................",
        "...B...B...B....",
        "..B...B...B.....",
        "................",
        "................",
    ],
    "snow": [
        "................",
        "......GGGG......",
        ".....GGGGGG.....",
        "....GGGGGGGG....",
        "..GGGGGGGGGGGG..",
        ".GGGGGGGGGGGGGG.",
        ".GGGGGGGGGGGGGG.",
        "..GGGGGGGGGGGG..",
        "................",
        "...W.....W......",
        "......W.....W...",
        "................",
        "...W.....W......",
        "......W.....W...",
        "................",
        "................",
    ],
    "storm": [
        "................",
        "......DDDD......",
        ".....DDDDDD.....",
        "....DDDDDDDD....",
        "..DDDDDDDDDDDD..",
        ".DDDDDDDDDDDDDD.",
        ".DDDDDDDDDDDDDD.",
        "..DDDDDDDDDDDD..",
        "........YY......",
        ".......YY.......",
        "......YYYY......",
        ".......YY.......",
        "......YY........",
        ".....Y..........",
        "................",
        "................",
    ],
}

ICONS8 = {
    "sun": [
        "...Y....",
        ".Y.Y.Y..",
        "..YYY...",
        "YYYYYYY.",
        "..YYY...",
        ".Y.Y.Y..",
        "...Y....",
        "........",
    ],
    "moon": [
        "..yyy...",
        ".yyy....",
        ".yy.....",
        ".yy.....",
        ".yy.....",
        ".yyy....",
        "..yyyy..",
        "........",
    ],
    "part": [
        ".Y......",
        "..YY....",
        ".YYYW...",
        "..YWWWW.",
        ".WWWWWW.",
        "WWWWWWW.",
        ".WWWWW..",
        "........",
    ],
    "cloud": [
        "........",
        "...WW...",
        "..WWWW..",
        ".WWWWWW.",
        "WWWWWWW.",
        "WWWWWWW.",
        ".WWWWW..",
        "........",
    ],
    "fog": [
        "........",
        ".GGGGG..",
        "........",
        "..GGGGG.",
        "........",
        "GGGGG...",
        "........",
        "........",
    ],
    "rain": [
        "...GG...",
        "..GGGG..",
        ".GGGGGG.",
        "GGGGGGG.",
        "........",
        ".B..B...",
        "B..B..B.",
        "........",
    ],
    "snow": [
        "...GG...",
        "..GGGG..",
        ".GGGGGG.",
        "GGGGGGG.",
        "........",
        ".W..W...",
        "...W..W.",
        "........",
    ],
    "storm": [
        "...DD...",
        "..DDDD..",
        ".DDDDDD.",
        "DDDDDDD.",
        "....Y...",
        "...YY...",
        "..Y.....",
        "........",
    ],
}


def draw(rows, size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()
    assert len(rows) == size, rows
    for y, row in enumerate(rows):
        assert len(row) == size, row
        for x, ch in enumerate(row):
            c = PALETTE[ch]
            if c is not None:
                px[x, y] = (*c, 255)
    return img


def main():
    for name, rows in ICONS16.items():
        draw(rows, 16).save(OUT / f"wx-{name}.png")
    for name, rows in ICONS8.items():
        draw(rows, 8).save(OUT / f"wx8-{name}.png")
    print(f"{len(ICONS16) + len(ICONS8)} icons in {OUT}")


if __name__ == "__main__":
    main()
