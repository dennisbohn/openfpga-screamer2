"""The attract sequence's screen (firmware/s2attract.c) as a model made again from STARTL.EXE's own code, for tools/menu_screens.sh.

Usage: python -I tools/attract_reference.py <game folder> <out dir>

The program's routine is 0x37ec1 (its caller 0x45882 draws it in place of the race screen in the attract race, mode word 0x80); this file follows its machine code one
instruction at a time (the addresses are in the comments) and holds the program's own variables:
    0x127ecc stage   0x128890 line   0x128888 x   0x127ed0 U   0x127ed4 T   0x132498 L (ending)   0x13249c B (way to black)   0x11f09c cued
The frame is one 20 ms step (the program at 50 frames a second: dt = 0.02). Floats are stored as the program stores them (a float32 after every `fstp dword`), the
sums between are done in the x87's registers (double here, which is exact for the sums that matter). The palette fade 0x514c1 is exact (fractions), the text and the
sprites come from tools/ui_reference.py's readers and its engine text routine (GAMEL.FNT, GAMEL.SPR), the credits from ATTRACT.TXT read here.

Written into <out dir>:
    attract_text.txt            ATTRACT.TXT as the program holds it: one line a line, carriage returns cut, empty lines kept
    attract_scenarios.txt       `scenario <name> <press step or -1> <steps>` and `frame <name> <step>` lines
    attract_<name>.states       a line for each step: the state after it and what it shows (hex for the floats)
    attract_<name>_<step>.idx   the frame (320 x 200 palette indices) over the test's background; .pal the palette it is shown in (256 words 0x00RRGGBB)
    attract_bg.idx / attract_bg.pal   the background (a stand-in for the race picture) and the palette the model fades
"""
import os
import struct
import sys
from fractions import Fraction

root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.join(root, "tools"))
import datfile  # noqa: E402
import ui_reference as U  # noqa: E402


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


DT = f32(0.02)


class Screen:
    """0x37ec1 and what it calls, a step at a time"""

    def __init__(self, lines, sprites):
        self.lines = lines
        self.spr = sprites                      # (x offset, y offset, width, height, pixels) by number
        self.stage = 0                          # 0x127ecc
        self.line = 0                           # 0x128890
        self.x = 0.0                            # 0x128888
        self.U = 0.0                            # 0x127ed0
        self.T = 0.0                            # 0x127ed4
        self.L = 0.0                            # 0x132498
        self.B = 0.0                            # 0x13249c
        self.cued = 0                           # 0x11f09c
        self.over = 0                           # 0x11f0a8 & 3
        self.white = None                       # the level of the palette last set towards white (the DAC keeps it)

    def step(self, key, dt=None, k=None):
        """one frame: dt is the program's 0x133350 (a float), k the easing table's value for the row 0x55 at the frame rate; the defaults are 50 frames a second"""
        dt = DT if dt is None else dt
        ease = f32(0.5 + 0x55 * 0.005) if k is None else k
        v = dict(black=0, sprite=-1, sx=0, caption=-1, title=0, first=0, count=0, white=0.0, dark=0.0, exit=0, music=0)
        # 0x37f20: a key and (0x132498 & 0x7fffffff) == 0 -> the ending flag (ENTER 0x10 / ESC 0x20 here: any button)
        if key and (bits(self.L) & 0x7FFFFFFF) == 0:
            self.L = 1.0
        if self.stage == 0:                     # 0x37f84 .. 0x38000
            v["black"] = 1                      # 0x51c74 (the screen to 0)
            self.line = 2                       # 0x37fdc
            self.stage += 1
            self.x = 1000.0                     # 0x447a0000
            self.T = 0.0
        elif self.stage < 3:                    # 0x38005 .. 0x381b3
            self.T = f32(dt + self.T)           # (0x38012: unused after)
            v["black"] = 1
            n = self.stage + 0x6d               # 0x38050
            xo, _, w, _, _ = self.spr[n]
            half = int(xo / 2)                  # 0x38081: x offset / 2 as the program cuts it
            target = ((320 - (xo + w)) >> 1) - half     # 0x380a2 .. 0x380b4
            tf = f32(float(target))             # pushed as a float (fild, fstp dword)
            self.x = f32((self.x - tf) * ease + tf)         # 0x111d6 with the row 0x55 (the table's value at 50 a second)
            v["sprite"] = n
            v["sx"] = int(self.x)               # 0x4ebe2: chop to a whole number
            if abs(self.x - float(target)) < 5.0:       # 0x38101 .. 0x38115 (the double 5.0)
                v["caption"] = self.stage - 1   # 0x38124 / 0x38147: the line 0 or 1 at y 0xa5
                self.U = f32(dt + self.U)       # 0x38168
                if bits(self.U) > 0x40400000:   # 0x3817a: an integer compare of the float (3.0)
                    self.U = 0.0                # 0x38186
                    self.x = 640.0              # 0x44200000
                    self.stage += 1
                    if self.stage > 2:
                        self.U = 1.0            # 0x381a9
        elif self.stage == 3:                   # 0x381b8 .. 0x3826a
            v["white"] = self.U                 # 0x381f3: 0x514c1(U, 255, 255, 255)
            self.white = self.U
            v["title"] = 1                      # 0x333d9(0x71, 4)
            if self.cued == 0:                  # 0x38207
                self.cued = 1
                v["music"] = 1                  # 0x4d928
            self.U = f32(float(self.U) - float(dt) * 1.2)       # 0x3821f .. 0x3823d: dt x 1.2 (a double), subtracted from U, stored as a float
            if self.U < 0.0:                    # 0x38243: 0 <= U goes on
                self.U = 1.0
                self.T = 0.0
                self.stage += 1
        elif self.stage >= 4:                   # 0x3826f: only stage 4 does anything (stage 5: not even the title)
            v["white"] = self.white if self.white is not None else 0.0      # (the palette is as stage 3 left it)
            if self.stage == 4:
                v["title"] = 1
                if (bits(self.T) & 0x7FFFFFFF) != 0:        # 0x3828b
                    self.T = f32(float(self.T) - float(dt))     # 0x38297: T - dt
                    if not (0.0 < self.T):      # 0x382a9 .. 0x382b4
                        self.T = 0.0
                else:
                    k = 0                       # 0x382c5
                    while True:
                        s = self.lines[self.line + k] if self.line + k < len(self.lines) else "#"
                        if s[:1] == "#":        # 0x382dd: cmp byte [eax], 0x23
                            break
                        k += 1
                    v["first"], v["count"] = self.line, k
                    if k == 0:                  # 0x38318
                        self.stage += 1
                        self.L = 1.0
                    else:
                        self.U = f32(float(self.U) - float(dt) * 0.25)     # 0x38330
                        if not (0.0 < self.U):  # 0x38354: jb skips while 0 < U
                            self.line += k + 1  # 0x38361
                            self.U = 1.0
                            self.T = 1.0        # 0x38375: T = U
        v["dark"] = self.B                      # 0x38381: 0x514c1(B, 0, 0, 0) while B is not 0
        if (bits(self.L) & 0x7FFFFFFF) != 0:    # 0x383a3
            if self.stage < 3:                  # 0x383b3: at once
                self.over = 1
            elif self.stage >= 4:               # 0x38401 .. 0x3846a
                self.B = f32(float(dt) * 2.0 + float(self.B))   # 0x3840a: dt x 2.0 (a float) + B
                if not (bits(self.B) < 0x3F800000):             # 0x38422: B < 1.0 goes on
                    self.over = 1
        if self.over:                           # 0x358d1 (screen to 0, palette black), then the palette back and 0x333d9(0x70, 0x28)
            music = v["music"]
            v = dict(black=0, sprite=-1, sx=0, caption=-1, title=0, first=0, count=0, white=0.0, dark=0.0, exit=1, music=music)
        return v


def fade(pal, level, t):
    """0x514c1(level, t, t, t) on a palette: the 6-bit entries (c - t) x k + t cut towards zero, k = 1 - level (a float) clamped to 0 .. 1; exact"""
    k = f32(1.0 - level)
    k = 0.0 if k < 0.0 else 1.0 if k > 1.0 else k
    kf = Fraction(k)
    out = []
    for c in pal:
        o = 0
        for sh in (16, 8, 0):
            v6 = (c >> sh & 255) >> 2
            r = int((Fraction(v6 - t) * kf + t))        # (int() of a Fraction cuts towards zero)
            o |= (r << 2) << sh
        out.append(o)
    return out


def palette_of(base, v):
    if v["exit"]:
        return list(base)
    if v["dark"] != 0.0:
        return fade(base, v["dark"], 0)
    if v["white"] != 0.0:
        return fade(base, v["white"], 63)
    return list(base)


def draw(canvas, spr, font, lines, v):
    """the frame's drawing in the order the routine does it (0x51c74 clears, 0x50dda / 0x333d9 sprites, 0x33294 text)"""
    if v["black"] or v["exit"]:
        canvas.px[:] = bytes(320 * 200)

    def centred_sprite(n, y):
        xo, _, w, _, _ = spr[n]
        half = int(xo / 2)
        canvas.sprite(spr, n, ((320 - (xo + w)) >> 1) - half, y)

    def centred_text(y, s):
        glyphs, width = canvas.layout(font, 2, s)
        canvas.text(font, 1, 2, (320 - width) >> 1, y, s)

    if v["exit"]:
        centred_sprite(0x70, 40)
        return
    if v["sprite"] >= 0:
        canvas.sprite(spr, v["sprite"], v["sx"], 10)
    if v["caption"] >= 0:
        centred_text(0xA5, lines[v["caption"]])
    if v["title"]:
        centred_sprite(0x71, 4)
    for k in range(v["count"]):
        centred_text(100 + 16 * k, lines[v["first"] + k])


def main():
    game, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    local = datfile.read(os.path.join(game, "DATA/LOCAL.DAT"))
    raw = datfile.extract(local, "ATTRACT.TXT").decode("latin-1")
    lines = [s.rstrip("\r") for s in raw.split("\n")]
    if lines and lines[-1] == "":
        lines.pop()                             # (the text ends with a line end: no line after it)
    with open(os.path.join(out, "attract_text.txt"), "w", newline="\n") as f:
        for s in lines:
            f.write(s + "\n")
    graf = U.container(game, "GFX/GRAF1.DAT")
    game1 = U.container(game, "GFX/GAME1.DAT")
    spr = U.sprites(datfile.extract(game1, "GAMEL.SPR"))
    font = U.fonts(datfile.extract(game1, "GAMEL.FNT"))[0]
    base, _ = U.bkk(datfile.extract(graf, "BKG_1L.BKK"))
    bg = bytes(100 + (x + 2 * y) % 100 for y in range(200) for x in range(320))
    open(os.path.join(out, "attract_bg.idx"), "wb").write(bg)
    open(os.path.join(out, "attract_bg.pal"), "wb").write(struct.pack("<256I", *base))

    def run(press, limit):
        s = Screen(lines, spr)
        states, views = [], []
        for step in range(limit):
            v = s.step(1 if step == press else 0)
            states.append((step, s.stage, s.line, bits(s.x), bits(s.U), bits(s.T) if s.stage >= 4 else 0, bits(s.B), bits(s.L), s.cued, s.over, bits(s.white if s.white is not None else 0.0)))
            views.append(v)
            if s.over:
                break
        return states, views

    full_states, full_views = run(-1, 8000)
    first = {}
    for st in full_states:
        first.setdefault(st[1], st[0])
    t3, t4 = first[3], first[4]
    scenarios = [("full", -1), ("press0", 0), ("press_s1", 40), ("press_s2", first[2] + 100), ("press_s3", t3 + 10), ("press_s4", t4 + 700), ("press_gap", t4 + 245), ("press_late", len(full_states) - 12)]
    sc_lines, fr_lines = [], []
    for name, press in scenarios:
        states, views = run(press, 8000)
        n = len(states)
        sc_lines.append("scenario %s %d %d" % (name, press, n))
        with open(os.path.join(out, "attract_%s.states" % name), "w") as f:
            for st, v in zip(states, views):
                f.write("%d %d %d %08x %08x %08x %08x %08x %d %d %08x | %d %d %d %d %d %d %d %08x %08x %d %d\n" % (
                    st + (v["black"], v["sprite"], v["sx"], v["caption"], v["title"], v["first"], v["count"], bits(v["white"]), bits(v["dark"]), v["exit"], v["music"])))
        pick = set(range(0, min(n, 200), 7)) | {n - 1, n - 2}
        for step in range(n):
            if step % 97 == 0 or (step > 0 and states[step][1] != states[step - 1][1]) or (step > 1 and states[step - 1][1] != states[step - 2][1]) or step in (press, press + 1):
                pick.add(step)
        for step in sorted(pick):
            if step >= n:
                continue
            v = views[step]
            canvas = U.Canvas(bg)
            draw(canvas, spr, font, lines, v)
            open(os.path.join(out, "attract_%s_%d.idx" % (name, step)), "wb").write(bytes(canvas.px))
            open(os.path.join(out, "attract_%s_%d.pal" % (name, step)), "wb").write(struct.pack("<256I", *palette_of(base, v)))
            fr_lines.append("frame %s %d" % (name, step))
        print("attract: %-10s press %5d: %4d steps, over at stage %d, first of each stage %s" % (name, press, n, states[-1][1], {k: v for k, v in sorted(first.items())} if name == "full" else ""))
    open(os.path.join(out, "attract_scenarios.txt"), "w").write("\n".join(sc_lines + fr_lines) + "\n")
    idle, n = 30.0, 0                           # the idle timer (0x40847): t - dt stored as a float until it is below 0
    while True:
        idle = f32(idle - float(DT))
        n += 1
        if idle < 0.0:
            break
    open(os.path.join(out, "attract_idle.txt"), "w").write("%d\n" % n)
    print("attract: the idle timer runs out after %d steps (%.2f s)" % (n, n * 0.02))


if __name__ == "__main__":
    main()
