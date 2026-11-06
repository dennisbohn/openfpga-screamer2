"""The states of the running original round every screenshot of tools/s2/dosbox_desk.py (build/check/dosbox_desk, or a folder it was copied to): for each screenshot the distinct
states of the page's memory in the recording (frames.jsonl) in the window of 0.12 s before to 0.04 s after the moment it was asked for, as the lines tools/host/desk_match.c and
tools/host/plates_match.c read. The shot is the frame the page was showing then; a row of the recording is one frame (or a frame half written: such rows are told by the numbers not
fitting together and the matcher takes the best of the window).

    welcome:  <name> <label> <frame> then for the plates 0 .. 3: x y z a0 a1 a2 (the engine's object record) and 1 while the plate is not finished
    desk:     <name> <label> <frame> <scene> <camera x> <y up> <z> <a0> <a1> <textures' clock x 65535> <car x> <y up> <z> <a1> <wheels' phase as float bits>

A third and fourth form give every row of the recording (the numbers of the states above and the page's flags: the fade's flag 0x132498, its level, the done word) for the tests of the
rules, one line a row, a `#` line where the poke of a new scene starts:

    welcome-rows: <fade flag> <fade level bits> <hundredths> then x y z a0 a1 a2 and the plate's u (float bits) and its phase and finished (1) for the plates 0 .. 3
    desk-rows:    <fade flag> <fade level bits> <hundredths> <scene> <camera x> <y up> <z> <a0> <a1> <a2> <textures' clock x 65535> <car x> <y up> <z> <a0> <a1> <a2> <wheels' phase bits>
                  <league> <objects> <paths>

Usage: python -I tools/desk_shots.py welcome|desk|welcome-rows|desk-rows <folder of a run> <output file>
"""
import glob
import json
import os
import struct
import sys

BEFORE, AFTER = 0.12, 0.04


def rows_text(kind, folder, out):
    lines = []
    for l in open(os.path.join(folder, "frames.jsonl")):
        r = json.loads(l)
        if "poke" in r:
            lines.append("# %d" % r["poke"])
        if "cam" not in r:
            continue
        flags = struct.unpack("<7i", bytes.fromhex(r["flags"]))
        head = "%d %d %d" % (flags[1], flags[2], struct.unpack("<i", bytes.fromhex(r["hund"]))[0])
        if kind == "welcome-rows":
            b = bytes.fromhex(r["plates"])
            v = []
            for i in range(4):
                o = r["objs"][i]
                ob = struct.unpack("<16i", bytes.fromhex(o)) if o else [0] * 16
                u = struct.unpack_from("<i", b, 0x4C * i + 0x3C)[0]
                ph, fin = struct.unpack_from("<i", b, 0x4C * i + 0x48)[0], struct.unpack_from("<f", b, 0x4C * i + 0x40)[0]
                v += list(ob[2:8]) + [u, ph, 0 if fin < 0 else 1]
            lines.append("%s %s" % (head, " ".join(map(str, v))))
        else:
            cam = struct.unpack("<6i", bytes.fromhex(r["cam"]))
            vs = struct.unpack("<iiffif", bytes.fromhex(r["vars"]))
            if vs[0] < 1 or vs[4] < 1:
                continue                                    # (the scene is not loaded yet: the rows are the report page's)
            ob = struct.unpack("<16i", bytes.fromhex(r["objs"][0])) if r["objs"] and r["objs"][0] else [-2147483648] * 16      # (-2^31: a recording without the car's record)
            anim = struct.unpack("<i", bytes.fromhex(r["anim"]))[0]
            ph = struct.unpack("<i", struct.pack("<f", vs[5]))[0]
            lg = struct.unpack("<i", bytes.fromhex(r["league"]))[0]
            lines.append("%s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d" % (head, vs[4], cam[3], cam[4], cam[5], cam[0], cam[1], cam[2], anim, ob[2], ob[3], ob[4], ob[5], ob[6], ob[7], ph, lg, vs[0], vs[1], 0))
    open(out, "w", newline="\n").write("\n".join(lines) + "\n")
    print("%d rows" % len([x for x in lines if x[0] != "#"]))


def main():
    kind, folder, out = sys.argv[1], sys.argv[2], sys.argv[3]
    if kind.endswith("-rows"):
        return rows_text(kind, folder, out)
    rows = [json.loads(l) for l in open(os.path.join(folder, "frames.jsonl"))]
    pngs = sorted(glob.glob(os.path.join(folder, "*.png")))
    if os.path.exists(os.path.join(folder, "labels.txt")):
        labels = open(os.path.join(folder, "labels.txt")).read().split()
    else:                                       # (a run that ended early, as a DOSBox that stopped: the shots in the order they were asked for, the first screenshot is the selection's)
        labels = ["selection"] * (len(pngs) - len([r for r in rows if "shot" in r])) + [r["shot"] for r in rows if "shot" in r]
    assert len(labels) == len(pngs), "%d labels for %d screenshots" % (len(labels), len(pngs))
    queue = {}                                  # label -> the times the shots with it were asked for, in order (a recording of several scenes uses its labels again)
    seg = 0                                     # the recording's parts (one for each poke of the desk scene) have clocks of their own
    for r in rows:
        if "poke" in r:
            seg += 1
        r["seg"] = seg
        if "shot" in r:
            queue.setdefault(r["shot"], []).append(r)
    states = [r for r in rows if "cam" in r]
    lines = []
    for png, label in zip(pngs, labels):
        if label not in queue or not queue[label]:
            continue                            # (a screenshot the recording has no row for: the selection page)
        shot = queue[label].pop(0)
        t = shot["t"]
        base = os.path.splitext(os.path.basename(png))[0]
        seen = set()
        near = [r for r in states if r["seg"] == shot["seg"] and t - BEFORE <= r["t"] <= t + AFTER]
        for r in near:
            hund = struct.unpack("<i", bytes.fromhex(r["hund"]))[0]
            if kind == "welcome":
                b = bytes.fromhex(r["plates"])
                v = []
                for i in range(4):
                    o = r["objs"][i]
                    if not o:
                        v += [0] * 7
                        continue
                    ob = struct.unpack("<16i", bytes.fromhex(o))
                    fin = struct.unpack_from("<f", b, 0x4C * i + 0x40)[0]
                    v += list(ob[2:5]) + list(ob[5:8]) + [0 if fin < 0 else 1]
                key = tuple(v)
                if key in seen:
                    continue
                seen.add(key)
                lines.append("%s %s %d %s" % (base, label, hund, " ".join(map(str, v))))
            else:
                cam = struct.unpack("<6i", bytes.fromhex(r["cam"]))
                vars_ = struct.unpack("<iiffif", bytes.fromhex(r["vars"]))
                if vars_[0] < 1 or vars_[4] < 1:
                    continue
                ob = struct.unpack("<16i", bytes.fromhex(r["objs"][0])) if r["objs"] and r["objs"][0] else [-2147483648] * 16
                anim = struct.unpack("<i", bytes.fromhex(r["anim"]))[0]
                ph = struct.unpack("<i", struct.pack("<f", vars_[5]))[0]
                v = (vars_[4], cam[3], cam[4], cam[5], cam[0], cam[1], anim, ob[2], ob[3], ob[4], ob[6], ph)
                key = v
                if key in seen:
                    continue
                seen.add(key)
                lines.append("%s %s %d %s" % (base, label, hund, " ".join(map(str, v))))
    open(out, "w", newline="\n").write("\n".join(lines) + "\n")
    print("%d states for %d screenshots" % (len(lines), len({l.split()[0] for l in lines})))


if __name__ == "__main__":
    main()
