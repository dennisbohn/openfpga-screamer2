"""The recording of tools/s2/dosbox_scene.py (build/check/dosbox_scene/frames.jsonl: every change of the ARCADE title's camera record and four cars' object records, the screenshots
among them) as the lines tools/host/scene_match.c reads:

    R <row> <time> <hundredths> <a0> <a1> <a2> <x> <y> <z>  then per car  <x> <y> <z> <a0> <a1> <a2>       one per change of the records (y is up)
    S <name of the screenshot without .png> <label> <the last row before the request for it>

and, for the screenshots, the same <name>.idx / <name>.pal as tools/pages3d_idx.py makes (written by tools/scene.sh). Usage: python -I tools/scene_shots.py <folder of a run> <output file>
"""
import glob
import json
import os
import struct
import sys


def main():
    folder, out = sys.argv[1], sys.argv[2]
    labels = open(os.path.join(folder, "labels.txt")).read().split()
    pngs = sorted(glob.glob(os.path.join(folder, "*.png")))
    assert len(labels) == len(pngs), "%d labels for %d screenshots" % (len(labels), len(pngs))
    lines, shots, row = [], [], -1
    for n, line in enumerate(open(os.path.join(folder, "frames.jsonl"))):
        r = json.loads(line)
        if "shot" in r:
            shots.append(row)
        elif "cam" in r:
            row += 1
            cam = struct.unpack("<6i", bytes.fromhex(r["cam"]))
            cars = []
            for c in r["cars"]:
                w = struct.unpack("<16i", bytes.fromhex(c))
                cars += [w[2], w[3], w[4], w[5], w[6], w[7]]       # the record: +8 x, +0xc y up, +0x10 z, +0x14 .. +0x1c the angles
            hund = struct.unpack("<I", bytes.fromhex(r["hund"]))[0]
            lines.append("R %d %.4f %d %s %s" % (row, r["t"], hund, " ".join(map(str, cam)), " ".join(map(str, cars))))
    assert len(shots) == len(pngs), "%d requests for %d screenshots" % (len(shots), len(pngs))
    for png, label, at in zip(pngs, labels, shots):
        lines.append("S %s %s %d" % (os.path.splitext(os.path.basename(png))[0], label, at))
    open(out, "w", newline="\n").write("\n".join(lines) + "\n")
    print("%d rows, %d screenshots" % (row + 1, len(shots)))


if __name__ == "__main__":
    main()
