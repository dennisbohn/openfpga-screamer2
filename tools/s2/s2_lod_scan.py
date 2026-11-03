"""Screamer 2's levels of detail in the data (docs/screamer2/performance.md, "Detail levels and LOD at HIGH").

Usage: python -I tools/s2/s2_lod_scan.py <Screamer 2 folder>
Prints, from the game's own files (nothing is written):
  - per track, the H and L sets (GFX/MAPn{H,L}01.DAT): placement records of the map (run 0 = the road's
    pieces, run 1 = the scenery), meshes in the .UNR
  - per track (H set): the faces and vertices of the blocks of every placement at level 0, 1 and 2,
    from the block table in the object header (+0x18, 44 entries: block b at level L = entry b + 13 L,
    0 = no block), and how many meshes have all levels
  - per car file (GFX/CARS101 .. 411 .DAT, AUTO1..5): the same for the 12 blocks of a car
"""
import os
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile

i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]


def objects(unr):
    count = i32(unr, 4)
    return [8 + 4 * count + i32(unr, 8 + 4 * k) for k in range(count)]


def placements(mmp):
    """records per object index, and the two runs' record counts, over all cells of the map"""
    per, runs = Counter(), [0, 0]
    for gy in range(256):
        row = i32(mmp, 4 * gy)
        if row == -1:
            continue
        for gx in range(256):
            c = i32(mmp, row + 4 * gx)
            if c == -1:
                continue
            a, b = i32(mmp, c + 0x14), i32(mmp, c + 0x18)
            runs[0] += a
            runs[1] += b
            for k in range(a + b):
                per[i32(mmp, c + 0x28 + 64 * k)] += 1
    return per, runs


def levels(x, ob, weight=1):
    """faces, vertices, blocks of the object at x[ob:] at level 0, 1, 2 (times weight)"""
    nb = i32(x, ob + 0x14)
    out = []
    for lvl in range(3):
        f = v = n = 0
        for b in range(nb):
            off = i32(x, ob + 0x18 + 4 * (b + 13 * lvl))
            if off > 0:
                f += i32(x, ob + off + 0x14)
                v += i32(x, ob + off + 0x10)
                n += 1
        out.append((f * weight, v * weight, n * weight))
    return out


def main():
    game = sys.argv[1]
    print("track | H: records run0+run1, meshes | L: the same | levels 0/1/2 of the H set weighted by placements: "
          "faces, vertices, meshes with all levels")
    for n in range(1, 8):
        info = []
        for s in ("H01", "L01"):
            g = datfile.read(os.path.join(game, "GFX", f"MAP{n}{s}.DAT"))
            unr, mmp = datfile.extract(g, f"MAP{n}.UNR"), datfile.extract(g, f"MAP{n}.MMP")
            per, runs = placements(mmp)
            info.append((runs, i32(unr, 4)))
            if s == "H01":
                obs = objects(unr)
                tot = [[0, 0, 0] for _ in range(3)]
                full, nh = 0, len(per)
                for k, w in per.items():
                    lv = levels(unr, obs[k], w)
                    full += all(l[2] == lv[0][2] for l in lv)
                    for i in range(3):
                        for j in range(3):
                            tot[i][j] += lv[i][j]
                f = [t[0] for t in tot]
                v = [t[1] for t in tot]
        (rh, mh), (rl, ml) = info
        print(f"{n} | {rh[0]}+{rh[1]}, {mh} | {rl[0]}+{rl[1]}, {ml} | faces {f} = 100 : {100 * f[1] / f[0]:.0f} : {100 * f[2] / f[0]:.0f}, "
              f"vertices {v} = 100 : {100 * v[1] / v[0]:.0f} : {100 * v[2] / v[0]:.0f}, {full} of {nh}")
    print("cars | faces, vertices, blocks at level 0 / 1 / 2")
    for team in range(1, 5):
        g = datfile.read(os.path.join(game, "GFX", f"CARS{team}01.DAT"))
        for a in range(1, 6):
            x = datfile.extract(g, f"AUTO{a}.3DX")
            print(f"CARS{team}01 AUTO{a} | " + " / ".join(f"{f}, {v}, {n}" for f, v, n in levels(x, 0)))


if __name__ == "__main__":
    main()
