"""Screamer 2 experiment: the .3DX objects of a container (a car, a prop) against our limits.
Usage: python tools/s2/s2_scan3dx.py <container.DAT> <name.3DX>... (names: "AUTO1" etc.; matching MLB beside them)
Prints per object: blocks, frames, vertices, faces, flags, row pointers, the nonzero entries of the
block table, material routines used."""
import os
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile

i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
i16 = lambda b, o: struct.unpack_from("<h", b, o)[0]


def main():
    g = datfile.read(sys.argv[1])
    for name in sys.argv[2:]:
        x = datfile.extract(g, name + ".3DX")
        mlb = None
        try:
            mlb = datfile.extract(g, name + ".MLB")
        except KeyError:
            pass
        nb, fr = i32(x, 0x14), i32(x, 0x10)
        table = [(k, i32(x, 0x18 + 4 * k)) for k in range(44) if i32(x, 0x18 + 4 * k)]
        print(f"{name}: {len(x)} bytes, {nb} blocks, frames {fr}, block table entries used {len(table)} of 44: "
              f"{[k for k, _ in table]}")
        routine, flags, mats = Counter(), Counter(), set()
        maxv = maxf = 0
        for k, bo in table:
            if k >= nb:
                continue                      # further sets (levels of detail?): not read
            blk = bo
            nv, nf, fl = i32(x, blk + 0x10), i32(x, blk + 0x14), i32(x, blk + 0x18)
            flags[hex(fl)] += 1
            maxv, maxf = max(maxv, nv), max(maxf, nf)
            ro = i32(x, blk + 0x28)
            if ro <= 0:
                continue
            n = 0
            while u16(x, blk + ro + 2 * n) != 0xFFFF:
                fc = blk + u16(x, blk + ro + 2 * n)
                mats.add(u16(x, fc + 4))
                n += 1
        if mlb:
            for m in mats:
                routine[i32(mlb, m)] += 1
        print(f"   max vertices {maxv}, max faces {maxf}, flags {dict(flags)}, materials used {len(mats)}, routines {dict(routine)}"
              f", names {[x[bo + 0x68:bo + 0x78].split(b'\0')[0].decode('latin-1') for k, bo in table if k < nb][:14]}")


main()
