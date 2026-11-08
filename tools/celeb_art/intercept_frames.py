"""Draws the interception's pixel-art frames (bench pilot) as indexed PNGs,
128x64, one a frame, into frames/intercept/. pack.py turns them into the
firmware's celeb_art_intercept.h.

The PNGs use the art palette below. Indexes 1 to 5 are the team's colors
(previewed here in red and white); the panel swaps in each team's own. Edit
the frames in any pixel-art tool that keeps the indexed palette, or change
the poses here and run it again.

Usage: python3 intercept_frames.py [out dir]"""
import math
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
W, H, FPS = 128, 64, 12

# index: (name, preview RGB). 15 is reserved for "unchanged" by pack.py.
PALETTE = [
    ("black", (0, 0, 0)),
    ("team", (210, 28, 40)),
    ("team_shade", (118, 16, 22)),
    ("alt", (255, 255, 255)),
    ("alt_shade", (150, 150, 150)),
    ("team_light", (232, 130, 136)),
    ("grass", (8, 44, 12)),
    ("grass_stripe", (12, 62, 18)),
    ("yard_line", (70, 130, 76)),
    ("ball", (176, 88, 28)),
    ("ball_dark", (100, 46, 12)),
    ("white", (255, 255, 255)),
    ("gray", (140, 140, 140)),
    ("crowd", (36, 36, 54)),
    ("spark", (255, 208, 0)),
]
IDX = {name: i for i, (name, _) in enumerate(PALETTE)}

CHARS = {
    "T": "team", "t": "team_shade", "A": "alt", "a": "alt_shade", "L": "team_light",
    "W": "white", "G": "gray", "B": "ball", "b": "ball_dark", "K": "black", "Y": "spark",
}

# The helmet, facing left: dome, the stripe on the crown, the eye slot, the
# facemask bars in front.
HELMET = """
...AAAA...
..TLLTTTt.
.TLTTTTTTt
.KKTTTTATt
GKKTTTTAtt
GGGTTTTTt.
G.GtTTTt..
.GG.......
"""
# Jersey and shoulder pads with a side number, pants below.
TORSO = """
.tTTTTTTt.
tTLTTTTTTt
TTLTTTTTTT
TTTTAATTTt
.TTTAATTTt
.TTTTTTTt.
.tTTTTTTt.
.AAAAAAAa.
.AAAAAAAa.
"""
BALL = ["""
.bBb.
bBWBb
.bBb.
""", """
.bBb.
bBBWb
.bBb.
""", """
.bBb.
bWBBb
.bBb.
"""]
STAR = ["""
..Y..
.YAY.
..Y..
""", """
...Y...
...Y...
.YYAYY.
...Y...
...Y...
""", """
Y...Y...Y
.Y..Y..Y.
..Y.A.Y..
....A....
YYAA.AAYY
....A....
..Y.A.Y..
.Y..Y..Y.
Y...Y...Y
"""]


def sprite(text):
    rows = [r for r in text.strip("\n").splitlines()]
    return [[IDX[CHARS[c]] if c in CHARS else None for c in row] for row in rows]


class Canvas:
    def __init__(self):
        self.px = [[0] * W for _ in range(H)]

    def put(self, x, y, i):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = i

    def get(self, x, y):
        return self.px[y][x] if 0 <= x < W and 0 <= y < H else 0

    def blit(self, spr, x, y):
        for dy, row in enumerate(spr):
            for dx, i in enumerate(row):
                if i is not None:
                    self.put(x + dx, y + dy, i)

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

    def image(self):
        img = Image.new("P", (W, H))
        flat = []
        for _, rgb in PALETTE:
            flat.extend(rgb)
        flat.extend([255, 0, 255])  # 15, unused in the frames
        img.putpalette(flat + [0] * (768 - len(flat)))
        img.putdata([i for row in self.px for i in row])
        return img


# ---- the scene ----------------------------------------------------------------
HORIZON = 42
GROUND = 60  # the feet's row


def field(c, t, roar=0.0, seed=0):
    # The stands: rows of dim fans, a few in the team's colors; flashes when
    # the crowd roars.
    for y in range(26, HORIZON - 1, 3):
        for x in range((y // 3) % 2, W, 2):
            h = (x * 7919 + y * 104729) % 97
            c.put(x, y, IDX["crowd"])
            if h < 9:
                c.put(x, y, IDX["team"] if h < 6 else IDX["alt_shade"])
            if roar > 0 and ((h + seed * 13) % 97) < roar * 30:
                c.put(x, y - 1, IDX["team"] if h % 2 else IDX["alt"])
    c.put(0, 0, 0)
    for x in range(W):
        c.put(x, HORIZON - 1, IDX["gray"])  # the wall in front of the stands
    # The grass in mown bands, with yard lines leaning in perspective.
    for y in range(HORIZON, H):
        d = (y - HORIZON) / (H - HORIZON)
        for x in range(W):
            band = int((x + (y - HORIZON) * 0.6) // 16) % 2
            c.put(x, y, IDX["grass_stripe"] if band else IDX["grass"])
        for lx in range(-40, W + 40, 32):
            c.put(lx + (y - HORIZON) * 0.6, y, IDX["yard_line"])
        if y in (HORIZON + 4, H - 4):
            for lx in range(-40, W + 40, 32):
                for k in (8, 16, 24):
                    c.put(lx + k + (y - HORIZON) * 0.6, y, IDX["yard_line"])


# A pose: the legs as (thigh, shin) vectors and the arms as (upper, fore)
# vectors from the hips and shoulders, front limb then back limb; facing left.
POSES = {
    "run_a": dict(legs=[((-5, 5), (-1, 6)), ((3, 5), (5, 3))], arms=[((3, 4), (3, -2)), ((-4, 3), (-3, -3))]),
    "run_b": dict(legs=[((3, 5), (5, 3)), ((-5, 5), (-1, 6))], arms=[((-4, 3), (-3, -3)), ((3, 4), (3, -2))]),
    "crouch": dict(legs=[((-5, 3), (1, 6)), ((2, 4), (2, 5))], arms=[((4, 3), (4, 1)), ((5, 2), (4, 2))], drop=3),
    "leap": dict(legs=[((-4, 4), (4, 4)), ((3, 3), (4, 5))], arms=[((-3, -5), (-2, -5)), ((-1, -5), (-1, -5))]),
    "catch": dict(legs=[((-3, 5), (3, 4)), ((3, 4), (3, 5))], arms=[((-1, -6), (0, -5)), ((1, -6), (0, -5))],
                  ball="hands"),
    "land": dict(legs=[((-5, 3), (1, 6)), ((3, 3), (2, 6))], arms=[((-2, 5), (-4, 0)), ((4, 3), (4, -1))],
                 drop=3, ball="chest"),
    "carry_a": dict(legs=[((-5, 5), (-1, 6)), ((3, 5), (5, 3))], arms=[((-1, 4), (-4, -1)), ((-4, 3), (-3, -3))],
                    ball="chest"),
    "carry_b": dict(legs=[((3, 5), (5, 3)), ((-5, 5), (-1, 6))], arms=[((-1, 4), (-4, -1)), ((3, 4), (3, -2))],
                    ball="chest"),
}


def player(c, x, feet, pose, ball_frame=0):
    """The defender, x his middle, feet the lowest foot's row."""
    p = POSES[pose]
    legs = p["legs"]
    x, feet = int(x), int(feet)  # whole pixels, so the parts line up
    # Hips as high as the longer leg lets the feet reach the grass.
    reach = max(th[1] + sh[1] for th, sh in legs)
    hip_y = feet - reach - 1 - p.get("drop", 0) * 0
    hip_x = x
    top = hip_y - 8  # the torso's top row (pants are its last two)
    sh_front, sh_back = (hip_x - 2, top + 2), (hip_x + 2, top + 2)

    def leg(i, color, sock):
        (tx, ty), (sx, sy) = legs[i]
        hx, hy = hip_x + (-1 if i == 0 else 1), hip_y
        kx, ky = hx + tx, hy + ty
        ax, ay = kx + sx, ky + sy
        c.thick(hx, hy, kx, ky, color, IDX["alt_shade"])
        c.thick(kx, ky, ax, ay, sock, IDX["team_shade"])
        # The cleat, pointing forward (left).
        c.put(ax - 1, ay + 1, IDX["gray"])
        c.put(ax, ay + 1, IDX["gray"])
        c.put(ax + 1, ay + 1, IDX["gray"])

    def arm(i, shade=False):
        (ux, uy), (fx, fy) = p["arms"][i]
        sx, sy = sh_front if i == 0 else sh_back
        ex, ey = sx + ux, sy + uy
        hx, hy = ex + fx, ey + fy
        c.thick(sx, sy, ex, ey, IDX["team_shade"] if shade else IDX["team"], IDX["team_shade"])
        c.thick(ex, ey, hx, hy, IDX["team_shade"] if shade else IDX["team"], IDX["team_shade"])
        c.put(hx, hy, IDX["white"])
        c.put(hx - 1, hy, IDX["white"])
        c.put(hx, hy - 1, IDX["white"])
        return hx, hy

    arm(1, shade=True)
    leg(1, IDX["alt_shade"], IDX["team_shade"])
    c.blit(sprite(TORSO), hip_x - 5, top)
    c.blit(sprite(HELMET), hip_x - 6, top - 7)
    leg(0, IDX["alt"], IDX["team"])
    hx, hy = arm(0)
    b = p.get("ball")
    if b == "hands":
        c.blit(sprite(BALL[ball_frame % 3]), hx - 2, hy - 3)
    elif b == "chest":
        c.blit(sprite(BALL[0]), hip_x - 6, top + 2)
        arm(0)  # the arm wraps over the ball
    return hx, hy


def text2x(c, glyphs, s, y, fill, edge, drop_y=None):
    """Spleen 5x8 at twice the size, an edge color all round, centered."""
    adv = 12
    x0 = (W - len(s) * adv) // 2 + 1
    cells = []
    for n, ch in enumerate(s):
        rows = glyphs.get(ord(ch), [])
        dy = 0 if drop_y is None else drop_y[n]
        for ry, bits in enumerate(rows[:8]):
            for rx in range(5):
                if bits & (0x80 >> rx):
                    for sy in range(2):
                        for sx in range(2):
                            cells.append((x0 + n * adv + rx * 2 + sx, y + dy + ry * 2 + sy))
    on = set(cells)
    for (x, y) in on:
        for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1), (1, 1), (2, 2), (1, 2), (2, 1)):
            if (x + ox, y + oy) not in on:
                c.put(x + ox, y + oy, edge)
    for (x, y) in on:
        c.put(x, y, fill)


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


def ball_path(u):
    """The pass, from off the left edge to the catch point, u 0 to 1."""
    x0, y0, x1, y1 = -6, 44, 76, 12
    return x0 + (x1 - x0) * u, y0 + (y1 - y0) * u - math.sin(u * math.pi) * 14


def frames():
    glyphs = bdf(HERE.parents[1] / "firmware" / "fonts" / "spleen-5x8.bdf")
    out = []
    CATCH = 14
    for f in range(60):
        c = Canvas()
        roar = 0.0 if f < CATCH else min(1.0, (f - CATCH) / 6)
        field(c, f / FPS, roar, f)
        # The pass.
        if f <= CATCH:
            u = f / CATCH
            for k in (3, 2, 1):
                uu = u - k * 0.04
                if uu > 0:
                    tx, ty = ball_path(uu)
                    c.put(tx, ty + 1, IDX["ball_dark"])
            bx, by = ball_path(u)
            if f < CATCH:
                c.blit(sprite(BALL[f % 3]), bx - 2, by - 1)
        # The defender.
        if f < 9:  # running in
            x = 140 - (140 - 86) * f / 9
            player(c, x, GROUND, "run_a" if (f // 2) % 2 == 0 else "run_b")
        elif f < 11:
            player(c, 85, GROUND + 3, "crouch")
        elif f < CATCH:
            lift = [6, 10, 13][f - 11]
            player(c, 84 - (f - 11), GROUND - lift, "leap")
        elif f < 21:
            lift = [14, 14, 13, 11, 8, 5, 2][f - CATCH]
            hx, hy = player(c, 81 - (f - CATCH) * 0.5, GROUND - lift, "catch", f)
            if f - CATCH < 3:
                st = sprite(STAR[f - CATCH])
                c.blit(st, hx - len(st[0]) // 2, hy - 2 - len(st) // 2)
        elif f < 24:
            player(c, 77, GROUND + 3, "land")
        elif f < 44:  # the return, off to the left with speed lines behind
            x = 76 - (f - 24) * 5.2
            player(c, x, GROUND, "carry_a" if (f // 2) % 2 == 0 else "carry_b")
            for j in range(4):
                ly = GROUND - 26 + j * 6
                for d in range(10 - j * 2):
                    c.put(x + 9 + j % 2 * 2 + d * 2, ly, IDX["team_light"] if d < 2 else IDX["team_shade"])
        # The title drops in, one letter after another, and bounces.
        if f >= 36:
            def drops(word, start, sign):
                out = []
                for n in range(len(word)):
                    k = f - start - n
                    out.append(sign * 60 if k < 0 else sign * [24, 12, 2, -3, 0, 1, 0][k] if k < 7 else 0)
                return out
            # PICKED drops from the top, OFF! jumps up from the grass.
            text2x(c, glyphs, "PICKED", 6, IDX["team"], IDX["alt"], drops("PICKED", 36, -1))
            text2x(c, glyphs, "OFF!", 25, IDX["team"], IDX["alt"], drops("OFF!", 42, 1))
        out.append(c)
    return out


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "frames" / "intercept"
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.png"):
        old.unlink()
    fr = frames()
    for n, c in enumerate(fr):
        c.image().save(out / f"{n:03d}.png")
    print(f"{len(fr)} frames at {FPS} fps into {out}")


if __name__ == "__main__":
    main()
