"""The raw DOSBox screenshots of tools/s2/dosbox_arcade.py --pages3d (build/check/dosbox3d/*.png, palette-indexed 320 x 200) as the 64,000 palette
indices of each frame (<name>.idx) and its palette (<name>.pal, 256 x 0x00RRGGBB little-endian), for tools/host/pages3d_match.c (tools/pages3d.sh).

Usage: python -I tools/pages3d_idx.py <folder of screenshots> <output folder>
"""
import glob
import os
import struct
import sys
import zlib


def read_png(path):
    """indices and palette of a palette-indexed 320 x 200 PNG"""
    d = open(path, "rb").read()
    p, idat, plte = 8, b"", None
    while p < len(d):
        n, kind = struct.unpack(">I4s", d[p:p + 8])
        if kind == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", d[p + 8:p + 18])
        elif kind == b"PLTE":
            plte = d[p + 8:p + 8 + n]
        elif kind == b"IDAT":
            idat += d[p + 8:p + 8 + n]
        p += 12 + n
    assert (w, h, bd, ct) == (320, 200, 8, 3), "not a raw 320 x 200 capture: " + path
    raw, rows, prev = zlib.decompress(idat), [], bytearray(w)
    for y in range(h):
        f, line = raw[y * (w + 1)], bytearray(raw[y * (w + 1) + 1:(y + 1) * (w + 1)])
        for i in range(w):
            a, b, c = line[i - 1] if i else 0, prev[i], prev[i - 1] if i else 0
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
    pal = [plte[3 * i] << 16 | plte[3 * i + 1] << 8 | plte[3 * i + 2] for i in range(256)]
    return b"".join(rows), pal


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    n = 0
    for f in sorted(glob.glob(os.path.join(src, "*.png"))):
        idx, pal = read_png(f)
        base = os.path.join(dst, os.path.splitext(os.path.basename(f))[0])
        open(base + ".idx", "wb").write(idx)
        open(base + ".pal", "wb").write(b"".join(struct.pack("<I", c) for c in pal))
        n += 1
    print(f"{n} frames")


if __name__ == "__main__":
    main()
