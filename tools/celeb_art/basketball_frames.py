"""Draws basketball's big plays as indexed PNGs, at 128x64 (two panels) and
64x64 (one panel), into frames/<play>_<width>/. pack.py turns each set into
components/gameday/celeb_art_<play>_<width>.h.

- dunk: a player in the team's colors dribbles in, rises, slams the ball
  through the rim and hangs on it while the rim shakes, the net snaps and
  "DUNK!" drops in. 30 frames, 2.5 s.
- alley_oop: the lob arcs high to the rim (thrown by a teammate on the wide
  board, from off the edge on the narrow one), the player catches it at the
  top of the leap and finishes. "ALLEY-OOP!" on the wide board, "ALLEY" then
  "OOP!" on the narrow one. 42 frames, 3.5 s.

Generic players only: no logos, numbers or real people.

Usage: python3 basketball_frames.py [--sheet]   (--sheet also writes
frames/<play>_<width>.png with every frame on one image)
Then, from this folder, for each set:
  python3 pack.py frames/dunk_128 dunk_128 12 ../../components/gameday/celeb_art_dunk_128.h --palette basketball_frames"""
import math
import sys

from art import HERE, TEAM_PREVIEW, Canvas, bdf, index_of, save_frames, sheet, sprite, text2x

FPS = 12
H = 64

# index: (name, preview RGB); team colors from art.TEAM_PREVIEW at 1 to 5.
PALETTE = [("black", (0, 0, 0))] + TEAM_PREVIEW + [
    ("wood", (92, 50, 16)),
    ("wood_dark", (52, 26, 8)),
    ("crowd", (34, 34, 52)),
    ("white", (255, 255, 255)),
    ("gray", (120, 120, 132)),
    ("rim", (255, 84, 0)),
    ("ball", (232, 110, 24)),
    ("ball_dark", (104, 40, 8)),
    ("skin", (172, 108, 70)),
]
IDX = index_of(PALETTE)

CHARS = {
    "T": "team", "t": "team_shade", "A": "alt", "a": "alt_shade", "L": "team_light",
    "W": "white", "G": "gray", "B": "ball", "b": "ball_dark", "K": "black", "S": "skin",
    "Y": "team_light",
}


def spr(text):
    return sprite(text, CHARS, IDX)


# The player faces right, toward the hoop: the face and the front limbs on
# the right. Hair in the ball's dark brown.
HEAD = spr("""
.bbbb.
bbbbbb
bSSSSS
bSSSKS
.SSSSS
..SSS.
""")
# The jersey (team color, a stripe at the neck and arms in the second color)
# and the shorts with a side stripe. Row 8 is the hips.
TORSO = spr("""
.AT..TA.
TTTTTTTT
TTTTTTTT
TTTTTTTT
TTTTTTTT
.TTTTTT.
.TTTTTT.
.AAAAAA.
.TTTTTTA
.TTTTTTA
.tT..TtA
""")
BALL = [spr("""
.BBB.
BbBBB
BBbBB
BBBbB
.BBB.
"""), spr("""
.BBB.
BBBbB
BBbBB
BbBBB
.BBB.
""")]
STAR = [spr("""
..Y..
.YAY.
..Y..
"""), spr("""
...Y...
...Y...
.YYAYY.
...Y...
...Y...
"""), spr("""
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

# A pose: the legs as (thigh, shin) and the arms as (upper, fore) vectors
# from the hips and the shoulders, front limb then back; facing right.
POSES = {
    "run_a": dict(legs=[((4, 4), (0, 6)), ((-3, 4), (-4, 4))], arms=[((3, 3), (3, 2)), ((-3, 3), (-2, 3))]),
    "run_b": dict(legs=[((-2, 5), (-3, 5)), ((4, 4), (1, 5))], arms=[((3, 3), (3, 2)), ((-1, 4), (2, 3))]),
    "gather": dict(legs=[((4, 3), (-1, 5)), ((-3, 3), (1, 5))], arms=[((3, 2), (2, -2)), ((2, 3), (3, -2))]),
    "rise": dict(legs=[((3, 4), (-2, 5)), ((-2, 4), (-3, 5))], arms=[((2, -4), (1, -4)), ((0, -4), (1, -4))]),
    "reach": dict(legs=[((3, 4), (-2, 5)), ((-2, 4), (-3, 5))], arms=[((3, -4), (2, -4)), ((1, -4), (1, -4))]),
    "slam": dict(legs=[((3, 4), (-2, 5)), ((-2, 5), (-3, 4))], arms=[((4, -3), (4, -2)), ((3, -3), (4, -2))]),
    "hang_a": dict(legs=[((1, 5), (0, 5)), ((-1, 5), (-1, 5))], arms=[((2, -4), (1, -4)), ((1, -4), (1, -4))]),
    "hang_b": dict(legs=[((2, 5), (1, 5)), ((0, 5), (0, 5))], arms=[((2, -4), (1, -4)), ((1, -4), (1, -4))]),
    "land": dict(legs=[((4, 3), (-1, 4)), ((-3, 3), (1, 4))], arms=[((3, 2), (3, 1)), ((-3, 2), (-2, 2))]),
    "flex": dict(legs=[((2, 5), (0, 5)), ((-2, 5), (0, 5))], arms=[((3, -3), (2, -4)), ((-3, -3), (-2, -4))]),
    "stand": dict(legs=[((1, 5), (0, 5)), ((-1, 5), (0, 5))], arms=[((1, 4), (1, 3)), ((-1, 4), (-1, 3))]),
    "hold": dict(legs=[((1, 5), (0, 5)), ((-1, 5), (0, 5))], arms=[((3, 2), (2, -2)), ((2, 3), (3, -2))]),
    "throw": dict(legs=[((2, 5), (1, 5)), ((-2, 5), (-1, 5))], arms=[((3, -3), (3, -3)), ((2, -3), (3, -3))]),
    "point": dict(legs=[((1, 5), (0, 5)), ((-1, 5), (0, 5))], arms=[((4, -2), (4, -2)), ((-1, 4), (-1, 3))]),
}


def reach(pose):
    return max(th[1] + sh[1] for th, sh in POSES[pose]["legs"])


def hip_for_feet(pose, feet):
    return feet - reach(pose) - 1


def hands(pose, x, hip_y):
    p = POSES[pose]
    top = hip_y - 8
    out = []
    for i, ((ux, uy), (fx, fy)) in enumerate(p["arms"]):
        sx, sy = (x + 3, top + 1) if i == 0 else (x - 3, top + 1)
        out.append((sx + ux + fx, sy + uy + fy))
    return out


def player(c, x, hip_y, pose, ball=None, ball_frame=0):
    """The player with the hips at (x, hip_y). ball: None, "hand" (at the
    front hand) or "over" (held overhead in both hands). Returns the front
    hand."""
    p = POSES[pose]
    x, hip_y = int(round(x)), int(round(hip_y))
    top = hip_y - 8

    def leg(i):
        (tx, ty), (sx, sy) = p["legs"][i]
        hx, hy = x + (1 if i == 0 else -2), hip_y + 1
        kx, ky = hx + tx, hy + ty
        ax, ay = kx + sx, ky + sy
        shade = IDX["ball_dark"]
        c.thick(hx, hy, kx, ky, IDX["skin"], shade)
        c.thick(kx, ky, ax, ay, IDX["skin"], shade)
        c.put(ax, ay - 1, IDX["white"])  # the sock
        c.put(ax + 1, ay - 1, IDX["white"])
        for d in (-1, 0, 1, 2):  # the shoe, pointing forward (right)
            c.put(ax + d, ay + 1, IDX["alt"] if i == 0 else IDX["alt_shade"])

    def arm(i):
        (ux, uy), (fx, fy) = p["arms"][i]
        sx, sy = (x + 3, top + 1) if i == 0 else (x - 3, top + 1)
        ex, ey = sx + ux, sy + uy
        hx, hy = ex + fx, ey + fy
        c.thick(sx, sy, ex, ey, IDX["skin"], IDX["ball_dark"])
        c.thick(ex, ey, hx, hy, IDX["skin"], IDX["ball_dark"])
        return hx, hy

    arm(1)
    leg(1)
    c.blit(TORSO, x - 4, top)
    c.blit(HEAD, x - 2, top - 6)
    leg(0)
    hx, hy = arm(0)
    if ball == "hand":
        c.blit(BALL[ball_frame % 2], hx - 1, hy - 2)
    elif ball == "over":
        (bx, by), (cx, cy) = hands(pose, x, hip_y)
        c.blit(BALL[ball_frame % 2], (bx + cx) // 2 - 2, min(by, cy) - 4)
    return hx, hy


# ---- the arena -------------------------------------------------------------------
FLOOR = 50   # the floor's first row
FEET = 58    # where a standing player's shoes are
RIM_Y = 18  # a rim about one and a half players up, so a hanging player's feet are off the floor


class Hoop:
    def __init__(self, w):
        self.w = w
        self.bb = w - 8           # the backboard's left column
        self.left = self.bb - 13  # the rim's free end
        self.mid = self.bb - 7    # where the ball goes through

    def draw(self, c, dip=0, net=0):
        """dip: how far the rim's free end is pulled down (negative: up).
        net: the net stretched longer (positive) or snapped up short and
        flared (negative)."""
        bb = self.bb
        # The stanchion: the arm off the back of the board and the pole.
        c.rect(bb + 2, 10, self.w - 3, 11, IDX["gray"])
        c.rect(self.w - 3, 10, self.w - 2, FEET - 2, IDX["gray"])
        c.rect(self.w - 5, FEET - 2, self.w, FEET - 1, IDX["gray"])
        # The board edge-on, with its padding at the bottom.
        c.rect(bb, 3, bb + 1, 21, IDX["white"])
        c.rect(bb, 20, bb + 1, 22, IDX["alt_shade"])
        # The net: diamonds narrowing down, longer when the ball drags it.
        length = 8 + net
        for k in range(max(3, length)):
            y = RIM_Y + 1 + k
            inset = k // 3 if net >= 0 else max(0, k // 3 - 1)
            x0, x1 = self.left + 1 + inset, bb - 2 - inset
            if net < 0 and k == max(3, length) - 1:
                x0, x1 = x0 - 1, x1 + 1  # the snap flares the bottom out
            for x in range(x0, x1 + 1):
                if (x + k) % 2 == 0 or x in (x0, x1):
                    c.put(x, y, IDX["white"])
        # The rim, bent down at its free end by dip.
        n = bb - self.left
        for i in range(n + 1):
            x = self.left + i
            y = RIM_Y + round(dip * (n - i) / n)
            c.put(x, y, IDX["rim"])
        c.put(bb - 1, RIM_Y + 1, IDX["gray"])  # the bracket


def arena(c, hoop, roar=0.0, seed=0):
    w = c.w
    # The crowd in rows, a few in the team's colors; on the roar they jump
    # up and light up.
    for y in range(3, 40, 3):
        for x in range((y // 3) % 2, w, 2):
            h = (x * 7919 + y * 104729) % 97
            c.put(x, y, IDX["crowd"])
            if h < 9:
                c.put(x, y, IDX["team"] if h < 6 else IDX["alt_shade"])
            if roar > 0 and ((h + seed * 13) % 97) < roar * 20:
                c.put(x, y - 1, IDX["team"] if h % 3 else IDX["team_light"])
    # The scorer's table band in the team's color, lights along it.
    c.rect(0, 41, w - 1, 41, IDX["gray"])
    c.rect(0, 42, w - 1, 47, IDX["team_shade"])
    for x in range(2, w, 6):
        c.put(x, 44, IDX["team_light"] if roar <= 0 or (x // 6 + seed) % 2 else IDX["alt"])
    # The floor: boards running across, the lane under the hoop in the team's
    # color, its edge in white.
    for y in range(FLOOR, H):
        for x in range(w):
            c.put(x, y, IDX["wood"])
        if (y - FLOOR) % 4 == 3:
            for x in range((y * 5) % 9, w, 9):
                c.put(x, y, IDX["wood_dark"])
    lane = hoop.bb - 30
    for y in range(FLOOR + 2, H):
        x0 = lane - (y - FLOOR) // 2
        for x in range(x0, w):
            c.put(x, y, IDX["team_shade"])
        c.put(x0 - 1, y, IDX["white"])
    for x in range(lane - 1, w):
        c.put(x, FLOOR + 1, IDX["white"])


def title(c, glyphs, word, f, start, y=3, cx=None, step=None):
    """word drops in letter by letter from start (step frames apart; 0 drops
    them all at once), bounces and holds."""
    if f < start:
        return
    drops = []
    if step is None:
        step = 1 if len(word) <= 5 else 0.5  # a long word lands two letters a frame
    for n in range(len(word)):
        k = int(f - start - n * step)
        drops.append(-40 if k < 0 else [-14, 2, -2, 1, 0][k] if k < 5 else 0)
    text2x(c, glyphs, word, y, IDX["team"], IDX["alt"], cx=cx, drop_y=drops)


def lob(p0, p1, apex, u):
    """The ball from p0 to p1, u 0 to 1, over a top at row apex."""
    (x0, y0), (x1, y1) = p0, p1
    lift = ((y0 + y1) / 2 - apex) * 4  # the top of the curve reaches apex
    return x0 + (x1 - x0) * u, y0 + (y1 - y0) * u - lift * u * (1 - u)


def finish(c, glyphs, hoop, f, ball):
    """The slam and after, f from 0 (hands over the rim) to 15: the rim, the
    net, the ball, the player and the sparks. ball is False once it is gone."""
    rc = hoop.mid
    dips = [0, 3, 2, -1, 1, 0, -1, 0]
    nets = [0, 4, -2, -1, 0, 0, 0, 0]
    hoop.draw(c, dip=dips[f] if f < len(dips) else 0, net=nets[f] if f < len(nets) else 0)
    # The ball: through the net, a bounce on the floor, away under the board.
    path = [(rc, RIM_Y - 3), (rc, RIM_Y + 3), (rc, RIM_Y + 12), (rc + 1, RIM_Y + 20), (rc + 3, 53),
            (rc + 5, 46), (rc + 7, 51), (rc + 9, 54), (rc + 11, 50), (rc + 13, 54), (rc + 15, 53), (rc + 17, 54)]
    if f < len(path):
        bx, by = path[f]
        if f > 0:  # in front of the net from here; drawn over the rim while it goes in
            c.blit(BALL[f % 2], bx - 2, by - 2)
    # The player: the slam, hanging on the rim and swinging, the drop, the landing.
    if f == 0:
        x = rc - 10
        hx, hy = hands("slam", x, 0)[0]
        hip = RIM_Y - 4 - hy
        player(c, x, hip, "slam")
        c.blit(BALL[0], rc - 2, RIM_Y - 5)
    elif f < 7:
        x = rc - 7 + [0, 1, 0, -1, 0, 1][f - 1]
        player(c, x, RIM_Y + 15, "hang_a" if f % 2 else "hang_b")
    elif f < 9:
        x = rc - 8
        hip = [RIM_Y + 15 + 6, hip_for_feet("stand", FEET) - 2][f - 7]
        player(c, x, hip, "stand")
    elif f < 11:
        player(c, rc - 9, hip_for_feet("land", FEET), "land")
    else:
        player(c, rc - 9, hip_for_feet("flex", FEET), "flex")
    # The sparks off the rim.
    if 1 <= f <= 3:
        st = STAR[f - 1]
        c.blit(st, rc - len(st[0]) // 2, RIM_Y - len(st) // 2)


def dunk(w):
    glyphs = bdf()
    hoop = Hoop(w)
    rc = hoop.mid
    start = 22 if w >= 128 else -6
    run_end = rc - 29
    out = []
    for f in range(30):
        c = Canvas(w, H, PALETTE)
        roar = 0.0 if f < 15 else min(1.0, (f - 14) / 4)
        arena(c, hoop, roar, f // 3)  # the crowd changes every third frame: fewer bytes
        if f < 14:
            hoop.draw(c)
            if f < 8:  # dribbling in
                x = start + (run_end - start) * f / 7
                pose = "run_a" if (f // 2) % 2 == 0 else "run_b"
                hip = hip_for_feet(pose, FEET)
                hx, hy = player(c, x, hip, pose)
                bounce = [0, 0.5, 1, 0.5][f % 4]
                c.blit(BALL[f % 2], hx - 1, hy + 1 + (FEET - 3 - hy) * bounce - 2)
            elif f < 10:  # the gather
                player(c, rc - 25, hip_for_feet("gather", FEET), "gather", ball="hand")
            else:  # the rise, the ball up over his head
                k = f - 10
                x = rc - [21, 17, 14, 12][k]
                lift = [5, 11, 16, 19][k]
                player(c, x, hip_for_feet("rise", FEET) - lift, "rise", ball="over", ball_frame=f)
        else:
            finish(c, glyphs, hoop, f - 14, True)
        # On a narrow board the words would cover the rim, so they wait until
        # he lets go of it.
        title(c, glyphs, "DUNK!", f, 15 if w >= 128 else 20, y=2, step=None if w >= 128 else 0)
        out.append(c)
    return out


ALLEY_FRAMES = 42


def alley_oop(w):
    glyphs = bdf()
    hoop = Hoop(w)
    rc = hoop.mid
    wide = w >= 128
    run_end = rc - 29
    start = 44 if wide else -8
    catch_x = rc - 12  # where the leap tops out; the ball meets the hands over his head
    out = []
    for f in range(ALLEY_FRAMES):
        c = Canvas(w, H, PALETTE)
        roar = 0.0 if f < 21 else min(1.0, (f - 20) / 4)
        arena(c, hoop, roar, f // 3)  # the crowd changes every third frame: fewer bytes
        if f < 20:
            hoop.draw(c)
        # The passer, wide board only: holds it, lobs it, points at the rim.
        if wide:
            pose = "hold" if f < 4 else "throw" if f < 7 else "point"
            player(c, 14, hip_for_feet(pose, FEET - 2), pose, ball="hand" if f < 4 else None)
        # The finisher.
        if f < 20:
            if f < 13:
                x = start + (run_end - start) * f / 12
                pose = "run_a" if (f // 2) % 2 == 0 else "run_b"
                player(c, x, hip_for_feet(pose, FEET), pose)
            elif f < 15:
                player(c, rc - 25, hip_for_feet("gather", FEET), "gather")
            else:
                k = f - 15
                x = rc - [21, 17, 14, 12, 12][k]
                lift = [5, 11, 16, 19, 19][k]
                hip = hip_for_feet("reach", FEET) - lift
                player(c, x, hip, "reach" if f < 19 else "rise", ball="over" if f == 19 else None, ball_frame=f)
        else:
            finish(c, glyphs, hoop, f - 20, True)
        # The lob, from the passer's hands (or the edge) to the top of the leap.
        throw_f = 6 if wide else 4
        if throw_f <= f < 19:
            p0 = (26, 30) if wide else (-4, 34)
            p1 = (catch_x + 3, 11)
            u = (f - throw_f) / (19 - throw_f)
            bx, by = lob(p0, p1, 2 if wide else 4, u)
            for k in (3, 2, 1):
                uu = u - k * 0.03
                if uu > 0:
                    tx, ty = lob(p0, p1, 2 if wide else 4, uu)
                    c.put(tx, ty, IDX["team_light"] if k == 1 else IDX["team_shade"])
            c.blit(BALL[f % 2], bx - 2, by - 2)
        if wide:
            title(c, glyphs, "ALLEY-OOP!", f, 21, y=2)
        elif f < 33:  # one word at a time, as he lets go of the rim
            title(c, glyphs, "ALLEY", f, 25, y=2, step=0)
        else:
            title(c, glyphs, "OOP!", f, 33, y=2, step=0)
        out.append(c)
    return out


SHOWS = {"dunk": dunk, "alley_oop": alley_oop}


def main():
    want_sheet = "--sheet" in sys.argv
    for name, make in SHOWS.items():
        for w in (128, 64):
            fr = make(w)
            out = HERE / "frames" / f"{name}_{w}"
            save_frames(fr, out)
            if want_sheet:
                sheet(fr, HERE / "frames" / f"{name}_{w}.png", cols=6 if w >= 128 else 9)
            print(f"{name}_{w}: {len(fr)} frames at {FPS} fps into {out}")


if __name__ == "__main__":
    main()
