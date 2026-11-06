"""The ARCADE title's cars as the original's page 0x3b6e8 has them, written again from the program for tools/host/scene_screen.c (tools/menu_screens.sh) to check the firmware's
firmware/s2arcscene.c against: an independent reading in double precision (tools/s2/anim_ref.py's paths), the paths found by their names (the firmware's rule is the kind of path at
+0x60), the clock stored as a float after every step.

Usage: python -I tools/ui_scene_ref.py <game folder> <output folder>      writes scene_states.txt, one line per frame of four takes (a take is 201 frames):

    <frame> <take> <car 0: x, y up, z, heading> <car 1> <car 2> <car 3> <camera: x, y up, z, a0, a1>      (whole numbers: the places cut, the angles in 2048ths)

The page (STARTL.EXE 0x3b6e8, tools/dis.sh -f): take k's cars j are the paths "MV_car<j>" of DATA/GEN.DAT's SCENE<k>.PTH and the camera the first place of "Camera01", looking at the
first place of "Camera01.target"; the clock t goes 0.02 x 0.25 on each frame after drawing (the sum in the x87's double, stored as a float), past 1.0 it is 0 and the take the next of
four; a car is at its path's place at t cut to whole numbers, x and height and depth as the engine's record has them (the path's x, height and y), its heading the angle of the
path's way (anim_ref.angles), the camera's angles the look of 0x33905 (the angle round the vertical from acos(dx / sqrt(dx^2 + dz^2)), 2 pi less it for dz < 0, less a quarter turn; the
pitch acos(sqrt(dx^2 + dz^2) / |d|) or 2 pi less it, in the program's floats) x 1024 / pi, cut, & 0x7ff.
"""
import math
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
sys.path.insert(0, os.path.join(here, "s2"))
import anim_ref  # noqa: E402
import datfile  # noqa: E402

TAKE_FRAMES = 201
FRAMES = 4 * TAKE_FRAMES + 3
DT = struct.unpack("<f", struct.pack("<f", 0.02))[0]          # the frame time the page runs at: the float 0.02 (0x3ca3d70a)


def f32(v):
    return struct.unpack("<f", struct.pack("<f", v))[0]


def scene_paths(gen, k):
    """SCENE<k>.PTH: {name: Path} (the names are the files' own: MV_car0 .. MV_car3, Camera01, Camera01.target)"""
    raw = datfile.extract(gen, "SCENE%d.PTH" % k)
    count, = struct.unpack_from("<i", raw, 0)
    out, p = {}, 4
    for _ in range(count):
        head = raw[p:p + 100]
        p += 100
        name = head[8:0x28].split(b"\0")[0].decode()
        chans = []
        for _ in range(3):
            n, = struct.unpack_from("<i", raw, p + 0x1C)
            p += 0x30
            keys = []
            for _ in range(n):
                keys.append(struct.unpack_from("<4f", raw, p + 8))
                p += 0x2C
            chans.append(keys)
        path = anim_ref.Path(chans)
        path.start, path.end = struct.unpack_from("<2f", head, 0x28)
        out[name] = path
    return out


def look(pos, aim):
    """0x33905: (a0, a1) of the look from pos at aim, the engine's (x, y up, z); the program's floats where it keeps floats (the angle round the vertical and the pitch are stored as
    32-bit numbers, its constants 2 pi, a quarter turn and pi are 32-bit too: double precision alone gives 1024 for the camera's heading where the program has 1023)"""
    dx, dy, dz = (pos[i] - aim[i] for i in range(3))
    horiz2 = float(dx * dx + dz * dz)
    v = f32(math.acos(dx / math.sqrt(horiz2)))
    if dz < 0:
        v = f32(f32(6.2831855) - v)
    head = f32(v + f32(-1.5707964))
    horiz, total = f32(math.sqrt(horiz2)), f32(math.sqrt(float(dx * dx + dy * dy + dz * dz)))
    a = abs(math.acos(f32(horiz / total)))
    pitch = f32(a) if dy < 0 else f32(6.283185482025146 - a)
    return int(pitch * 1024.0 / 3.1415927410125732) & 0x7FF, int(head * 1024.0 / 3.1415927410125732) & 0x7FF


def main():
    game, out = sys.argv[1], sys.argv[2]
    gen = datfile.read(os.path.join(game, "DATA", "GEN.DAT"))
    takes = [scene_paths(gen, k) for k in range(4)]
    lines = []
    take, t = 0, 0.0
    for frame in range(FRAMES):
        paths = takes[take]
        cars = []
        for j in range(4):
            place, way = anim_ref.path_at(paths["MV_car%d" % j], t)
            heading, _ = anim_ref.angles(way)
            cars += [int(place[0]), int(place[2]), int(place[1]), heading]
        pos = [int(paths["Camera01"][c][0][1]) for c in (0, 2, 1)]       # (x, y up, z) of the first keys
        aim = [int(paths["Camera01.target"][c][0][1]) for c in (0, 2, 1)]
        a0, a1 = look(pos, aim)
        lines.append("%d %d %s %s %d %d" % (frame, take, " ".join(map(str, cars)), " ".join(map(str, pos)), a0, a1))
        t = f32(DT * 0.25 + t)
        if t > 1.0:
            t, take = 0.0, (take + 1) % 4
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "scene_states.txt"), "w", newline="\n").write("\n".join(lines) + "\n")
    print("%d frames of the title's cars written" % len(lines))


if __name__ == "__main__":
    main()
