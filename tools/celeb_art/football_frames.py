"""Draws football's big plays as indexed PNGs, one a frame, at 128x64 (two
panels) or 64x64 (one panel): the interception, the pick-six, the fumble
and the sack. pack.py turns each folder into celeb_art_<play>_<width>.h.
The pick six opens as the interception, so the panel stores only its end
(pick_six_end) and plays the interception's frames first; pick_six draws
the whole thing for contact sheets.

The scene is one stadium: the stands in rows of fans, a wall, the grass in
mown bands with leaning yard lines. Our player is a 32 px cartoon defender
with helmet, facemask and pads, in the team's colors (previewed here in red
and white; the panel swaps in each team's own). The other team's player is
drawn in grays, so he never reads as ours whatever our colors are. Every
scene is laid out from the board's width: the 64-wide set is the tighter
shot of the same moment.

Usage: python3 football_frames.py <play> <width> [out dir] [--sheet <png>]
  play: interception, pick_six, pick_six_end, fumble, sack; width: 128 or 64

The pick six's header:
  python3 football_frames.py pick_six_end 128 frames/pick_six_end_128
  python3 pack.py frames/pick_six_end_128 pick_six_128 12 \
    ../../components/gameday/celeb_art_pick_six_128.h --palette football_frames \
    --lead interception_128 36"""
import math
import sys
from pathlib import Path

import art

HERE = Path(__file__).resolve().parent
H, FPS = 64, 12

# index: (name, preview RGB). 15 is pack.py's "unchanged".
PALETTE = [("black", (0, 0, 0))] + art.TEAM_PREVIEW + [
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
IDX = art.index_of(PALETTE)

CHARS = {
    "T": "team", "t": "team_shade", "A": "alt", "a": "alt_shade", "L": "team_light",
    "W": "white", "G": "gray", "B": "ball", "b": "ball_dark", "K": "black", "Y": "spark",
}


def sprite(text):
    return art.sprite(text, CHARS, IDX)


def canvas(w):
    return art.Canvas(w, H, PALETTE)


# ---- the player ----

# The helmet, facing left: dome, the stripe on the crown, the eye slot, the
# facemask bars in front.
HELMET = sprite("""
...AAAA...
..TLLTTTt.
.TLTTTTTTt
.KKTTTTATt
GKKTTTTAtt
GGGTTTTTt.
G.GtTTTt..
.GG.......
""")
# Jersey and shoulder pads with a side number, pants below.
TORSO = sprite("""
.tTTTTTTt.
tTLTTTTTTt
TTLTTTTTTT
TTTTAATTTt
.TTTAATTTt
.TTTTTTTt.
.tTTTTTTt.
.AAAAAAAa.
.AAAAAAAa.
""")
# The ball side on, turning a little as it flies (the pass), and end over
# end (loose on the ground).
BALL = [sprite(s) for s in ("""
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
""")]
BALL_SPIN = [sprite(s) for s in ("""
.bBb.
bBWBb
.bBb.
""", """
..bb
.bBb
bWB.
bb..
""", """
.b.
bBb
BWB
bBb
.b.
""", """
bb..
bWB.
.bBb
..bb
""")]
STAR = [sprite(s) for s in ("""
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
""")]
POST = sprite("""
Y.....Y
Y.....Y
Y.....Y
YYYYYYY
...Y...
...Y...
...Y...
...Y...
""")

# A pose: the legs as (thigh, shin) vectors and the arms as (upper, fore)
# vectors from the hips and shoulders, front limb then back limb; facing left.
POSES = {
    "run_a": dict(legs=[((-5, 5), (-1, 6)), ((3, 5), (5, 3))], arms=[((3, 4), (3, -2)), ((-4, 3), (-3, -3))]),
    "run_b": dict(legs=[((3, 5), (5, 3)), ((-5, 5), (-1, 6))], arms=[((-4, 3), (-3, -3)), ((3, 4), (3, -2))]),
    "crouch": dict(legs=[((-5, 3), (1, 6)), ((2, 4), (2, 5))], arms=[((4, 3), (4, 1)), ((5, 2), (4, 2))]),
    "leap": dict(legs=[((-4, 4), (4, 4)), ((3, 3), (4, 5))], arms=[((-3, -5), (-2, -5)), ((-1, -5), (-1, -5))]),
    "catch": dict(legs=[((-3, 5), (3, 4)), ((3, 4), (3, 5))], arms=[((-1, -6), (0, -5)), ((1, -6), (0, -5))],
                  ball="hands"),
    "land": dict(legs=[((-5, 3), (1, 6)), ((3, 3), (2, 6))], arms=[((-2, 5), (-4, 0)), ((4, 3), (4, -1))],
                 ball="chest"),
    "carry_a": dict(legs=[((-5, 5), (-1, 6)), ((3, 5), (5, 3))], arms=[((-1, 4), (-4, -1)), ((-4, 3), (-3, -3))],
                    ball="chest"),
    "carry_b": dict(legs=[((3, 5), (5, 3)), ((-5, 5), (-1, 6))], arms=[((-1, 4), (-4, -1)), ((3, 4), (3, -2))],
                    ball="chest"),
    # The quarterback set to throw, the ball cocked back by his ear.
    "throw": dict(legs=[((-4, 5), (0, 6)), ((3, 5), (2, 6))], arms=[((-4, 2), (-2, 2)), ((3, -3), (1, -4))]),
    "stand": dict(legs=[((-2, 5), (0, 6)), ((2, 5), (0, 6))], arms=[((-2, 4), (-1, 4)), ((2, 4), (1, 4))]),
    # Arms out front, legs trailing: the dive (turned a quarter to lie flat).
    "dive": dict(legs=[((1, 6), (1, 5)), ((3, 5), (2, 6))], arms=[((-1, -6), (-1, -5)), ((1, -6), (0, -5))]),
    # On one knee, the ball up in both hands.
    "kneel_up": dict(legs=[((-5, 3), (1, 6)), ((2, 4), (6, 2))], arms=[((-1, -6), (0, -5)), ((1, -6), (0, -5))],
                     ball="hands"),
}


def player(c, x, feet, pose, ball_frame=0):
    """Our defender, x his middle, feet the lowest foot's row. Returns where
    his front hand is."""
    p = POSES[pose]
    legs = p["legs"]
    x, feet = int(x), int(feet)  # whole pixels, so the parts line up
    reach = max(th[1] + sh[1] for th, sh in legs)
    hip_x, hip_y = x, feet - reach - 1
    top = hip_y - 8  # the torso's top row (pants are its last two)
    sh_front, sh_back = (hip_x - 2, top + 2), (hip_x + 2, top + 2)

    def leg(i, color, sock):
        (tx, ty), (sx, sy) = legs[i]
        hx, hy = hip_x + (-1 if i == 0 else 1), hip_y
        kx, ky = hx + tx, hy + ty
        ax, ay = kx + sx, ky + sy
        c.thick(hx, hy, kx, ky, color, IDX["alt_shade"])
        c.thick(kx, ky, ax, ay, sock, IDX["team_shade"])
        for d in (-1, 0, 1):  # the cleat, pointing forward (left)
            c.put(ax + d, ay + 1, IDX["gray"])

    def arm(i, shade=False):
        (ux, uy), (fx, fy) = p["arms"][i]
        sx, sy = sh_front if i == 0 else sh_back
        ex, ey = sx + ux, sy + uy
        hx, hy = ex + fx, ey + fy
        col = IDX["team_shade"] if shade else IDX["team"]
        c.thick(sx, sy, ex, ey, col, IDX["team_shade"])
        c.thick(ex, ey, hx, hy, col, IDX["team_shade"])
        for px, py in ((hx, hy), (hx - 1, hy), (hx, hy - 1)):
            c.put(px, py, IDX["white"])
        return hx, hy

    arm(1, shade=True)
    leg(1, IDX["alt_shade"], IDX["team_shade"])
    c.blit(TORSO, hip_x - 5, top)
    c.blit(HELMET, hip_x - 6, top - 7)
    leg(0, IDX["alt"], IDX["team"])
    hx, hy = arm(0)
    b = p.get("ball")
    if b == "hands":
        c.blit(BALL[ball_frame % 3], hx - 2, hy - 3)
    elif b == "chest":
        c.blit(BALL[0], hip_x - 6, top + 2)
        arm(0)  # the arm wraps over the ball
    return hx, hy


class Layer(art.Canvas):
    """A see-through canvas wider than any board, for one figure, so a
    figure past the board's edge stays whole until it lands."""

    def __init__(self):
        super().__init__(200, 100, PALETTE)
        self.px = [[None] * self.w for _ in range(self.h)]

    def onto(self, c, remap=None, turn=0, about=(0, 0), shift=(0, 0)):
        """Copies the drawn pixels onto c, turned a quarter counterclockwise
        turn times about a point (pixel art keeps its right angles) and with
        palette indexes swapped by remap."""
        cx, cy = about
        for y, row in enumerate(self.px):
            for x, i in enumerate(row):
                if i is None:
                    continue
                if remap:
                    i = remap.get(i, i)
                dx, dy = x - cx, y - cy
                for _ in range(turn % 4):
                    dx, dy = dy, -dx
                c.put(cx + dx + shift[0], cy + dy + shift[1], i)


# The other team's player: gray jersey and helmet, white pants.
THEM = {
    IDX["team"]: IDX["gray"],
    IDX["team_shade"]: IDX["crowd"],
    IDX["team_light"]: IDX["white"],
    IDX["alt"]: IDX["white"],
    IDX["alt_shade"]: IDX["gray"],
}


def figure(pose, x, feet, ball_frame=0):
    lay = Layer()
    hand = player(lay, x, feet, pose, ball_frame)
    return lay, hand


def ball(c, x, y, k):
    s = BALL_SPIN[k % 4]
    c.blit(s, x - len(s[0]) // 2, y - len(s) // 2)


# ---- the field ----

HORIZON = 42
GROUND = 60  # the feet's row


def field(c, roar=0.0, seed=0):
    """The stands (rows of dim fans, a few in the team's colors, lighting up
    as the crowd roars), the wall, the grass with leaning yard lines. The
    roaring fans change every third frame: a sparkle that changes every
    frame costs as many bytes again as the players."""
    seed //= 3
    for y in range(26, HORIZON - 1, 3):
        for x in range((y // 3) % 2, c.w, 2):
            h = (x * 7919 + y * 104729) % 97
            c.put(x, y, IDX["crowd"])
            if h < 9:
                c.put(x, y, IDX["team"] if h < 6 else IDX["alt_shade"])
            if roar > 0 and ((h + seed * 13) % 97) < roar * 30:
                c.put(x, y - 1, IDX["team"] if h % 2 else IDX["alt"])
    for x in range(c.w):
        c.put(x, HORIZON - 1, IDX["gray"])
    for y in range(HORIZON, H):
        for x in range(c.w):
            band = int((x + (y - HORIZON) * 0.6) // 16) % 2
            c.put(x, y, IDX["grass_stripe"] if band else IDX["grass"])
        for lx in range(-40, c.w + 40, 32):
            c.put(lx + (y - HORIZON) * 0.6, y, IDX["yard_line"])
        if y in (HORIZON + 4, H - 4):
            for lx in range(-40, c.w + 40, 32):
                for k in (8, 16, 24):
                    c.put(lx + k + (y - HORIZON) * 0.6, y, IDX["yard_line"])


def roar_for(f, start):
    return 0.0 if f < start else min(1.0, (f - start) / 6)


def burst(c, f, start):
    """The catch star over three frames from start."""
    return STAR[f - start] if 0 <= f - start < 3 else None


# ---- titles ----

GLYPHS = art.bdf()
EDGE = ((-1, 0), (1, 0), (0, -1), (0, 1), (1, 1), (2, 2), (1, 2), (2, 1), (-1, -1), (-1, 1), (1, -1))


def title(c, s, y, sx=2, sy=2, drop=None, x_off=0):
    """Spleen 5x8 scaled sx by sy in the team's color with an edge in its
    second color, centered; drop moves each letter up or down. art.text2x
    is the 2x2 case; the narrow board also needs 1x2 (tall) and the sack's
    slam 3x and 4x."""
    adv = 6 * sx
    x0 = (c.w - (len(s) * adv - sx)) // 2 + x_off
    on = set()
    for n, ch in enumerate(s):
        dy = 0 if drop is None else drop[n]
        for ry, bits in enumerate(GLYPHS.get(ord(ch), [])[:8]):
            for rx in range(5):
                if bits & (0x80 >> rx):
                    for yy in range(sy):
                        for xx in range(sx):
                            on.add((x0 + n * adv + rx * sx + xx, y + dy + ry * sy + yy))
    for (x, yy) in on:
        for ox, oy in EDGE:
            if (x + ox, yy + oy) not in on:
                c.put(x + ox, yy + oy, IDX["alt"])
    for (x, yy) in on:
        c.put(x, yy, IDX["team"])


def drops(word, f, start, sign, every=1):
    """Each letter falls in (sign -1) or jumps up (1) after the one before,
    and bounces."""
    out = []
    for n in range(len(word)):
        k = f - start - n * every
        out.append(sign * 60 if k < 0 else sign * [24, 12, 2, -3, 0, 1, 0][k] if k < 7 else 0)
    return out


# ---- the interception: 5 s ----
# The pass arcs in from the left, our defender runs in, leaps and picks it
# off (the star, the crowd lights up), lands and runs it back off the left
# edge with speed lines behind him. PICKED / OFF! drops in letter by letter.

CATCH = 14
TITLE_AT = 36  # the pick-six cuts to the end zone here instead


def intercept(w, n=60, with_title=True):
    wide = w >= 128
    catch_x = 76 if wide else int(w * 0.62) - 6
    run_to = 86 if wide else catch_x + 10
    arc = 14 if wide else 10
    out = []

    def path(u):
        x0, y0, x1, y1 = -6, 44, catch_x, 12
        return x0 + (x1 - x0) * u, y0 + (y1 - y0) * u - math.sin(u * math.pi) * arc

    for f in range(n):
        c = canvas(w)
        field(c, roar_for(f, CATCH), f)
        if f <= CATCH:
            u = f / CATCH
            for k in (3, 2, 1):
                uu = u - k * 0.04
                if uu > 0:
                    tx, ty = path(uu)
                    c.put(tx, ty + 1, IDX["ball_dark"])
            bx, by = path(u)
            if f < CATCH:
                c.blit(BALL[f % 3], bx - 2, by - 1)
        if f < 9:
            x = (w + 12) - ((w + 12) - run_to) * f / 9
            player(c, x, GROUND, "run_a" if (f // 2) % 2 == 0 else "run_b")
        elif f < 11:
            player(c, run_to - 1, GROUND + 3, "crouch")
        elif f < CATCH:
            player(c, run_to - 2 - (f - 11), GROUND - [6, 10, 13][f - 11], "leap")
        elif f < 21:
            lift = [14, 14, 13, 11, 8, 5, 2][f - CATCH]
            hx, hy = player(c, run_to - 5 - (f - CATCH) * 0.5, GROUND - lift, "catch", f)
            st = burst(c, f, CATCH)
            if st:
                c.blit(st, hx - len(st[0]) // 2, hy - 2 - len(st) // 2)
        elif f < 24:
            player(c, run_to - 9, GROUND + 3, "land")
        elif f < 44:
            x = run_to - 10 - (f - 24) * 5.2
            player(c, x, GROUND, "carry_a" if (f // 2) % 2 == 0 else "carry_b")
            for j in range(4):
                ly = GROUND - 26 + j * 6
                for d in range((10 if wide else 8) - j * 2):
                    c.put(x + 9 + j % 2 * 2 + d * 2, ly, IDX["team_light"] if d < 2 else IDX["team_shade"])
        if with_title and f >= TITLE_AT:
            # PICKED drops from the top, OFF! jumps up from the grass. Six
            # letters at twice the size are 70 px, so the narrow board draws
            # PICKED tall and thin.
            if wide:
                title(c, "PICKED", 6, drop=drops("PICKED", f, TITLE_AT, -1))
                title(c, "OFF!", 25, drop=drops("OFF!", f, TITLE_AT + 6, 1))
            else:
                title(c, "PICKED", 4, sx=1, drop=drops("PICKED", f, TITLE_AT, -1))
                title(c, "OFF!", 23, drop=drops("OFF!", f, TITLE_AT + 6, 1))
        out.append(c)
    return out


# ---- the pick-six: 6 s ----
# The interception's catch and return (no title), then the end zone: he
# crosses the goal line, spikes it, the sparks burst and PICK / SIX! drops
# in. The posts stand left of the title on the wide board; on the narrow
# one they sit in the middle and go when the title lands.


def goalposts(c, cx, top, sx, sy):
    for y, row in enumerate(POST):
        for x, i in enumerate(row):
            if i is not None:
                c.rect(cx + (x - 3) * sx, top + y * sy, cx + (x - 3) * sx + sx - 1, top + y * sy + sy - 1, i)


def pick_six(w, n_end=36):
    wide = w >= 128
    out = intercept(w, n=TITLE_AT, with_title=False)
    goal_x = 26 if wide else w // 2
    score_x = 100 if wide else w // 2 - 2
    SPIKE = 14
    for f in range(n_end):
        c = canvas(w)
        field(c, roar_for(f, SPIKE), TITLE_AT + f)
        for x in range(w):
            c.put(x, HORIZON + 2, IDX["white"])  # the goal line
        if wide or f < SPIKE + 2:
            goalposts(c, goal_x, 8, 3 if wide else 2, 4)
        if f < SPIKE:
            x = w + 10 - (w + 10 - score_x) * min(1.0, f / (SPIKE - 2))
            player(c, x, GROUND, "carry_a" if (f // 2) % 2 == 0 else "carry_b")
        else:
            k = f - SPIKE
            player(c, score_x, GROUND, "catch" if k < 2 else "stand", 0)
            if k >= 2:  # the spike bounces off the turf
                by = GROUND - 2 - abs(math.sin(k * 0.6)) * max(0, 12 - k)
                ball(c, score_x - 6 - k, int(by), k)
            # Sparks in the team's colors burst over the posts (and over
            # him on the wide board).
            for j in range(10):
                ang = j * math.pi / 5 + (0.3 if j % 2 else 0)
                r = k * (2.2 if wide else 1.6)
                if r >= 26:
                    continue
                for b, cx in enumerate((goal_x, score_x) if wide else (goal_x,)):
                    col = IDX["team"] if (j + b) % 2 else IDX["alt"]
                    c.put(cx + math.cos(ang) * r, 14 + math.sin(ang) * r * 0.7 + k * 0.4, col)
                    c.put(cx + math.cos(ang) * (r - 2), 14 + math.sin(ang) * (r - 2) * 0.7 + k * 0.4,
                          IDX["team_light"])
        if f >= SPIKE + 2:
            title(c, "PICK", 4, drop=drops("PICK", f, SPIKE + 2, -1))
            title(c, "SIX!", 23, drop=drops("SIX!", f, SPIKE + 6, 1))
        out.append(c)
    return out


# ---- the fumble: 4 s ----
# The ball squirts loose and bounces end over end, each hop lower; our
# defender runs in, dives on it, and comes up on one knee holding it high.
# FUMBLE! drops in.


def fumble(w, n=48):
    wide = w >= 128
    rest_x = int(w * (0.52 if wide else 0.45))
    a, b = (40, 14) if wide else (24, 9)
    hops = [(-6, rest_x - a, 16), (rest_x - a, rest_x - b, 8), (rest_x - b, rest_x, 3)]
    hop_frames = [7, 5, 4]
    DIVE, LAND, UP = 17, 21, 29
    run_from, run_to = w + 12, rest_x + (26 if wide else 20)
    out = []
    for f in range(n):
        c = canvas(w)
        field(c, roar_for(f, LAND + 1), f)
        if f < LAND:
            g, hop = f, None
            for (x0, x1, top), hf in zip(hops, hop_frames):
                if g < hf:
                    hop = (x0, x1, top, g / hf)
                    break
                g -= hf
            if hop is None:  # at rest, wobbling
                bx, by, spin = rest_x, GROUND - 2, (f // 2) % 2 * 2
            else:
                x0, x1, top, u = hop
                bx = x0 + (x1 - x0) * u
                by = GROUND - 2 - math.sin(u * math.pi) * top
                spin = f
                for k in (1, 2, 3):  # a short trail, so the eye finds it
                    uu = u - k * 0.12
                    if uu > 0:
                        c.put(x0 + (x1 - x0) * uu, GROUND - 1 - math.sin(uu * math.pi) * top, IDX["ball_dark"])
                if u < 0.2 and x0 > 0:  # a puff of turf where it bounced
                    for d in (-3, -1, 1, 3):
                        c.put(x0 + d, GROUND - 1 - abs(d) // 2, IDX["gray"])
            ball(c, int(bx), int(by), spin)
        if f < DIVE:
            if f >= 4:
                x = run_from + (run_to - run_from) * (f - 4) / (DIVE - 4)
                lay, _ = figure("run_a" if (f // 2) % 2 == 0 else "run_b", x, GROUND)
                lay.onto(c)
        elif f < UP:
            # In the air, then sliding: the dive pose turned to lie flat, head
            # first toward the ball.
            k = min(f - DIVE, 11)
            lay, _ = figure("dive", 64, GROUND)
            body_x = [run_to - 6, run_to - 11, run_to - 16, rest_x + 6, rest_x + 4][k] if k < 5 else rest_x + 3
            lift = [10, 9, 6, 3][k] if k < 4 else 0
            lay.onto(c, turn=1, about=(64, GROUND - 14), shift=(body_x - 52, 11 - lift))
            if LAND <= f < LAND + 3:  # the dust as he hits
                for d in range(6):
                    c.put(rest_x - 6 + d * 3 + (f - LAND), GROUND - (f - LAND) - d % 2, IDX["gray"])
        else:
            hx, hy = player(c, rest_x + 6, GROUND, "kneel_up")
            st = burst(c, f, UP)
            if st:
                c.blit(st, hx - len(st[0]) // 2, hy - 2 - len(st) // 2)
        if f >= UP + 1:
            if wide:
                title(c, "FUMBLE!", 6, drop=drops("FUMBLE!", f, UP + 1, -1))
            else:  # seven letters at twice the size are 82 px: tall and thin
                title(c, "FUMBLE!", 4, sx=1, drop=drops("FUMBLE!", f, UP + 1, -1))
        out.append(c)
    return out


# ---- the sack: 2 s ----
# The quarterback (the other team's grays) sets to throw; our defender comes
# off the edge and buries him. The board jolts and SACK! slams in big, then
# settles.


def sack(w, n=24):
    wide = w >= 128
    qb_x = int(w * (0.38 if wide else 0.4))
    HIT = 7
    out = []
    for f in range(n):
        c = canvas(w)
        field(c, roar_for(f, HIT), f)
        jolt = [0, 3, -3, 2, -1, 0][f - HIT] if HIT <= f < HIT + 6 else 0
        scene = Layer()
        if f < HIT:
            qb, _ = figure("throw", qb_x, GROUND)
            qb.onto(scene, remap=THEM)
            ball(scene, qb_x + 4, GROUND - 31, 0)
            x = (w + 14) + (qb_x + 9 - (w + 14)) * (f / HIT) ** 0.8
            d, _ = figure("run_a" if f % 2 == 0 else "run_b", x, GROUND)
            d.onto(scene)
        else:
            k = f - HIT
            fall = min(k, 3)
            flat = fall >= 2
            qb, _ = figure("stand", 64, GROUND)
            qb.onto(scene, remap=THEM, turn=1 if flat else 0, about=(64, GROUND - 14),
                    shift=(qb_x - 64 - fall * 3 + (10 if flat else 0), 11 if flat else fall * 2))
            d, _ = figure("dive", 64, GROUND)
            d.onto(scene, turn=1, about=(64, GROUND - 14), shift=(qb_x - 44 - fall * 3, 4 if flat else -2))
            if k < 3:  # the hit
                st = STAR[2 - k]
                c.blit(st, qb_x + 3 - len(st[0]) // 2, GROUND - 26 - len(st) // 2)
            if k >= 2:  # the ball pops free behind them
                ball(scene, qb_x - 16 - k * 2, GROUND - 2 - max(0, 6 - k * 2), k)
        scene.onto(c, shift=(jolt, 0))
        if f >= HIT:
            k = min(f - HIT, 2)
            s = [4, 3, 3][k] if wide else [3, 2, 2][k]
            title(c, "SACK!", [2, 4, 4][k], sx=s, sy=s if wide or k < 1 else 3, x_off=jolt)
        out.append(c)
    return out


def pick_six_end(w):
    """The pick six from the end zone on: what the panel stores for it. The
    first TITLE_AT frames are the interception's own, played from its set
    (pack.py --lead interception_<w> TITLE_AT)."""
    return pick_six(w)[TITLE_AT:]


PLAYS = {"interception": intercept, "pick_six": pick_six, "pick_six_end": pick_six_end, "fumble": fumble,
         "sack": sack}


def main():
    args = sys.argv[1:]
    sheet = None
    if "--sheet" in args:
        i = args.index("--sheet")
        sheet = args[i + 1]
        del args[i:i + 2]
    if len(args) < 2 or args[0] not in PLAYS or args[1] not in ("128", "64"):
        print(__doc__)
        sys.exit(2)
    play, w = args[0], int(args[1])
    out = Path(args[2]) if len(args) > 2 else HERE / "frames" / f"{play}_{w}"
    frames = PLAYS[play](w)
    art.save_frames(frames, out)
    if sheet:
        art.sheet(frames[::3], sheet, cols=4, scale=2)
    print(f"{len(frames)} frames at {FPS} fps into {out}")


if __name__ == "__main__":
    main()
