"""The original's wheel smoke and shadows in DOSBox (docs/development.md, "Wheel smoke and shadows").

Usage: python -I tools/s2/dosbox_smoke_s2.py <track 1..7> [seconds of race, default 14]      (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2")

Starts STARTL.EXE in the game folder's own DOSBox on the private copy of the game folder of tools/s2/dosbox_arcade.py (build/dosgame, never linked; its CONFIG has DETAIL LEVEL HIGH,
which the smoke needs, and the TRACK page's cursor at the first track), types its way to an ARCADE race on the track (as tools/s2/dosbox_race_s2.py), hands the player's car to the game's
own driver (so that all four cars take the line with their wheels spinning, as the attract race does) and from the start of the race looks at the game's memory in a thread of its own:
  smoke_t<track>.bin        per frame (a frame's state is the last look before the race clock moves again): the race clock, the 100 Hz counter the puffs' dispersion uses (0x4cfd6), the raw
                            frame time (0x133354), the four cars' whole records (0xCC0 bytes from 0x119300: the puff records at +0x228 (eight of 0x28 bytes, 'asmoke') and +0x368 ('a2smoke'),
                            the puffs' timer at +0x224, the car's place block +0x4d8, the wheels' records +0x574 + 0x70 n), the objects' table (0x1c bytes each from 0x11da20: the puffs' and the
                            shadows' places and angles) and the camera record (the pointer 0x1192f4): FRAME bytes a frame
  smoke_t<track>.table_<when>.bin   the see-through table the game made for the race (112 rows of 256), from the buffer behind the pointer at 0xbf5a0, at the start, after 6 s and at the end
  smoke_t<track>.data_<when>.bin    the game's whole data object at the start and at 6 s (the palette and TRACK_DATA the table was made from are in it)
  smoke_t<track>_<clock>.png  raw 320 x 200 screenshots (DOSBox's Ctrl+F5) taken while the race runs; smoke_t<track>.shots.txt: each with the race clocks just before and after the key
All of it under build/check/dosbox_smoke/ (game data, never committed). The keys go only to the DOSBox this script started and only while its window is in front (else it stops).
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

out = os.path.join(da.root, "build", "check", "dosbox_smoke")
REC, CARS = 0xCC0, 0x119300
OBJS, NOBJ = 0x11DA20, 170
CAM = 0xAEC
OBJ = 0x40                                              # what is kept of an object's record (the table's entries are pointers to them): place at +8, angles at +0x14, the puff's age at +0x20
FRAME = 4 + 4 + 4 + 4 * REC + 0x1C * NOBJ + CAM + 4 * 17 * OBJ  # clock, T, raw time, the cars, the objects' table, the camera record, then per car the shadow's object and the 16 puffs' (by the car's record: +0xbf8, and the index each puff record starts with)
SHOTS = (3.3, 3.8, 4.3, 5.0, 6.5, 8.0, 11.0)             # race clock seconds at which a screenshot is asked for (the lights are out at about 3 s)


def f32(b):
    return struct.unpack("<f", b)[0]


class Sampler(threading.Thread):
    """the frames of the race: the last look before the clock moves again is that clock's frame"""
    def __init__(self, mem, path):
        super().__init__(daemon=True)
        self.mem, self.g, self.path = mem, mem.g, path
        self.clock = 0.0
        self.stop_now = False
        self.frames = 0

    def look(self):
        g = self.g
        cam = self.mem.word(0x1192F4)
        cars, table = g.flat(CARS, 4 * REC), g.flat(OBJS, 0x1C * NOBJ)
        objs = b""
        for k in range(4):
            r = cars[k * REC:(k + 1) * REC]
            idx = [struct.unpack_from("<i", r, 0xBF8)[0]] + [struct.unpack_from("<i", r, base + 0x28 * j)[0] for base in (0x228, 0x368) for j in range(8)]
            for i in idx:
                ptr = struct.unpack_from("<I", table, 0x1C * i)[0] if 0 <= i < NOBJ else 0
                objs += (g.ptr(ptr, OBJ) if ptr else None) or bytes(OBJ)
        return (g.flat(0x4CFD6, 4) + g.flat(0x133354, 4) + cars + table + (g.ptr(cam, CAM) or bytes(CAM)) + objs)

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
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 14.0
    os.makedirs(out, exist_ok=True)
    dr.out = out
    p, mem = dr.start(track, seconds, detail=2)
    pid = p.pid
    da.keyed_pid = pid
    g = mem.g
    ok = mem.wait(lambda: mem.word(0x130D10) == 1 and mem.word(0x130C48) == 4 and f32(g.flat(0x13335C, 4)) < 1.0, 200)
    if not ok:
        sys.exit("the race did not start")
    g.poke(0x1193B4, struct.pack("<i", 0))             # the player's car to the game's driver
    print("detail %d (2 = HIGH), cars %d, mode %d" % (mem.word(0x130C54), mem.word(0x130C48), mem.word(0x130D10)))
    name = lambda what: os.path.join(out, "smoke_t%d.%s.bin" % (track, what))

    def table(when):
        ptr = mem.word(0xBF5A0)
        open(name("table_" + when), "wb").write(g.ptr(ptr, 112 * 256))
        print("the see-through table (%s): pointer 0x%x" % (when, ptr))
    table("start")
    lo, hi = g.objs[3]
    open(name("data_start"), "wb").write(g.flat(lo, hi - lo))
    s = Sampler(mem, name("frames").replace(".frames.bin", ".bin"))
    s.start()
    shots, log = list(SHOTS), []
    dumped = False
    while s.is_alive() and s.clock <= seconds:
        time.sleep(0.01)
        if not dumped and s.clock >= 6.0:
            dumped = True
            table("6s")
            open(name("data_6s"), "wb").write(g.flat(lo, hi - lo))
        if shots and s.clock >= shots[0]:
            want = shots.pop(0)
            ours(p, pid)
            before = s.clock
            for sc, up in ((0x1D, 0), (0x3F, 0), (0x3F, 1), (0x1D, 1)):
                da.key(sc, up)
                time.sleep(0.05)
            after = s.clock
            time.sleep(0.5)
            pngs = sorted((os.path.join(out, x) for x in os.listdir(out) if x.lower().endswith(".png") and not x.startswith("smoke_")), key=os.path.getmtime)
            if pngs:
                new = os.path.join(out, "smoke_t%d_%05.2f.png" % (track, want))
                os.replace(pngs[-1], new)
                log.append("%s %.4f %.4f" % (os.path.basename(new), before, after))
                print("screenshot", log[-1])
    s.stop_now = True
    s.join(5)
    table("end")
    open(name("shots").replace(".shots.bin", ".shots.txt"), "w").write("\n".join(log) + "\n")
    print("%d frames written, race clock to %.2f s" % (s.frames, s.clock))
    p.kill()


if __name__ == "__main__":
    main()
