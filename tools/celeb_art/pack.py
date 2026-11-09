"""Packs indexed PNG frames (a <sport>_frames.py script's, or frames edited
in a pixel-art tool with the same palette) into a C++ header for
celeb_art.h.

Each frame becomes runs of one byte: the palette index in the high nibble,
the run length - 1 in the low one, or 15 there and one more byte for runs of
16 to 271. After the first frame, pixels that match the frame before are
index 15 ("unchanged"), so a still background costs almost nothing.

The palette comes from the frames script: --palette names a module next to
this one whose PALETTE is a list of (name, (r, g, b)), the names team,
team_shade, alt, alt_shade and team_light standing for the team's colors.

Usage: python3 pack.py <frames dir> <play>_<width> <fps> <out .h> --palette <module>
  e.g. python3 pack.py frames/dunk_128 dunk_128 12 \
         ../../components/gameday/celeb_art_dunk_128.h --palette basketball_frames"""
import importlib
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))

KEEP = 15
ROLES = {
    "team": "kArtTeam",
    "team_shade": "kArtTeamShade",
    "alt": "kArtAlt",
    "alt_shade": "kArtAltShade",
    "team_light": "kArtTeamLight",
}


def runs(pixels):
    out = bytearray()
    i, n = 0, len(pixels)
    while i < n:
        j = i
        while j < n and pixels[j] == pixels[i] and j - i < 271:
            j += 1
        length = j - i
        if length <= 15:
            out.append((pixels[i] << 4) | (length - 1))
        else:
            out.append((pixels[i] << 4) | 15)
            out.append(length - 16)
        i = j
    return out


def main():
    args = sys.argv[1:]
    if "--palette" not in args or len(args) != 6:
        sys.exit(__doc__)
    k = args.index("--palette")
    module = args[k + 1]
    del args[k:k + 2]
    PALETTE = importlib.import_module(module).PALETTE
    if len(PALETTE) > 15:
        sys.exit(f"{module}.PALETTE has {len(PALETTE)} colors; 15 is the most (index 15 is reserved)")
    src, name, fps, dst = Path(args[0]), args[1], int(args[2]), Path(args[3])
    files = sorted(src.glob("*.png"))
    if not files:
        sys.exit(f"{src}: no frames")
    frames = []
    w = h = None
    for f in files:
        img = Image.open(f)
        if img.mode != "P":
            sys.exit(f"{f}: not an indexed PNG")
        w, h = img.size
        px = list(img.getdata())
        if max(px) >= KEEP:
            sys.exit(f"{f}: uses index {max(px)}; 15 is reserved")
        frames.append(px)
    data, offsets, prev = bytearray(), [], None
    for px in frames:
        offsets.append(len(data))
        coded = px if prev is None else [KEEP if a == b else a for a, b in zip(px, prev)]
        data += runs(coded)
        prev = px
    offsets.append(len(data))
    pal = []
    for pname, rgb in PALETTE:
        pal.append(ROLES.get(pname, "0x%06X" % ((rgb[0] << 16) | (rgb[1] << 8) | rgb[2])))
    while len(pal) < 16:
        pal.append("0x000000")
    ident = "".join(p.capitalize() for p in name.split("_"))
    lines = [
        f"// Made by tools/celeb_art/pack.py from {len(frames)} frames at {w}x{h}, {fps} fps; do not edit.",
        f"// {len(data)} bytes of runs for {len(frames) * w * h} pixels.",
        "#pragma once",
        "#include <cstdint>",
        "",
        '#include "celeb_art.h"',
        "",
        "namespace espn {",
        "",
        f"inline constexpr uint32_t kArt{ident}Palette[16] = {{{', '.join(pal)}}};",
        f"inline constexpr uint32_t kArt{ident}Offsets[{len(offsets)}] = {{{', '.join(map(str, offsets))}}};",
        f"inline constexpr uint8_t kArt{ident}Data[{len(data)}] = {{",
    ]
    for k in range(0, len(data), 24):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[k:k + 24]) + ",")
    lines += [
        "};",
        f"inline const CelebArt &celeb_art_{name}() {{",
        f"  static constexpr CelebArt art{{{w}, {h}, {fps}, {len(frames)}, kArt{ident}Palette, kArt{ident}Offsets, "
        f"kArt{ident}Data}};",
        "  return art;",
        "}",
        "",
        "}  // namespace espn",
        "",
    ]
    dst.write_text("\n".join(lines))
    print(f"{len(frames)} frames, {len(data)} bytes of data, {len(offsets) * 4 + 64} bytes of tables -> {dst}")


if __name__ == "__main__":
    main()
