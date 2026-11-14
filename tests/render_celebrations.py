"""Turns render_celebrations' raw frames into GIFs that look like the panel:
round LEDs on black, the words in Spleen 5x8 drawn twice size over the frames
where the panel's label goes. Needs Pillow.
Usage: render_celebrations.py <render_celebrations binary> <out dir> [width]"""
import json
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
S = 6  # screen pixels per LED


def bdf_glyphs(path):
    """The 5x8 Spleen glyphs as {codepoint: [row bitmasks]}."""
    glyphs, code, rows, in_bitmap = {}, None, [], False
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("ENCODING"):
            code = int(line.split()[1])
        elif line.startswith("BITMAP"):
            in_bitmap, rows = True, []
        elif line.startswith("ENDCHAR"):
            if code is not None and code >= 0:
                glyphs[code] = rows
            in_bitmap, code = False, None
        elif in_bitmap:
            rows.append(int(line.strip(), 16))
    return glyphs


def draw_text(img, glyphs, text, y, color, scale):
    """Centered text, each glyph 5x8 scaled, 6 px advance like the panel's."""
    w = img.width
    adv = 6 * scale
    x = (w - len(text) * adv) // 2
    px = img.load()
    for ch in text:
        rows = glyphs.get(ord(ch), glyphs.get(63, []))
        for ry, bits in enumerate(rows[:8]):
            for rx in range(5):
                if bits & (0x80 >> rx):
                    for dy in range(scale):
                        for dx in range(scale):
                            xx, yy = x + rx * scale + dx, y + ry * scale + dy
                            if 0 <= xx < w and 0 <= yy < img.height:
                                px[xx, yy] = color
        x += adv


def leds(img):
    out = Image.new("RGB", (img.width * S, img.height * S), (10, 10, 10))
    d = ImageDraw.Draw(out)
    for y in range(img.height):
        for x in range(img.width):
            c = img.getpixel((x, y))
            d.ellipse([x * S + 1, y * S + 1, x * S + S - 2, y * S + S - 2], fill=c if any(c) else (24, 24, 24))
    return out


def main():
    binary, out = sys.argv[1], Path(sys.argv[2])
    width = sys.argv[3] if len(sys.argv) > 3 else "128"
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([binary, str(out), width], check=True)
    glyphs = bdf_glyphs(HERE.parent / "firmware" / "fonts" / "spleen-5x8.bdf")
    for meta_path in sorted(out.glob(f"*-{width}.json")):
        m = json.loads(meta_path.read_text())
        raw = (out / (meta_path.stem + ".rgb565")).read_bytes()
        w, h, n = m["w"], m["h"], m["frames"]
        color = tuple(int(m["text_color"][i:i + 2], 16) for i in (0, 2, 4))
        scale = 2 if m["font"] >= 16 else 1
        frames = []
        for i in range(n):
            img = Image.new("RGB", (w, h))
            px = img.load()
            off = i * w * h * 2
            for y in range(h):
                for x in range(w):
                    v = raw[off + (y * w + x) * 2] | (raw[off + (y * w + x) * 2 + 1] << 8)
                    px[x, y] = ((v >> 11) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31)
            # The words, as the panel's label puts them; the fade is the
            # engine's, the label just goes away with the splash.
            draw_text(img, glyphs, m["text"], m["text_y"], color, scale)
            if m["line2"]:
                draw_text(img, glyphs, m["line2"], m["line2_y"], color, scale)
            frames.append(leds(img) if i % 1 == 0 else None)
        gif = out / (meta_path.stem + ".gif")
        frames[0].save(gif, save_all=True, append_images=frames[1:], duration=int(1000 / m["fps"]), loop=0,
                       optimize=False)
        frames[min(len(frames) - 1, int(m["fps"] * 1.2))].save(out / (meta_path.stem + ".png"))
        # A contact sheet: six moments of the show, three up, at 3x.
        picks = [min(len(frames) - 1, int(m["fps"] * t)) for t in (0.15, 0.6, 1.1, 1.7, 2.5, 3.6)]
        sheet = Image.new("RGB", (w * 3 * 3 + 8, h * 3 * 2 + 4), (40, 40, 40))
        for k, i in enumerate(picks):
            small = frames[i].resize((w * 3, h * 3), Image.BOX)
            sheet.paste(small, ((k % 3) * (w * 3 + 4), (k // 3) * (h * 3 + 4)))
        sheet.save(out / (meta_path.stem + "-sheet.png"))
        print(gif, n, "frames")
        os.remove(out / (meta_path.stem + ".rgb565"))


if __name__ == "__main__":
    main()
