"""The tracks' animated scenery as the original's routines work it, written again from the program (docs/development.md "Animated scenery"): an independent reading
for the firmware's firmware/s2anim.c to be checked against (tools/s2/check_anim_s2.sh) and for the original's own numbers (tools/s2/dosbox_anim.py) to be compared with.

    STARTL.EXE 0x30ec2 reads MAPn.PTH, 0x11c0c makes a path of it, 0x126ad is a channel's value at a time, 0x129d5 a path's place and way at a part (0 .. 1);
    0x31060 sets the ANIMATIONS rows up (at DETAIL HIGH only), 0x2cee4 runs them once a frame of the game's.

A path number is the character after "MV_" in the path's name ('1' .. '9' are 0 .. 8, 'a' on are 9 on), not the place in the file. A row of the ANIMATIONS table (MAPnDAT.DTR):
    name (an `@` in front: a sound at the path's first place, no object), seconds one run takes, seconds the race's clock must have reached, the distance within which a
    row with the flag 1 starts (-1: 52000), flags (1 starts when the player comes that near, 2 once only, 4 faces the screen), the path, the lap (the player's lap + 1; 0 any),
    the sample (the track's AMBIENT<n - 1>.DTR; 0 none).
Whole numbers are what the game stores: the places truncated, the angles in 2048ths of a turn.
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile  # noqa: E402

ALIVE = 52000                                   # the reach of a row that must be near the player (0xcb20)
HEARD = 80000                                   # how far a row's sound carries (0x13880)


class Path(list):
    """a path's three channels (x, y, height; each a list of (time, value, slope out, slope in)) and the span of the run its keys cover, start and end (the head's +0x28, +0x2c)"""


def load_paths(pth):
    """MAPn.PTH: {number: Path}; 100 bytes a path (the name at +8, the span at +0x28 and +0x2c: 0 and 1, the trains' 0 and 0.5), then three channels of 0x30
    bytes (the key count at +0x1c) with their keys of 0x2c bytes (time +8, value +0xc, slopes +0x10, +0x14)"""
    count, = struct.unpack_from("<i", pth, 0)
    out, p = {}, 4
    for _ in range(count):
        head = pth[p:p + 100]
        p += 100
        c = head[11]
        span = struct.unpack_from("<2f", head, 0x28)
        number = c - ord("1") if ord("0") <= c <= ord("9") else c - 0x58 if ord("a") <= c <= ord("z") else c - 0x38
        chans = []
        for _ in range(3):
            n, = struct.unpack_from("<i", pth, p + 0x1C)
            p += 0x30
            keys = []
            for _ in range(n):
                keys.append(struct.unpack_from("<4f", pth, p + 8))
                p += 0x2C
            chans.append(keys)
        out[number] = Path(chans)
        out[number].start, out[number].end = span
    return out


def channel_at(keys, s):
    """0x126ad: the value and the rate of change at s (0 .. 1). Between keys p and q the cubic with the controls v_p, v_p + out_p h, v_q - in_q h, v_q, h = (t_q - t_p) / 3;
    before the first key the first one's, from the last one on 0.001 before it"""
    if not keys:
        return 0.0, 0.0
    if len(keys) < 2:
        return keys[0][1], 0.0
    if keys[0][0] <= s:
        if keys[-1][0] <= s:
            s = keys[-1][0] - 0.001
    else:
        s = keys[0][0]
    q = 1
    while q < len(keys) - 1 and keys[q][0] <= s:
        q += 1
    p = q - 1
    dt = keys[q][0] - keys[p][0]
    u = (s - keys[p][0]) / dt
    h = dt / 3.0
    v0, v3 = keys[p][1], keys[q][1]
    b1 = keys[p][2] * h + v0
    b2 = v3 - keys[q][3] * h
    a3, a2, a1 = 3 * b1 + v3 - 3 * b2 - v0, 3 * v0 + 3 * b2 - 6 * b1, 3 * b1 - 3 * v0
    return ((a3 * u + a2) * u + a1) * u + v0, ((2 * a2 + 3 * a3 * u) * u + a1) / dt


def path_at(path, part):
    """0x129d5: the place (x, y, height) and the way (the rate of change, made a unit vector) at part (0 .. 1): the channels are read at (part - start) / (end - start)"""
    part = min(max(part, 0.0), 1.0)
    u = (part - path.start) / (path.end - path.start)
    pl, way = [], []
    for keys in path:
        v, r = channel_at(keys, u)
        pl.append(v)
        way.append(r)
    ln = math.sqrt(sum(w * w for w in way))
    if ln != 0.0:
        way = [w / ln for w in way]
    return pl, way


def angles(way):
    """0x2cee4: the heading and the climb of a way (x, y, height), in 2048ths of a turn: the heading is the angle of (x, -y), the climb the angle of the height against what is
    left along the ground once the heading is turned back (always -1 times the ground speed), each from the quadrant by the program's own steps"""
    wx, wy, wh = way
    turn = math.pi / 2 if wy == 0.0 else math.atan(abs(wx / wy))
    if 0.0 < wy:
        turn = math.pi - turn
    if wx < 0.0:
        turn = 2 * math.pi - turn
    along = wy * math.cos(-turn) + wx * math.sin(-turn)
    climb = math.pi / 2 if along == 0.0 else math.atan(abs(wh / along))
    if 0.0 < along:
        climb = math.pi - climb
    if wh < 0.0:
        climb = 2 * math.pi - climb
    return int(turn * 1024.0 / math.pi) & 0x7FF, int(climb * 1024.0 / math.pi) & 0x7FF


def tokens(text, section):
    """the words of a .DTR section's rows: lists of (kind, text) per line"""
    lines, on = [], False
    for ln in text.replace("\r", "").split("\n"):
        w = ln.split()
        if not on:
            on = w == [section]
            continue
        if w and w[0] == "END_LIST":
            break
        if w and not w[0].startswith("*"):
            lines.append(w)
    return lines


def load_rows(dtr):
    """ANIMATIONS: [dict] with name, time, start, range, flags, path, lap, sample"""
    rows = []
    for w in tokens(dtr.decode("latin-1"), "ANIMATIONS"):
        assert w[0][0] == "s" and len(w) == 8, w
        rows.append(dict(name=w[0][1:], time=float(w[1][1:]), start=float(w[2][1:]), range=float(w[3][1:]), flags=int(w[4][1:]), path=int(w[5][1:]),
                         lap=int(w[6][1:]), sample=int(w[7][1:], 16) if w[7][0] == "x" else int(w[7][1:])))
    return rows


def load(game, n):
    """the track's rows and paths from the game's folder (DATA/MAPn.DAT)"""
    dat = datfile.read(os.path.join(game, "DATA", "MAP%d.DAT" % n))
    return load_rows(datfile.extract(dat, "MAP%dDAT.DTR" % n)), load_paths(datfile.extract(dat, "MAP%d.PTH" % n))


class Rows:
    """the rows' state, stepped once a frame as 0x2cee4 does"""
    def __init__(self, rows, paths):
        self.rows, self.paths = rows, paths
        self.started = [0.0 if r["flags"] & 1 else r["start"] for r in rows]       # 0x31060: +0x10 is 0 for a row that starts by the player's coming near
        self.place = []
        for r in rows:
            pl = [k[0][1] if k else 0.0 for k in paths[r["path"]]] if r["name"][0] == "@" else path_at(paths[r["path"]], 0.0)[0]
            self.place.append([int(v) for v in pl])
        self.shown = [False] * len(rows)                # an object is in the drawing
        self.heard = [False] * len(rows)                # a sound is on
        self.part = [-1.0] * len(rows)
        self.angle = [(0, 0)] * len(rows)

    def step(self, clock, px, py, lap):
        """clock: the race's clock (0x13335c), px, py: the player's place (whole numbers), lap: its lap counter (+0x4a8)"""
        for i, r in enumerate(self.rows):
            own = r["name"][0] != "@"
            dx, dy = abs(px - self.place[i][0]), abs(py - self.place[i][1])
            flags = r["flags"]
            shown = False
            alive = (dx < ALIVE and dy < ALIVE) or ((flags & 3) == 0 and own)
            if alive and (lap + 1 == r["lap"] or r["lap"] == 0):
                if not own:
                    shown = True
                else:
                    rng = r["range"]
                    if rng == -1.0:
                        rng = 52000.0
                    if dx < rng and dy < rng and flags & 1 and self.started[i] == 0.0:
                        self.started[i] = clock
                    f = (clock - self.started[i]) / r["time"]
                    if f < 0.0 or ((flags & 1) and self.started[i] == 0.0):
                        part = -1.0
                    elif f > 1.0:
                        if flags & 2:
                            part = -1.0
                        elif flags & 1:
                            self.started[i] = 0.0
                            part = 0.0
                        else:
                            part = f - math.trunc(f)
                    else:
                        part = f - math.trunc(f)
                    if part != -1.0:
                        pl, way = path_at(self.paths[r["path"]], part)
                        self.place[i] = [int(v) for v in pl]
                        if not flags & 4:
                            self.angle[i] = angles(way)
                        shown = True
                    self.part[i] = part
            elif own and flags & 1 and not flags & 2 and self.started[i] != 0.0:
                self.started[i] = 0.0
            self.shown[i] = shown and own
            self.heard[i] = shown
        return self
