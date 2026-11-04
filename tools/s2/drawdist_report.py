"""Table for docs/screamer2/draw_distance.md: the cost model of tools/s2/perf_model.py (calibrated on the six
Screamer 1 start frames) applied to the road sweeps without and with the original's cell visibility lists
(tools/s2/drawdist_sweeps.sh), Screamer 2 tracks 1..7, the near chase camera, player + 3 opponents (and the
scenery alone).

Usage: python -I tools/s2/drawdist_report.py [build dir]            (default: build)
Inputs under build/ (never committed): sim/t1..6.log and stats/start1..6.csv (the model's calibration),
stats/dd/before_t1..7.csv and after_t1..7.csv. Prints markdown; writes stats/dd/drawdist_report.md.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import perf_model as pm
from lod_report import Sweep, load

ms, mean = pm.ms, pm.mean


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    model = load(build)
    S = {(w, n): Sweep(f"{build}/stats/dd/{w}_t{n}.csv", model) for w in ("before", "after") for n in range(1, 8)}
    out = []
    P = out.append
    P("| S2 track | pieces handed on, mean | placements tested, mean | faces looked at, mean (max) | mean ms | worst ms "
      "| road within 20 ms | pictures/s mean / worst |")
    P("|---|---|---|---|---|---|---|---|")
    for n in range(1, 8):
        a, b = S[("before", n)], S[("after", n)]
        sa, sb = a.summary(4), b.summary(4)
        fa, fb = [r["faces_row"] for r in a.by[("road", 4)]], [r["faces_row"] for r in b.by[("road", 4)]]
        P(f"| {n} | {a.mean_of('gu_pieces'):.0f} -> {b.mean_of('gu_pieces'):.0f} | {a.mean_of('tested'):.0f} -> {b.mean_of('tested'):.0f} | "
          f"{sa['faces']:,.0f} ({max(fa):,.0f}) -> {sb['faces']:,.0f} ({max(fb):,.0f}) | {sa['mean']:.1f} -> {sb['mean']:.1f} | "
          f"{sa['max']:.1f} -> {sb['max']:.1f} | {sa['le20']:.0f} % -> {sb['le20']:.0f} % | "
          f"{sa['fps']:.1f} / {sa['fps_min']:.1f} -> {sb['fps']:.1f} / {sb['fps_min']:.1f} |")
    P("")
    P("Scenery only (no car): mean ms")
    P("")
    P("| S2 track | " + " | ".join(str(n) for n in range(1, 8)) + " |")
    P("|---|" + "---|" * 7)
    P("| before -> after | " + " | ".join(f"{S[('before', n)].summary(0)['mean']:.1f} -> {S[('after', n)].summary(0)['mean']:.1f}"
                                          for n in range(1, 8)) + " |")
    P("")
    P("Frames whose cost went up (4 cars, same road point): "
      + ", ".join(f"track {n}: {sum(fb['T'] > fa['T'] + 1 for fa, fb in zip(S[('before', n)].frames(4), S[('after', n)].frames(4)))} of "
                  f"{len(S[('after', n)].frames(4))}" for n in range(1, 8)))
    txt = "\n".join(out)
    print(txt)
    open(f"{build}/stats/dd/drawdist_report.md", "w", encoding="utf-8").write(txt + "\n")


if __name__ == "__main__":
    main()
