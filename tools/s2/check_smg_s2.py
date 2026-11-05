"""Cases for tools/s2/check_smg_s2.c, made with tools/s2/smg_ref.py (the independent reading of the rules: smg_ref.py and firmware/smg.c are two writings of
the same rules, and the image check compares what both build byte for byte; this checks the parts alone, on cases the game's files do not have).

Usage: python -I tools/s2/check_smg_s2.py <game folder> <out file>

The file (little endian): u32 count, then per case u32 kind and
  kind 1, the table's rows:  u8 col[768], i32 asmoke[3], a2smoke[3], u32 n, u8 texel[n], then n rows of 256 (the original's row of each texel: smg_ref.original_row)
  kind 2, the textures' copies:  u32 ramps, nfront, ntrack, size, u8 front[16 nfront], u8 mlb[size], then the result: u32 newsize, u8 front'[16 nfront], u8 mlb'[newsize],
                                 u32 rows, u8 alive[rows], u8 lut[256], u32 found
Kind 1 cases: the seven tracks' palettes and TRACK_DATA colours with every texel 1 .. 111, and random palettes (duplicates, a black filler, saturated colours, ramps
of greys) with random colours, which the C's search outwards from the colour's red value has to answer as the plain search over all 256 does. Kind 2 cases: made-up
libraries with every sort of material (routines 16, 64, 128, 208, 224), textures shared between materials, frames side by side in one sheet, frames that overlap,
a texture over the end of the file (left alone), junk texels, from a few texels to more than 62 in one ramp and across ramps (the merging).
"""
import os
import random
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, ".."))
sys.path.insert(0, here)
import datfile
import smg_ref


def track_colours(game, n):
    g = datfile.read(os.path.join(game, "GFX", f"MAP{n}H01.DAT"))
    col = datfile.extract(g, f"MAP{n}.COL")
    d = datfile.read(os.path.join(game, "DATA", f"MAP{n}.DAT"))
    t = datfile.extract(d, f"MAP{n}DAT.DTR").decode("latin-1")
    nums = []
    for line in t[t.index("TRACK_DATA"):].splitlines()[1:]:
        s = line.strip()
        if not s or s.startswith("*"):
            continue
        if s.startswith("END_LIST"):
            break
        nums += [int(v[1:]) for v in s.split() if v[0] == "d"]
        if len(nums) >= 11:
            break
    return bytes(col[:768]), nums[5:8], nums[8:11]


def table_case(col, asmoke, a2smoke, texels):
    pal6 = [tuple(col[3 * i:3 * i + 3]) for i in range(256)]
    out = struct.pack("<I", 1) + col + struct.pack("<6i", *asmoke, *a2smoke) + struct.pack("<I", len(texels)) + bytes(texels)
    for t in texels:
        out += smg_ref.original_row(pal6, t, asmoke, a2smoke)
    return out


def random_palette(rng, kind):
    if kind == 0:
        return bytes(rng.randrange(64) for _ in range(768))
    if kind == 1:                                   # a few colours over and over, the rest black
        base = [tuple(rng.randrange(64) for _ in range(3)) for _ in range(rng.randrange(2, 12))]
        return bytes(v for i in range(256) for v in (base[i % len(base)] if i < 200 else (0, 0, 0)))
    if kind == 2:                                   # greys and a ramp of one colour
        return bytes(v for i in range(256) for v in ((i // 4,) * 3 if i < 128 else (min(63, i // 3), i // 8, i // 16)))
    return bytes(rng.choice((0, 63, rng.randrange(64))) for _ in range(768))


def put(m, o, routine, ro, w, h):
    struct.pack_into("<4i", m, 16 * o, routine, ro, w, h)


def copies_case(rng, ramps):
    """a library: nrec records first (the track's own, of which some are not see-through), then texture records and frames (rows of 256, frames side by side in a
    sheet where they fit), the texels from a pool; nfront more records (the libraries' in front of the MLB) that point at the same texture records"""
    nrec = rng.randrange(4, 24)
    size = 256 * rng.randrange(300, 600)
    m = bytearray(size)
    texel_pool = rng.choice([[rng.randrange(1, 40) for _ in range(rng.randrange(3, 12))],
                             [rng.randrange(1, 112) for _ in range(rng.randrange(20, 90))],
                             [rng.randrange(1, 256) for _ in range(rng.randrange(40, 120))],
                             list(range(1, 112))])
    at = 16 * nrec + 256
    recs = []
    for k in range(nrec):
        routine = rng.choice((16, 64, 128, 208, 208, 208, 224))
        w, h = rng.randrange(4, 100), rng.randrange(4, 24)
        nfr = rng.choice((1, 1, 2, 3, 6))
        ro = at
        at += (8 + 4 * nfr + 3) & ~3
        offs = [at + (82 * f if 82 * f + w <= 256 else 256 * h * f) for f in range(nfr)]
        if rng.random() < 0.1:
            offs[-1] = size - 100                       # a frame over the end of the file: the material is left alone
        top = max(offs) + 256 * h
        if top + 4096 > size:
            break
        for o in offs:
            for y in range(h):
                if o + 256 * y + w <= size:
                    m[o + 256 * y:o + 256 * y + w] = bytes(rng.choice(texel_pool + [0, 0]) for _ in range(w))
        at = top + 64
        struct.pack_into("<2i", m, ro, nfr if rng.random() < 0.8 else -nfr, rng.randrange(256))        # (a negative count: the frames are its low half)
        for f, o in enumerate(offs):
            struct.pack_into("<i", m, ro + 8 + 4 * f, o)
        recs.append((routine, ro, w, h))
    if not recs:
        recs = [(208, 16 * nrec, 8, 8)]
    for k in range(nrec):
        put(m, k, *(recs[k] if k < len(recs) else (16, 0, 8, 8)))
    for k in range(min(nrec, len(recs))):               # materials may share a texture record
        if rng.random() < 0.25 and k:
            put(m, k, rng.choice((208, 224)), recs[rng.randrange(len(recs))][1], rng.randrange(4, 100), rng.randrange(4, 24))
    nfront = rng.randrange(0, 5)
    front = bytearray(16 * nfront)
    for k in range(nfront):
        r = rng.choice(recs)
        put(front, k, rng.choice((16, 208, 208, 224)), r[1], r[2], r[3])
    f2, m2, rowmap, alive, found = smg_ref.copies(bytes(front), nfront, bytes(m), nrec, ramps)
    lut = bytes(rowmap.get(t, 0) for t in range(256))
    out = struct.pack("<5I", 2, ramps, nfront, nrec, size) + bytes(front) + bytes(m)
    out += struct.pack("<I", len(m2)) + f2 + bytes(m2) + struct.pack("<I", len(alive)) + bytes(alive).ljust(62, bytes(1)) + lut + struct.pack("<I", len(found))
    return out


def main():
    game, out = sys.argv[1], sys.argv[2]
    rng = random.Random(20261105)
    cases = []
    for n in range(1, 8):
        col, ast, a2 = track_colours(game, n)
        for lo, hi in ((1, 63), (63, 112)):
            cases.append(table_case(col, ast, a2, list(range(lo, hi))))
    for i in range(24):
        col = random_palette(rng, i % 4)
        ast = [rng.randrange(-1, 256) for _ in range(3)] if i % 5 else [255, 255, 255]
        a2 = [rng.randrange(0, 256) for _ in range(3)]
        cases.append(table_case(col, ast, a2, sorted(rng.sample(range(1, 112), 40))))
    for i in range(120):
        cases.append(copies_case(rng, rng.choice((smg_ref.SHADOW, smg_ref.SMOKE | smg_ref.SHADOW | smg_ref.SMOKE2 | smg_ref.BRIGHT | smg_ref.WHITE,
                                                  smg_ref.SMOKE | smg_ref.SHADOW | smg_ref.SMOKE2))))
    with open(out, "wb") as f:
        f.write(struct.pack("<I", len(cases)) + b"".join(cases))
    print(f"{out}: {len(cases)} cases")


main()
