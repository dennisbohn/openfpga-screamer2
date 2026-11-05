"""The 3D pages' 2D parts (titles, names, plate, sprites, digits) against the running original: tools/pages3d.sh gives this the matcher's lines (which screenshot
is which page and state; only the ones whose objects have settled) and it compares, in the boxes the 2D parts fill, the screenshots with the firmware's frames that tools/menu_screens.sh drew
(build/check/menu/menu_team3d.png, menu_name3d.png, menu_car3d_0.png: team 1 CONDOR in front, the name MIL with the cursor on the first letter, car 2 HORIZON
chosen on team 1). The colours are compared as the VGA's 6 bits (DOSBox's screenshot expands them to 8).

Usage: python -I tools/pages3d_2d.py <matcher log> <folder of the screenshots (.png)> <folder with menu_*3d*.png>
"""
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pages3d_idx


def read_rgb(path):
    d = open(path, "rb").read()
    p, idat = 8, b""
    while p < len(d):
        n, kind = struct.unpack(">I4s", d[p:p + 8])
        if kind == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", d[p + 8:p + 18])
        elif kind == b"IDAT":
            idat += d[p + 8:p + 8 + n]
        p += 12 + n
    assert (w, h, bd, ct) == (320, 200, 8, 2), path
    raw, rows, prev, bpp = zlib.decompress(idat), [], bytearray(w * 3), 3
    for y in range(h):
        f, line = raw[y * (w * bpp + 1)], bytearray(raw[y * (w * bpp + 1) + 1:(y + 1) * (w * bpp + 1)])
        for i in range(w * bpp):
            a, b, c = line[i - bpp] if i >= bpp else 0, prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return rows


BOXES = {
    "team": {"title": (0, 0, 320, 32), "team name": (0, 170, 320, 200)},
    "name": {"title": (0, 0, 320, 32), "name and cursor": (100, 184, 220, 200)},
    "car": {"title": (28, 15, 213, 44), "badge": (226, 1, 290, 50), "plate": (18, 155, 213, 182), "maxspeed label": (166, 170, 212, 177),
            "speed digits": (213, 169, 232, 176), "unit": (231, 171, 251, 176), "drive": (251, 163, 263, 176)},
}


def main():
    log, shots, menu = sys.argv[1], sys.argv[2], sys.argv[3]
    mine = {"team": read_rgb(os.path.join(menu, "menu_team3d.png")), "name": read_rgb(os.path.join(menu, "menu_name3d.png")),
            "car": read_rgb(os.path.join(menu, "menu_car3d_0.png"))}
    totals = {}
    for line in open(log):
        m = re.match(r"(startl_\d+)\.idx: palette \w+\s+(team|name|car) page \((?:team 1|letter 12|car 1)\): \d+ of (\d+) object pixels differ \((\d+) at the outline", line)
        if not m:
            continue
        name, kind, covered, outline = m.group(1), m.group(2), int(m.group(3)), int(m.group(4))
        if outline * 20 > covered:                      # (a frame caught in a move: its objects are not where the settled page has them, and the text may already have changed)
            continue
        idx, pal = pages3d_idx.read_png(os.path.join(shots, name + ".png"))
        for what, (x0, y0, x1, y1) in BOXES[kind].items():
            diff = 0
            for y in range(y0, y1):
                for x in range(x0, x1):
                    c = pal[idx[y * 320 + x]]
                    if tuple(v >> 2 for v in mine[kind][y][3 * x:3 * x + 3]) != ((c >> 16 & 255) >> 2, (c >> 8 & 255) >> 2, (c & 255) >> 2):
                        diff += 1
            t = totals.setdefault((kind, what), [0, 0, (x1 - x0) * (y1 - y0)])
            t[0] += diff
            t[1] += 1
    for (kind, what), (diff, n, area) in totals.items():
        print(f"2D parts, {kind} page, {what}: {diff} pixels differ in {n} screenshots ({area} pixels each)")


if __name__ == "__main__":
    main()
