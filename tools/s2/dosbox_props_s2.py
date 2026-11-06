"""The original's obstacles (cones, fences, chests, crates) and sparks in DOSBox (docs/development.md, "Obstacles and sparks"): what tools/s2/check_props_s2.sh holds the firmware's
code against.

Usage: python -I tools/s2/dosbox_props_s2.py <track 1..7> [seconds of race, default 45]      (Windows; $GAME_DIR, default "C:/GOG Games/Screamer 2")

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
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 45.0
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
    while s.is_alive() and s.clock <= seconds:
        time.sleep(0.01)
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
    print("%d frames written (%d bytes each), race clock to %.2f s" % (s.frames, FRAME, s.clock))
    p.kill()


if __name__ == "__main__":
    main()
