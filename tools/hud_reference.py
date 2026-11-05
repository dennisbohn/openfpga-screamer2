"""Independent reference for the race screen (firmware/s2hud.c) from the game's own files, for tools/hud_screens.sh.

Usage: python -I tools/hud_reference.py <game folder> <out dir>

Writes into <out dir> (never committed): scenario.txt, the made-up race the harness (tools/host/hud_screen.c) feeds the firmware, one line a
command, and for every frame of it the picture the race screen must make, `<name>.idx` (320 x 200 palette indices), drawn over a race picture
of the repository (docs/screamer2/s2_track4_city.png quantized to the track's palette).

The model here is written from STARTL.EXE's race screen (0x45a4e, the instrument 0x46d42, the bar 0x458c7, the bends 0x456c8, the checkpoints
0x1ffcb) with the sprites and fonts read from GAME1.DAT by ui_reference.py's readers and the text routine as the engine has it; the
firmware's code is not consulted. Arithmetic is single precision where the machine code's floats are (f32()), so the numbers cut off at the
same places. Every frame of the scenario is compared pixel for pixel.
"""
import math
import os
import struct
import sys

root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(root, "tools"))
import datfile  # noqa: E402
import ui_reference as ui  # noqa: E402


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def trunc(x):
    return int(x)                                   # toward zero, as the machine code's float to int


class Canvas(ui.Canvas):
    def fill(self, x, y, w, h, v):
        for r in range(y, y + h):
            for c in range(x, x + w):
                self.put(c, r, v)

    def line(self, x0, y0, x1, y1, v):
        """Bresenham as the blitter draws it (docs/soc.md): the pixels inside the screen, the end points included."""
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy, err = 1 if x1 >= x0 else -1, 1 if y1 >= y0 else -1, dx + dy
        while True:
            self.put(x0, y0, v)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                x0 += sx
                err += dy
            if e2 <= dx:
                y0 += sy
                err += dx


def clock_digits(t, empty=True):
    """0x37e1e: minutes, seconds and hundredths each cut off; 3599.999 is the empty record."""
    if t > f32(3599.9):
        return "--:--:--"
    whole = trunc(t) if t > 0 else 0
    hundredths = trunc(f32(f32(t - float(whole)) * 100.0))
    return "%02d:%02d:%02d" % (whole // 60, whole % 60, hundredths)


class Car:
    def __init__(self, team):
        self.team, self.n4b8, self.n4bc, self.n4d8, self.n4e0 = team, 0.0, 0.0, 0.0, 0.0
        self.place, self.gear, self.flags, self.lapcnt = 0, 0, 0, 0
        self.total, self.lapt, self.best = 0.0, 0.0, f32(3599.999)
        self.speed, self.rpm, self.maxrpm, self.wrong = 0.0, 0.0, 1.0, 1.0
        self.px = self.py = self.ph = 0.0


FIELDS = ("team", "n4b8", "n4bc", "n4d8", "n4e0", "place", "gear", "flags", "lapcnt", "total", "lapt", "best", "speed", "rpm", "maxrpm", "wrong",
          "px", "py", "ph")


class Hud:
    def __init__(self, game):
        g = datfile.read(os.path.join(game, "GFX", "GAME1.DAT"))
        self.spr = ui.sprites(datfile.extract(g, "GAMEL.SPR"))
        self.fnt = ui.fonts(datfile.extract(g, "GAMEL.FNT"))
        local = datfile.read(os.path.join(game, "DATA", "LOCAL.DAT"))
        lines = [l.rstrip(" \r") for l in datfile.extract(local, "GAME.TXT").decode("latin-1").split("\n")]
        lines = [l for l in lines if l and not l.startswith("*")]
        first = lines.index("THIS LAP")
        self.text = lines[first:first + 9]
        self.miles = self.text[8][0].lower() == "m"
        self.game = game
        self.kind, self.ncars = 0, 4                    # the kind of race (0 arcade, 1 time attack, 2 the free run) and the cars in it (time attack alone has one, with a ghost two)
        self.free = dict(lt=[3599.999] * 5, rt=[3599.999] * 5, sign=" " * 5, msg=("_", 0.0, 0.0))

    # a race begins: the track's best lap and total, the first checkpoint, the bends' arrows (MAPnDIR.SPL: a count, nodes of 56 bytes)
    def begin(self, track, laps, rec_lap, rec_total, cars):
        self.laps, self.rec_lap, self.rec_total = laps, f32(rec_lap), f32(rec_total)
        self.speed = self.revs = self.wrong = 0.0
        self.msg_left = self.best_left = self.bend_left = 0.0
        self.bend_next = self.bend_id = 0
        self.chk, self.chk_max, self.leader, self.msg = [0] * 4, [0] * 4, [0] * 64, ""
        self.seen_best = cars[0].best
        d = datfile.read(os.path.join(self.game, "DATA", "MAP%d.DAT" % track))
        s = datfile.extract(d, "MAP%dDIR.SPL" % track)
        n = struct.unpack_from("<i", s, 0)[0]
        self.bends = [struct.unpack_from("<4i", s, 8 + 56 * i)[1:4] for i in range(n)]

    def frame(self, c, steps, top, cars, clock, cam, view, nodes=36):
        """One frame of the race screen: the state moves on `steps` of 20 ms, then the picture is drawn into the Canvas c."""
        p = cars[0]
        finished = (p.flags & 1) != 0 or (p.flags & 2) != 0
        # ---- what changes
        dt = f32(float(steps) * f32(0.02))
        k = 1.0
        for _ in range(steps):
            k = f32(k * f32(0.75))
        speed = f32(p.speed * f32(1.513))
        revs = f32(p.rpm / p.maxrpm) if p.maxrpm != 0 else 0.0
        self.speed = f32(f32(f32(self.speed - speed) * k) + speed)
        self.revs = f32(f32(f32(self.revs - revs) * k) + revs)
        self.msg_left = f32(self.msg_left - dt) if self.msg_left > dt else 0.0
        self.wrong = f32(self.wrong + dt) if p.wrong == -1.0 else 0.0
        for i, car in enumerate(cars[:self.ncars]):
            chk = trunc(f32(f32(car.n4b8 * 5.0) / float(nodes)))
            if chk == self.chk[i]:
                continue
            self.chk[i] = chk
            if chk <= self.chk_max[i] or chk >= 64:
                continue
            self.chk_max[i] = chk
            if car.place == 1:
                self.leader[chk] = trunc(car.total)
            elif i == 0:
                self.msg = "\x01+" + clock_digits(abs(f32(float(self.leader[chk]) - car.total)))
                self.msg_left = 4.0
        if not finished:
            if self.bends:
                bx, bz, bid = self.bends[self.bend_next]
                dx, dz = trunc(p.n4d8) - bx, trunc(p.n4e0) - bz
                fx, fz = f32(float(dx) * f32(0.01)), f32(float(dz) * f32(0.01))
                if f32(math.sqrt(f32(f32(fx * fx) + f32(fz * fz)))) < 80.0:
                    self.bend_left, self.bend_id = 1.0, bid
                    self.bend_next = (self.bend_next + 1) % len(self.bends)
                self.bend_left = f32(self.bend_left - dt) if self.bend_left > dt else 0.0
            self.best_left = f32(self.best_left - dt) if self.best_left > dt else 0.0
            if p.best < self.seen_best:
                self.seen_best = p.best
                if p.best < self.rec_lap:
                    self.rec_lap, self.best_left = p.best, 4.0
        self.draw(c, cars, clock, cam, view, top, finished)

    def sprite(self, c, n, x, y):
        c.sprite(self.spr, n, x, y)

    def centred(self, c, n, y):
        sx, _, w, _, _ = self.spr[n]
        c.sprite(self.spr, n, (320 - (w + sx)) // 2 - (sx - (1 if sx < 0 else 0)) // 2, y)

    def put_text(self, c, font, colour, space, x, y, s):
        c.text(self.fnt[font], colour, space, x, y, s)
        return c.text_width(self.fnt[font], space, s)

    def say(self, c, font, colour, space, y, s):
        w = c.text_width(self.fnt[font], space, s)
        c.text(self.fnt[font], colour, space, (320 - w) >> 1, y, s)

    @staticmethod
    def clock(t):
        return "\x01" + clock_digits(t)

    def draw(self, c, cars, clock, cam, view, top, finished):
        p, T = cars[0], self.text
        # the 1ST mark over a car that leads (0x4962c): the point 500 above it as the scene projects it, 10 left and 12 up of it
        for i in range(1, self.ncars):
            if cars[i].place != 1:
                continue
            x, y, h, yaw, pitch, roll = cam
            sy, cy, sp, cp, sr, cr = (math.sin(yaw), math.cos(yaw), math.sin(pitch), math.cos(pitch), math.sin(roll), math.cos(roll))
            f = (sy * cp, -cy * cp, sp)
            r0 = (-cy, -sy, 0.0)
            u0 = (r0[1] * f[2], -r0[0] * f[2], r0[0] * f[1] - r0[1] * f[0])
            r = (r0[0] * cr + u0[0] * sr, r0[1] * cr + u0[1] * sr, u0[2] * sr)
            u = (u0[0] * cr - r0[0] * sr, u0[1] * cr - r0[1] * sr, u0[2] * cr)
            d = (cars[i].px - x, cars[i].py - y, cars[i].ph + 500.0 - h)
            depth = sum(d[j] * f[j] for j in range(3))
            if depth <= 0 or depth > 20000:
                continue
            k = view[2] / 16.0 / (depth + 128.0)
            mx = trunc(view[0] / 16.0 + sum(d[j] * r[j] for j in range(3)) * k) - 10
            my = trunc(view[1] / 16.0 - sum(d[j] * u[j] for j in range(3)) * k) - 12
            if -15 < mx < 320 and -11 < my < 200:
                self.sprite(c, 0x26, mx, my)
        # the dial (0x46d42)
        self.sprite(c, 0x1a, 238, 132)
        shown = self.speed / f32(1.6) if self.miles else self.speed
        v = trunc(shown) if shown > 0 else 0
        self.put_text(c, 2, 5, 1, 267, 177, "%03d" % v)
        self.put_text(c, 2, 4, 2, 299, 177, "%d" % max(p.gear, 0))
        a = f32(f32(self.revs * f32(math.pi)) - 2.0)
        if abs(a) < 0.001:
            a = 0.0
        s, co = f32(math.sin(a)), f32(math.cos(a))
        px, py = 271, 168
        tx, ty = trunc(f32(float(px) + f32(s * 30.0))), trunc(f32(float(py) - f32(co * 30.0)))
        for ox, oy in ((0, 0), (co, s), (-co, -s)):
            c.line(trunc(f32(float(px) + ox)), trunc(f32(float(py) + oy)), tx, ty, 0x5b)
        if not finished and self.kind == 2:                  # the free run (0x460ae ..): THIS LAP and BEST LAP on top, the five markers' list
            if self.bend_left > 0:
                self.centred(c, 0x1b + self.bend_id, 0x32 + top)
            self.put_text(c, 3, 3, -1, 8, 16, T[0])
            self.put_text(c, 3, 3, -1, 0x10e, 16, T[2])
            self.put_text(c, 3, 1, -1, 8, 24, self.clock(p.lapt))
            self.put_text(c, 3, 1, -1, 0x10e, 24, self.clock(self.rec_lap))
            fr = self.free
            for k in range(5):
                self.put_text(c, 3, 3, -1, 8, 0x38 + 8 * k, chr(65 + k))
                self.put_text(c, 3, 3, -1, 0x105, 0x38 + 8 * k, chr(65 + k))
                self.put_text(c, 3, 1, -1, 0x12, 0x38 + 8 * k, self.clock(fr["lt"][k]))
                self.put_text(c, 3, 1, -1, 0x10a, 0x38 + 8 * k, "\x01" + fr["sign"][k] + clock_digits(fr["rt"][k]))
            sg, val, left = fr["msg"]
            if left > 0:
                s = "\x01" + (" " if sg == "_" else sg) + clock_digits(val)
                if sg == "+":
                    self.say(c, 8, 3, -1, 0x23 + top, s)
                else:
                    self.say(c, 7, 1, -1, 0x23 + top, s)
            if self.best_left > 0 and trunc(f32(self.best_left * 2.0)) & 1:
                self.say(c, 0, 1, 2, 0xf + top, T[4])
        elif not finished:
            if self.bend_left > 0:
                self.centred(c, 0x1b + self.bend_id, 0x32 + top)
            if self.ncars > 1:
                if p.place == 1:
                    self.sprite(c, 0x72, 0x10e, 4)
                w = c.text_width(self.fnt[3], -1, T[6])
                self.put_text(c, 3, 3, -1, 0x138 - w, 2, T[6])
                self.put_text(c, 1, 1, 2, 0x11e, 0xb, "%d" % p.place)
            if self.msg_left > 0:
                self.say(c, 8, 3, -1, 0x23 + top, self.msg)
            self.put_text(c, 3, 3, -1, 8, 2 + top, T[7])
            lap = min(p.lapcnt + 1, self.laps)
            w = self.put_text(c, 1, 1, 2, 8, 0xb + top, "%d:" % lap)
            self.put_text(c, 4, 1, 2, w + 5, 0x14 + top, "%d" % self.laps)
            self.put_text(c, 3, 3, -1, 8, 0x38, T[0])
            self.put_text(c, 3, 1, -1, 8, 0x40, self.clock(p.lapt))
            self.put_text(c, 3, 3, -1, 8, 0x48, T[1])
            self.put_text(c, 3, 1, -1, 8, 0x50, self.clock(p.total))
            self.put_text(c, 3, 3, -1, 0x10e, 0x38, T[2])
            self.put_text(c, 3, 1, -1, 0x10e, 0x40, self.clock(self.rec_lap))
            self.put_text(c, 3, 3, -1, 0x10e, 0x48, T[3])
            self.put_text(c, 3, 1, -1, 0x10e, 0x50, self.clock(self.rec_total))
            if self.best_left > 0 and trunc(f32(self.best_left * 2.0)) & 1:
                self.say(c, 0, 1, 2, 0xf + top, T[4])
            y = 3 + top                                      # the bar (0x458c7)
            if self.ncars > 1:
                c.fill(0x6c, y + 1, 0x68, 7, 0)
                self.sprite(c, 0x25, 0x6c, y - 1)
                for i in range(1, self.ncars):
                    d = f32(f32(p.n4bc - cars[i].n4bc) / 200000.0)
                    d = max(-1.0, min(1.0, d))
                    self.sprite(c, 4 + cars[i].team - 1, trunc(f32(f32(160.0 - f32(d * 50.0)) + -4.0)), y)
                self.sprite(c, 9, 0x9c, y)
        if trunc(self.wrong) & 1:
            self.say(c, 0, 1, 2, 0xf + top, T[5])
        if 0 <= (3 - trunc(clock) if clock < 4.0 else -1) and not finished:
            self.centred(c, 0x42 + trunc(clock), 60)


# ------------------------------------------------------------------------------------------------ the scenario


def main():
    game, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    hud = Hud(game)
    # the race picture the frames are drawn over, and its palette (as ui_reference.py makes them for the pause tests)
    gfx = ui.container(game, "GFX/MAP4H01.DAT")
    col = datfile.extract(gfx, "MAP4.COL")
    pal = [(col[3 * i] * 4, col[3 * i + 1] * 4, col[3 * i + 2] * 4) for i in range(256)]
    w, h, rows = ui.read_png_rgb(os.path.join(root, "docs", "screamer2", "s2_track4_city.png"))
    first = {}
    for i, p in enumerate(pal):
        first.setdefault(p, i)
    bg = bytearray()
    for y in range(h):
        for x in range(w):
            rgb = tuple(rows[y][3 * x:3 * x + 3])
            i = first.get(rgb)
            if i is None:
                i = min(range(256), key=lambda k: sum((a - b) ** 2 for a, b in zip(pal[k], rgb)))
            bg.append(i)
    open(os.path.join(out, "hud_bg.idx"), "wb").write(bytes(bg))
    open(os.path.join(out, "hud.pal"), "wb").write(b"".join(struct.pack("<I", p[0] << 16 | p[1] << 8 | p[2]) for p in pal))
    lines = ["palette hud.pal", "background hud_bg.idx"]
    view = (159 * 16, 130 * 16, 128 * 16)
    cam = (0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
    cars = [Car(t) for t in (1, 2, 3, 4)]
    clock = 0.0
    nframes = [0]

    def begin(track, laps, rec_lap, rec_total):
        nonlocal cars, clock
        cars = [Car(t) for t in (1, 2, 3, 4)]
        clock = 0.0
        lines.append("begin %d %d %r %r" % (track, laps, rec_lap, rec_total))
        hud.begin(track, laps, rec_lap, rec_total, cars)

    def car(i, **kw):
        s = []
        for k, v in kw.items():
            assert k in FIELDS, k
            setattr(cars[i], k, v if k in ("team", "place", "gear", "flags", "lapcnt") else f32(float(v)))     # (the record's words are floats)
            s.append("%s=%r" % (k, v))
        lines.append("car %d %s" % (i, " ".join(s)))

    def set_clock(t):
        nonlocal clock
        clock = t
        lines.append("clock %r" % t)

    def camera(*c):
        nonlocal cam
        cam = tuple(float(v) for v in c)
        lines.append("camera " + " ".join(repr(float(v)) for v in c))

    def emit(name, steps=1, top=0):
        c = Canvas()
        c.px = bytearray(bg)                                # the screen is drawn over the race picture
        hud.frame(c, steps, top, cars, clock, cam, view)
        open(os.path.join(out, name + ".idx"), "wb").write(bytes(c.px))
        lines.append("frame %s %d %d %s.idx" % (name, steps, top, name))
        nframes[0] += 1

    # ---------------- 1. the grid: the lights 3, 2, 1, GO, the player last
    begin(4, 3, 3599.999, 3599.999)
    for i, (t, n4bc) in enumerate(((1, -2000.0), (2, -4000.0), (3, -6000.0), (4, -8000.0))):
        car(i, n4b8=-1.2 - 0.1 * i, n4bc=n4bc + 0.0)
    car(0, place=4)
    for i in range(1, 4):
        car(i, place=i)
    set_clock(0.5)
    emit("grid_3")
    emit("grid_3_top", 1, 16)
    set_clock(1.5)
    emit("grid_2")
    set_clock(2.5)
    emit("grid_1")
    set_clock(3.5)
    emit("grid_go")
    set_clock(4.5)
    emit("grid_none", 50)

    # ---------------- 2. racing: the dial settles on the speed, a checkpoint passed, the gap shown, then gone
    car(1, place=1, n4b8=7.1, total=60.0)
    car(0, place=2, n4b8=7.0, n4bc=1000.0, speed=90.0, rpm=0.6, maxrpm=1.0, gear=3, lapcnt=1, lapt=40.25, total=75.5)
    car(1, n4bc=9000.0)
    car(2, place=3, n4b8=6.9, n4bc=-90000.0, total=0.0)
    car(3, place=4, n4b8=6.0, n4bc=300000.0)
    for k in range(6):
        emit("dial_%d" % k, 1, 16 if k % 2 else 0)
    car(1, n4b8=7.4, total=60.0)                            # the leader gets to checkpoint 1 (5 x 7.4 / 36 = 1.03) at 60 s
    emit("leader_checkpoint")
    car(0, n4b8=7.3, total=63.55)                           # the player after him: 3.55 s behind (the leader's whole seconds against his time)
    emit("checkpoint_gap")
    emit("checkpoint_gap_top", 1, 16)
    for k in range(8):
        emit("settle_%d" % k, 5)
    emit("gap_late", 100)
    emit("gap_gone", 100)

    # ---------------- 3. speeds and gears: the digits padded to three, the unit
    for name, sp, rev, gear in (("v0", 0.0, 0.0, 0), ("v9", 5.0, 0.12, 1), ("v99", 40.0, 0.4, 2), ("v100", 66.0, 0.55, 3), ("v104", 110.0, 0.97, 4)):
        car(0, speed=sp, rpm=rev, gear=gear)
        emit(name, 200)

    # ---------------- 4. leading: the laurel, BEST LAP! blinking over a record, the times
    begin(4, 5, 83.45, 250.0)
    car(0, place=1, lapcnt=2, lapt=12.34, total=99.99, speed=60.0, rpm=0.5, maxrpm=1.0, gear=2, n4b8=-1.0)
    for i in range(1, 4):
        car(i, place=i + 1, n4bc=-5000.0 * i, n4b8=-1.5)
    emit("lead", 50)
    car(0, best=70.0, lapcnt=3, lapt=0.0)                   # a lap of 70 s beats the player's best and the record of 83.45
    emit("record_0", 1)
    for k in range(10):
        emit("record_%d" % (k + 1), 10, 16 if k >= 5 else 0)
    emit("record_gone", 100)
    car(0, best=65.0, lapcnt=4)                             # again: 65 beats the new record
    emit("record_again", 1)
    emit("record_again_2", 30)

    # ---------------- 5. wrong way: shown in the second second, hidden in the third, ...
    car(0, wrong=-1.0)
    for k in range(6):
        emit("wrong_%d" % k, 25, 16 if k >= 3 else 0)
    car(0, wrong=1.0)
    emit("wrong_off", 5)

    # ---------------- 6. bends: the arrow shows for a second when the car is within 8,000 units of the node; the next node is the next
    begin(1, 3, 3599.999, 3599.999)
    car(0, place=1, n4d8=5767 + 4000, n4e0=-17308 + 3000, speed=50.0, rpm=0.4, gear=2)     # (5,000 units from node 0 of track 1)
    for i in range(1, 4):
        car(i, place=i + 1, n4bc=-6000.0 * i, n4b8=-1.5)
    car(0, n4b8=-1.0)
    emit("bend_0", 1)
    emit("bend_1", 20)
    emit("bend_top", 5, 16)
    emit("bend_gone", 30)
    car(0, n4d8=-12824 + 100, n4e0=-69878 - 100)            # node 1: its arrow
    emit("bend_next", 1)
    car(0, n4d8=0, n4e0=0)

    # ---------------- 7. a computer's car leads: the 1ST over it, as the camera sees it
    begin(4, 3, 3599.999, 3599.999)
    car(0, place=2, speed=70.0, rpm=0.5, gear=3, n4b8=-1.0)
    car(1, place=1, n4b8=-1.0, n4bc=500.0, px=0.0, py=-3000.0, ph=100.0)
    car(2, place=3, n4b8=-1.0)
    car(3, place=4, n4b8=-1.0)
    camera(0.0, 0.0, 300.0, 0.0, 0.0, 0.0)                  # looking along -y (yaw 0: forward (0, -1, 0)): the car is 3,000 ahead, to the middle
    emit("first_ahead", 1)
    camera(0.0, 0.0, 300.0, 0.05, 0.1, 0.02)
    lines.append("# the same with a turned camera: the mark within a pixel of the double precision position")
    x, y, h_, yaw, pitch, roll = cam
    sy, cy, sp, cp, sr, cr = (math.sin(yaw), math.cos(yaw), math.sin(pitch), math.cos(pitch), math.sin(roll), math.cos(roll))
    f = (sy * cp, -cy * cp, sp)
    r0 = (-cy, -sy, 0.0)
    u0 = (r0[1] * f[2], -r0[0] * f[2], r0[0] * f[1] - r0[1] * f[0])
    r = (r0[0] * cr + u0[0] * sr, r0[1] * cr + u0[1] * sr, u0[2] * sr)
    u = (u0[0] * cr - r0[0] * sr, u0[1] * cr - r0[1] * sr, u0[2] * cr)
    d = (0.0 - x, -3000.0 - y, 100.0 + 500.0 - h_)
    depth = sum(d[j] * f[j] for j in range(3))
    k = view[2] / 16.0 / (depth + 128.0)
    ex = view[0] / 16.0 + sum(d[j] * r[j] for j in range(3)) * k - 10
    ey = view[1] / 16.0 - sum(d[j] * u[j] for j in range(3)) * k - 12
    emit("first_turned", 1)
    lines.append("expect_cmd first_turned 0 %.3f %.3f 1" % (ex, ey))
    car(1, py=60000.0)                                      # too far (over 20,000): no mark
    emit("first_far", 1)
    car(1, py=5000.0)                                       # behind the camera: none
    emit("first_behind", 1)

    # ---------------- 9. time attack (menus row 7): alone no places, no bar, no laurel; with the ghost they are there (the ghost is car 1: its team's mark); the records are time attack's
    def kind(k, n):
        hud.kind, hud.ncars = k, n
        lines.append("kind %d %d" % (k, n))

    kind(1, 1)
    begin(4, 3, 77.7, 200.5)
    car(0, place=1, lapcnt=1, lapt=12.34, total=52.5, speed=60.0, rpm=0.5, maxrpm=1.0, gear=2, n4b8=-1.0)
    emit("ta_alone", 50)
    emit("ta_alone_top", 1, 16)
    kind(1, 2)
    begin(4, 3, 77.7, 200.5)
    car(0, place=2, lapcnt=0, lapt=12.34, total=12.34, speed=60.0, rpm=0.5, maxrpm=1.0, gear=2, n4b8=-1.0, n4bc=1000.0)
    car(1, place=1, team=3, n4b8=-1.0, n4bc=40000.0)
    emit("ta_ghost_ahead", 50)
    car(0, place=1, n4bc=90000.0)
    car(1, place=2, n4bc=20000.0)
    emit("ta_ghost_behind", 50)
    emit("ta_ghost_behind_top", 1, 16)
    kind(0, 4)

    # ---------------- 10. the free run: no lap block, THIS LAP and BEST LAP on top, the list of the five markers (A .. E): the running time in the row of the next marker,
    # the differences against the quickest lap's splits with their signs (- quicker, + slower), the quickest lap's own time in the row after, BEST LAP! over a record
    def free(lt, rt, sign, best, msg=("_", 0.0, 0.0)):
        hud.free = dict(lt=[f32(float(v)) for v in lt], rt=[f32(float(v)) for v in rt], sign=sign, msg=(msg[0], f32(msg[1]), f32(msg[2])))
        lines.append("freemsg %s %r %r" % (msg[0], msg[1], msg[2]))
        lines.append("free %s %s %s" % (" ".join(repr(float(v)) for v in lt), " ".join(repr(float(v)) for v in rt), sign.replace(" ", "_")))

    NONE = 3599.999
    kind(2, 1)
    lines.append("freebest 3599.999")
    begin(4, 1, 3599.999, 3599.999)
    free([NONE] * 5, [NONE] * 5, " " * 5, NONE)
    car(0, place=1, lapcnt=0, lapt=0.0, total=0.0, speed=0.0, n4b8=-1.0)
    emit("free_start", 1)
    free([12.34, NONE, NONE, NONE, NONE], [NONE] * 5, " " * 5, NONE)
    car(0, lapt=12.34, speed=50.0, rpm=0.4, gear=2, n4b8=0.5)
    emit("free_first", 5)
    emit("free_first_top", 1, 16)
    free([12.34, 25.5, 38.75, NONE, NONE], [NONE] * 5, " " * 5, NONE)
    car(0, lapt=40.0, speed=80.0, rpm=0.6, gear=3)
    emit("free_three", 5)
    free([12.34, 25.5, 38.75, 51.0, 64.25], [NONE] * 5, " " * 5, NONE)           # the lap's list stays four seconds
    car(0, lapt=0.5, lapcnt=1, best=64.25)
    emit("free_lap_done", 1)
    for k in range(6):
        emit("free_lap_blink_%d" % k, 10)
    # the second lap against the first: the list was cleared, the first row on the right shows the quickest lap's first split, signs and differences come with each marker
    lines.append("freebest 64.25")
    begin(4, 1, 64.25, 3599.999)
    free([12.0, NONE, NONE, NONE, NONE], [12.34, NONE, NONE, NONE, NONE], " " * 5, 64.25)
    car(0, lapt=12.0, speed=60.0, rpm=0.5, gear=2, place=1, n4b8=0.4)
    emit("free_second_a", 3)
    free([12.0, 25.0, 37.0, NONE, NONE], [0.34, 0.5, 1.75, NONE, NONE], "--   ", 64.25, ("-", 0.5, 3.5))
    car(0, lapt=37.5, speed=70.0, rpm=0.55, gear=3)
    emit("free_second_b", 3)
    free([12.0, 25.0, 38.9, 52.0, NONE], [0.34, 0.5, 1.15, 0.25, 64.25], "--++ ", 64.25, ("+", 0.25, 2.0))
    emit("free_second_c", 3)
    kind(0, 4)

    # ---------------- 8. the player has finished: no texts, the dial and the rest of the screen stay as they were
    begin(4, 3, 3599.999, 3599.999)
    car(0, place=1, flags=3, speed=20.0, rpm=0.2, gear=1, lapcnt=3, total=200.0, n4b8=-1.0)
    emit("finished", 50)
    open(os.path.join(out, "scenario.txt"), "w", newline="\n").write("\n".join(lines) + "\n")
    import zipfile
    with zipfile.ZipFile(os.path.join(out, "game_deflated.zip"), "w", zipfile.ZIP_DEFLATED) as z:      # for the firmware's path for a compressed ZIP
        z.write(os.path.join(game, "GFX", "GAME1.DAT"), "GFX/GAME1.DAT")
        z.write(os.path.join(game, "DATA", "LOCAL.DAT"), "DATA/LOCAL.DAT")
    print("%d frames in %s" % (nframes[0], out))


if __name__ == "__main__":
    main()
