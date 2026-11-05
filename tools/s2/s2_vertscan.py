"""Screamer 2: which blocks of the game's 3D objects are over the geometry unit's limits (512 vertices of a block, 1,024 faces of a row; firmware/geom.h).
The unit drops a block over a limit without a word (geom.h: "more: the piece is left out"), so this counts them in every container of the game's folder.

Usage: python -I tools/s2/s2_vertscan.py <Screamer 2 folder>
Reads GFX/*.DAT, ANIMS/*.DAT and DATA/*.DAT (nothing is written) and, of every .3DX (one object) and .UNR (a list of objects) in them, every block of every level of detail
(object +0x18: entries 0 .. 38, block b at level L is entry b + 13 L; 0 = none; block +0x10 vertices, +0x14 faces, +0x28 the 16 rows of 16-bit face offsets ended by 0xFFFF).
Prints per container the most vertices of a block and the longest row, then every block over a limit, then the same maxima per kind of file. Exit code 1 if a block is over a limit.
"""
import glob
import os
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile

MAX_VERT, MAX_FACES = 512, 1024
i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]


def blocks(x, ob):
    """(level, block number, vertices, longest row) of every block of the object at x[ob:]; None if it does not look like an object"""
    if ob < 0 or ob + 0x18 + 4 * 44 > len(x):
        return None
    nb = i32(x, ob + 0x14)
    if not 1 <= nb <= 13:
        return None
    out = []
    for e in range(39):                         # the 3 levels of 13 blocks; entries 39 .. 43 are not blocks
        off = i32(x, ob + 0x18 + 4 * e)
        if off <= 0:
            continue
        blk = ob + off
        if blk + 0x68 > len(x):
            return None
        nv = i32(x, blk + 0x10)
        if not 0 <= nv <= 4096:
            return None
        longest = 0
        for r in range(16):
            ro = i32(x, blk + 0x28 + 4 * r)
            if ro <= 0 or blk + ro + 2 > len(x):
                continue
            n, p = 0, blk + ro
            while p + 2 <= len(x) and u16(x, p) != 0xFFFF and n < 20000:
                n += 1
                p += 2
            longest = max(longest, n)
        out.append((e // 13, e % 13, nv, longest))
    return out


def kind_of(rel):
    r = rel.upper()
    if r.startswith("ANIMS/SCENE"):
        return "arcade scene (ANIMS/SCENE*)"
    if "/MENU" in r:
        return "options rooms (GFX/MENU*)"
    if "/MAPS" in r:
        return "track maps (GFX/MAPS*)"
    if "/MAP" in r:
        return "race tracks (GFX/MAPn*)"
    if "CARS" in r or "SCCAR" in r:
        return "cars (GFX/CARS*, ANIMS/SCCAR*)"
    return "other (" + rel + ")"


def objects_of(name, x):
    if name.upper().endswith(".3DX"):
        return [0]
    count = i32(x, 4)
    if not 0 < count < 5000:
        return []
    return [8 + 4 * count + i32(x, 8 + 4 * k) for k in range(count)]


def main():
    game = sys.argv[1]
    files = sorted(glob.glob(os.path.join(game, "GFX", "*.DAT")) + glob.glob(os.path.join(game, "ANIMS", "*.DAT")) + glob.glob(os.path.join(game, "DATA", "*.DAT")))
    over, kinds, total = [], defaultdict(lambda: [0, 0, 0, 0]), [0, 0, 0]
    print(f"{'container':<22} {'objects':>8} {'blocks':>7} {'most vertices':>14} {'longest row':>12}")
    for path in files:
        try:
            g = datfile.read(path)
            ents = datfile.entries(g)
        except Exception:
            continue
        rel = os.path.relpath(path, game).replace("\\", "/")
        objs = blks = mv = ml = 0
        for e in ents:
            up = e.name.upper()
            if not (up.endswith(".3DX") or up.endswith(".UNR")):
                continue
            try:
                x = datfile.extract(g, e.name)
                offs = objects_of(e.name, x)
            except Exception:
                continue
            for k, ob in enumerate(offs):
                bl = blocks(x, ob)
                if bl is None:
                    continue
                objs += 1
                for lvl, b, nv, longest in bl:
                    blks += 1
                    mv, ml = max(mv, nv), max(ml, longest)
                    kk = kinds[kind_of(rel)]
                    kk[0] += 1
                    kk[1], kk[2] = max(kk[1], nv), max(kk[2], longest)
                    if nv > MAX_VERT or longest > MAX_FACES:
                        over.append((rel, e.name, k, lvl, b, nv, longest))
        total[0] += objs
        total[1] += blks
        if objs:
            print(f"{rel:<22} {objs:>8} {blks:>7} {mv:>14} {ml:>12}")
    print(f"\n{total[0]} objects, {total[1]} blocks in all (levels of detail counted)")
    print("\nmost vertices of a block / longest row by kind of file:")
    for k, (n, mv, ml, _) in sorted(kinds.items()):
        print(f"  {k:<32} {n:>6} blocks, most vertices {mv}, longest row {ml}")
    print(f"\nblocks over a limit ({MAX_VERT} vertices or {MAX_FACES} faces in a row): {len(over)}")
    for rel, name, k, lvl, b, nv, longest in over:
        print(f"  {rel} {name} object {k} level {lvl} block {b}: {nv} vertices, longest row {longest}")
    return 1 if over else 0


if __name__ == "__main__":
    sys.exit(main())
