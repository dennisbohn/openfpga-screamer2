"""Screamer 2 experiment (docs/screamer2/comparison.md): measure a track's files against the
limits of our renderer (firmware/track.c, geom.h, geom.v) and compare with Screamer 1's.

Usage: python tools/s2/s2_scan.py <GFX container> [...]     e.g. "C:/GOG Games/Screamer 2/GFX/MAP1H01.DAT"
Prints per container: the objects (UNR: count, blocks, vertices, faces, row pointers, flags),
the materials the faces use (MLB: routines, texture sizes, frames), the map (MMP: cells,
placements, drivable ones, walls, road points). The names (MAPn.UNR ...) are found by the
container's own name, as tools/make_track.py does.
"""
import os
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import datfile

i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
i16 = lambda b, o: struct.unpack_from("<h", b, o)[0]


def scan(path):
    g = datfile.read(path)
    base = os.path.basename(path)
    track = base[:4]                                  # MAPn
    ents = {e.name.upper(): e for e in datfile.entries(g)}
    get = lambda n: datfile.extract(g, n)
    out = {"file": base, "entries": len(ents)}
    unr, mlb, mmp = get(track + ".UNR"), get(track + ".MLB"), get(track + ".MMP")
    out["sizes"] = {k: ents[k].size for k in ents if k.split(".")[0] in (track, "DEPTH1", "DEPTH2", "MISC") or k.endswith((".WLB", ".SMG", ".BCK", ".COL"))}

    # --- UNR: size, count, offsets[count], then the objects
    count = i32(unr, 4)
    offs = [i32(unr, 8 + 4 * k) for k in range(count)]
    blob = 8 + 4 * count
    nb_hist, frames_hist, flags, rows_used, names, cls = Counter(), Counter(), Counter(), Counter(), Counter(), Counter()
    max_vert = max_faces = max_row = 0
    face_stride = Counter()
    first_hdr = Counter()
    obj_sizes = []
    mats = set()
    bad_face = 0
    nfaces_total = 0
    for k, o in enumerate(offs):
        ob = blob + o
        nb, fr = i32(unr, ob + 0x14), i32(unr, ob + 0x10)
        nb_hist[nb] += 1
        frames_hist[fr] += 1
        if ob + 0x330 <= len(unr):
            cls[i32(unr, ob + 0x32C)] += 1
        first_hdr[i32(unr, ob + 0x18)] += 1
        for b in range(min(nb, 44)):
            bo = i32(unr, ob + 0x18 + 4 * b)
            if bo <= 0:
                continue
            blk = ob + bo
            nv = i32(unr, blk + 0x10)
            fl = i32(unr, blk + 0x18)
            flags[hex(fl)] += 1
            names[unr[blk + 0x68:blk + 0x69].decode("latin-1")] += 1
            max_vert = max(max_vert, nv)
            offsets_all = set()
            for r in range(16):
                ro = i32(unr, blk + 0x28 + 4 * r)
                if ro <= 0:
                    continue
                rows_used[r] += 1
                n = 0
                while True:
                    e = u16(unr, blk + ro + 2 * n)
                    if e == 0xFFFF:
                        break
                    offsets_all.add(e)
                    n += 1
                    if n > 20000:
                        break
                max_row = max(max_row, n)
            so = sorted(offsets_all)
            for a, c in zip(so, so[1:]):
                face_stride[c - a] += 1
            for e in so:
                fc = blk + e
                nfaces_total += 1
                mats.add(u16(unr, fc + 4))
                if not 3 <= i16(unr, fc + 6) <= 4:
                    bad_face += 1
            max_faces = max(max_faces, i32(unr, blk + 0x14))
    out["unr"] = dict(objects=count, blocks=dict(sorted(nb_hist.items())), frames=dict(sorted(frames_hist.items())),
                      max_vertices=max_vert, max_faces_block=max_faces, max_row_entries=max_row,
                      rows_used=dict(sorted(rows_used.items())), flags=dict(flags), name_letter=dict(names.most_common(8)),
                      class_word_distinct=len(cls), face_stride=dict(face_stride.most_common(4)), faces=nfaces_total,
                      face_not_3_4=bad_face, first_block_offset=dict(first_hdr.most_common(3)), size=len(unr))

    # --- MLB: the materials the faces name (16-byte records at the start), texture records
    routine, tsz, frames = Counter(), Counter(), Counter()
    maxw = maxh = 0
    for m in sorted(mats):
        r = i32(mlb, m)
        routine[r] += 1
        if r in (128, 192):
            continue
        w, h = i32(mlb, m + 8), i32(mlb, m + 12)
        maxw, maxh = max(maxw, w), max(maxh, h)
        tsz[(w, h)] += 1
        tr = i32(mlb, m + 4)
        frames[i32(mlb, tr)] += 1
    out["mlb"] = dict(size=len(mlb), materials_used=len(mats), max_material_offset=max(mats), routines=dict(sorted(routine.items())),
                      max_w=maxw, max_h=maxh, texture_sizes=len(tsz), frames=dict(sorted(frames.items())),
                      big=[k for k in tsz if k[0] > 256 or k[1] > 256])

    # --- MMP: two-level grid, cells, 64-byte records
    cells = []
    for gy in range(256):
        row = i32(mmp, 4 * gy)
        if row == -1:
            continue
        for gx in range(256):
            c = i32(mmp, row + 4 * gx)
            if c != -1:
                cells.append(c)
    n0 = n1 = nd = nw = 0
    objmax = -1
    nres = 0
    for c in cells:
        a, b = i32(mmp, c + 0x14), i32(mmp, c + 0x18)
        n0 += a
        n1 += b
        nd += i32(mmp, c + 0x1C)
        nw += i32(mmp, c + 0x20)
        for k in range(a + b):
            ob = i32(mmp, c + 0x28 + 64 * k)
            objmax = max(objmax, ob)
    out["mmp"] = dict(size=len(mmp), cells=len(cells), records=n0 + n1, run0_drivable=n0, run1=n1, road_points=nd, walls=nw,
                      max_object_index=objmax)
    return out


def main():
    for p in sys.argv[1:]:
        r = scan(p)
        print(f"== {r['file']} ({r['entries']} entries)")
        print("  files:", {k: v for k, v in r["sizes"].items()})
        for k in ("unr", "mlb", "mmp"):
            print(f"  {k}:", r[k])


if __name__ == "__main__":
    main()
