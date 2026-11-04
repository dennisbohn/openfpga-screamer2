"""Check the cars in a Screamer 2 track image (tools/s2/make_track_s2.py, firmware/track_build.c) against the cars'
own files. The image holds one car model per grid slot k, from GFX/CARS<team>{look}1.DAT: AUTO<car>.3DX and .MLB.

For each slot the model's mesh must be the car's .3DX byte for byte except the material word of its faces (face
+4), and for every face the image's material (a record in front of the MLB, a negative offset from its start) must
be the car's own: the same routine, size and texture record (frames and word), and, but for the windows (routine
160, the reflection: tools/s2/check_env_s2.py), the same pixels of every frame (rows of 256 bytes, w of them). The
palette: the car's own colours 0..119 must be the image's, and the car's textures must use only those (the
tracks own 120..255).

Usage: python -I tools/s2/check_cars_s2.py <Screamer 2 folder> <image.bin> <track 1..7> <grid, "1:1,2:2,3:3,4:4"> [look 0|1]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile

i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
i16 = lambda b, o: struct.unpack_from("<h", b, o)[0]


def faces(x):
    """The offsets of every face's material word (face +4) in a .3DX: every block of the header's table at the
    three levels of detail, every face of its 16 rows"""
    out = set()
    for lvl in range(3):
        for b in range(i32(x, 0x14)):
            blk = i32(x, 0x18 + 4 * (b + 13 * lvl))
            if blk <= 0 or blk + 0x68 > len(x):
                continue
            for r in range(16):
                ro = i32(x, blk + 0x28 + 4 * r)
                if ro <= 0:
                    continue
                for j in range(1024):
                    p = blk + ro + 2 * j
                    if p + 2 > len(x) or u16(x, p) == 0xFFFF:
                        break
                    if blk + u16(x, p) + 6 <= len(x):
                        out.add(blk + u16(x, p) + 4)
    return out


def frames(m, ro):
    n = i32(m, ro)
    return n & 0xFFFF if n > 0 else 1


def main():
    root, image, n, grid = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    look = sys.argv[5] if len(sys.argv) > 5 else "0"
    pick = [tuple(int(v) for v in c.split(":")) for c in grid.split(",")]
    img = open(image, "rb").read()
    mlb, models, nm, pal = i32(img, 0x08), i32(img, 0x44), i32(img, 0x48), i32(img, 0x18)
    assert nm == len(pick), f"{nm} car models in the image, {len(pick)} in the grid"
    teams = {t: datfile.read(os.path.join(root, "GFX", f"CARS{t}{look}1.DAT")) for t, _ in pick}
    nface = ntex = 0
    for k, (t, c) in enumerate(pick):
        x = datfile.extract(teams[t], f"AUTO{c}.3DX")
        cm = datfile.extract(teams[t], f"AUTO{c}.MLB")
        col = datfile.extract(teams[t], f"AUTO{c}.COL")
        mo, ms = u32(img, models + 24 * k + 16), u32(img, models + 24 * k + 20)
        mesh = img[models + mo:models + mo + ms]
        assert len(mesh) == len(x) == ms, f"slot {k}: the mesh is {ms} bytes, the car's .3DX {len(x)}"
        fa = faces(x)
        assert fa, f"slot {k}: no faces"
        skip = bytearray(len(x))
        for f in fa:
            skip[f] = skip[f + 1] = 1
        for i, (a, b) in enumerate(zip(mesh, x)):
            assert a == b or skip[i], f"slot {k}: mesh byte {i} differs outside the faces' materials"
        seen, top = set(), 0
        for f in sorted(fa):
            mi, mn = i16(mesh, f), i16(x, f)
            assert mn >= 0 and mn % 16 == 0 and mn + 16 <= len(cm), f"slot {k}: face material {mn}"
            assert mi < 0 and mlb + mi >= 0x100, f"slot {k}: image material offset {mi}"
            if mn in seen:
                continue
            seen.add(mn)
            ri, ro = img[mlb + mi:mlb + mi + 16], cm[mn:mn + 16]
            assert (u32(ri, 0), u32(ri, 8), u32(ri, 12)) == (u32(ro, 0), u32(ro, 8), u32(ro, 12)), f"slot {k}: material {mn}"
            nface += 1
            tn, ti = i32(ro, 4), i32(ri, 4)
            w, h = u32(ro, 8), u32(ro, 12)
            if not (tn > 0 and tn + 12 <= len(cm)):
                assert tn == ti, f"slot {k}: material {mn} without a texture record"
                continue
            if u32(ro, 0) == 160:                               # a window: the reflection's picture
                continue
            ti += mlb
            fr = frames(cm, tn)
            assert frames(img, ti) == fr and u32(img, ti + 4) == u32(cm, tn + 4), f"slot {k}: texture record of {mn}"
            assert w <= 256 and h <= 256, f"slot {k}: material {mn} is {w} x {h}"
            for q in range(fr):
                fo, fi = u32(cm, tn + 8 + 4 * q), u32(img, ti + 8 + 4 * q) + mlb
                for r in range(h):
                    a, b = img[fi + 256 * r:fi + 256 * r + w], cm[fo + 256 * r:fo + 256 * r + w]
                    assert a == b and len(a) == w, f"slot {k}: material {mn} frame {q} row {r}"
                    top = max(top, max(b))
                ntex += 1
        # the colours: the car's 0..119 are the image's, and its textures stay inside them
        for i in range(120):
            want = col[3 * i] * 4 << 16 | col[3 * i + 1] * 4 << 8 | col[3 * i + 2] * 4
            assert u32(img, pal + 4 * i) == want, f"slot {k}: colour {i} differs from the car's own"
        assert top < 120, f"slot {k}: the car's textures use colour {top}, which the tracks own"
    print(f"track {n}, grid {grid}: {len(pick)} cars, {nface} materials and {ntex} texture frames equal the cars' own, "
          f"colours 0..119 equal")


main()
