"""The firmware's obstacles and sparks against the original's screenshots (docs/development.md, "Obstacles and sparks").

Usage: python -I tools/s2/props_pics.py <track> [out folder = build/s2/props_pics]            (after python -I tools/s2/dosbox_props_s2.py <track> and tools/s2/check_props_s2.sh rec <track>)

The screenshots are tools/s2/dosbox_props_s2.py's (raw 320 x 200, the game's own palette in the file), each with the race clocks just before and after the key (props_t<track>.shots.txt). For
each the firmware's engine draws the recorded frames round those clocks from the recorded camera, cars, obstacles and sparks (tools/s2/check_props_s2.sh render: <prefix>with_<n>.rgb, and
nofx_<n>.rgb without the props). Which recorded frame the screenshot is of is not known to the frame (DOSBox takes the next one after the key): the frame whose picture (with the props) is nearest
to the screenshot in the part of the scene round the cars (the race screen, which the firmware's picture here does not have, lies outside it) is taken. The textures and lights are not the
original's pixel for pixel (a quarter of the pixels agree), so the props are compared by what they do to an area: in the pixels the firmware's props change (its picture with the props
differs from the one without), how many of the original's pixels are nearer (the sum of the palette's red, green and blue, 6-bit values) the firmware's picture with the props than the one without, and
how many are within 6 of 189 of it; and the same in the pixels of a bounding box a few pixels wider than theirs (the props' own edges). A prop the original does not draw there, or draws elsewhere,
shows as the original being nearer the picture without it. Side by side pictures (the original's, the firmware's with the props, the firmware's without) go to <out folder>/shot_<clock>.png.
"""
import os
import struct
import subprocess
import sys
import zlib

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from smoke_pics import read_png, read_rgb, region      # (the screenshot reader and the part of the picture round the cars)

root = os.path.normpath(os.path.join(here, "..", ".."))
FB = 24 + 4 * 3264 + 60 * 160 + 2796 + 64 * 160


def clocks(path):
    d = open(path, "rb")
    out = []
    while True:
        h = d.read(4)
        if len(h) < 4:
            return out
        out.append(struct.unpack("<f", h)[0])
        d.seek(FB - 4, 1)


def png(path, w, h, rows):
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in rows)
    def chunk(k, b):
        return struct.pack(">I", len(b)) + k + b + struct.pack(">I", zlib.crc32(k + b) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    track = int(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "build", "s2", "props_pics")
    os.makedirs(out, exist_ok=True)
    rec = os.path.join(root, "build", "check", "dosbox_props")
    ck = clocks(os.path.join(rec, "props_t%d.bin" % track))
    shots = [l.split() for l in open(os.path.join(rec, "props_t%d.shots.txt" % track)) if l.strip()]
    ranges = []
    for name, before, after in shots:
        frames = [i for i, c in enumerate(ck) if float(before) - 0.04 <= c <= float(after) + 0.14]
        ranges.append((name, frames))
    args = ["%d-%d" % (f[0], f[-1]) for _, f in ranges]
    subprocess.run(["sh", os.path.join(here, "check_props_s2.sh"), "render", str(track)] + args, check=True, capture_output=True)
    tot = [0, 0, 0, 0, 0, 0]
    for name, frames in ranges:
        orig = read_png(os.path.join(rec, name))
        best = None
        for n in frames:
            with_ = read_rgb(os.path.join(root, "build", "s2", "pp_with_%d.rgb" % n))
            diff = sum(1 for y in range(200) for x in range(320) if region(y, x) and with_[y][x] != orig[y][x])
            if best is None or diff < best[0]:
                best = (diff, n)
        n = best[1]
        nofx, with_ = read_rgb(os.path.join(root, "build", "s2", "pp_nofx_%d.rgb" % n)), read_rgb(os.path.join(root, "build", "s2", "pp_with_%d.rgb" % n))
        def dist(a, b):
            return sum(abs(a[k] - b[k]) for k in range(3))
        pix = [(y, x) for y in range(200) for x in range(320) if region(y, x) and with_[y][x] != nofx[y][x]]
        if pix:
            ys, xs = [p[0] for p in pix], [p[1] for p in pix]
            box = [(y, x) for y in range(max(0, min(ys) - 4), min(200, max(ys) + 5)) for x in range(max(0, min(xs) - 4), min(320, max(xs) + 5)) if region(y, x)]
        else:
            box = []
        near = sum(1 for y, x in pix if dist(orig[y][x], with_[y][x]) < dist(orig[y][x], nofx[y][x]))
        close = sum(1 for y, x in pix if dist(orig[y][x], with_[y][x]) <= 6)
        bnear = sum(1 for y, x in box if dist(orig[y][x], with_[y][x]) < dist(orig[y][x], nofx[y][x]) or with_[y][x] == nofx[y][x])
        print("%s: frame %d (%d pixels of the scene differ from the original): the props change %d pixels, the original is nearer the picture with them in %d, within 6 in %d; "
              "in their box (%d pixels) %d are nearer the picture with the props or equal in both" % (name, n, best[0], len(pix), near, close, len(box), bnear))
        tot = [tot[0] + 1, tot[1] + len(pix), tot[2] + near, tot[3] + close, tot[4] + len(box), tot[5] + bnear]
        rows = [list(orig[y]) + list(with_[y]) + list(nofx[y]) for y in range(200)]
        png(os.path.join(out, "shot_%s.png" % name[len("props_t%d_" % track):-4]), 960, 200, [[tuple(min(255, v * 4) for v in px) for px in row] for row in rows])
    print("%d screenshots: the props change %d pixels, the original is nearer the picture with them in %d (%.0f %%), within 6 of 189 in %d (%.0f %%); in their boxes (%d pixels) %.0f %%" % (
        tot[0], tot[1], tot[2], 100.0 * tot[2] / max(1, tot[1]), tot[3], 100.0 * tot[3] / max(1, tot[1]), tot[4], 100.0 * tot[5] / max(1, tot[4])))


if __name__ == "__main__":
    main()
