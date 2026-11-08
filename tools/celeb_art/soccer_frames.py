"""Draws soccer's big plays as indexed PNG frames, at 128x64 (two panels)
and 64x64 (one panel), into frames/<play>_<width>/. pack.py turns each set
into components/gameday/celeb_art_<play>_<width>.h.

- brace, hat_trick: the ball flies into the net and it ripples, then the
  title drops in and two or three balls bounce in under the scorer's name.
- penalty: the ball on the spot, the keeper dives the wrong way, the ball
  hits the other corner and the net ripples.
- late_goal: the net ripples, then the title and the minute, flashing.

The scorer's name and the minute change from game to game, so the panel
stamps them over the frames (celeb_caption.h); CAPTIONS below says where.

The PNGs use PALETTE. Indexes 1 to 5 are the team's colors (previewed in
red and white); the panel swaps in each team's own. Edit the frames in any
pixel-art tool that keeps the indexed palette, or change the scenes here
and run it again.

Usage: python3 soccer_frames.py [out dir]   (writes every play at both widths)"""
import math
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
H, FPS = 64, 12
FRAMES = 60  # 5 s

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
    ("skin", (196, 140, 100)),
    ("net", (70, 74, 84)),
    ("net_lit", (200, 204, 214)),
    ("white", (255, 255, 255)),
    ("ball_dark", (40, 40, 44)),
    ("keeper", (90, 220, 40)),
    ("gold", (255, 208, 0)),
]
IDX = {name: i for i, (name, _) in enumerate(PALETTE)}

CHARS = {
    "T": "team", "t": "team_shade", "A": "alt", "a": "alt_shade", "L": "team_light",
    "W": "white", "S": "skin", "K": "keeper", "d": "ball_dark", "Y": "gold", "n": "net",
}

# Where the panel puts the words over each play's frames: (top row, the
# first frame, scale, blink). celeb_art_index.h carries the same numbers.
CAPTIONS = {
    ("brace", 128): (22, 27, 2, False),
    ("brace", 64): (21, 27, 2, False),
    ("hat_trick", 128): (22, 27, 2, False),
    ("hat_trick", 64): (37, 27, 1, False),
    ("late_goal", 128): (24, 27, 3, True),
    ("late_goal", 64): (38, 27, 2, True),
}

# The keeper, facing us: arms out with the gloves, then diving to his right
# (our left), stretched out flat.
KEEPER = """
...SSS...
..SSSSS..
...SSS...
.KKKKKKK.
WKKKKKKKW
W.KKKKK.W
..KKKKK..
..KKKKK..
..ddddd..
..dd.dd..
..SS.SS..
..SS.SS..
..SS.SS..
.KK...KK.
"""
KEEPER_SET = """
.........
...SSS...
..SSSSS..
...SSS...
WKKKKKKKW
WKKKKKKKW
..KKKKK..
..KKKKK..
..ddddd..
.dd...dd.
.SS...SS.
SS.....SS
KK.....KK
"""
KEEPER_DIVE = """
WW.................
WKK...SSS..........
.KKKKSSSSS.........
..KKKKKSSSKKKddd...
...KKKKKKKKKKddddSS.
.....WKKKKKKKKdd.SSSK
......W...........SKK
"""


def sprite(text):
    rows = [r for r in text.strip("\n").splitlines()]
    return [[IDX[CHARS[c]] if c in CHARS else None for c in row] for row in rows]


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


GLYPHS = bdf(HERE.parents[1] / "firmware" / "fonts" / "spleen-5x8.bdf")


class Canvas:
    def __init__(self, w):
        self.w = w
        self.px = [[0] * w for _ in range(H)]

    def put(self, x, y, i):
        x, y = int(round(x)), int(round(y))
        if 0 <= x < self.w and 0 <= y < H:
            self.px[y][x] = i

    def get(self, x, y):
        return self.px[y][x] if 0 <= x < self.w and 0 <= y < H else 0

    def blit(self, spr, x, y, flip=False):
        x, y = int(round(x)), int(round(y))
        for dy, row in enumerate(spr):
            for dx, i in enumerate(row[::-1] if flip else row):
                if i is not None:
                    self.put(x + dx, y + dy, i)

    def rect(self, x0, y0, x1, y1, i):
        for y in range(int(y0), int(y1) + 1):
            for x in range(int(x0), int(x1) + 1):
                self.put(x, y, i)

    def image(self):
        img = Image.new("P", (self.w, H))
        flat = []
        for _, rgb in PALETTE:
            flat.extend(rgb)
        flat.extend([255, 0, 255])  # 15, unused in the frames
        img.putpalette(flat + [0] * (768 - len(flat)))
        img.putdata([i for row in self.px for i in row])
        return img


def text(c, s, y, scale, fill, edge, drops=None):
    """Spleen 5x8 at scale, an edge all round and a drop shadow, centered."""
    adv = 5 * scale
    width = (len(s) * 5 - 1) * scale
    x0 = (c.w - width) // 2
    on = set()
    for n, ch in enumerate(s):
        dy = 0 if drops is None else drops[n]
        for ry, bits in enumerate(GLYPHS.get(ord(ch), [])[:8]):
            for rx in range(5):
                if bits & (0x80 >> rx):
                    for sy in range(scale):
                        for sx in range(scale):
                            on.add((x0 + n * adv + rx * scale + sx, y + dy + ry * scale + sy))
    for (x, y) in on:
        for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1), (1, 1), (-1, -1), (1, -1), (-1, 1), (2, 2)):
            if (x + ox, y + oy) not in on:
                c.put(x + ox, y + oy, edge)
    for (x, y) in on:
        c.put(x, y, fill)


def drops(word, f, start, sign=-1, every=1):
    """Each letter falls in after the one before and bounces once."""
    out = []
    for n in range(len(word)):
        k = f - start - n * every
        out.append(sign * 70 if k < 0 else sign * [26, 12, 3, -3, 0, 1, 0][k] if k < 7 else 0)
    return out


def ball(c, cx, cy, r, spin=0.0):
    """A soccer ball of radius r: white with a gray rim, a dark patch in the
    middle and three smaller ones round it that turn with spin."""
    if r < 1.2:
        c.put(cx, cy, IDX["white"])
        return
    spots = [(cx + math.cos(spin + j * 2.094) * r * 0.72, cy + math.sin(spin + j * 2.094) * r * 0.72)
             for j in range(3)]
    for y in range(int(cy - r - 1), int(cy + r + 2)):
        for x in range(int(cx - r - 1), int(cx + r + 2)):
            d = math.hypot(x - cx, y - cy)
            if d > r:
                continue
            i = IDX["white"]
            if r >= 3.5 and d > r - 0.8:
                i = IDX["net_lit"]
            if d < max(0.6, r * 0.3):
                i = IDX["ball_dark"]
            elif r >= 4 and any(math.hypot(x - sx, y - sy) < r * 0.2 for sx, sy in spots):
                i = IDX["ball_dark"]
            c.put(x, y, i)


# ---- the goal -------------------------------------------------------------------


class Goal:
    """The goal seen from the pitch: posts, crossbar and net, the stands
    behind, the grass in front."""

    def __init__(self, w, penalty=False):
        self.w = w
        if w >= 128:
            self.x0, self.x1, self.top, self.line = (34, 93, 13, 40) if penalty else (24, 103, 8, 44)
        else:
            self.x0, self.x1, self.top, self.line = (6, 57, 10, 38) if penalty else (4, 59, 6, 42)
        self.mesh = 4 if w >= 128 else 3

    def stands(self, c, roar, seed):
        for y in range(1, self.line - 1, 3):
            for x in range((y // 3) % 2, c.w, 2):
                h = (x * 7919 + y * 104729) % 97
                c.put(x, y, IDX["net"] if h < 50 else IDX["ball_dark"])
                if h < 9:
                    c.put(x, y, IDX["team_shade"] if h < 6 else IDX["alt_shade"])
                if roar > 0 and ((h + seed * 13) % 97) < roar * 14:
                    c.put(x, y - 1, IDX["team"] if h % 2 else IDX["alt"])
        for x in range(c.w):
            c.put(x, self.line - 2, IDX["team_shade"])  # the boards along the byline
            c.put(x, self.line - 1, IDX["team_shade"] if (x // 6) % 2 else IDX["team"])

    def grass(self, c):
        for y in range(self.line, H):
            band = int(math.sqrt(y - self.line) * 1.6) % 2
            for x in range(c.w):
                c.put(x, y, IDX["grass_stripe"] if band else IDX["grass"])
        for x in range(c.w):
            c.put(x, self.line, IDX["white"])  # the goal line
        # The six-yard box, leaning out in perspective.
        depth = 7 if c.w >= 128 else 6
        bx0, bx1 = self.x0 - 10, self.x1 + 10
        for k in range(depth + 1):
            c.put(bx0 - k * 1.6, self.line + k, IDX["white"])
            c.put(bx1 + k * 1.6, self.line + k, IDX["white"])
        for x in range(int(bx0 - depth * 1.6), int(bx1 + depth * 1.6) + 1):
            c.put(x, self.line + depth, IDX["white"])

    def net(self, c, impact=None, tt=0.0, ball_r=0.0):
        """The mesh, rippling out from impact (x, y) tt seconds after the
        ball hit it."""
        c.rect(self.x0 + 2, self.top + 2, self.x1 - 2, self.line - 1, IDX["black"])
        strands = []
        for x in range(self.x0 + 2 + self.mesh // 2, self.x1 - 1, self.mesh):
            for y in range(self.top + 2, self.line):
                strands.append((x, y))
        for y in range(self.top + 2 + self.mesh // 2, self.line, self.mesh):
            for x in range(self.x0 + 2, self.x1 - 1):
                strands.append((x, y))
        for (x, y) in strands:
            i, px, py = IDX["net"], x, y
            if impact is not None and tt >= 0:
                ix, iy = impact
                dx, dy = x - ix, y - iy
                r = math.hypot(dx, dy) + 0.01
                scale = 1.0 if c.w >= 128 else 0.75
                # The bulge where the ball is, and rings running out from it.
                bulge = 5 * scale * math.exp(-r / (7 * scale)) * math.exp(-tt * 2.2)
                ring = 2.4 * scale * math.sin(r * 0.55 - tt * 26) * math.exp(-tt * 2.6) * math.exp(-r / 40)
                d = bulge + ring if r < tt * 90 + 4 else bulge
                px, py = x + dx / r * d, y + dy / r * d * 0.7
                if abs(d) > 0.9:
                    i = IDX["net_lit"]
            if self.x0 + 2 <= px <= self.x1 - 2 and self.top + 2 <= py <= self.line - 1:
                c.put(px, py, i)

    def frame(self, c):
        for y in range(self.top, self.line + 1):
            for k in (0, 1):
                c.put(self.x0 + k, y, IDX["white"])
                c.put(self.x1 - k, y, IDX["white"])
        for x in range(self.x0, self.x1 + 1):
            c.put(x, self.top, IDX["white"])
            c.put(x, self.top + 1, IDX["white"])
        # The posts' shadow on the grass.
        c.put(self.x0 + 2, self.line + 1, IDX["grass_stripe"])


def goal_shot(c, f, goal, impact, start_x, hit_frame, roar_from):
    """A shot into the net: the ball flies from (start_x, bottom) to impact,
    shrinking, and the net ripples from hit_frame."""
    roar = 0.0 if f < roar_from else min(1.0, (f - roar_from) / 4)
    goal.stands(c, roar, f)
    goal.grass(c)
    tt = (f - hit_frame) / FPS
    goal.net(c, impact if f >= hit_frame else None, tt)
    goal.frame(c)
    big = 4.5 if c.w >= 128 else 3.5
    ix, iy = impact
    if f < hit_frame:
        u = f / hit_frame
        x = start_x + (ix - start_x) * u
        y = (H - 5) + (iy - (H - 5)) * u - math.sin(u * math.pi) * 6
        ball(c, x, y, big - (big - 1.6) * u, f * 0.9)
    else:
        # In the net, then dropping to the grass inside it.
        k = f - hit_frame
        y = min(goal.line - 3, iy + 0.25 * k * k)
        ball(c, ix, y, 1.6 if k < 3 else 1.4, 0)


# ---- confetti -----------------------------------------------------------------


def confetti(c, f, since, n):
    colors = [IDX["team"], IDX["alt"], IDX["gold"], IDX["team_light"]]
    for k in range(n):
        h = (k * 2654435761) & 0xFFFFFFFF
        x = h % c.w
        speed = 1.2 + (h >> 8) % 7 * 0.35
        y = ((h >> 12) % (H + 16) + (f - since) * speed) % (H + 16) - 16
        if y < 0:
            continue
        sway = math.sin((f + k) * 0.7) * 1.5
        col = colors[(h >> 4) % 4]
        c.put(x + sway, y, col)
        if (f + k) % 3:
            c.put(x + sway + 1, y, col)


# ---- the plays ----------------------------------------------------------------


def balls_row(c, f, n, start, y, r):
    """n balls bounce up from below one after another and settle in a row."""
    gap = r * 2 + (6 if c.w >= 128 else 4)
    x0 = (c.w - (n - 1) * gap) / 2
    for k in range(n):
        t = f - start - k * 6
        if t < 0:
            continue
        hops = [30, 14, 2, -5, -2, 0, -1, 0]
        dy = hops[t] if t < len(hops) else 0
        ball(c, x0 + k * gap, y + dy, r, (f - start) * 0.5 * (1 if k % 2 else -1))
        if t < 3:
            for s in range(4):
                a = s * math.pi / 2 + 0.6
                c.put(x0 + k * gap + math.cos(a) * (r + 2 + t), y + math.sin(a) * (r + 2 + t), IDX["gold"])


def brace(w, n_balls):
    out = []
    goal = Goal(w)
    impact = (goal.x1 - (18 if w >= 128 else 12), goal.top + (11 if w >= 128 else 9))
    title = "HAT TRICK!" if n_balls == 3 else "BRACE!"
    for f in range(FRAMES):
        c = Canvas(w)
        if f < 21:
            goal_shot(c, f, goal, impact, w * 0.45, 6, 6)
        else:
            confetti(c, f, 21, 46 if w >= 128 else 26)
            if w >= 128 or n_balls == 2:
                text(c, title, 2, 2, IDX["team"], IDX["alt"], drops(title, f, 21))
            else:
                text(c, "HAT", 1, 2, IDX["team"], IDX["alt"], drops("HAT", f, 21))
                text(c, "TRICK!", 18, 2, IDX["team"], IDX["alt"], drops("TRICK!", f, 23))
            if w >= 128:
                balls_row(c, f, n_balls, 31, 51, 6)
            else:
                balls_row(c, f, n_balls, 31, 51 if n_balls == 2 else 55, 5 if n_balls == 2 else 4)
        out.append(c)
    return out


def late_goal(w):
    out = []
    goal = Goal(w)
    impact = (goal.x0 + (16 if w >= 128 else 11), goal.top + (10 if w >= 128 else 8))
    for f in range(FRAMES):
        c = Canvas(w)
        if f < 21:
            goal_shot(c, f, goal, impact, w * 0.6, 6, 6)
        else:
            confetti(c, f, 21, 46 if w >= 128 else 26)
            if w >= 128:
                text(c, "LATE GOAL!", 2, 2, IDX["team"], IDX["alt"], drops("LATE GOAL!", f, 21))
            else:
                text(c, "LATE", 1, 2, IDX["team"], IDX["alt"], drops("LATE", f, 21))
                text(c, "GOAL!", 18, 2, IDX["team"], IDX["alt"], drops("GOAL!", f, 23))
            # A clock's sweep hand round the minute: a ring of ticks lighting
            # up one after another, in the team's colors.
            cx, cy = w / 2, (37 if w >= 128 else 46)
            rx, ry = (30, 14) if w >= 128 else (24, 10)
            for k in range(24):
                a = k / 24 * 2 * math.pi - math.pi / 2
                lit = (f - 24) * 2 % 24
                col = IDX["team"] if (k - lit) % 24 < 4 else IDX["team_shade"]
                if f >= 24:
                    c.put(cx + math.cos(a) * rx, cy + math.sin(a) * ry, col)
        out.append(c)
    return out


def penalty(w):
    out = []
    goal = Goal(w, penalty=True)
    wide = w >= 128
    spot = (w / 2, 55 if wide else 53)
    target = (goal.x1 - (9 if wide else 7), goal.top + (7 if wide else 6))
    kick, hit = 14, 19
    stand, ready, dive = sprite(KEEPER), sprite(KEEPER_SET), sprite(KEEPER_DIVE)
    kx = w / 2 - 4
    for f in range(FRAMES):
        c = Canvas(w)
        roar = 0.0 if f < hit else min(1.0, (f - hit) / 4)
        goal.stands(c, roar, f)
        goal.grass(c)
        # The spot.
        c.put(spot[0], spot[1] + (4 if wide else 3), IDX["white"])
        c.put(spot[0] + 1, spot[1] + (4 if wide else 3), IDX["white"])
        goal.net(c, target if f >= hit else None, (f - hit) / FPS)
        # The keeper: on his line, bouncing, set, then diving the wrong way.
        feet = goal.line - 1
        if f < kick - 3:
            c.blit(stand, kx + (1 if (f // 3) % 2 else 0), feet - len(stand) + 1)
        elif f < kick + 1:
            c.blit(ready, kx, feet - len(ready) + 2)
        else:
            k = f - kick - 1
            dx = min(k, 7) * (3.2 if wide else 2.4)
            lift = [3, 6, 7, 7, 6, 4, 1, 0][k] if k < 8 else 0
            c.blit(dive, kx - dx - 4, feet - 6 - lift)
        goal.frame(c)
        # The ball: still on the spot, then struck into the far corner.
        r0 = 3.5 if wide else 3.0
        if f < kick:
            ball(c, spot[0], spot[1], r0, 0)
        elif f < hit:
            u = (f - kick + 1) / (hit - kick)
            x = spot[0] + (target[0] - spot[0]) * u
            y = spot[1] + (target[1] - spot[1]) * u - math.sin(u * math.pi) * 3
            ball(c, x, y, r0 - (r0 - 1.5) * u, f)
            # A streak behind it.
            for s in (1, 2, 3):
                uu = max(0.0, u - s * 0.08)
                c.put(spot[0] + (target[0] - spot[0]) * uu, spot[1] + (target[1] - spot[1]) * uu, IDX["alt_shade"])
        else:
            k = f - hit
            ball(c, target[0], min(goal.line - 3, target[1] + 0.22 * k * k), 1.5, 0)
            if k < 4:
                for s in range(6):
                    a = s * math.pi / 3
                    c.put(target[0] + math.cos(a) * (3 + k * 2), target[1] + math.sin(a) * (3 + k * 2), IDX["gold"])
        if f >= 30:
            if wide:
                text(c, "PENALTY!", 46, 2, IDX["team"], IDX["alt"], drops("PENALTY!", f, 30, sign=1))
            else:
                band = min(1.0, (f - 30) / 3)
                half = int(c.w / 2 * band)
                c.rect(c.w / 2 - half, 42, c.w / 2 + half - 1, 55, IDX["team_shade"])
                if f >= 33:
                    text(c, "PENALTY!", 45, 1, IDX["white"], IDX["team"])
        out.append(c)
    return out


PLAYS = {
    "brace": lambda w: brace(w, 2),
    "hat_trick": lambda w: brace(w, 3),
    "penalty": penalty,
    "late_goal": late_goal,
}


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "frames"
    for name, make in PLAYS.items():
        for w in (128, 64):
            out = root / f"{name}_{w}"
            out.mkdir(parents=True, exist_ok=True)
            for old in out.glob("*.png"):
                old.unlink()
            fr = make(w)
            for n, c in enumerate(fr):
                c.image().save(out / f"{n:03d}.png")
            print(f"{name}_{w}: {len(fr)} frames at {FPS} fps into {out}")


if __name__ == "__main__":
    main()
