"""The recording of tools/s2/dosbox_garage.py (build/check/dosbox_garage*/frames.jsonl: the running original's camera record, the pages' words and the cars' and shadows' object records
every time one of them changed, with the keys and the screenshots) as the lines tools/host/garage_match.c reads:

    R <t> <page> <state> <item> <chosen> <sway> <a0> <a1> <x> <y> <z> <s0 x y z> ... <s4 x y z> <fade> <dt> <anim> <setup> <clock> <phase>     one distinct state of the camera record
    S <t> <name> <png without .png>                                                                          a screenshot asked for at t

page 0 the CAR page, 1 the CAR CUSTOMIZATION page (told by the camera's place: the garage is at x 55,000, the cars' places near the origin); state the CAR page's (0 cars, 1 the list, 2 the
choice, 0x1324d4); item the CUSTOMIZE page's cursor, which the game keeps in a local: the right-arrow keys since the page began; chosen the car the camera goes round (the team's record's first
word, 0x128a9c); sway [0x1324a4] x 10^6; the five shadows' places as the game's records have them (x, y up, z); fade the page's fade level; dt the frame time x 10^6; anim the engine's animation clock (0x1347a0); setup the chosen set-up (0x13237c, 0 Standard, 1 .. 4 Custom); clock the page's hundredths (0x4cfd6, the blinking's); phase x 10^9 the swing's phase, which the game keeps in a local: the sway's growth since the page began, or since the car was changed (the phase
starts again at 0.125 there, 0.126 after the frame's step).
Usage: python -I tools/garage_rows.py <folder of a run> <output file>
"""
import glob
import json
import os
import struct
import sys


def main():
    folder, out = sys.argv[1], sys.argv[2]
    rows = [json.loads(l) for l in open(os.path.join(folder, "frames.jsonl"))]
    pngs = sorted(glob.glob(os.path.join(folder, "*.png")))
    shots = [(r["t"], r["shot"]) for r in rows if "shot" in r]
    assert len(shots) == len(pngs), "%d screenshots asked for, %d files" % (len(shots), len(pngs))
    keys = [(r["t"], r["key"]) for r in rows if "key" in r]
    refused = [r for r in rows if "refused" in r]
    custom_at = next((r["t"] for r in rows if "cam" in r and struct.unpack_from("<i", bytes.fromhex(r["cam"]), 12)[0] > 30000), None)
    lines, last = [], None
    base, prev_kind, prev_chosen = 0.0, None, None
    for r in rows:
        if "cam" not in r:
            continue
        a0, a1, a2, x, y, z = struct.unpack("<6i", bytes.fromhex(r["cam"]))
        page = struct.unpack("<IIfIfff", bytes.fromhex(r["page"]))
        w = [struct.unpack("<i", bytes.fromhex(v)[:4])[0] for v in r["words"]]
        chosen = w[9]
        objs = [bytes.fromhex(h) for h in r["objs"]]
        sh = []
        for b in objs[5:10]:
            sh += struct.unpack_from("<3i", b, 8) if len(b) >= 0x14 else (0, 0, 0)
        kind = 1 if x > 30000 else 0
        sway = page[4]
        if kind == 0 and (prev_kind != 0 or prev_kind is None):
            base = sway                                         # (the page begins: the swing's phase is 0 here, the sway's own growth counts from it)
        elif kind == 0 and prev_chosen is not None and chosen != prev_chosen:
            base = sway - 0.126                                  # (a car was changed: the phase is 0.125 for that frame, 0.126 after it)
        prev_kind, prev_chosen = kind, chosen
        phase = sway - base
        item = sum(1 for t, k in keys if k == "ke4d" and custom_at is not None and custom_at <= t <= r["t"] - 0.02) if kind else 0
        key = (kind, w[2], item, chosen, int(page[4] * 1e6), a0, a1, x, y, z, tuple(sh))
        if key == last:
            continue
        last = key
        dt = struct.unpack("<f", bytes.fromhex(r["words"][6])[:4])[0]
        lines.append("R %.4f %d %d %d %d %d %d %d %d %d %d %s %.3f %d %d %d %d %d" % (r["t"], kind, w[2], item, chosen, int(round(page[4] * 1e6)), a0, a1, x, y, z, " ".join(str(v) for v in sh),
                                                                               page[2], int(round(dt * 1e6)), w[8], w[0], w[7], int(round(phase * 1e9))))
    for (t, name), png in zip(shots, pngs):
        lines.append("S %.4f %s %s" % (t, name, os.path.splitext(os.path.basename(png))[0]))
    lines.sort(key=lambda l: float(l.split()[1]))
    open(out, "w", newline="\n").write("\n".join(lines) + "\n")
    print("%d rows, %d screenshots, %d keys refused (%s)" % (sum(1 for l in lines if l[0] == "R"), len(shots), len(refused), folder))


if __name__ == "__main__":
    main()
