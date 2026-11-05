"""The firmware's cars touching against the original's (tools/s2/check_aicontact_s2.sh), the analysis half.

The reference (OpenScreamer's routines, tools/s2/ref_trace/ref_run.sh race) writes every car's whole record, 0xCC0 bytes, after every frame of a
race of four cars; the firmware (tools/s2/race_replay_s2.c) is started from the reference's state a few frames before a touch and run on, writing
the same. Words 0x4d8 and 0x4e0 of the reference are whole numbers (the game's), the firmware's floats (built with S2_WHOLE_PLACES, so that they are too).

  aicontact_s2.py events <ref.bin>                 the frames at which the original's solver stopped a step (car +0x4c bit 8), one line "frame car,car" for each
                                                   run of them (a touch more than 40 frames after the last one is a new event)
  aicontact_s2.py compare <ref.bin> <replay.bin> <k0> <frame> <car,car>
                                                   one event: a line of numbers (see below)
  aicontact_s2.py window <ref.bin> <replay.bin> <k0>   a window with no touch asked for: how long the three computers stay within 2 units, the error after 20 frames and the worst in 150
  aicontact_s2.py summary <table>                  the table of check_aicontact_s2.sh in numbers, and whether the firmware passes

The numbers of an event, F the frame of the touch, errors the distance in units between the firmware's car and the original's, for the two cars of the touch:
  same     the cars the solver stopped are the same in the firmware and the original
  e0 e3    the error at F and F + 3
  e10      the largest error in the 10 frames from F
  within   how many frames from F the error stays within 2 units (170 at most)
  e150     the largest error in the 150 frames (3 s) from F
  jerk     the largest difference of a car's move in one frame from the original's move in the 6 frames from F (the "shove": the old code made tens to hundreds)
"""
import math
import mmap
import struct
import sys

REC = 0xCC0


class Trace:
    """frames x 4 cars x 0xCC0 bytes"""

    def __init__(self, path):
        self.f = open(path, "rb")
        self.m = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        self.n = len(self.m) // (REC * 4)

    def at(self, frame, car, off):
        return frame * REC * 4 + car * REC + off

    def flags(self, frame, car):
        return self.m[self.at(frame, car, 0x4c)]

    def place(self, frame, car, ref):
        """x, z of the car's middle: the original's are whole numbers, the firmware's floats"""
        a, b = self.at(frame, car, 0x4d8), self.at(frame, car, 0x4e0)
        fmt = "<i" if ref else "<f"
        return struct.unpack_from(fmt, self.m, a)[0], struct.unpack_from(fmt, self.m, b)[0]


def events(path, gap=40):
    ref = Trace(path)
    out, last = [], -1000
    for k in range(ref.n):
        cars = [c for c in range(4) if ref.flags(k, c) & 8]
        if cars:
            if k - last > gap or not out:
                out.append((k, cars))
            last = k
    return out


def error(ref, rep, k0, j, car):
    """distance of the firmware's car from the original's after row j of the replay (the original's frame k0 + 1 + j)"""
    rx, rz = ref.place(k0 + 1 + j, car, True)
    ox, oz = rep.place(j, car, False)
    return math.hypot(rx - ox, rz - oz)


def move(tr, j, car, ref):
    ax, az = tr.place(j - 1, car, ref)
    bx, bz = tr.place(j, car, ref)
    return math.hypot(bx - ax, bz - az)


def compare(refp, repp, k0, frame, cars):
    ref, rep = Trace(refp), Trace(repp)
    n = min(rep.n, ref.n - k0 - 1)
    j = frame - k0 - 1
    pair = [max(error(ref, rep, k0, i, c) for c in cars) for i in range(n)]
    within = next((i - j for i in range(j, n) if pair[i] > 2.0), n - j)
    fr = [c for c in range(4) if ref.flags(frame, c) & 8]
    fo = [c for c in range(4) if rep.flags(j, c) & 8]
    jerk = 0.0
    for c in cars:
        for i in range(j, min(j + 6, n)):
            mr = move(_Shift(ref, k0 + 1), i, c, True)
            mo = move(rep, i, c, False)
            jerk = max(jerk, abs(mr - mo))
    return dict(frame=frame, cars=cars, same=int(fr == fo), e0=pair[j], e3=pair[min(j + 3, n - 1)], e10=max(pair[j:j + 11]), within=min(within, 170),
                e150=max(pair[j:j + 151]), jerk=jerk)


class _Shift:
    """a reference trace seen from the replay's rows: row j is the original's frame k0 + 1 + j"""

    def __init__(self, tr, base):
        self.tr, self.base = tr, base

    def place(self, j, car, ref):
        return self.tr.place(self.base + j, car, ref)


def window(refp, repp, k0):
    ref, rep = Trace(refp), Trace(repp)
    n = min(rep.n, ref.n - k0 - 1)
    worst = [max(error(ref, rep, k0, i, c) for c in (1, 2, 3)) for i in range(n)]
    within = next((i for i in range(n) if worst[i] > 2.0), n)
    return dict(k0=k0, within=within, e20=worst[min(19, n - 1)], e150=max(worst[:151]))


def fields(line):
    d = {}
    for w in line.split():
        if "=" in w:
            k, v = w.split("=", 1)
            try:
                d[k] = float(v)
            except ValueError:
                d[k] = v
    return d


def pct(a, q):
    s = sorted(a)
    return s[min(len(s) - 1, int(round(q / 100.0 * (len(s) - 1))))]


def summary(path):
    ev = [fields(l) for l in open(path) if l.startswith("event")]
    wi = [fields(l) for l in open(path) if l.startswith("window")]
    if not ev or not wi:
        print("FAIL: no events or no windows in", path)
        return 1
    e10, jerk, within, e150 = [e["e10"] for e in ev], [e["jerk"] for e in ev], [e["within"] for e in ev], [e["e150"] for e in ev]
    same = int(sum(e["same"] for e in ev))
    ww = [w["within"] for w in wi]
    print("%d touches: the solver stops the same cars as the original in %d (%.0f %%)" % (len(ev), same, 100.0 * same / len(ev)))
    print("  error in the 10 frames from the touch (units):   median %.1f  75%% %.1f  90%% %.1f  worst %.1f" % (pct(e10, 50), pct(e10, 75), pct(e10, 90), max(e10)))
    print("  a car's move in one frame, off by (units):       median %.1f  75%% %.1f  90%% %.1f  worst %.1f" % (pct(jerk, 50), pct(jerk, 75), pct(jerk, 90), max(jerk)))
    print("  frames within 2 units of the original:           median %.0f  25%% %.0f  (170 = the whole window)" % (pct(within, 50), pct(within, 25)))
    print("  error in the 150 frames (3 s) from the touch:    median %.0f  75%% %.0f  worst %.0f" % (pct(e150, 50), pct(e150, 75), max(e150)))
    w150 = [w["e150"] for w in wi]
    print("%d windows with no touch asked for: frames within 2 units: median %.0f  25%% %.0f (175 = the whole window)" % (len(wi), pct(ww, 50), pct(ww, 25)))
    print("  the worst error of the three computers in 150 frames: median %.0f  75%% %.0f  worst %.0f" % (pct(w150, 50), pct(w150, 75), max(w150)))
    bad = []
    if same < 0.9 * len(ev):
        bad.append("the solver stops other cars than the original's in more than 10 %")
    if pct(e10, 50) > 2.0:
        bad.append("median error after a touch over 2 units")
    if pct(e10, 75) > 25.0:
        bad.append("75 % error after a touch over 25 units")
    if pct(jerk, 90) > 25.0:
        bad.append("90 % of the frame moves off by over 25 units (a shove)")
    if pct(ww, 25) < 20:
        bad.append("a quarter of the free windows leave the original within 20 frames")
    if pct(e150, 50) > 500.0:
        bad.append("the median error 3 s after a touch is over 500 units (the same code with another rounding is at about 170, the old code was at 3,300)")
    for b in bad:
        print("FAIL:", b)
    print("PASS" if not bad else "FAILED")
    return 1 if bad else 0


def show(d):
    return " ".join("%s=%s" % (k, ("%.1f" % v) if isinstance(v, float) else ",".join(map(str, v)) if isinstance(v, list) else v) for k, v in d.items())


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "events":
        for f, c in events(sys.argv[2]):
            print(f, ",".join(map(str, c)))
    elif cmd == "compare":
        print(show(compare(sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]), [int(x) for x in sys.argv[6].split(",")])))
    elif cmd == "window":
        print(show(window(sys.argv[2], sys.argv[3], int(sys.argv[4]))))
    elif cmd == "summary":
        sys.exit(summary(sys.argv[2]))
    else:
        print(__doc__)
        sys.exit(2)
