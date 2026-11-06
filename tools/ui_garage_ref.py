"""The cameras of the CAR page and the CAR CUSTOMIZATION page (firmware/s2garage.c) as a model, worked out again from the program (STARTL.EXE 0x3cc2c, the camera 0x3d749 .. 0x3d964; 0x3e1d5,
the camera 0x3e854 .. 0x3e94e; tools/dis.sh -f) in plain Python in double precision, as the x87 works: the places and aims of DATA/GEN.DAT's P_<team>.MCM read from the game's own file, the
camera frame by frame for scripted visits (the CAR page: the cars' state, the set-up, the choice, a car changed; the CUSTOMIZE page: the cursor on each item), and the shadows' places.
The camera model was compared with the running original frame by frame (tools/s2/dosbox_garage.py records its memory, tools/garage.sh against the firmware: 1,223 of 1,285 recorded
states of the CAR page and 734 of 796 of the other are a step of the firmware's camera, the rest half-written; this model gives the same frames as the firmware's).

Usage: python -I tools/ui_garage_ref.py <game folder> <out folder>          writes <out folder>/garage_ref.txt for tools/host/garage_screen.c
The file: `mcm <team> <the 3 records' counts, places and aims>` and per scripted visit `run <name> <car|cust> <team> <chosen car 0 .. 4>` followed by one line a frame:
`f <state or item> <choose: -1 none, else the car changed to> <a0> <a1> <x> <y> <z> <sway x 10^9> <s0 x y z> .. <s4 x y z>`; the CAR page's lines come with the five shadows (as the camera stood
before the frame's step), the CUSTOMIZE page's with zeros.
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import datfile

f32 = lambda x: struct.unpack("<f", struct.pack("<f", x))[0]
PI_F = f32(3.1415927)
DT = f32(0.02)


def easing(row):
    """the game's table 0x1240c0: 0.5 + row x 0.005, the sum in extended precision stored as a float"""
    return f32(f32(0.5) + row * f32(0.005))


def records(game, team):
    """P_<team>.MCM of DATA/GEN.DAT: [(places, aims)] of its three records"""
    m = datfile.extract(datfile.read(os.path.join(game, "DATA", "GEN.DAT")), "P_%d.MCM" % team)
    out, at = [], 0
    for _ in range(3):
        n = struct.unpack_from("<i", m, at)[0]
        pts = [struct.unpack_from("<3i", m, at + 4 + 12 * i) for i in range(2 * n)]
        out.append((pts[:n], pts[n:]))
        at += 4 + 24 * n
    assert at == len(m)
    return out


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
            a = f32(-((target - a) + -6.2831853) * e + target) if target - a >= PI_F else f32((a - target) * e + target)
        else:
            a = f32(-((target - a) + 6.2831853) * e + target) if a - target >= PI_F else f32((a - target) * e + target)
    if a < 0:
        a += 6.2831853
    elif a >= 6.2831853:
        a += -6.2831853
    return f32(a)


def angle(rad):
    return int(rad * 1024.0 / 3.1415927410125732) & 0x7FF


def radians(a):
    return f32(f32(a * f32(0.0009765625)) * PI_F)


def ease_place(pos, target, row):
    f = float(easing(row))
    return [int((pos[i] - target[i]) * f + target[i]) for i in range(3)]


class Car:
    """the CAR page's camera (0x3d749): sway [0x1324a4] outlives the visit (the program's life), the swing's phase [ebp-0x10] does not"""
    def __init__(self, recs, chosen, sway=0.0):
        self.aims, self.chosen, self.sway, self.phase = recs[1][1], chosen, sway, 0.0
        a = self.aims[chosen]
        self.pos = [a[0] + 900, a[1] + 360, a[2]]
        p, h = look_at(self.pos, a)
        self.a0, self.a1 = angle(p), angle(h)

    def shadows(self):
        out = []
        for a in self.aims:
            dx, dz = self.pos[0] - a[0], self.pos[2] - a[2]
            d = f32(math.sqrt(dx * dx + dz * dz))
            nx, nz = f32(dx / d), f32(dz / d)
            out.append((int(a[0] - nx * 30.0), a[1] - 30, int(a[2] - nz * 30.0)))
        return out

    def step(self, state):
        a = self.aims[self.chosen]
        height = 450 if state else int(math.sin(self.phase * float(f32(12.566370964050293))) * 180.0 + 360.0)
        ang = f32(self.sway * float(f32(6.2831854820251465)))
        s, c = f32(math.sin(ang)), f32(math.cos(ang))
        vx, vy, vz = 900, height, 0
        target = [a[0] + int(vz * s + vx * c), a[1] + vy, a[2] + int(vz * c - vx * s)]
        self.pos = ease_place(self.pos, target, 0x5A)
        pitch, head = look_at(target, a)
        self.sway = f32(float(DT) * 0.05 + self.sway)
        if self.sway > 1.0:
            self.sway = f32(self.sway - 1.0)
        self.phase = f32(float(DT) * 0.05 + self.phase)
        if self.phase > 1.0:
            self.phase = f32(self.phase - 1.0)
        p, h = radians(self.a0), radians(self.a1)
        self.a0, self.a1 = angle(ease_angle(p, pitch, 0x5A)), angle(ease_angle(h, head, 0x5A))


class Custom:
    """the CAR CUSTOMIZATION page's camera (0x3e854): from the last station, to the station of the item under the cursor"""
    def __init__(self, recs):
        self.places, self.aims = recs[2]
        self.pos = list(self.places[7])
        p, h = look_at(self.pos, self.aims[0])
        self.a0, self.a1 = angle(p), angle(h)

    def step(self, item):
        k = min(item + 1, 7)
        self.pos = ease_place(self.pos, self.places[k], 0x5A)
        pitch, head = look_at(self.pos, self.aims[k])
        p, h = radians(self.a0), radians(self.a1)
        self.a0, self.a1 = angle(ease_angle(p, pitch, 0x5D)), angle(ease_angle(h, head, 0x5D))


def car_visit(recs, chosen, script, frames, sway=0.0):
    """script: {frame: ('state', n) | ('choose', car)}; the lines of the frames"""
    c = Car(recs, chosen, sway)
    state, lines = 0, []
    for t in range(frames):
        ev = script.get(t)
        choose = -1
        if ev and ev[0] == "state":
            state = ev[1]
        elif ev and ev[0] == "choose":
            choose = ev[1]
            c.chosen, c.phase = choose, 0.125
        sh = c.shadows()
        c.step(state)
        lines.append("f %d %d %d %d %d %d %d %d %s" % (state, choose, c.a0, c.a1, c.pos[0], c.pos[1], c.pos[2], round(c.sway * 1e9), " ".join("%d %d %d" % s for s in sh)))
    return lines


def custom_visit(recs, script, frames):
    """script: {frame: item}"""
    c = Custom(recs)
    item, lines = 0, []
    for t in range(frames):
        item = script.get(t, item)
        c.step(item)
        lines.append("f %d -1 %d %d %d %d %d 0 %s" % (item, c.a0, c.a1, c.pos[0], c.pos[1], c.pos[2], " ".join(["0 0 0"] * 5)))
    return lines


def main():
    game, out = sys.argv[1], sys.argv[2]
    lines = []
    for team in range(1, 5):
        recs = records(game, team)
        lines.append("mcm %d %s" % (team, " ".join("%d %s" % (len(p), " ".join("%d %d %d" % q for q in p + a)) for p, a in recs)))
    for team in (1, 3):
        recs = records(game, team)
        lines.append("run car_cars car %d 1" % team)
        lines += car_visit(recs, 1, {}, 420)                                                      # the cars' state: the swing and the sway for 8.4 s
        lines.append("run car_states car %d 0" % team)
        lines += car_visit(recs, 0, {40: ("state", 1), 120: ("state", 2), 200: ("state", 1), 260: ("state", 0)}, 400)
        lines.append("run car_change car %d 2" % team)
        lines += car_visit(recs, 2, {30: ("choose", 3), 100: ("choose", 4), 190: ("choose", 0), 200: ("state", 1)}, 330)
        lines.append("run car_again car %d 3" % team)
        lines += car_visit(recs, 3, {}, 150, sway=f32(0.3125))                                    # a visit with the sway already turned (the program's, past a first one)
        lines.append("run cust_walk cust %d 1" % team)
        lines += custom_visit(recs, {20: 1, 80: 2, 140: 3, 200: 4, 260: 5, 320: 6, 380: 7, 440: 6, 470: 0, 540: 2}, 640)
    open(os.path.join(out, "garage_ref.txt"), "w", newline="\n").write("\n".join(lines) + "\n")
    print("garage: %d lines" % len(lines))


if __name__ == "__main__":
    main()
