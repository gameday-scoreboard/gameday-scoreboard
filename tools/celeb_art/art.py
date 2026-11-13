"""Shared drawing for the big plays' pixel art: an indexed canvas, text
sprites, 2 px limbs, the panel's Spleen 5x8 font at twice the size, and a
contact sheet for looking at a whole show at once.

Each sport's frames script (<sport>_frames.py) has its own PALETTE, a list of
(name, (r, g, b)) of at most 15 colors; index 15 is pack.py's "unchanged".
The names team, team_shade, alt, alt_shade and team_light are the team's
colors: the PNGs preview them in red and white and the panel swaps in each
team's own. pack.py reads the same PALETTE."""
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
FONT = HERE.parents[1] / "firmware" / "fonts" / "spleen-5x8.bdf"

# The team's colors as the previews show them.
TEAM_PREVIEW = [
    ("team", (210, 28, 40)),
    ("team_shade", (118, 16, 22)),
    ("alt", (255, 255, 255)),
    ("alt_shade", (150, 150, 150)),
    ("team_light", (232, 130, 136)),
]


def index_of(palette):
    names = [n for n, _ in palette]
    if len(names) != len(set(names)):
        raise ValueError("palette names repeat")
    if len(palette) > 15:
        raise ValueError(f"{len(palette)} colors; 15 is the most")
    return {n: i for i, n in enumerate(names)}


def sprite(text, chars, idx):
    """A sprite from rows of letters: chars maps a letter to a palette name;
    any other letter is see-through."""
    rows = text.strip("\n").splitlines()
    return [[idx[chars[c]] if c in chars else None for c in row] for row in rows]


def mirror(spr):
    return [list(reversed(r)) for r in spr]


class Canvas:
    def __init__(self, w, h, palette):
        self.w, self.h, self.palette = w, h, palette
        self.px = [[0] * w for _ in range(h)]

    def put(self, x, y, i):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = i

    def get(self, x, y):
        return self.px[y][x] if 0 <= x < self.w and 0 <= y < self.h else 0

    def rect(self, x0, y0, x1, y1, i):
        for y in range(int(y0), int(y1) + 1):
            for x in range(int(x0), int(x1) + 1):
                self.put(x, y, i)

    def blit(self, spr, x, y):
        for dy, row in enumerate(spr):
            for dx, i in enumerate(row):
                if i is not None:
                    self.put(x + dx, y + dy, i)

    def line(self, x0, y0, x1, y1, i):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for k in range(n + 1):
            u = k / n
            self.put(x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, i)

    def thick(self, x0, y0, x1, y1, color, shade):
        """A 2 px limb: the color, its lower-right edge in the shade."""
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for k in range(n + 1):
            u = k / n
            x, y = x0 + (x1 - x0) * u, y0 + (y1 - y0) * u
            self.put(x + 1, y, shade)
            self.put(x, y + 1, shade)
        for k in range(n + 1):
            u = k / n
            self.put(x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, color)

    def shifted(self, dx, dy):
        """The frame moved by dx, dy (a screen shake), black where it left."""
        out = Canvas(self.w, self.h, self.palette)
        for y in range(self.h):
            for x in range(self.w):
                out.px[y][x] = self.get(x - dx, y - dy)
        return out

    def image(self):
        img = Image.new("P", (self.w, self.h))
        flat = []
        for _, rgb in self.palette:
            flat.extend(rgb)
        while len(flat) < 45:
            flat.extend([0, 0, 0])
        flat.extend([255, 0, 255])  # 15, never in a frame
        img.putpalette(flat + [0] * (768 - len(flat)))
        img.putdata([i for row in self.px for i in row])
        return img


def bdf(path=FONT):
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


ADV2 = 12  # a letter's advance at twice the size


def text2x_width(s):
    return len(s) * ADV2 - 2


def text2x(c, glyphs, s, y, fill, edge, cx=None, drop_y=None):
    """Spleen 5x8 at twice the size with an edge color all round, centered on
    cx (the middle of the canvas by default). drop_y moves each letter."""
    if cx is None:
        cx = c.w / 2
    x0 = int(round(cx - text2x_width(s) / 2))
    on = set()
    for n, ch in enumerate(s):
        rows = glyphs.get(ord(ch), [])
        dy = 0 if drop_y is None else drop_y[n]
        for ry, bits in enumerate(rows[:8]):
            for rx in range(5):
                if bits & (0x80 >> rx):
                    for sy in range(2):
                        for sx in range(2):
                            on.add((x0 + n * ADV2 + rx * 2 + sx, y + dy + ry * 2 + sy))
    for (x, y) in on:
        for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1), (1, 1), (2, 2), (1, 2), (2, 1), (-1, -1), (-1, 1), (1, -1)):
            if (x + ox, y + oy) not in on:
                c.put(x + ox, y + oy, edge)
    for (x, y) in on:
        c.put(x, y, fill)


def save_frames(frames, out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.png"):
        old.unlink()
    for n, c in enumerate(frames):
        c.image().save(out / f"{n:03d}.png")


def sheet(frames, path, cols=6, scale=3):
    """Every frame on one PNG, scaled up, with a gap between them."""
    w, h = frames[0].w, frames[0].h
    rows = (len(frames) + cols - 1) // cols
    img = Image.new("RGB", (cols * (w * scale + 4), rows * (h * scale + 4)), (40, 40, 40))
    for n, c in enumerate(frames):
        im = c.image().convert("RGB").resize((w * scale, h * scale), Image.NEAREST)
        img.paste(im, ((n % cols) * (w * scale + 4), (n // cols) * (h * scale + 4)))
    img.save(path)
