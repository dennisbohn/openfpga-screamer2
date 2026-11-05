"""The animated scenery's pictures against the running original's (docs/development.md "Animated scenery"): the screenshots of a recording of tools/s2/dosbox_anim.py, each
compared with the firmware's own drawing (tools/s2/render_s2.sh, the camera letter a) of the same state.

Usage: python -I tools/s2/anim_shots.py <track> [folder] [row]       (needs build/s2/track<n>.bin: python -I tools/s2/make_track_s2.py <game folder> <n> build/s2/track<n>.bin)

For every screenshot the game's state of the frames round it (the sampler sees the game between two of its steps, and the capture falls on a frame of its own) is the camera
record (+0x48 .. +0x5c: place, pitch, heading, roll), the race clock, the player's place and lap. The firmware draws that scene for each of those frames, with the objects of the
animated rows and without them. The pixels the objects change are the object as the firmware draws it; of these the share that has the screenshot's palette index is the match
(the frame with the best match is taken as the one the screenshot shows). Round the object (its box, 6 pixels wider) the pixels that differ between the screenshot and the
drawing with the objects are counted against those that differ between the screenshot and the drawing without them: if the original drew something the firmware does not, the
first number is the larger. The comparison is of the 3D picture only (the race screen's elements are drawn over it; the box is clear of them or the pixels are left out).
"""
import json
import os
import re
import struct
import subprocess
import sys
import glob

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.normpath(os.path.join(here, "..", ".."))
sys.path.insert(0, here)
import dosbox_arcade as da  # noqa: E402

W, H = 320, 200


def frames_and_shots(folder):
    fr, shots = [], []
    with open(os.path.join(folder, "frames.jsonl")) as f:
        f.readline()
        for ln in f:
            j = json.loads(ln)
            if "clock" in j:
                fr.append(j)
            else:
                shots.append(j)
    return fr, shots


def camera_spec(j, show):
    clock = struct.unpack("<f", bytes.fromhex(j["clock"]))[0]
    car = bytes.fromhex(j["car"])
    lap, = struct.unpack_from("<i", car, 8)
    px, _, py = struct.unpack_from("<3i", car, 0x38)
    cam = bytes.fromhex(j["cam"])
    cx, ch, cy = struct.unpack_from("<3i", cam, 0x48)
    pitch, yaw, roll = struct.unpack_from("<3f", cam, 0x54)
    return "a%.7g,%d,%d,%d,%d,%d,%d,%.9g,%.9g,%.9g,%d" % (clock, px, py, lap, cx, cy, ch, yaw, pitch, roll, show)


def main():
    track = int(sys.argv[1])
    folder = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "build", "check", "dosbox_anim_t%d" % track)
    fr, shots = frames_and_shots(folder)
    pngs = sorted(glob.glob(os.path.join(folder, "*.png")))
    labels = open(os.path.join(folder, "labels.txt")).read().split()
    assert len(pngs) == len(labels), (len(pngs), len(labels))
    byLabel = dict(zip(labels, pngs))
    image = os.path.join(root, "build", "s2", "track%d.bin" % track)
    specs, plan = [], []
    for s in shots:
        if s["shot"] not in byLabel:
            continue
        for k in range(max(0, s["after_frame"] - 14), min(len(fr), s["after_frame"] + 8)):
            plan.append((s["shot"], k, len(specs)))
            specs += [camera_spec(fr[k], 1), camera_spec(fr[k], 0)]
    out = os.path.join(root, "build", "check", "anim_pics_t%d" % track)
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, "r*")):
        os.remove(f)
    os.makedirs(os.path.join(root, "build", "s2host"), exist_ok=True)
    open(os.path.join(root, "build", "s2host", "specs.txt"), "w").write("\n".join(specs) + "\n")
    cmd = ["sh", os.path.join(here, "render_s2.sh"), image, os.path.join(out, "r"), "@specs.txt"]
    r = subprocess.run(cmd, capture_output=True, text=True, env=dict(os.environ, S2_GAME=da.game))
    if r.returncode not in (0, 3):
        sys.exit(r.stdout + r.stderr)
    best = {}
    for label, k, i in plan:
        shot = da.read_png(byLabel[label])
        with_ = open(os.path.join(out, "r%d.idx" % i), "rb").read()
        without = open(os.path.join(out, "r%d.idx" % (i + 1)), "rb").read()
        # the frame the screenshot shows is the one whose drawing agrees with it most over the whole picture (the road, the buildings, the sky: the camera's), the race screen's
        # elements and the cars being the same few thousand pixels whatever the frame
        overall = sum(shot[p] == without[p] for p in range(W * H))
        mask = set(p for p in range(W * H) if with_[p] != without[p])
        if not mask:
            res = (0, 0, 0, 0, 0, 0)
        else:
            xs, ys = [p % W for p in mask], [p // W for p in mask]
            x0, x1, y0, y1 = max(0, min(xs) - 10), min(W - 1, max(xs) + 10), max(0, min(ys) - 10), min(H - 1, max(ys) + 10)
            box = [y * W + x for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
            theirs = set(p for p in box if shot[p] != without[p])      # what the screenshot has there that the drawing without the objects does not
            both = mask & theirs
            same = sum(shot[p] == with_[p] for p in both)
            res = (len(mask), len(theirs), len(both), same, len(box), len(theirs - mask))
        if label not in best or overall > best[label][0]:
            best[label] = (overall, k, res, i)
    for label, (overall, k, res, i) in sorted(best.items()):
        m, t, both, same, box, extra = res
        print("%-10s frame %5d clock %7.2f: picture %4.1f %% equal; the object: %5d pixels drawn by the firmware, %5d changed in the screenshot (box %5d pixels), %5d in both "
              "(%.0f %% of the union), their index equal in %.0f %%; %d changed in the screenshot only" %
              (label, k, struct.unpack("<f", bytes.fromhex(fr[k]["clock"]))[0], 100.0 * overall / (W * H), m, t, box, both, 100.0 * both / (m + t - both) if m + t - both else 0.0,
               100.0 * same / both if both else 0.0, extra))


if __name__ == "__main__":
    main()
