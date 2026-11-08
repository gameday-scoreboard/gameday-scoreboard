"""Draws hockey's big-play pixel art as indexed PNGs, one a frame, at 128x64
and at 64x64: the hat trick, the shorthanded goal and the power play goal.
Each starts on the goal lamp, then the play's own picture. pack.py turns a
folder into a firmware header (celeb_art_<play>_<w>.h).

Indexes 1 to 5 are the team's colors (previewed in red and white); the panel
swaps in each team's own. The other side's skaters are gray, so the art needs
no second team.

Usage: python3 hockey_frames.py [out dir]   (default: frames/ next to this)
  writes <out>/hattrick_128, hattrick_64, shorthanded_128, ..."""
import math
import random
import sys
from pathlib import Path

import art

HERE = Path(__file__).resolve().parent
FPS = 12

# index: (name, preview RGB). 15 is reserved for "unchanged" by pack.py.
PALETTE = [("black", (0, 0, 0))] + art.TEAM_PREVIEW + [
    ("ice", (18, 30, 46)),         # dark on purpose: a lit panel of pale ice glares
    ("ice_mark", (52, 78, 104)),   # skate scratches, the boards' top rail
    ("lamp", (255, 24, 24)),
    ("lamp_dark", (110, 0, 0)),
    ("blue_line", (24, 60, 190)),
    ("white", (255, 255, 255)),
    ("gray", (140, 140, 140)),
    ("crowd", (36, 36, 54)),
    ("spark", (255, 208, 0)),
]
IDX = art.index_of(PALETTE)

CHARS = {
    "T": "team", "t": "team_shade", "A": "alt", "a": "alt_shade", "L": "team_light",
    "W": "white", "G": "gray", "K": "black", "R": "lamp", "r": "lamp_dark",
    "Y": "spark", "B": "blue_line", "I": "ice", "M": "ice_mark", "C": "crowd",
}


def sprite(text, remap=None):
    """art.sprite, with letters first swapped by remap (a skater's colors)."""
    if remap:
        text = "".join(remap.get(ch, ch) for ch in text)
    return art.sprite(text, CHARS, IDX)


mirror = art.mirror


class Canvas(art.Canvas):
    def __init__(self, w, h):
        super().__init__(w, h, PALETTE)

    def blit(self, spr, x, y, scale=1):
        """art.Canvas.blit on whole pixels, each sprite pixel scale x scale."""
        x, y = int(round(x)), int(round(y))
        for dy, row in enumerate(spr):
            for dx, i in enumerate(row):
                if i is not None:
                    for sy in range(scale):
                        for sx in range(scale):
                            self.put(x + dx * scale + sx, y + dy * scale + sy, i)


# ---- the title ------------------------------------------------------------------
GLYPHS = art.bdf()
# Spleen's 4-wide W reads as an H and its Y as a y at twice the size; the
# titles use these 5-wide ones.
GLYPHS[ord("W")] = [0x00, 0x88, 0x88, 0x88, 0xA8, 0xA8, 0x50, 0x00]
GLYPHS[ord("Y")] = [0x00, 0x88, 0x88, 0x50, 0x20, 0x20, 0x20, 0x00]


def text2x(c, s, y, fill, edge, adv=art.ADV2, drops=None):
    """art.text2x with a letter advance: a 64 px board fits six letters at 10
    (Spleen leaves most letters' 5th column blank); 12 is art's own."""
    if adv == art.ADV2:
        art.text2x(c, GLYPHS, s, y, fill, edge, drop_y=drops)
        return
    width = (len(s) - 1) * adv + 10
    x0 = (c.w - width) // 2
    on = set()
    for n, ch in enumerate(s):
        dy = 0 if drops is None else drops[n]
        for ry, bits in enumerate(GLYPHS.get(ord(ch), [])[:8]):
            for rx in range(5):
                if bits & (0x80 >> rx):
                    for sy in range(2):
                        for sx in range(2):
                            on.add((x0 + n * adv + rx * 2 + sx, y + dy + ry * 2 + sy))
    for (x, yy) in on:
        for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1), (1, 1), (2, 2), (1, 2), (2, 1)):
            if (x + ox, yy + oy) not in on:
                c.put(x + ox, yy + oy, edge)
    for (x, yy) in on:
        c.put(x, yy, fill)


def drop_in(word, f, start, sign=-1, step=1):
    """Per-letter offsets: each letter falls in (sign -1 from above, 1 from
    below) step frames after the one before, and bounces."""
    out = []
    for n in range(len(word)):
        k = f - start - n * step
        out.append(sign * 70 if k < 0 else sign * [24, 12, 2, -3, 0, 1, 0][k] if k < 7 else 0)
    return out


# ---- the arena --------------------------------------------------------------
def rink(c, boards, f=0, roar=0.0):
    """The crowd above, the boards, the ice below with its lines."""
    for y in range(1, boards - 2, 3):
        for x in range((y // 3) % 2, c.w, 2):
            h = (x * 7919 + y * 104729) % 97
            c.put(x, y, IDX["crowd"])
            if h < 9:
                c.put(x, y, IDX["team"] if h < 6 else IDX["alt_shade"])
            # The crowd changes every third frame: a twinkle every frame
            # would cost as much flash as the play itself.
            if roar > 0 and ((h + (f // 3) * 13) % 97) < roar * 20:
                c.put(x, y - 1, IDX["team"] if h % 2 else IDX["alt"])
    # The glass's top edge, the boards in white with the team's stripe.
    for x in range(c.w):
        c.put(x, boards - 2, IDX["ice_mark"])
        c.put(x, boards - 1, IDX["white"])
        c.put(x, boards, IDX["team"])
    for y in range(boards + 1, c.h):
        for x in range(c.w):
            c.put(x, y, IDX["ice"])
    # Skate scratches, fixed so they cost nothing frame to frame.
    rnd = random.Random(c.w)
    for _ in range(c.w // 6):
        x, y = rnd.randrange(c.w), rnd.randrange(boards + 3, c.h)
        for d in range(rnd.randrange(3, 7)):
            c.put(x + d, y + (d // 4), IDX["ice_mark"])
    # The red line in the middle, a blue line each side, leaning in perspective.
    top, depth = boards + 1, c.h - boards - 1
    for y in range(top, c.h):
        lean = (y - top) * 0.35
        mid = c.w // 2
        c.put(mid - lean * 0.2, y, IDX["lamp_dark"])
        c.put(mid + 1 - lean * 0.2, y, IDX["lamp_dark"])
        for side in (-1, 1):
            bx = mid + side * (c.w // 4 + lean * 0.6)
            c.put(bx, y, IDX["blue_line"])
            c.put(bx + 1, y, IDX["blue_line"])


LAMP = """
....GGGGG....
...rRRRRRr...
..rRRWWRRRr..
.rRRWRRRRRRr.
.rRRRRRRRRRr.
.rRRRRRRRRRr.
GGGGGGGGGGGGG
.GGGGGGGGGGG.
"""
LAMP_OFF = LAMP.replace("R", "r").replace("W", "R")


def lamp(c, f, cx, top, rays=True, scale=1):
    """The goal lamp at (cx, top), lit on even frames, its two beams sweeping."""
    lit = f % 2 == 0
    if rays:
        th = f * 0.55
        ox, oy = cx, top + 4 * scale
        for side in (0, math.pi):
            for spread in (-0.06, 0.0, 0.06):
                a = th + side + spread
                c.line(ox, oy, ox + math.cos(a) * c.w, oy + math.sin(a) * c.w * 0.6,
                       IDX["lamp"] if spread == 0 else IDX["lamp_dark"])
    spr = sprite(LAMP if lit else LAMP_OFF)
    c.blit(spr, cx - len(spr[0]) * scale // 2, top, scale)
    if lit:  # the glow round the dome
        for dx, dy in ((-8, 3), (8, 3), (0, -2), (-6, -1), (6, -1)):
            c.put(cx + dx * scale, top + dy * scale, IDX["lamp"])


def lamp_border(c, f):
    """The board's edge flashing red, as the code lamp does."""
    if f % 2:
        return
    for x in range(c.w):
        c.put(x, 0, IDX["lamp"])
        c.put(x, c.h - 1, IDX["lamp"])
    for y in range(c.h):
        c.put(0, y, IDX["lamp"])
        c.put(c.w - 1, y, IDX["lamp"])


# ---- skaters ----------------------------------------------------------------
# Side view, facing right. H helmet, V visor, J jersey, j its shade, N the
# number, P pants, S skate. Remapped to the team's colors or the gray side.
SKATER = ["""
..HHH...
.HHHHV..
..HHH...
.JJJJ...
JJJNJJ..
jJJNJJ..
.JJJJ...
.PPPP...
.P..P...
P....P..
S....SS.
""", """
..HHH...
.HHHHV..
..HHH...
.JJJJ...
JJJNJJ..
jJJNJJ..
.JJJJ...
.PPPP...
..PP....
..P.P...
.SS.SS..
"""]
OURS = {"H": "T", "V": "A", "J": "T", "j": "t", "N": "A", "P": "t", "S": "A"}
THEIRS = {"H": "W", "V": "K", "J": "G", "j": "a", "N": "W", "P": "C", "S": "a"}


def skater(c, x, feet, ours=True, step=0, left=False, stick=True):
    spr = sprite(SKATER[step % 2], OURS if ours else THEIRS)
    if left:
        spr = mirror(spr)
    top = feet - len(spr) + 1
    c.blit(spr, x - 4, top)
    if stick:  # from the hands down and forward to the blade on the ice
        d = -1 if left else 1
        hx, hy = x + d * 2, top + 5
        bx = x + d * 7
        col = IDX["alt_shade"] if ours else IDX["gray"]
        c.line(hx, hy, bx, feet, col)
        c.put(bx + d, feet, col)
    return x + (-8 if left else 8), feet  # where the blade is


def net(c, x, feet, ripple=0):
    """A goal seen from the side, its mouth facing left; ripple bulges the mesh."""
    h = 9
    for y in range(feet - h, feet + 1):
        c.put(x, y, IDX["lamp"])  # the post
    for xx in range(x, x + 7):
        c.put(xx, feet - h, IDX["lamp"])  # the crossbar running back
    for y in range(feet - h + 1, feet + 1):
        bulge = ripple if feet - h + 3 <= y <= feet - 2 else 0
        for xx in range(x + 1, x + 7 + bulge):
            if (xx + y) % 2 == 0:
                c.put(xx, y, IDX["white"] if xx < x + 6 + bulge else IDX["gray"])


def puck(c, x, y):
    c.put(x, y, IDX["white"])
    c.put(x + 1, y, IDX["gray"])


# ---- the hat trick --------------------------------------------------------------
# Hats thrown onto the ice: caps and toques. C cap crown, c its shade, b the
# brim; recolored per hat.
CAP = ["""
..ccc..
.cCCCc.
cCCCCCbb
""", """
..ccc..
.cCCCc.
bbCCCCc
"""]
TOQUE = ["""
...Y...
..CCC..
.CCCCC.
bbbbbbb
"""]
HAT_COLORS = [("T", "t", "A"), ("A", "a", "T"), ("G", "a", "W"), ("R", "r", "W"), ("W", "a", "B"),
              ("T", "t", "Y"), ("B", "C", "W"), ("L", "T", "A")]


def hat(c, x, y, kind, color, spin):
    cc, sh, brim = HAT_COLORS[color % len(HAT_COLORS)]
    if kind == 0:
        spr = sprite(CAP[spin % 2], {"C": cc, "c": sh, "b": brim})
    else:
        spr = sprite(TOQUE[0], {"C": cc, "b": brim})
    c.blit(spr, x - len(spr[0]) // 2, y - len(spr) + 1)


def hattrick(w):
    h = 64
    boards = 30 if w >= 128 else 34
    rnd = random.Random(3)
    n_hats = 34 if w >= 128 else 22
    hats = []
    for k in range(n_hats):
        start = 12 + k * (30 / n_hats) + rnd.random() * 3
        x = rnd.randrange(4, w - 4)
        land = rnd.randrange(boards + 8, h - 1)
        hats.append(dict(start=start, x=x, land=land, kind=0 if rnd.random() < 0.7 else 1,
                         color=rnd.randrange(8), sway=rnd.random() * 6.28, speed=3.2 + rnd.random() * 1.6))
    frames = []
    LAMP_END = 13
    for f in range(54):
        c = Canvas(w, h)
        rink(c, boards, f, roar=min(1.0, max(0.0, (f - 8) / 6)))
        if f < LAMP_END:
            lamp(c, f, w // 2, 4, scale=2)
            lamp_border(c, f)
        # Hats, the landed ones first so the falling ones pass over them.
        for hd in sorted(hats, key=lambda d: d["land"]):
            k = f - hd["start"]
            if k < 0:
                continue
            y = -4 + k * hd["speed"] * 1.6 + k * k * 0.12
            if y >= hd["land"]:
                hat(c, hd["x"], hd["land"], hd["kind"], hd["color"], 0)
            else:
                x = hd["x"] + math.sin(hd["sway"] + k * 0.7) * 3
                hat(c, x, y, hd["kind"], hd["color"], int(k / 2))
        # The title.
        if f >= LAMP_END:
            if w >= 128:
                text2x(c, "HAT TRICK!", 6, IDX["team"], IDX["alt"], drops=drop_in("HAT TRICK!", f, LAMP_END))
            else:
                text2x(c, "HAT", 1, IDX["team"], IDX["alt"], adv=10, drops=drop_in("HAT", f, LAMP_END))
                text2x(c, "TRICK!", 18, IDX["team"], IDX["alt"], adv=10, drops=drop_in("TRICK!", f, LAMP_END + 3))
        frames.append(c)
    return frames


# ---- the shorthanded goal ---------------------------------------------------
def shorthanded(w):
    """Four of ours against five grays: one of ours takes the puck the length
    of the ice past two of them and scores."""
    h = 64
    wide = w >= 128
    boards = 22 if wide else 36
    feet_rows = [boards + 13, boards + 20, h - 3] if wide else [boards + 13, h - 2]
    net_x = w - 12
    LAMP_END = 12
    frames = []
    for f in range(54):
        c = Canvas(w, h)
        rink(c, boards, f, roar=1.0 if f >= 40 else 0.0)
        if f < LAMP_END:
            lamp(c, f, w // 2, 3 if wide else 8, scale=2)
            lamp_border(c, f)
            frames.append(c)
            continue
        k = f - LAMP_END
        # Five grays, four of ours (three here and the puck carrier). On the
        # wide board they drift back toward their net and ours follow; the
        # narrow one has no room to drift, so its skaters keep their places
        # and only the top row drops its sticks.
        if wide:
            grays = [(46, 0), (70, 1), (86, 2), (60, 2), (96, 0)]
            ours = [(14, 0), (28, 2), (34, 1)]
            drift = min(k, 20) * 0.3
        else:
            grays = [(16, 0), (36, 0), (24, 1), (40, 1), (54, 0)]
            ours = [(6, 0), (26, 0), (46, 0)]
            drift = 0
        carrier_row = feet_rows[1]
        net(c, net_x, carrier_row, ripple=2 if 26 <= k <= 32 and k % 2 == 0 else 0)
        for x, row in grays:
            skater(c, x + drift, feet_rows[row], ours=False, step=(k // 3), left=True, stick=wide or row == 1)
        for x, row in ours:
            skater(c, x + drift * 1.5, feet_rows[row], ours=True, step=(k // 3) + row, stick=wide)
        # The puck carrier: up the ice in a weave, the shot at k 22.
        if k < 22:
            u = k / 22
            x = (20 if wide else 6) + u * (net_x - (34 if wide else 20))
            y = carrier_row + math.sin(u * 9) * (3 if wide else 0)
            bx, by = skater(c, x, int(y), ours=True, step=k // 2)
            puck(c, bx, int(y))
        else:
            x = net_x - (14 if wide else 13)
            skater(c, x, carrier_row, ours=True, step=0)
            u = min(1.0, (k - 22) / 3)
            puck(c, x + 8 + u * (9 if wide else 7), carrier_row - 2 * u)
        # The title drops in; once the puck is in, GOAL! jumps up from the
        # ice and the board's edge flashes again.
        if wide:
            text2x(c, "SHORTHANDED", 2, IDX["team"], IDX["alt"], adv=11, drops=drop_in("SHORTHANDED", f, LAMP_END))
        else:
            text2x(c, "SHORT", 1, IDX["team"], IDX["alt"], adv=10, drops=drop_in("SHORT", f, LAMP_END))
            text2x(c, "HANDED", 18, IDX["team"], IDX["alt"], adv=10, drops=drop_in("HANDED", f, LAMP_END + 3))
        if k >= 25:
            lamp_border(c, f)
            text2x(c, "GOAL!", 30 if wide else 40, IDX["alt"], IDX["team"], adv=12 if wide else 10,
                   drops=drop_in("GOAL!", f, LAMP_END + 25, sign=1))
        frames.append(c)
    return frames


# ---- the power play goal --------------------------------------------------------
def bolt(c, x, top, bottom, seed, core, glow):
    """A jagged 3 px bolt from top to bottom; returns where it hits."""
    rnd = random.Random(seed)
    y, xx = top, x
    pts = [(xx, y)]
    while y < bottom:
        y += rnd.randrange(4, 8)
        xx += rnd.choice((-5, -4, -3, 3, 4, 5))
        pts.append((xx, min(y, bottom)))
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        for d in (-2, -1, 1, 2):
            c.line(x0 + d, y0, x1 + d, y1, glow)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        c.line(x0, y0, x1, y1, core)
    # A short fork off the middle.
    mx, my = pts[len(pts) // 2]
    c.line(mx, my, mx + rnd.choice((-8, 8)), my + 6, core)
    return pts[-1]


def powerplay(w):
    h = 64
    wide = w >= 128
    boards = 40 if wide else 44
    LAMP_END = 12
    frames = []
    strikes = ([(22, 12), (104, 13), (62, 16), (36, 19), (90, 21), (12, 24), (70, 26), (116, 28), (44, 31),
                (96, 33), (26, 35)] if wide else
               [(14, 12), (50, 14), (30, 17), (8, 20), (56, 23), (36, 26), (20, 29), (46, 32)])
    for f in range(42):
        c = Canvas(w, h)
        rink(c, boards, f, roar=1.0 if f >= LAMP_END else 0.0)
        if f < LAMP_END:
            lamp(c, f, w // 2, 6 if wide else 8, scale=2)
        else:
            lamp(c, f, w // 2, 1, rays=False)
        if f < LAMP_END:
            lamp_border(c, f)
            frames.append(c)
            continue
        # Lightning in the team's color strikes the ice; where it hits, the
        # ice lights up round it and sparks fly.
        for x, at in strikes:
            k = f - at
            if not 0 <= k < 3:
                continue
            hit_y = boards + 8 + (x % 7)
            if k < 2:
                end = bolt(c, x, 0, hit_y, x * 31 + at, IDX["white"] if k == 0 else IDX["team_light"], IDX["team"])
            else:
                end = (x, hit_y)
            r = 3 + k * 4
            for yy in range(boards + 2, h):
                for xx in range(int(end[0] - r * 2), int(end[0] + r * 2) + 1):
                    if k < 2 and ((xx - end[0]) / 2) ** 2 + (yy - end[1]) ** 2 <= r * r and c.get(xx, yy) == IDX["ice"]:
                        c.put(xx, yy, IDX["team_shade"])
            for a in range(10):
                th = a * math.pi / 5
                rr = 3 + k * 3
                c.put(end[0] + math.cos(th) * rr * 1.6, end[1] + math.sin(th) * rr * 0.6,
                      IDX["spark"] if k < 2 else IDX["team_light"])
        if wide:
            text2x(c, "POWER PLAY", 11, IDX["team"], IDX["alt"], drops=drop_in("POWER PLAY", f, LAMP_END))
            text2x(c, "GOAL!", 30, IDX["alt"], IDX["team"], drops=drop_in("GOAL!", f, LAMP_END + 8, sign=1))
        else:
            text2x(c, "POWER", 10, IDX["team"], IDX["alt"], adv=11, drops=drop_in("POWER", f, LAMP_END))
            text2x(c, "PLAY", 27, IDX["team"], IDX["alt"], adv=11, drops=drop_in("PLAY", f, LAMP_END + 3))
            text2x(c, "GOAL!", 45, IDX["alt"], IDX["team"], adv=10, drops=drop_in("GOAL!", f, LAMP_END + 8, sign=1))
        frames.append(c)
    return frames


PLAYS = {"hattrick": hattrick, "shorthanded": shorthanded, "powerplay": powerplay}


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "frames"
    for name in sys.argv[2:] or list(PLAYS):
        for w in (128, 64):
            fr = PLAYS[name](w)
            art.save_frames(fr, out / f"{name}_{w}")
            art.sheet(fr[::3], out / f"{name}_{w}.png", cols=6 if w >= 128 else 9)
            print(f"{name}_{w}: {len(fr)} frames at {FPS} fps into {out / f'{name}_{w}'}")


if __name__ == "__main__":
    main()
