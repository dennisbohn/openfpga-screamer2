"""Screamer 2 experiment: the collision tables of firmware/track.c (MAX_CFACES 8192, MAX_GROUND 4096,
MAX_PIECES 2560, MAX_OBJ 512) against a track: drivable pieces (record run 0), the distinct
objects they use, and those objects' faces in the first block (what track_init copies).
Usage: python tools/s2/s2_collide.py <GFX container>..."""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile

i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]

for path in sys.argv[1:]:
    g = datfile.read(path)
    tr = os.path.basename(path)[:4]
    unr, mmp = datfile.extract(g, tr + ".UNR"), datfile.extract(g, tr + ".MMP")
    count = i32(unr, 4)
    blob = 8 + 4 * count
    objs, n0 = set(), 0
    for gy in range(256):
        row = i32(mmp, 4 * gy)
        if row == -1:
            continue
        for gx in range(256):
            c = i32(mmp, row + 4 * gx)
            if c == -1:
                continue
            a = i32(mmp, c + 0x14)
            n0 += a
            for k in range(a):
                objs.add(i32(mmp, c + 0x28 + 64 * k))
    faces = 0
    for k in objs:
        ob = blob + i32(unr, 8 + 4 * k)
        blk = ob + i32(unr, ob + 0x18)
        ro = i32(unr, blk + 0x28)
        faces += sum(1 for j in range(i32(unr, blk + 0x14)) if u16(unr, blk + ro + 2 * j) != 0xFFFF)
    print(f"{os.path.basename(path)}: drivable pieces {n0} (limit 4096), distinct drivable objects {len(objs)}, their faces {faces} (limit 8192)")
