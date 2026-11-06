"""The rear-view mirror's pass against the ordinary pass (tools/s2/check_mirror_s2.sh): each camera's two pictures of the top 35 rows (render_s2.c, MIRROR_ROWS) are compared with the mirror's
turned over left to right: row y of the mirror's pass at x against the ordinary pass's at SHIFT - x, for the shifts 315 .. 320; the share of equal pixels is printed and the test passes where the
best shift is 319 and at least 75 % are equal (the rest are textures sampled at the other side of a pixel edge and, where the backdrop window runs past a row end, the panorama's seam). The negative control (the same pictures compared without the flip) must be far below.
Usage: python -I tools/s2/check_mirror_s2.py <folder> <tracks...>
"""
import glob
import os
import sys

W, ROWS = 320, 35


def load(path):
    d = open(path, "rb").read()
    return [d[y * W:(y + 1) * W] for y in range(ROWS)]


def share(n, m, shift):
    eq = tot = 0
    for y in range(ROWS):
        for x in range(W):
            xs = shift - x
            if 0 <= xs < W:
                tot += 1
                eq += m[y][x] == n[y][xs]
    return eq / tot


def main():
    folder, tracks = sys.argv[1], sys.argv[2:]
    fails = 0
    for t in tracks:
        k = 0
        backs = []
        while os.path.exists(os.path.join(folder, "t%s%dn.idx" % (t, k))):
            n, m = load(os.path.join(folder, "t%s%dn.idx" % (t, k))), load(os.path.join(folder, "t%s%dm.idx" % (t, k)))
            best = max(range(317, 322), key=lambda s: share(n, m, s))
            s319, plain = share(n, m, 319), sum(n[y][x] == m[y][x] for y in range(ROWS) for x in range(W)) / (ROWS * W)
            other = os.path.join(folder, "t%s%dn.idx" % (t, k + 1 if os.path.exists(os.path.join(folder, "t%s%dn.idx" % (t, k + 1))) else 0))
            control = share(load(other), m, 319)                  # (the negative control: the mirror's pass against another camera's ordinary one)
            bn, bm = load(os.path.join(folder, "t%s%dbn.idx" % (t, k))), load(os.path.join(folder, "t%s%dbm.idx" % (t, k)))
            back = share(bn, bm, 319)                              # the backdrops alone: the panorama turned (the seam of the panorama's row end is the one thing that differs)
            ok = best == 319 and s319 >= 0.75 and s319 - max(plain, control) >= 0.5
            fails += not ok
            backs.append(back)
            print("track %s camera %d: mirror = ordinary turned over at 319 - x in %.1f %% of the pixels (best shift %d), without the flip %.1f %%, against another camera's %.1f %%, the backdrops alone %.1f %% %s" % (t, k, 100 * s319, best, 100 * plain, 100 * control, 100 * back,
                  "ok" if ok else "FAIL"))
            k += 1
        if not k:
            print("track %s: no pictures" % t)
            fails += 1
        elif max(backs) < 0.99:                                    # (the panorama's seam: a window that runs past the end of a row reads the next row's start, which the turned panorama has at the other end)
            print("track %s: no camera has the turned backdrop equal to the ordinary one turned over (best %.1f %%) FAIL" % (t, 100 * max(backs)))
            fails += 1
    sys.exit(1 if fails else 0)


main()
