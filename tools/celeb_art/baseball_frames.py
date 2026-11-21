"""Draws baseball's big plays as indexed PNGs, one a frame, at 128x64 and
64x64: the grand slam and the walk-off. pack.py turns each set into the
firmware's celeb_art_<play>_<width>.h.

The art palette is PALETTE below; team, team_shade, alt, alt_shade and
team_light are the team's colors (previewed in red and white, art.py), which
the panel swaps for each team's own. Generic players in the team's colors:
no logos, no real players.

Usage: python3 baseball_frames.py [out dir]
  writes <out>/grand_slam_128/, grand_slam_64/, walk_off_128/, walk_off_64/
  and a contact sheet of each (<name>.png). Then, for each set:
  python3 pack.py frames/grand_slam_128 grand_slam_128 12 \\
      ../../components/gameday/celeb_art_grand_slam_128.h --palette baseball_frames"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import art  # noqa: E402

FPS = 12

PALETTE = art.TEAM_PREVIEW + [
    ("sky", (4, 6, 22)),
    ("crowd", (34, 34, 52)),
    ("wall", (10, 52, 30)),
    ("grass", (10, 60, 16)),
    ("grass_stripe", (16, 82, 24)),
    ("dirt", (120, 66, 30)),
    ("dirt_dark", (72, 38, 16)),
    ("white", (255, 255, 255)),
    ("gray", (140, 140, 140)),
    ("spark", (255, 208, 0)),
]
IDX = art.index_of(PALETTE)
I = IDX  # short for the drawing below

CHARS = {
    "T": "team", "t": "team_shade", "A": "alt", "a": "alt_shade", "L": "team_light",
    "W": "white", "G": "gray", "K": "sky", "Y": "spark", "D": "dirt_dark",
}


def spr(text):
    return art.sprite(text, CHARS, IDX)


# A runner seen from the stands, about 9 px tall: cap, face, jersey in the
# team's color, pants in the second color. Two strides.
RUNNER = [spr("""
.TTt.
TTTTT
.LWL.
.TTT.
TTTTT
.TTT.
.AaA.
A...a
A...a
"""), spr("""
.TTt.
TTTTT
.LWL.
.TTT.
TTTTT
.TTT.
.AAa.
.Aa..
.a.A.
""")]

# A bigger runner for the walk-off's dash home, 14 px tall, two strides and
# the slide.
DASH = [spr("""
...TTTt..
..TTTTTTT
...LWWL..
...LWWL..
..tTTTTt.
.TTTTTTTT
TT.TTTT.T
W..TTTT..
...AAAA..
...AAaA..
..AA..aA.
.AA....aa
.G......G
"""), spr("""
...TTTt..
..TTTTTTT
...LWWL..
...LWWL..
..tTTTTt.
.TTTTTTT.
.T.TTTTTW
.W.TTTT..
...AAAA..
...AaAA..
....AaA..
...Aa.a..
...G..G..
""")]
SLIDE = spr("""
..........TTTt..
.........TTTTTTT
..........LWWL..
.......tTTTTTTt.
WTTTTTTTTTTTT...
.......AAAAAAAa.
GaAAAAAAAa......
""")

# A teammate pouring out of the dugout: arms up, 10 px tall, two hops.
FAN = [spr("""
W.TTt.W
T.TTT.T
.TLWLT.
..TTT..
..TTT..
..AAA..
..A.a..
..A.a..
.GG.GG.
"""), spr("""
.......
W.TTt.W
T.TTT.T
.TLWLT.
..TTT..
..TTT..
..AAA..
.A...a.
GG...GG
""")]

# The batter from behind home, swinging left to right: the bat's angle
# carries the swing, the body stays.
BATTER = spr("""
..TTTt..
.TTTTTTt
..tLLt..
..TTTT..
.TTTTTT.
.TTTTTT.
..TTTT..
..AAAA..
..AAaa..
..AA.a..
.AA..aa.
.GG..GG.
""")

BALL = [spr("""
.W.
WWW
.W.
"""), spr("""
WW
WW
""")]


class Field:
    """Where things are on a board w wide: home plate low in the middle,
    the diamond in perspective, the outfield wall and the stands above."""

    def __init__(self, w):
        self.w, self.h = w, 64
        self.cx = w // 2
        s = 1.0 if w >= 128 else 0.62
        self.s = s
        self.home = (self.cx, 57)
        self.first = (self.cx + int(34 * s), 45)
        self.second = (self.cx, 36)
        self.third = (self.cx - int(34 * s), 45)
        self.wall = 22
        self.bases = [self.home, self.first, self.second, self.third, self.home]


def ballpark(c, f, crowd_roar=0.0, seed=0, lights=True):
    w = f.w
    # The night sky and the light towers.
    for y in range(0, f.wall - 8):
        for x in range(w):
            c.put(x, y, I["sky"])
    if lights:
        for lx in (6, w - 7):
            for dx in range(-2, 3):
                c.put(lx + dx, 2, I["white"])
                c.put(lx + dx, 3, I["gray"])
            c.line(lx, 4, lx, f.wall - 9, I["gray"])
    # The stands: dim fans in rows, some in the team's colors, standing up
    # in the team's colors when the crowd roars.
    for y in range(f.wall - 8, f.wall):
        for x in range(w):
            c.put(x, y, I["sky"])
    for y in range(f.wall - 8, f.wall - 1, 2):
        for x in range((y // 2) % 2, w, 2):
            h = (x * 7919 + y * 104729) % 97
            c.put(x, y, I["crowd"])
            if h < 8:
                c.put(x, y, I["team"] if h < 5 else I["alt_shade"])
            if crowd_roar > 0 and ((h + seed * 17) % 97) < crowd_roar * 40:
                c.put(x, y - 1, I["team"] if h % 2 else I["alt"])
    # The outfield wall, then the grass in mown bands, then the infield.
    for x in range(w):
        c.put(x, f.wall - 1, I["wall"])
        c.put(x, f.wall, I["wall"])
    for y in range(f.wall + 1, f.h):
        band = ((y - f.wall) // 4) % 2
        for x in range(w):
            c.put(x, y, I["grass_stripe"] if band else I["grass"])
    # The infield: the dirt diamond around the base paths, the grass inside
    # it, and the dirt circle at home.
    hx, hy = f.home
    half_w = f.first[0] - f.cx
    top, bottom = f.second[1], hy

    def in_diamond(x, y, grow):
        # A diamond through the four bases, grown by grow pixels.
        mid = f.first[1]
        if y < top - grow or y > bottom + grow:
            return False
        if y <= mid:
            span = (y - (top - grow)) / max(1, mid - top + grow) * (half_w + grow * 1.6)
        else:
            span = ((bottom + grow) - y) / max(1, bottom - mid + grow) * (half_w + grow * 1.6)
        return abs(x - f.cx) <= span

    for y in range(top - 6, f.h):
        for x in range(w):
            if in_diamond(x, y, 4) and not in_diamond(x, y, -3):
                c.put(x, y, I["dirt"])
    for y in range(hy - 5, f.h):
        for x in range(hx - int(8 * f.s) - 3, hx + int(8 * f.s) + 4):
            if ((x - hx) / (8 * f.s + 3)) ** 2 + ((y - hy) / 5) ** 2 < 1:
                c.put(x, y, I["dirt"])
    # The baselines in chalk, and the bases.
    for a, b in zip(f.bases, f.bases[1:]):
        c.line(a[0], a[1], b[0], b[1], I["dirt_dark"] if a == f.first or a == f.second else I["white"])
    c.line(f.home[0], f.home[1], f.first[0] + (f.first[0] - f.home[0]) * 3, f.first[1] - (f.home[1] - f.first[1]) * 3,
           I["white"])
    c.line(f.home[0], f.home[1], f.third[0] - (f.home[0] - f.third[0]) * 3, f.third[1] - (f.home[1] - f.third[1]) * 3,
           I["white"])
    for bx, by in (f.first, f.second, f.third):
        c.rect(bx - 1, by - 1, bx + 1, by, I["white"])
    c.rect(hx - 1, hy, hx + 1, hy + 1, I["white"])
    # The mound.
    mx, my = f.cx, (f.second[1] + hy) // 2
    c.rect(mx - 2, my - 1, mx + 2, my, I["dirt"])
    c.put(mx, my - 1, I["white"])


def glyphs():
    """The panel's Spleen 5x8, with a W that reads as one at twice the size
    (Spleen's own is four columns wide and looks like an H)."""
    g = art.bdf()
    g[ord("W")] = [0x00, 0x88, 0x88, 0xA8, 0xA8, 0xA8, 0x50, 0x00]
    return g


def drops(word, f, start, sign, steps=(24, 12, 2, -3, 0, 1, 0)):
    out = []
    for n in range(len(word)):
        k = f - start - n
        out.append(sign * 70 if k < 0 else sign * steps[k] if k < len(steps) else 0)
    return out


def burst(c, x, y, k, big, colors):
    """A firework k frames old at x, y: rays out, then sparks falling."""
    rays = 16 if big else 10
    reach = (3 + k * 2.6) * (1.6 if big else 1.1)
    for r in range(rays):
        a = r * 2 * math.pi / rays + (0.2 if big else 0)
        for d in range(int(reach * 0.55), int(reach) + 1):
            if k > 6 and d < reach * 0.8:
                continue
            col = colors[(r + (d > reach * 0.8)) % len(colors)]
            c.put(x + math.cos(a) * d, y + math.sin(a) * d * 0.8 + (k * k) * 0.04, col)


def lerp(a, b, u):
    return a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u


def on_bases(f, start_base, u):
    """Where a runner is u bases along from start_base (0 home, 1 first...)."""
    pos = start_base + u
    if pos >= 4:
        return f.home
    k = int(pos)
    return lerp(f.bases[k], f.bases[k + 1], pos - k)


# ---- the grand slam ----------------------------------------------------------
def grand_slam(w):
    f = Field(w)
    g = glyphs()
    out = []
    TEAM = [I["team"], I["alt"], I["spark"], I["team_light"]]
    HIT = 7          # the bat meets the ball
    GONE = 22        # the ball clears the wall
    TROT = 6         # frames a base
    START = 10       # the runners set off
    for n in range(60):
        c = art.Canvas(w, 64, PALETTE)
        crossed = sum(1 for b in (3, 2, 1, 0) if n >= START + (4 - b) * TROT)
        roar = min(1.0, max(0.0, (n - GONE) / 8))
        ballpark(c, f, roar, n)
        # The batter, the bat swinging around, the crack.
        hx, hy = f.home
        bx0, by0 = hx - int(9 * f.s) - 4, hy - 13
        if n < START:
            c.blit(BATTER, bx0, by0)
            ang = [-2.3, -2.3, -2.4, -2.5, -2.2, -1.4, -0.6, 0.1, 0.9, 1.6][n]
            px, py = bx0 + 5, by0 + 4
            ln = 9 if w >= 128 else 7
            c.thick(px, py, px + math.cos(ang) * ln, py + math.sin(ang) * ln, I["dirt"], I["dirt_dark"])
            if HIT <= n < HIT + 3:
                for r in range(8):
                    a = r * math.pi / 4
                    d = 2 + (n - HIT) * 2
                    c.put(hx - 2 + math.cos(a) * d, hy - 8 + math.sin(a) * d, I["spark"])
        # The pitch in, then the ball up and out to right center.
        if 2 <= n < HIT:
            u = (n - 2) / (HIT - 2)
            c.blit(BALL[1], *map(int, lerp((f.cx, (f.second[1] + hy) // 2 - 2), (hx - 2, hy - 9), u)))
        elif HIT <= n < GONE:
            u = (n - HIT) / (GONE - HIT)
            ex, ey = f.cx + w * 0.34, 4
            x = hx + (ex - hx) * u
            y = (hy - 9) + (ey - (hy - 9)) * u - math.sin(u * math.pi) * 10
            for k in range(1, 5):
                uu = max(0.0, u - k * 0.05)
                c.put(hx + (ex - hx) * uu, (hy - 9) + (ey - (hy - 9)) * uu - math.sin(uu * math.pi) * 10,
                      I["team_light"] if k < 3 else I["team_shade"])
            c.blit(BALL[0] if u < 0.6 else BALL[1], int(x) - 1, int(y) - 1)
        # Fireworks where it left the park, two more as the runs come in.
        for at, (x, y), big in ((GONE, (int(f.cx + w * 0.34), 7), True),
                                (GONE + 9, (int(f.cx - w * 0.3), 6), False),
                                (GONE + 15, (int(f.cx + w * 0.12), 4), w >= 128),
                                (44, (int(w * 0.14), 12), True),
                                (48, (int(w * 0.86), 10), True)):
            if at <= n < at + 12:
                burst(c, x, y, n - at, big, TEAM)
        # Four runners round the bases: the bases-loaded three and the batter.
        for b in (3, 2, 1, 0):
            t0 = START
            u = (n - t0) / TROT
            if n < t0:
                u = 0
            dist = 4 - b
            if u >= dist:
                continue  # home
            x, y = on_bases(f, b, max(0.0, u))
            if b == 0 and n < START:
                continue  # still the batter
            c.blit(RUNNER[(n // 2) % 2], int(x) - 2, int(y) - 9)
        # Each run in: a pip in the corner, a flash at the plate.
        for k in range(crossed):
            px = 2 + k * 5 if w >= 128 else 1 + k * 4
            c.rect(px, f.h - 4, px + (2 if w >= 128 else 1), f.h - 2, I["spark"])
        for b in (3, 2, 1, 0):
            at = START + (4 - b) * TROT
            if at <= n < at + 3:
                for r in range(6):
                    a = -math.pi * r / 5
                    c.put(hx + math.cos(a) * (3 + (n - at) * 2), hy + math.sin(a) * (2 + (n - at)), I["spark"])
        # The words, one letter after another: GRAND from the top, SLAM! up
        # from the grass.
        if n >= 36:
            art.text2x(c, g, "GRAND", 3, I["team"], I["alt"], drop_y=drops("GRAND", n, 36, -1))
            art.text2x(c, g, "SLAM!", 22, I["team"], I["alt"], drop_y=drops("SLAM!", n, 41, 1))
        out.append(c)
    return out


# ---- the walk-off ------------------------------------------------------------
def walk_off(w):
    f = Field(w)
    g = glyphs()
    out = []
    SAFE = 13    # the slide reaches the plate
    FLOOD = 16   # the dugout empties
    hx, hy = f.home
    for n in range(54):
        c = art.Canvas(w, 64, PALETTE)
        roar = 0.0 if n < SAFE else min(1.0, (n - SAFE) / 4)
        ballpark(c, f, roar, n)
        # The winning run: down the line from third, the slide, the dust.
        if n < SAFE + 6:
            if n < 9:
                u = n / 9
                x, y = lerp((f.third[0] - 4, f.third[1] - 12), (hx - 16 * f.s - 6, hy - 13), u)
                c.blit(DASH[(n // 2) % 2], int(x), int(y))
            else:
                sx = int(hx - 16 * f.s - 6 + (n - 9) * 3 * f.s) if n < SAFE else int(hx - 10)
                c.blit(SLIDE, sx - 4, hy - 6)
                for k in range(6 + (n - 9) * 3):
                    h = (k * 2654435761 + n * 97) % 1000
                    c.put(sx - 4 - (h % 9), hy - (h // 9) % 5, I["dirt"] if h % 3 else I["gray"])
        # SAFE: a white flash at the plate.
        if SAFE <= n < SAFE + 3:
            for r in range(12):
                a = r * math.pi / 6
                d = 3 + (n - SAFE) * 4
                c.put(hx + math.cos(a) * d, hy - 4 + math.sin(a) * d * 0.7, I["white"])
        # The dugouts empty: the team's color floods up from the bottom and
        # teammates pour in from both sides, hopping.
        if n >= FLOOD:
            k = n - FLOOD
            level = f.h - min(26 if w >= 128 else 22, k * 3)
            for y in range(level, f.h):
                for x in range(w):
                    edge = y == level or (y == level + 1 and (x + n) % 3 == 0)
                    c.put(x, y, I["team_light"] if edge else I["team_shade"] if (x + y) % 5 == 0 else I["team"])
            crowd = 12 if w >= 128 else 7
            for p in range(crowd):
                side = -1 if p % 2 == 0 else 1
                target = hx + side * (4 + (p // 2) * (9 if w >= 128 else 7))
                start = hx + side * (w // 2 + 8 + (p // 2) * 6)
                u = min(1.0, max(0.0, (k - (p // 2)) / 6))
                x = start + (target - start) * u
                hop = ((n + p) // 2) % 2
                y = level - 8 + (p % 3) - (hop if u >= 1 else 0)
                c.blit(FAN[hop if u >= 1 else 0], int(x) - 3, int(y))
        # Confetti once they're all in.
        if n >= FLOOD + 8:
            for k in range(30 if w >= 128 else 16):
                h = (k * 7919) % 997
                x = (h * 13 + k * 5) % w
                y = (h // 7 + (n - FLOOD - 8) * (2 + h % 3)) % (f.h - 20)
                c.put(x, y, [I["spark"], I["alt"], I["team_light"]][h % 3])
        # The words.
        if n >= 22:
            if w >= 128:
                art.text2x(c, g, "WALK-OFF!", 6, I["team"], I["alt"], drop_y=drops("WALK-OFF!", n, 22, -1))
            else:
                art.text2x(c, g, "WALK", 2, I["team"], I["alt"], drop_y=drops("WALK", n, 22, -1))
                art.text2x(c, g, "OFF!", 20, I["team"], I["alt"], drop_y=drops("OFF!", n, 26, -1))
        out.append(c)
    return out


SHOWS = {"grand_slam": grand_slam, "walk_off": walk_off}


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else art.HERE / "frames"
    for name, make in SHOWS.items():
        for w in (128, 64):
            frames = make(w)
            art.save_frames(frames, out / f"{name}_{w}")
            art.sheet(frames, out / f"{name}_{w}.png", cols=6 if w >= 128 else 9)
            print(f"{name}_{w}: {len(frames)} frames at {FPS} fps")


if __name__ == "__main__":
    main()
