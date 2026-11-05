"""The options room (firmware/s2room.c) as a model, worked out again from the rules in the file's header and the program (STARTL.EXE 0x38e00 ..) in plain Python: P_<t>.MCM's stations
read from the game's own MENU<t>1.DAT, the camera of a visit frame by frame (the place eased towards the station or, once the picture fades out, towards the point it looks at;
the look-at angles of 0x33905 eased the short way round, 0x1016b; the easing table of the x87's single rounding) and the page's keys (a press once; LEFT / RIGHT move at once,
ENTER fades out over 25 frames, ESC and X leave at once) for the scripts tools/host/room_screen.c runs through the firmware's page. The camera model was compared with the running
original frame by frame (tools/s2/dosbox_arcade.py --options polls the game's memory; `python -I tools/ui_room_ref.py <game folder> --polls <folder>` does it again: every value the
poll saw of the camera's place and angles in a move, a fly-in or a fade out is a frame of this model, in order).

Usage: python -I tools/ui_room_ref.py <game folder> <out folder>            writes <out folder>/room_ref.txt for room_screen.c
       python -I tools/ui_room_ref.py <game folder> --polls <folder>        the model against the polls of a run (build/check/dosbox_room, dosbox_room_pages)
The file: `mcm <team> <the five places and five points, 30 numbers>` for each team, then per script `run <name> <spec>` (the spec as room_screen.c's put_keys takes it: <frame>:<key>),
`expect <result> <calls> <clicks...>` and `t <frame> <station> <x> <y> <z> <a0> <a1>` for every frame.
"""
import glob
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datfile

f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
PI_F, TWO_PI_F = f32(3.1415927), f32(6.2831853)
ROW_FIRST, ROW_MOVE, ROW_FADE = 0x5F, 0x55, 0x62
FADE_STEP = f32(0.02) * 2.0                                       # 0.02 s x 2.0 a second; a float
S_ENTER, S_ESC, S_LEFT, S_RIGHT = "B", "A", "L", "R"      # the keys of the test scripts (room_screen.c's put_keys)
RESULT = {"ARCADE": 1, "CHAMP": 2, "ATTACK": 3, "MULTI": 4, "SETTINGS": 5, "BACK": -1}


def easing(row):
    """the game's table 0x1240c0: 0.5 + row x 0.005, the sum in extended precision stored as a float"""
    return f32(f32(0.5) + row * f32(0.005))


def stations(game, team):
    """P_<team>.MCM of GFX/MENU<team>1.DAT: the first record's five places and five points"""
    data = datfile.read(os.path.join(game, "GFX", "MENU%d1.DAT" % team))
    m = datfile.extract(data, "P_%d.MCM" % team)
    n = struct.unpack_from("<i", m, 0)[0]
    assert n >= 5 and len(m) >= 4 + 24 * n
    pts = [struct.unpack_from("<3i", m, 4 + 12 * i) for i in range(2 * n)]
    return pts[:5], pts[n:n + 5]


def look_at(pos, aim):
    """0x33905: pitch (0x1324a8) and heading (0x1324ac) of the look from pos at aim, as floats"""
    dx, dy, dz = (pos[i] - aim[i] for i in range(3))
    r = math.sqrt(dx * dx + dz * dz)
    v = f32(math.acos(dx / r))
    if dz < 0:
        v = f32(f32(6.2831855) - v)
    head = f32(v + f32(-1.5707964))
    horiz, total = f32(r), f32(math.sqrt(dx * dx + dy * dy + dz * dz))
    a = abs(math.acos(f32(horiz / total)))
    pitch = f32(a) if dy < 0 else f32(6.283185482025146 - a)
    return pitch, head


def ease_angle(a, target, row):
    """0x1016b in floats: the short way round, kept in 0 .. 2 pi"""
    e = easing(row)
    if a != target:
        if a < target:
            v = -((target - a) + -TWO_PI_F) * e + target if target - a >= PI_F else (a - target) * e + target
        else:
            v = -((target - a) + TWO_PI_F) * e + target if a - target >= PI_F else (a - target) * e + target
    else:
        v = a
    v = f32(v)
    if v < 0:
        v = f32(v + TWO_PI_F)
    elif v >= TWO_PI_F:
        v = f32(v + -TWO_PI_F)
    return v


def angle(rad):
    return int(rad * 1024.0 / 3.1415927410125732) & 0x7FF


class Visit:
    """a visit of the room: the camera and the keys, one frame at a time"""

    def __init__(self, places, aims, station):
        self.P, self.A, self.st = places, aims, station
        self.pos = [places[station][i] + 4 * (places[station][i] - aims[station][i]) for i in range(3)]
        self.pitch, self.head = look_at(self.pos, aims[station])
        self.row, self.fade, self.leaving, self.before = ROW_FIRST, 0.0, False, ""
        self.result, self.clicks, self.last = 0, [], (tuple(self.pos), angle(self.pitch), angle(self.head), station)

    def frame(self, tick, key):
        """one frame with the key held in it ('.' none): the result if the page is over after it (the ENTER's fade is 25 frames: 0 now, the result later), else 0"""
        if self.result or self.leaving:
            pass
        else:
            edge = key if key != "." and key != self.before else ""
            if edge == S_ESC:                                 # the page leaves at once, before the frame's camera step and drawing
                self.result = RESULT["BACK"]
                return self.last
            elif edge == S_ENTER:
                self.leaving = True
                self.clicks.append(7)
                self.enter = tick
            elif edge in (S_LEFT, S_RIGHT):
                d = 1 if edge == S_RIGHT else -1
                if 0 <= self.st + d < 5:
                    self.st += d
                    self.row = ROW_MOVE
                    self.clicks.append(5)
        self.before = key
        if self.fade == 0.0:
            e = easing(self.row)
            self.pos = [int((self.pos[i] - self.P[self.st][i]) * e + self.P[self.st][i]) for i in range(3)]
            lp, lh = look_at(self.P[self.st], self.A[self.st])
        else:
            e = easing(ROW_FADE)
            self.pos = [int((self.pos[i] - self.A[self.st][i]) * e + self.A[self.st][i]) for i in range(3)]
            lp, lh = look_at(self.pos, self.A[self.st])
        row = int(self.fade * 12.0 + 85.0)
        self.pitch = ease_angle(self.pitch, lp, row)
        self.head = ease_angle(self.head, lh, row)
        if self.leaving:
            self.fade = f32(FADE_STEP + self.fade)
        self.last = (tuple(self.pos), angle(self.pitch), angle(self.head), self.st)
        return self.last


def run(places, aims, station, spec, frames=400):
    """the visit a script makes: (the result, the frames the page drew, the clicks, the states of the frames)"""
    script = ["."] * frames
    for item in spec.split():
        at, k = item.split(":")
        script[int(at)] = k
    v = Visit(places, aims, station)
    states = []
    enter_frame = None
    for t in range(frames):
        states.append(v.frame(t, script[t]))
        if v.result:
            return v.result, t + 1, v.clicks, states
        if v.leaving and enter_frame is None:
            enter_frame = t
        if enter_frame is not None and t == enter_frame + 24:         # the fade out: the ENTER's frame and 24 more (the palette at 1 - 0.04 k: 0.04 in the last)
            return v.st + 1, t + 1, v.clicks, states
    raise SystemExit("the script " + spec + " does not end")


SCRIPTS = [          # name, the station the visit starts at (the room keeps it from the visit before), the keys: <frame>:<key>
    ("flight", 0, "150:A"),
    ("moves", 0, "5:R 50:R 100:R 150:R 160:R 200:L 205:L 260:A"),
    ("ends", 0, "5:L 10:L 90:R 95:R 99:R 103:R 107:R 111:R 160:A"),
    ("held", 0, "5:R 6:R 7:R 8:R 9:R 10:R 11:R 12:R 13:R 14:R 15:R 16:R 17:R 18:R 19:R 20:R 21:R 22:R 23:R 24:R 25:R 26:R 27:R 28:R 29:R 30:R 31:R 32:R 33:R 34:R 35:R 36:R 37:R 38:R 39:R 40:R 41:R 42:R 43:R 44:R 45:R 46:R 47:R 48:R 49:R 50:R 51:R 52:R 53:R 54:R 55:R 56:R 57:R 58:R 59:R 60:R 70:A"),
    ("enter0", 0, "40:B"),
    ("enter2", 2, "30:B"),
    ("enter4", 4, "60:R 90:B"),
    ("enter_early", 1, "3:B"),
    ("esc", 3, "20:A"),
    ("enter_then_keys", 0, "10:B 12:A 14:R 16:X"),
]


def write(game, out):
    lines = []
    for team in (1, 2, 3, 4):
        places, aims = stations(game, team)
        lines.append("mcm %d " % team + " ".join(str(v) for p in places + aims for v in p))
    places, aims = stations(game, 1)
    for name, st, spec in SCRIPTS:
        result, calls, clicks, states = run(places, aims, st, spec)
        lines.append("run %s %d %s" % (name, st, spec))
        lines.append("expect %d %d %s" % (result, calls, " ".join(str(c) for c in clicks)))
        for t, (pos, a0, a1, at) in enumerate(states):
            lines.append("t %d %d %d %d %d %d %d" % (t, at, pos[0], pos[1], pos[2], a0, a1))
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "room_ref.txt"), "w", newline="\n").write("\n".join(lines) + "\n")
    print("room: %d scripts, the stations of four teams" % len(SCRIPTS))


def polls(game, folder):
    """the model against the running original's camera: for each poll file of a move or a fade out, the distinct values of every component (place, a0, a1) seen after the key must be
    among the model's frames' values, in order (the polls miss the frames the screenshots' stalls cost, so a subsequence), and the settled state at the start is the model's"""
    places, aims = stations(game, 1)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import room_shots
    bad = 0
    for f in sorted(glob.glob(os.path.join(folder, "poll_*.txt"))):
        name = os.path.basename(f)[5:-4]
        rows = room_shots.parse(f)
        seq, started = {k: [] for k in ("x", "y", "z", "a0", "a1")}, False
        key = "ke4d" if name.startswith("mv") else "k1c" if name.endswith("_enter") else None
        if key is None:
            continue
        for r in rows:
            if r[1] == "E":
                started = started or r[2] == key
            elif started:
                for k in seq:
                    if not seq[k] or seq[k][-1] != r[2][k]:
                        seq[k].append(r[2][k])
        st = int(name[2]) if name.startswith("mv") else int(name[1])
        v = Visit(places, aims, 0)
        for _ in range(500):
            v.frame(0, ".")
        for s in range(st):
            v.frame(0, "R")
            v.frame(0, ".")
            for _ in range(500):
                v.frame(0, ".")
            v.before = ""
        model = {k: [] for k in seq}
        out = [(tuple(v.pos), angle(v.pitch), angle(v.head))]
        v.before = ""
        out.append(v.frame(0, S_RIGHT if name.startswith("mv") else S_ENTER)[:3])
        for _ in range(400):                                          # (a poll runs for seconds: the glide's tail is a hundred frames long)
            out.append(v.frame(0, ".")[:3])
        for pos, a0, a1 in out:
            for k, val in zip(("x", "y", "z", "a0", "a1"), (pos[0], pos[1], pos[2], a0, a1)):
                if not model[k] or model[k][-1] != val:
                    model[k].append(val)
        ok = {}
        for k in seq:
            seen = seq[k][:-1] if name.endswith("_enter") else seq[k]        # (the last value is the next page's: the poll runs on into it)
            it = iter(model[k])
            ok[k] = all(any(x == y for y in it) for x in seen)
        bad += not all(ok.values())
        print("%-10s %s (%d values seen of the place and angles)" % (name, "every one is a frame of the model, in order" if all(ok.values()) else "NOT in the model: " + str(ok),
                                                                 sum(len(x) for x in seq.values())))
    return bad


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[2] == "--polls":
        sys.exit(1 if polls(sys.argv[1], sys.argv[3]) else 0)
    write(sys.argv[1], sys.argv[2])
