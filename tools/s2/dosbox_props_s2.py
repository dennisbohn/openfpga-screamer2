"""The original's obstacles (cones, fences, chests, crates) and sparks in DOSBox (docs/development.md, "Obstacles and sparks"): what tools/s2/check_props_s2.sh holds the firmware's
code against.

Usage: python -I tools/s2/dosbox_props_s2.py <track 1..7> [seconds of race, default 50]      (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2")

Starts STARTL.EXE in the game folder's own DOSBox on the private copy of the game folder of tools/s2/dosbox_arcade.py (build/dosgame, never linked; its CONFIG has DETAIL LEVEL
HIGH), types its way to an ARCADE race on the track (as tools/s2/dosbox_race_s2.py), hands the player's car to the game's own driver (so that all four cars take the line, touch
each other and the walls and knock what stands on the road, as the attract race does) and from the start of the race looks at the game's memory in a thread of its own:
  props_t<track>.bin     per frame (a frame's state is the last look before the race clock moves again; FRAME bytes): the race clock, the 100 Hz counter the game's chance is
                         seeded from (0x4cfd6), the step the routines work with (0x133350) and the raw frame time (0x133354), the count of steps a touch cut short (0x11cdb4), the
                         number of obstacles (0x126d4c), the four cars' whole records (0xCC0 bytes from 0x119300), the obstacles' records (0x3c bytes each from 0x124664, a hundred
                         of them) and the sparks' (sixty from 0x125dd4), the camera record (the pointer 0x1192f4, 0xAEC bytes), then the object records the obstacles' and the sparks' +0x28 name
                         (0x40 bytes each, zeros for a record that is not in use)
  props_t<track>_<clock>.png   raw 320 x 200 screenshots (DOSBox's Ctrl+F5) taken while the player's car is near an obstacle; props_t<track>.shots.txt: each with the race clocks
                         just before and after the key
  props_t<track>.pokes.txt   the cars of the arcade's race pass the obstacles of the tracks without a touch (the computers' line keeps clear of them: 45 s on California touched none), so that
                         nothing would fly: from 5 s on, every 2 s, the harness puts an obstacle that stands (one the game keeps in its table, in the private copy's memory) on the way of one
                         of the cars, 1,500 to 2,500 units ahead and up to 600 aside, at the car's height, heading as the car goes; a line each: the race clock, the car, the obstacle, where.
                         The check treats a standing obstacle that moves in such a frame as the harness's doing and takes it from the game
All of it under build/check/dosbox_props/ (game data, never committed). The keys go only to the DOSBox this script started and only while its window is in front (else it stops).
Take the lock folder (the rules of the agents' DOSBox runs) before you start it.
"""
import math
import os
import struct
import sys
import threading
import time

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import dosbox_arcade as da
import dosbox_race_s2 as dr

out = os.path.join(da.root, "build", "check", "dosbox_props")
REC, CARS = 0xCC0, 0x119300
PROPS, NPROPS, SPARKS = 0x124664, 100, 0x125DD4          # the obstacles' records (a hundred of 0x3c bytes) and the sparks' (sixty) follow each other
CAM = 0xAEC
HEAD = 4 + 4 * 5                                         # the clock, then the counter, the step, the raw time, the touch count, the obstacle count (the sampler writes the clock first)
OBJS, NOBJ, OBJ = 0x11DA20, 200, 0x40                     # the engine's table of objects (0x1c bytes each: the pointer to the object's record, 0x40 bytes of which are kept: the place at +8, the angles at +0x14)
FRAME = HEAD + 4 * REC + 0x3C * (NPROPS + 60) + CAM + OBJ * (NPROPS + 60)
DROP_FROM, DROP_EVERY = 5.0, 2.0                          # race clock seconds: the first obstacle put on a car's way and the gap between two
SHOT_GAP = 0.6                                           # race clock seconds between two screenshots
SHOTS = 14


def f32(b):
    return struct.unpack("<f", b)[0]


class Sampler(threading.Thread):
    """the frames of the race: the last look before the clock moves again is that clock's frame"""
    def __init__(self, mem, path):
        super().__init__(daemon=True)
        self.mem, self.g, self.path = mem, mem.g, path
        self.clock = 0.0
        self.near = 1e9                                  # the player's distance to the nearest obstacle in the last frame
        self.stop_now = False
        self.frames = 0

    def look(self):
        g = self.g
        cam = self.mem.word(0x1192F4)
        cars = g.flat(CARS, 4 * REC)
        props = g.flat(PROPS, 0x3C * (NPROPS + 60))
        n = struct.unpack("<i", g.flat(0x126D4C, 4))[0]
        px, pz = struct.unpack_from("<i", cars, 0x4D8)[0], struct.unpack_from("<i", cars, 0x4E0)[0]
        near = 1e9
        for k in range(max(0, min(n, NPROPS))):
            x, z = struct.unpack_from("<f", props, 0x3C * k)[0], struct.unpack_from("<f", props, 0x3C * k + 8)[0]
            near = min(near, math.hypot(px - x, pz - z))
        table = g.flat(OBJS, 0x1C * NOBJ)
        objs = b""
        for k in range(NPROPS + 60):
            at = 0x3C * k
            idx = struct.unpack_from("<i", props, at + 0x28)[0]
            ptr = struct.unpack_from("<I", table, 0x1C * idx)[0] if 0 <= idx < NOBJ and (k < n or k >= NPROPS) else 0
            objs += (g.ptr(ptr, OBJ) if ptr else None) or bytes(OBJ)
        head = g.flat(0x4CFD6, 4) + g.flat(0x133350, 4) + g.flat(0x133354, 4) + g.flat(0x11CDB4, 4) + g.flat(0x126D4C, 4)
        return head + cars + props + (g.ptr(cam, CAM) or bytes(CAM)) + objs, near

    def run(self):
        g = self.g
        last, cur = None, None
        with open(self.path, "wb") as f:
            while not self.stop_now:
                c0 = f32(g.flat(0x13335C, 4))
                body, near = self.look()
                sample = (c0, body)
                if f32(g.flat(0x13335C, 4)) != c0:
                    continue
                self.clock = c0
                self.near = near
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
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 50.0
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
    print("detail %d (2 = HIGH), cars %d, mode %d, obstacles %d" % (mem.word(0x130C54), mem.word(0x130C48), mem.word(0x130D10), mem.word(0x126D4C)))
    s = Sampler(mem, os.path.join(out, "props_t%d.bin" % track))
    s.start()
    log, shots, last_shot = [], 0, -1e9
    pokes, next_poke, drops = [], DROP_FROM, 0
    while s.is_alive() and s.clock <= seconds:
        time.sleep(0.01)
        if s.clock >= next_poke:                        # the harness's own doing, in the game's private copy: an obstacle that stands is put on the way of a car (the cars of the arcade's
            next_poke += DROP_EVERY                     # race pass the obstacles of the tracks without a touch, so that nothing would fly): the check treats a standing obstacle that moved as such
            car = drops % 4
            ahead = 1500.0 + 500.0 * ((drops // 4) % 3)
            side = (0.0, 250.0, -250.0, 600.0)[(drops // 2) % 4]
            drops += 1
            r = g.flat(CARS + REC * car, REC)
            x, h, z = struct.unpack_from("<iii", r, 0x4D8)[0], struct.unpack_from("<i", r, 0x4DC)[0], struct.unpack_from("<i", r, 0x4E0)[0]
            way = struct.unpack_from("<f", r, 0x508)[0]
            if struct.unpack_from("<f", r, 0x500)[0] > 20.0:
                n = mem.word(0x126D4C)
                for k in range(n):                          # the next obstacle that stands
                    a = PROPS + 0x3C * ((drops * 7 + k) % n)
                    rec = g.flat(a, 0x3C)
                    if struct.unpack_from("<i", rec, 0x34)[0] == 0:
                        px, pz = x + math.sin(way) * ahead + math.cos(way) * side, z - math.cos(way) * ahead + math.sin(way) * side
                        g.poke(a, struct.pack("<ffffff", px, float(h), pz, math.pi, way, math.pi) + bytes(12))
                        g.poke(a + 0x30, struct.pack("<iii", 1, 0, 0))
                        pokes.append("%.4f car %d obstacle %d to %.0f %d %.0f (way %.3f, ahead %.0f, side %.0f)" % (s.clock, car, (drops * 7 + k) % n, px, h, pz, way, ahead, side))
                        break
        if shots < SHOTS and s.near < 9000.0 and s.clock - last_shot >= SHOT_GAP:
            last_shot = s.clock
            shots += 1
            ours(p, pid)
            before = s.clock
            for sc, up in ((0x1D, 0), (0x3F, 0), (0x3F, 1), (0x1D, 1)):
                da.key(sc, up)
                time.sleep(0.05)
            after = s.clock
            time.sleep(0.5)
            pngs = sorted((os.path.join(out, x) for x in os.listdir(out) if x.lower().endswith(".png") and not x.startswith("props_")), key=os.path.getmtime)
            if pngs:
                new = os.path.join(out, "props_t%d_%05.2f.png" % (track, last_shot))
                os.replace(pngs[-1], new)
                log.append("%s %.4f %.4f" % (os.path.basename(new), before, after))
                print("screenshot", log[-1])
    s.stop_now = True
    s.join(5)
    open(os.path.join(out, "props_t%d.shots.txt" % track), "w").write("\n".join(log) + "\n")
    open(os.path.join(out, "props_t%d.pokes.txt" % track), "w").write("\n".join(pokes) + "\n")
    print("%d obstacles put on the cars' ways" % len(pokes))
    print("%d frames written (%d bytes each), race clock to %.2f s" % (s.frames, FRAME, s.clock))
    p.kill()


if __name__ == "__main__":
    main()
