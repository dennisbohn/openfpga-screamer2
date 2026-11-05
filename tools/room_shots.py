"""The camera of every screenshot of tools/s2/dosbox_arcade.py --options (build/check/dosbox_room, dosbox_room_pages): for each screenshot the states of the running original's
camera record (place, angles) and animation clock in the poll files (poll_<label>.txt, `tools/s2/dosbox_arcade.py` Mem.poll) around the moment the screenshot was asked for, as the
lines tools/host/room_match.c reads:

    <name of the screenshot, without .png> <label> <x> <y> <z> <a0> <a1> <animation clock> <station>

one line per distinct state in the window of 0.15 s round the request (a screenshot of a settled station has the camera of all of them and only the animation clock changes; one in the
middle of a move may be any of the frames in the window: the matcher picks the best). Usage: python -I tools/room_shots.py <folder of a run> <output file>
"""
import glob
import os
import struct
import sys

WINDOW = 0.15


def parse(path):
    """[(time, 'E', event) or (time, 'S', state dict)] of a poll file: the words are those of ROOM_BLOCKS in tools/s2/dosbox_arcade.py"""
    rows = []
    for line in open(path):
        p = line.split()
        if len(p) == 3 and p[1] == "E":
            rows.append((float(p[0]), "E", p[2]))
            continue
        w = struct.unpack("<6i i 7i i i i", bytes.fromhex(p[1]))
        f = lambda v: struct.unpack("<f", struct.pack("<i", v))[0]
        rows.append((float(p[0]), "S", dict(a0=w[0], a1=w[1], a2=w[2], x=w[3], y=w[4], z=w[5], station=w[6], next=w[7], leaving=f(w[8]), fade=f(w[9]), done=f(w[10]),
                                           sway=f(w[11]), look_p=f(w[12]), look_h=f(w[13]), dt=f(w[14]), clock=w[15], anim=w[16])))
    return rows


def main():
    folder, out = sys.argv[1], sys.argv[2]
    labels = open(os.path.join(folder, "labels.txt")).read().split()
    pngs = sorted(glob.glob(os.path.join(folder, "*.png")))
    assert len(labels) == len(pngs), "%d labels for %d screenshots" % (len(labels), len(pngs))
    events = {}                                 # label -> (poll file's rows, time)
    for f in sorted(glob.glob(os.path.join(folder, "poll_*.txt"))):
        rows = parse(f)
        for r in rows:
            if r[1] == "E" and r[2][0] == "s":
                events.setdefault(r[2][1:], []).append((rows, r[0]))
    lines = []
    for png, label in zip(pngs, labels):
        base = os.path.splitext(os.path.basename(png))[0]
        if label not in events:
            continue
        rows, t = events[label].pop(0)
        seen = set()
        for r in rows:
            if r[1] != "S" or not (t - WINDOW <= r[0] <= t + WINDOW):
                continue
            d = r[2]
            key = (d["x"], d["y"], d["z"], d["a0"], d["a1"], d["anim"])
            if key in seen or d["a2"] != 0 and d["a0"] == 0:        # (a row half written: the frame's angles are stored one after the other)
                continue
            seen.add(key)
            lines.append("%s %s %d %d %d %d %d %d %d" % (base, label, d["x"], d["y"], d["z"], d["a0"], d["a1"], d["anim"], d["station"]))
    open(out, "w", newline="\n").write("\n".join(lines) + "\n")
    print("%d states for %d screenshots" % (len(lines), len({l.split()[0] for l in lines})))


if __name__ == "__main__":
    main()
