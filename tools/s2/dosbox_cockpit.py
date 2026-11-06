"""The original's views from the car in DOSBox (docs/screamer2/hud.md "The views from the car"): the camera record and the player's car frame by frame, and raw screenshots.

Usage: python -I tools/s2/dosbox_cockpit.py <track 1..7> [seconds of race, default 26 [team car]]      (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2")
       team 0 .. 3 and car 0 .. 4 (4 the team's bonus car, all four open) choose the player's car; default the first team's second car (dashboard model 1); the files are then named
       cockpit_t<track>_c<team><car>...

Starts STARTL.EXE in the game folder's own DOSBox on the private copy of the game folder of tools/s2/dosbox_arcade.py (build/dosgame, never linked) and types its way to an ARCADE race
on the track (tools/s2/dosbox_race_s2.py: the first team's second car, the name MIL), then, by the race's clock, presses the keys of the views (F1 INCAR, F2 INSIDE, F3 NEAR, F4 BACK, M
the mirror: the game's own keys, scan codes 0x3b .. 0x3e, 0x32) and takes raw screenshots (Ctrl+F5, 320 x 200 palette indices) in each view. The first three seconds the cars stand on the
grid (the lights), so the first views are seen with nothing moving; from 3 s the player's car is handed to the game's own driver (its controls pointer, as the attract race has it) and
drives. From the start of the race the game's memory (tools/dosram.py finds it in DOSBox's process) is looked at as often as it can be: a frame's state is the last look before the race
clock 0x13335c moves again (tools/s2/dosbox_smoke_s2.py's way). Written to build/check/dosbox_cockpit/ (game data, never committed):
  cockpit_t<track>.bin     per frame FRAME bytes: the clock, the raw frame time, 0x133098 and 0x1330a0 (the dial's eased speed and revs), the mirror flag 0x11f0c4, the player's car's whole
                           record (0xCC0 bytes from 0x119300), the camera record (the pointer 0x1192f4, 0xaec bytes), the mirror's (0x1192fc) and the car's object (the pointer at
                           car +0x990, 0x40 bytes)
  cockpit_t<track>.shots.txt   each screenshot: its file, the label, the race clock just before and after the key
  cockpit_t<track>.crusc.bin / .data.bin   the dashboards' table the game loaded (0x1242b4, 8 x 0x58 bytes) and the game's data object (the palette is in it)
  cockpit_t<track>_<n>.png     the screenshots, in the order of the requests
The keys go only to the DOSBox this script started and only while its window is in front (else it stops); it does not touch another DOSBox (one DOSBox script at a time: the caller
holds whatever lock the work is shared under).
"""
import os
import struct
import sys
import threading
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import dosbox_arcade as da
import dosbox_race_s2 as dr

out = os.path.join(da.root, "build", "check", "dosbox_cockpit")
REC, CARS = 0xCC0, 0x119300
CAM = 0xAEC
OBJ = 0x40
HEAD = 4 * 4 + 4
FRAME = HEAD + REC + 2 * CAM + OBJ

# (race clock, what): k<hex> a key, s<label> a screenshot. The first views are seen on the grid (the lights hold the cars to 3 s), then the car drives and the mirror is switched on.
EVENTS = [
    (0.25, "sback_grid"),
    (0.60, "k3b"), (1.15, "sincar_grid"),
    (1.45, "k3c"), (2.00, "sinside_grid"),
    (2.25, "k3d"), (2.75, "snear_grid"),
    (3.60, "k3e"), (4.30, "sback_run"),
    (5.00, "k32"), (5.60, "sback_mirror"),
    (6.20, "k3b"), (6.90, "sincar_mirror"),
    (7.50, "k3c"), (8.20, "sinside_mirror"),
    (8.80, "k3d"), (9.50, "snear_mirror"),
    (10.20, "k32"), (10.90, "snear_run"),
    (11.50, "k3b"), (12.20, "sincar_run"),
    (12.80, "k3c"), (13.50, "sinside_run"),
    (14.1, "k3e"),
    (16.0, "k3d"), (18.0, "k3b"), (20.0, "k3c"), (22.0, "k3e"), (24.0, "k3d"),
]


def f32(b):
    return struct.unpack("<f", b)[0]


class Sampler(threading.Thread):
    def __init__(self, mem, path):
        super().__init__(daemon=True)
        self.mem, self.g, self.path = mem, mem.g, path
        self.clock = 0.0
        self.stop_now = False
        self.frames = 0

    def look(self):
        g = self.g
        car = g.flat(CARS, REC)
        cam = g.ptr(self.mem.word(0x1192F4), CAM) or bytes(CAM)
        cam2 = g.ptr(self.mem.word(0x1192FC), CAM) or bytes(CAM)
        optr = struct.unpack_from("<I", car, 0x990)[0]
        obj = (g.ptr(optr, OBJ) if optr else None) or bytes(OBJ)
        return g.flat(0x133354, 4) + g.flat(0x133098, 4) + g.flat(0x1330A0, 4) + g.flat(0x11F0C4, 4) + car + cam + cam2 + obj

    def run(self):
        g = self.g
        last, cur = None, None
        with open(self.path, "wb") as f:
            while not self.stop_now:
                c0 = f32(g.flat(0x13335C, 4))
                sample = (c0, self.look())
                if f32(g.flat(0x13335C, 4)) != c0:
                    continue
                self.clock = c0
                if last is not None and c0 < last - 1.0:
                    break
                if c0 != last:
                    if cur is not None:
                        f.write(struct.pack("<f", cur[0]) + cur[1])
                        self.frames += 1
                    last = c0
                cur = sample


def ours(p, pid):
    if p.poll() is not None:
        sys.exit("this script's DOSBox has ended")
    da.front(pid)
    if da.foreground_pid() != pid:
        sys.exit("this script's DOSBox (process %d) is not the window in front: no key is sent" % pid)


def main():
    track = int(sys.argv[1])
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 26.0
    team, car = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (0, 1)
    os.makedirs(out, exist_ok=True)
    dr.out = out
    if (team, car) != (0, 1):                                   # (dosbox_race_s2.start() makes the private folder for the first team's second car: another car, with every bonus car open)
        make = da.make_private
        da.make_private = lambda extra, **choice: make(extra, **dict(choice, team=team, car=car, cars5=True))
    p, mem = dr.start(track, seconds)
    pid = p.pid
    da.keyed_pid = pid
    g = mem.g
    ok = mem.wait(lambda: mem.word(0x130D10) == 1 and mem.word(0x130C48) == 4 and f32(g.flat(0x13335C, 4)) < 1.0, 200)
    if not ok:
        sys.exit("the race did not start")
    tag = "t%d" % track if (team, car) == (0, 1) else "t%d_c%d%d" % (track, team, car)
    name = lambda what: os.path.join(out, "cockpit_%s.%s" % (tag, what))
    open(name("crusc.bin"), "wb").write(g.flat(0x1242B4, 8 * 0x58))
    lo, hi = g.objs[3]
    open(name("data.bin"), "wb").write(g.flat(lo, hi - lo))
    print("view %d (the options'), mirror %d, mode %d" % (struct.unpack_from("<i", g.ptr(mem.word(0x1192F4), CAM), 0xAE4)[0], mem.word(0x11F0C4), mem.word(0x130D10)))
    s = Sampler(mem, name("bin"))
    s.start()
    ev, log, driven = list(EVENTS), [], False
    shots = 0
    while s.is_alive() and s.clock <= seconds:
        time.sleep(0.005)
        if not driven and s.clock >= 3.0:
            driven = True
            g.poke(0x1193B4, struct.pack("<i", 0))             # the player's car to the game's driver
        if ev and s.clock >= ev[0][0]:
            at, what = ev.pop(0)
            ours(p, pid)
            before = s.clock
            if what[0] == "k":
                sc = int(what[1:], 16)
                da.key(sc, 0)
                time.sleep(0.05)
                da.key(sc, 1)
            else:
                for sc, up in ((0x1D, 0), (0x3F, 0), (0x3F, 1), (0x1D, 1)):
                    da.key(sc, up)
                    time.sleep(0.04)
                shots += 1
            log.append("%s %.4f %.4f" % (what, before, s.clock))
            print(log[-1])
    s.stop_now = True
    s.join(5)
    time.sleep(0.5)
    pngs = sorted((os.path.join(out, x) for x in os.listdir(out) if x.lower().endswith(".png") and not x.startswith("cockpit_")), key=os.path.getmtime)
    shot_log = [l for l in log if l[0] == "s"]
    with open(name("shots.txt"), "w") as fh:
        for i, f in enumerate(pngs):
            os.replace(f, name("%02d.png" % i).replace("cockpit_%s." % tag, "cockpit_%s_" % tag))
            fh.write("cockpit_%s_%02d.png %s\n" % (tag, i, shot_log[i] if i < len(shot_log) else "?"))
        fh.write("# keys\n" + "\n".join(l for l in log if l[0] == "k") + "\n")
    print("%d frames written (%d bytes each), race clock to %.2f s, %d screenshots of %d asked" % (s.frames, FRAME, s.clock, len(pngs), shots))
    p.kill()


if __name__ == "__main__":
    main()
