"""Tables for docs/screamer2/performance.md, "Detail levels and LOD at HIGH": the cost model of
tools/s2/perf_model.py (calibrated on the six Screamer 1 start frames) applied to the sweeps of
tools/s2/lod_sweeps.sh, with and without the original's level of detail.

Usage: python -I tools/s2/lod_report.py [build dir]            (default: build)
Inputs under build/ (never committed): sim/t1..6.log and stats/start1..6.csv (the model's calibration, as
perf_model.py), stats/lod/s2{c,l,ls,lc,dc,dl}_t1..7.csv (lod_sweeps.sh); optional stats/out_reach12/s2_t*.csv and
s2d_t*.csv (the first forecast's sweeps, pieces to 1.2 times the far plane, copied away before perf_sweeps.sh was
changed to the original's piece distance; without them the comparison tables are left out).
Prints markdown; writes stats/lod/lod_report.md.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import perf_model as pm

BUDGET, ms, mean = pm.BUDGET, pm.ms, pm.mean


def load(build):
    rows = []
    for n in range(1, 7):
        d = pm.parse_log(f"{build}/sim/t{n}.log")
        d.update(pm.read_csv(f"{build}/stats/start{n}.csv")[0])
        d["n"] = n
        rows.append(d)
    return pm.Model(rows)


class Sweep:
    """one csv: the road frames with k cars, and the mirror's pass for each"""

    def __init__(self, path, model):
        self.m = model
        self.by = {}
        for r in pm.read_csv(path):
            pm.derived(r)
            self.by.setdefault((r["kind"], int(r["ncars"])), []).append(r)

    def frames(self, nc, mirror=False, a_extra=0.0):
        out = []
        mir = self.by.get(("mirror", nc))
        for j, r in enumerate(self.by[("road", nc)]):
            f = self.m.frame(r, a_extra, mirror=mir[j] if mirror else None)
            f["fps"] = pm.fps_of(f["T"])
            f["r"] = r
            out.append(f)
        return out

    def summary(self, nc, mirror=False):
        fr = self.frames(nc, mirror)
        T = [f["T"] for f in fr]
        w = max(fr, key=lambda f: f["T"])
        return {"mean": ms(mean(T)), "max": ms(max(T)), "le20": 100.0 * sum(t <= BUDGET for t in T) / len(T),
                "fps": mean([f["fps"] for f in fr]), "fps_min": min(f["fps"] for f in fr), "worst": w,
                "faces": mean([f["r"]["faces_row"] for f in fr]), "n": len(fr)}

    def mean_of(self, key, nc=4, kind="road"):
        return mean([r[key] for r in self.by[(kind, nc)]])


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    m = load(build)
    lod = f"{build}/stats/lod"
    S = {}
    for pre in ("c", "l", "ls", "lc", "dc", "dl"):
        for n in range(1, 8):
            p = f"{lod}/s2{pre}_t{n}.csv"
            if os.path.exists(p):
                S[(pre, n)] = Sweep(p, m)
    for pre, new in (("s2", "o"), ("s2d", "od")):
        for n in range(1, 8):
            p = f"{build}/stats/out_reach12/{pre}_t{n}.csv"
            if os.path.exists(p):
                S[(new, n)] = Sweep(p, m)
    out = []
    P = lambda s="": out.append(s)
    tr = range(1, 8)

    P("### HIGH, player + 3 opponents: without and with the original's level of detail")
    P()
    P("| S2 track | faces looked at, mean | mean ms | worst ms | road within 20 ms | pictures/s mean / worst |")
    P("|---|---|---|---|---|---|")
    for n in tr:
        a, b = S[("c", n)].summary(4), S[("l", n)].summary(4)
        P(f"| {n} | {a['faces']:,.0f} -> {b['faces']:,.0f} | {a['mean']:.1f} -> {b['mean']:.1f} | {a['max']:.1f} -> {b['max']:.1f} | "
          f"{a['le20']:.0f} % -> {b['le20']:.0f} % | {a['fps']:.1f} / {a['fps_min']:.1f} -> {b['fps']:.1f} / {b['fps_min']:.1f} |")
    P()

    P("### What the lower levels remove (means along the road, player + 3 opponents)")
    P()
    P("| S2 track | faces looked at | vertices | mesh-read misses (4 KB) | texture misses | pixels | placed blocks at L0 / L1 / L2 | the 48 blocks of the 4 cars at L0 / L1 / L2 / dropped |")
    P("|---|---|---|---|---|---|---|---|")
    tot = {k: [0.0, 0.0] for k in ("faces_row", "verts", "gu_miss", "tex_miss", "px_span")}
    for n in tr:
        a, b = S[("c", n)], S[("l", n)]
        cells = []
        for k in ("faces_row", "verts", "gu_miss", "tex_miss", "px_span"):
            x, y = a.mean_of(k), b.mean_of(k)
            tot[k][0] += x
            tot[k][1] += y
            cells.append(f"{100.0 * (y / x - 1):+.0f} %")
        pb = b.mean_of("gu_pieces") - b.mean_of("obj_blocks")      # blocks of placed meshes drawn
        l1, l2 = b.mean_of("lod_s1"), b.mean_of("lod_s2")
        cb = 48.0                                  # four cars of 12 blocks, made whether in view or not (the counters count then)
        c1, c2, cd = b.mean_of("lod_c1"), b.mean_of("lod_c2"), b.mean_of("lod_cd")
        P(f"| {n} | {cells[0]} | {cells[1]} | {cells[2]} | {cells[3]} | {cells[4]} | {pb - l1 - l2:.0f} / {l1:.0f} / {l2:.0f} | "
          f"{cb - c1 - c2 - cd:.0f} / {c1:.1f} / {c2:.1f} / {cd:.1f} |")
    P(f"| all | " + " | ".join(f"{100.0 * (tot[k][1] / tot[k][0] - 1):+.0f} %" for k in ("faces_row", "verts", "gu_miss", "tex_miss", "px_span")) + " | | |")
    P()

    P("### Where the saving comes from: mean ms, player + 3 opponents")
    P()
    P("| S2 track | no LOD | placed meshes only | cars only | both |")
    P("|---|---|---|---|---|")
    for n in tr:
        v = [S[(k, n)].summary(4)["mean"] for k in ("c", "ls", "lc", "l")]
        P(f"| {n} | {v[0]:.1f} | {v[1]:.1f} | {v[2]:.1f} | {v[3]:.1f} |")
    P()

    P("### Cars and the mirror: mean ms, no LOD -> LOD")
    P()
    P("| S2 track | scenery only (0 cars) | 4 cars | 4 cars and the mirror (full view; with LOD at level 1 or lower) | mirror: worst ms | mirror: within 20 ms | mirror: pictures/s mean / worst |")
    P("|---|---|---|---|---|---|---|")
    for n in tr:
        a0, b0 = S[("c", n)].summary(0), S[("l", n)].summary(0)
        a4, b4 = S[("c", n)].summary(4), S[("l", n)].summary(4)
        am, bm = S[("c", n)].summary(4, True), S[("l", n)].summary(4, True)
        P(f"| {n} | {a0['mean']:.1f} -> {b0['mean']:.1f} | {a4['mean']:.1f} -> {b4['mean']:.1f} | {am['mean']:.1f} -> {bm['mean']:.1f} | "
          f"{am['max']:.1f} -> {bm['max']:.1f} | {am['le20']:.0f} % -> {bm['le20']:.0f} % | "
          f"{am['fps']:.1f} / {am['fps_min']:.1f} -> {bm['fps']:.1f} / {bm['fps_min']:.1f} |")
    P()

    if ("o", 1) in S:
        P("### The piece distance: the first forecast's assumption (1.2 x far plane) against the original's (far plane + 8192), no LOD, 4 cars")
        P()
        P("| S2 track | pieces to | mean ms | worst ms | within 20 ms | pictures/s mean / worst | placements tested, mean |")
        P("|---|---|---|---|---|---|---|")
        for n in tr:
            a, b = S[("o", n)].summary(4), S[("c", n)].summary(4)
            far = 60000 if n == 4 else 50000
            P(f"| {n} | {1.2 * far:,.0f} -> {far + 8192:,} | {a['mean']:.1f} -> {b['mean']:.1f} | {a['max']:.1f} -> {b['max']:.1f} | "
              f"{a['le20']:.0f} % -> {b['le20']:.0f} % | {a['fps']:.1f} / {a['fps_min']:.1f} -> {b['fps']:.1f} / {b['fps_min']:.1f} | "
              f"{S[('o', n)].mean_of('tested'):.0f} -> {S[('c', n)].mean_of('tested'):.0f} |")
        P()

    P("### MEDIUM (far plane x 0.8, pieces to 0.8 far + 8192), 4 cars: without / with the level of detail")
    P()
    P("| S2 track | mean ms | worst ms | within 20 ms | pictures/s mean / worst |")
    P("|---|---|---|---|---|")
    for n in tr:
        b, c = S[("dc", n)].summary(4), S[("dl", n)].summary(4)
        P(f"| {n} | {b['mean']:.1f} -> {c['mean']:.1f} | {b['max']:.1f} -> {c['max']:.1f} | "
          f"{b['le20']:.0f} % -> {c['le20']:.0f} % | {b['fps']:.1f} / {b['fps_min']:.1f} -> {c['fps']:.1f} / {c['fps_min']:.1f} |")
    P()

    P("### Headroom of the CPU path with LOD (the model's CPU path: music, physics, preparation, cull, scene loop, tail work), 4 cars")
    P()
    P("| S2 track | CPU path mean / worst ms | frame (T) mean / worst ms | the CPU path is the bound in |")
    P("|---|---|---|---|")
    for n in tr:
        fr = S[("l", n)].frames(4)
        P(f"| {n} | {mean([ms(f['t_cpu']) for f in fr]):.1f} / {max(ms(f['t_cpu']) for f in fr):.1f} | "
          f"{mean([ms(f['T']) for f in fr]):.1f} / {max(ms(f['T']) for f in fr):.1f} | "
          f"{100.0 * sum(f['bound'] == 'CPU' for f in fr) / len(fr):.0f} % of the frames |")
    P()

    P("### Worst frame of each track with LOD: where the time goes (ms)")
    P()
    P("| S2 track | at road point | T | bound | cull | GU: cache misses | GU: faces looked at | GU: waiting for the units behind | RS busy | faces looked at, pixels, misses |")
    P("|---|---|---|---|---|---|---|---|---|---|")
    for n in tr:
        w = S[("l", n)].summary(4)["worst"]
        r = w["r"]
        P(f"| {n} | {int(r['idx'])} | {ms(w['T']):.1f} | {w['bound']} | {ms(w['C']):.1f} | {ms(pm.MISS_GU * r['gu_miss']):.1f} | "
          f"{ms(m.g2[0] * r['faces_row']):.1f} | {ms(m.g2[1] * m.rs(r)):.1f} | {ms(m.rs(r)):.1f} | "
          f"{int(r['faces_row'])}, {int(r['px_span'])}, {int(r['tex_miss'])} |")
    P()
    txt = "\n".join(out)
    print(txt)
    open(f"{lod}/lod_report.md", "w", encoding="utf-8").write(txt + "\n")


if __name__ == "__main__":
    main()
