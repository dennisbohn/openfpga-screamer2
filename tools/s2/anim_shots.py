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


def track_palette(image):
    """the 256 colours of a track image: its palette section is the one after the UNR (version 7: the header's word at +0x18 is its offset), 256 x 0x00RRGGBB"""
    img = open(image, "rb").read()
    o, = struct.unpack_from("<I", img, 0x18)
    return [(img[o + 4 * i + 2], img[o + 4 * i + 1], img[o + 4 * i]) for i in range(256)]


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
    import re as _re
    counts = [tuple(int(v) for v in m) for m in _re.findall(r"camera [-\d.]+ [-\d.]+ [-\d.]+ yaw [-\d.]+: (\d+) pieces handed on, (\d+) faces, (\d+) triangles, (\d+) vertices, (\d+) see-through", r.stdout)]
    best, cands = {}, {}
    pal = track_palette(image)
    near = lambda a, b: sum(abs(x - y) for x, y in zip(pal[a], pal[b])) <= 24
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
            both = set(q for q in (mask & theirs) if not 33 <= with_[q] <= 41)     # (the see-through rotors' texels, 33 to 41 here, are left out: they show through the table in the original)
            same = sum(near(shot[p], with_[p]) for p in both)
            res = (len(mask), len(theirs), len(both), same, len(box), len(theirs - mask))
        near_score = res[3]
        cands.setdefault(label, []).append((overall, near_score, k, res, i))
    for label, c in cands.items():
        top = max(x[0] for x in c)
        # the frame is the one the whole picture agrees with most, within 2 % of the best (the capture falls between the game's own steps: an object that moves 100 units a frame needs
        # the right one), then the one the object agrees with most
        best[label] = max((x for x in c if x[0] >= top - 0.02 * W * H), key=lambda x: x[1])
        best[label] = (best[label][0], best[label][2], best[label][3], best[label][4])
    cost = []
    for label, (overall, k, res, i) in sorted(best.items()):
        m, t, both, same, box, extra = res
        d = tuple(a - b for a, b in zip(counts[i], counts[i + 1])) if i + 1 < len(counts) else (0,) * 5
        cost.append(d)
        print("%-10s frame %5d clock %7.2f: picture %4.1f %% equal; the object: %5d pixels drawn by the firmware, %5d changed in the screenshot (box %5d pixels), %5d in both "
              "(%.0f %% of the union), their colour within 24 (of 765) in %.0f %%; %d changed in the screenshot only; the objects cost the geometry unit %d pieces, %d faces, "
              "%d triangles, %d vertices" %
              (label, k, struct.unpack("<f", bytes.fromhex(fr[k]["clock"]))[0], 100.0 * overall / (W * H), m, t, box, both, 100.0 * both / (m + t - both) if m + t - both else 0.0,
               100.0 * same / both if both else 0.0, extra, d[0], d[1], d[2], d[3]))
    if cost:
        print("the objects in these %d pictures: pieces %d to %d, faces %d to %d, triangles up to %d, vertices up to %d" %
              (len(cost), min(c[0] for c in cost), max(c[0] for c in cost), min(c[1] for c in cost), max(c[1] for c in cost), max(c[2] for c in cost), max(c[3] for c in cost)))


if __name__ == "__main__":
    main()
