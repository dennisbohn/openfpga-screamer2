"""Cost model of a frame, calibrated on the six Screamer 1 start frames, applied to the frame
statistics of tools/s2/framestats.sh (docs/screamer2/performance.md).

Usage: python -I tools/s2/perf_model.py [build dir]            (default: build)
Inputs (all under build/, never committed):
  sim/t1.log .. t6.log      the simulation's log of the start frame of S1 track n: CPU phases,
                            unit counters and the timeline marks (tools/simcheck.sh)
  stats/start1.csv ..       the PC renderer's counters for the same frame (framestats.sh start n)
  stats/out/s1_tN.csv, s2_tN.csv, s2s1_tN.csv, s2d_tN.csv, s2m_tN.csv   the sweeps along the road
                            (framestats.sh sweep; tools/s2/perf_sweeps.sh makes them all)
  sim/s2sim.log, stats/c_4_130.csv   (optional) the one Screamer 2 frame in the simulation and its counters
Prints markdown tables; writes build/stats/out/forecast_frames.csv (one line per frame).

The model (a frame is CPU phases in sequence, the geometry unit running behind the CPU):
  T_gu  = A + C + GU + tail         the geometry unit sets the pace (every S1 start frame)
  T_cpu = A + C + S + P             the CPU alone
  T_rs  = A + C + RS + tail         the rasterizer alone
  T     = max of the three; the one that wins is the bottleneck.
  A     music + physics + preparation before the cull (measured, mean of the six, a constant)
  C     cull: c0 + c1 * placements tested + c2 * pieces in view            (fitted)
  GU    geometry unit busy: 32.5 * read misses (4 KB cache) + 32.4 * reflection vertices
        + g1 * faces looked at + g2 * RS                                    (g1, g2 fitted; the last term is its
        wait for the units behind it; variant "px": g2 * pixels instead)
  RS    rasterizer busy: 0.83 * (pixels + 17.3 * texture misses (16 KB) + 41 * triangles
        + 6.5 * rows + 18,000)                      (cost per event from its own counters)
  S     CPU scene loop: s1 * static pieces + s2 * object blocks             (fitted)
  tail  from the last piece to the end: mean of the six
"""
import csv
import glob
import math
import os
import re
import sys

HZ = 74_250_000
BUDGET = HZ // 50                       # 20 ms
MS = 1000.0 / HZ
MISS_GU = 283879 / 8738                 # clocks per geometry-unit read miss: GEOM_STATS run of 2026-10-27
                                        # (283,879 clocks, 8,738 misses of the 4 KB cache, architecture.md)
REFL = 36000 / 1110                     # clocks per reflection vertex: +36,000 clocks for 1,110 (architecture.md)
P_CPU = 53123                           # CPU after the last piece (radar, race screen): S1 track 2's timeline


# ---------------------------------------------------------------------------------------- small maths
def ols(X, y):
    n, k = len(X), len(X[0])
    A = [[sum(X[r][i] * X[r][j] for r in range(n)) for j in range(k)] for i in range(k)]
    b = [sum(X[r][i] * y[r] for r in range(n)) for i in range(k)]
    for i in range(k):
        p = max(range(i, k), key=lambda r: abs(A[r][i]))
        A[i], A[p] = A[p], A[i]
        b[i], b[p] = b[p], b[i]
        for r in range(i + 1, k):
            f = A[r][i] / A[i][i]
            for c in range(i, k):
                A[r][c] -= f * A[i][c]
            b[r] -= f * b[i]
    x = [0.0] * k
    for i in range(k - 1, -1, -1):
        x[i] = (b[i] - sum(A[i][j] * x[j] for j in range(i + 1, k))) / A[i][i]
    return x


def mean(v):
    return sum(v) / len(v)


def pct(v, q):
    s = sorted(v)
    return s[min(len(s) - 1, int(q * (len(s) - 1) + 0.5))]


# ---------------------------------------------------------------------------------------- the swap rule
def swap_period(T_ms, n=1500):
    """Mean time between two picture swaps (ms) when every frame takes T_ms: the rule of
    core/src/fpga/core/video_out.v (a swap at the start of a blanking line, lines 210..511 and
    0..9 of 512, once per blanking, the CPU starting its next frame at the swap)."""
    L, line = 512, 20.0 / 512
    s, armed = 210.0, 210 + (L - 210) + 10
    first = None
    for k in range(n):
        ln = int(math.ceil(s + T_ms / line))
        while not ((ln % L < 10 or ln % L >= 210) and ln >= armed):
            ln += 1
        if k == 0:
            first = ln
        s = float(ln)
        y = ln % L
        armed = ln + ((10 - y) % L if y != 10 else L)
        if armed <= ln:
            armed += L
    return (s - first) * line / (n - 1)


_period_cache = {}


def fps_of(T_clocks):
    T = round(T_clocks * MS * 10) / 10          # 0.1 ms steps
    if T not in _period_cache:
        _period_cache[T] = swap_period(max(T, 5.0))
    return 1000.0 / _period_cache[T]


# ---------------------------------------------------------------------------------------- S1 data
def parse_log(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    line = re.search(r"track frame 2: .*", txt).group(0)
    d = {"phys": int(re.search(r"\((\d+) now, (\d+) cycles\)", line).group(2))}
    for k in ["pieces", "faces", "px", "cpu", "geometry", "setup", "raster", "misses", "blit", "backdrop"]:
        mm = re.search(r"(\d+) %s\b|%s (\d+)" % (k, k), line)
        d[k] = int(mm.group(1) or mm.group(2))
    mm = re.search(r"cycles: cull (\d+), pieces (\d+), out (\d+) \(wait (\d+)\), after (\d+), effects (\d+) \+ (\d+)", txt)
    d.update(dict(zip(["cull", "pieces_cpu", "out", "out_wait", "after", "fx_obj", "fx_radar"], map(int, mm.groups()))))
    mm = re.search(r"raster: miss (\d+), edges (\d+), rows (\d+), drain (\d+), starved (\d+)", txt)
    d.update(dict(zip(["r_miss", "r_edges", "r_rows", "r_drain", "r_starved"], map(int, mm.groups()))))
    i = txt.index("timeline (t gu su rs bl wait tag):")
    marks = []
    for ln in txt[i:].splitlines()[1:]:
        if not ln.startswith("    ") or not re.match(r"^[\d |]+$", ln.strip()):
            break
        for mk in ln.split("|"):
            v = tuple(int(x) for x in mk.split())
            if len(v) == 7:
                marks.append(v)
    tag = lambda t: next((m[0] for m in marks if m[6] == t), None)
    d["t_prep"], d["t_first"], d["t_scene"], d["t_hud"] = tag(2), tag(3), tag(5), tag(7)
    d["T"] = d["t_prep"] + d["cpu"]
    return d


def read_csv(path):
    out = []
    for r in csv.DictReader(open(path)):
        out.append({k: (v if k in ("label", "kind") else float(v)) for k, v in r.items()})
    return out


def derived(d):
    """the regressors every frame has, from the PC counters"""
    d["static"] = d["gu_pieces"] - d["obj_blocks"] - d["multi_blocks"]
    return d


# ---------------------------------------------------------------------------------------- model
class Model:
    def rs_events(self, d):
        return d["px_span"] + self.rs_miss * d["tex_miss"] + self.rs_edge * d["walk_tris"] + self.rs_row * d["rows"] + self.rs_drain

    def __init__(self, rows, variant="rs"):
        self.variant = variant              # how the geometry unit's waits for the units behind it are counted:
                                            # "rs" with the rasterizer's busy time, "px" with the pixels
        # the rasterizer: cost per event from its own counters (waiting for lines, per triangle, per row)
        self.rs_miss = mean([d["r_miss"] / d["tex_miss"] for d in rows])
        self.rs_edge = mean([d["r_edges"] / d["walk_tris"] for d in rows])
        self.rs_row = mean([d["r_rows"] / d["rows"] for d in rows])
        self.rs_drain = mean([d["r_drain"] for d in rows])
        self.rs_over = mean([d["raster"] / self.rs_events(d) for d in rows])
        for d in rows:
            derived(d)
        self.A = mean([d["t_prep"] for d in rows])
        self.c = ols([[1, d["tested"], d["visible"]] for d in rows], [d["cull"] for d in rows])
        self.s = ols([[d["static"], d["obj_blocks"]] for d in rows],
                     [d["pieces_cpu"] + d["out"] - d["out_wait"] for d in rows])
        rest = lambda d: d["geometry"] - MISS_GU * d["gu_miss"] - REFL * d["refl_verts"]
        self.g = ols([[d["faces_row"], d["px_span"]] for d in rows], [rest(d) for d in rows])
        self.g2 = ols([[d["faces_row"], self.rs(d)] for d in rows], [rest(d) for d in rows])
        self.tail = mean([d["T"] - d["t_first"] - d["geometry"] for d in rows])

    def cull(self, d):
        return self.c[0] + self.c[1] * d["tested"] + self.c[2] * d["visible"]

    def gu_fixed(self, d):
        return MISS_GU * d["gu_miss"] + REFL * d["refl_verts"]

    def gu(self, d):
        if self.variant == "rs":
            return self.gu_fixed(d) + self.g2[0] * d["faces_row"] + self.g2[1] * self.rs(d)
        return self.gu_fixed(d) + self.g[0] * d["faces_row"] + self.g[1] * d["px_span"]

    def rs(self, d):
        return self.rs_over * self.rs_events(d)

    def scene_cpu(self, d):
        return self.s[0] * d["static"] + self.s[1] * d["obj_blocks"]

    def frame(self, d, a_extra=0.0, mirror=None):
        """clocks of the frame and its parts; mirror: the counters of a second pass (rear-view mirror)"""
        A, C, G, S = self.A + a_extra, self.cull(d), self.gu(d), self.scene_cpu(d)
        if mirror is not None:
            C2, G2, S2 = self.cull(mirror), self.gu(mirror), self.scene_cpu(mirror)
        else:
            C2 = G2 = S2 = 0.0
        t_gu = A + C + G + G2 + self.tail
        t_cpu = A + C + S + C2 + S2 + P_CPU
        t_rs = A + C + self.rs(d) + (self.rs(mirror) if mirror is not None else 0.0) + self.tail
        T = max(t_gu, t_cpu, t_rs)
        which = "GU" if T == t_gu else "CPU" if T == t_cpu else "RS"
        return {"T": T, "bound": which, "A": A, "C": C + C2, "GU": G + G2, "S": S + S2, "RS": self.rs(d), "t_gu": t_gu,
                "t_cpu": t_cpu, "t_rs": t_rs}


# ---------------------------------------------------------------------------------------- report
def ms(c):
    return c * MS


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    out = []
    P = lambda s="": out.append(s)

    rows = []
    for n in range(1, 7):
        d = parse_log(f"{build}/sim/t{n}.log")
        d.update(read_csv(f"{build}/stats/start{n}.csv")[0])
        d["n"] = n
        rows.append(d)
    m = Model(rows)
    m_px = Model(rows, "px")
    P("## Calibration: the six Screamer 1 start frames (simulation log, measured; counters, PC renderer)")
    P()
    P("| Track | placements tested | pieces | faces looked at | faces drawn | pixels | tex misses (PC / HW) | cull | GU busy | RS busy | T (frame) |")
    P("|---|---|---|---|---|---|---|---|---|---|---|")
    for d in rows:
        P(f"| {d['n']} | {int(d['tested'])} | {d['pieces']} | {int(d['faces_row'])} | {d['faces']} | {int(d['px_span'])} | "
          f"{int(d['tex_miss'])} / {d['misses']} | {d['cull']:,} | {d['geometry']:,} | {d['raster']:,} | {d['T']:,} = {ms(d['T']):.1f} ms |")
    P()
    P(f"Fixed parts: A = {m.A:,.0f} clocks (music + physics + preparation, mean), tail = {m.tail:,.0f} (T - first piece - GU busy; "
      f"per frame {', '.join(f'{d['T'] - d['t_first'] - d['geometry']:,}' for d in rows)}).")
    P(f"Cull C = {m.c[0]:,.0f} + {m.c[1]:.1f} x tested + {m.c[2]:.1f} x in view. "
      f"Scene loop S = {m.s[0]:.0f} x static pieces + {m.s[1]:.0f} x object blocks. "
      f"GU = {MISS_GU:.1f} x misses + {REFL:.1f} x reflection vertices + {m.g2[0]:.1f} x faces looked at + {m.g2[1]:.2f} x RS "
      f"(variant px: {m.g[0]:.1f} x faces + {m.g[1]:.2f} x pixels instead of the RS term). "
      f"RS = {m.rs_over:.2f} x (px + {m.rs_miss:.1f} x misses + {m.rs_edge:.0f} x triangles + {m.rs_row:.1f} x rows + {m.rs_drain:,.0f}).")
    P()

    # in-sample and hold-out
    P("## Hold-out: each start frame predicted from the other five")
    P()
    P("| Held out | T measured | T predicted | error | error (variant px) | GU error | cull error | RS error | bound |")
    P("|---|---|---|---|---|---|---|---|---|")
    errs, errs_px = [], []
    for k in range(6):
        mk = Model([d for i, d in enumerate(rows) if i != k])
        d = rows[k]
        f = mk.frame(d)
        e = f["T"] / d["T"] - 1
        errs.append(e)
        e_px = Model([d2 for i, d2 in enumerate(rows) if i != k], "px").frame(d)["T"] / d["T"] - 1
        errs_px.append(e_px)
        P(f"| {d['n']} | {d['T']:,} ({ms(d['T']):.1f} ms) | {f['T']:,.0f} ({ms(f['T']):.1f} ms) | {100 * e:+.1f} % | {100 * e_px:+.1f} % | "
          f"{100 * (mk.gu(d) / d['geometry'] - 1):+.1f} % | {100 * (mk.cull(d) / d['cull'] - 1):+.1f} % | "
          f"{100 * (mk.rs(d) / d['raster'] - 1):+.1f} % | {f['bound']} |")
    P()
    P(f"Hold-out error of T: mean {100 * mean(errs):+.1f} %, worst {100 * max(errs, key=abs):+.1f} %, rms {100 * math.sqrt(mean([e * e for e in errs])):.1f} % "
      f"(variant px: mean {100 * mean(errs_px):+.1f} %, worst {100 * max(errs_px, key=abs):+.1f} %, rms {100 * math.sqrt(mean([e * e for e in errs_px])):.1f} %).")
    P()
    ins = [m.frame(d)["T"] / d["T"] - 1 for d in rows]
    P(f"In-sample error of T: worst {100 * max(ins, key=abs):+.1f} %. CPU path against T (bound check): "
      f"{', '.join(f'{m.frame(d)['t_cpu'] / d['T']:.2f}' for d in rows)}; RS path: {', '.join(f'{m.frame(d)['t_rs'] / d['T']:.2f}' for d in rows)}.")
    P()

    # ---- the one Screamer 2 frame in the simulation
    vp, vc = f"{build}/sim/s2sim.log", f"{build}/stats/c_4_130.csv"
    if os.path.exists(vp) and os.path.exists(vc):
        v = parse_log(vp)
        v.update(read_csv(vc)[0])
        derived(v)
        P("## Check on Screamer 2: one frame in the simulation (track 4, road point 130, the player's car alone; S1 view distances)")
        P()
        P("| | measured (simulation) | model | variant px |")
        P("|---|---|---|---|")
        fv, fp = m.frame(v), m_px.frame(v)
        P(f"| T | {v['T']:,} = {ms(v['T']):.1f} ms | {fv['T']:,.0f} = {ms(fv['T']):.1f} ms ({100 * (fv['T'] / v['T'] - 1):+.1f} %) | "
          f"{fp['T']:,.0f} = {ms(fp['T']):.1f} ms ({100 * (fp['T'] / v['T'] - 1):+.1f} %) |")
        P(f"| before the cull (music, physics, preparation) | {v['t_prep']:,} | {m.A:,.0f} (mean of the six) | |")
        P(f"| cull | {v['cull']:,} | {m.cull(v):,.0f} ({100 * (m.cull(v) / v['cull'] - 1):+.1f} %) | |")
        P(f"| geometry unit busy | {v['geometry']:,} | {m.gu(v):,.0f} ({100 * (m.gu(v) / v['geometry'] - 1):+.1f} %) | {m_px.gu(v):,.0f} ({100 * (m_px.gu(v) / v['geometry'] - 1):+.1f} %) |")
        P(f"| rasterizer busy | {v['raster']:,} | {m.rs(v):,.0f} ({100 * (m.rs(v) / v['raster'] - 1):+.1f} %) | |")
        P(f"| T less first piece less GU (tail) | {v['T'] - v['t_first'] - v['geometry']:,} (no race screen: blitter {v['blit']}) | {m.tail:,.0f} | |")
        P(f"| texture misses | {v['misses']:,} | {int(v['tex_miss']):,} (cache model) | |")
        P(f"| pixels, faces drawn, pieces | {v['px']:,}, {v['faces']}, {v['pieces']} | {int(v['px_span']):,}, -, - | |")
        P()

    # ---- the sweeps
    sets = []
    for pre, label in (("s1", "S1"), ("s2", "S2"), ("s2s1", "S2 (S1 view distances)"), ("s2d", "S2 (far plane x 0.8)")):
        for n in range(1, 8):
            p = f"{build}/stats/out/{pre}_t{n}.csv"
            if os.path.exists(p):
                sets.append((pre, n, label, read_csv(p)))
    frames_csv = open(f"{build}/stats/out/forecast_frames.csv", "w")
    frames_csv.write("set,track,kind,idx,ncars,a_extra,mirror,T_clocks,T_ms,bound,fps,C,GU,S,RS,faces_row,px_span,tex_miss,tested,x,y,z\n")

    short = {}                                  # the mirror with a short view (levers): its passes by track and cars
    for n in range(1, 8):
        p = f"{build}/stats/out/s2m_t{n}.csv"
        if os.path.exists(p):
            for r in read_csv(p):
                if r["kind"] == "mirror":
                    derived(r)
                    short.setdefault(n, {}).setdefault(int(r["ncars"]), []).append(r)

    results = {}
    for pre, n, label, rs_ in sets:
        by = {}
        for r in rs_:
            derived(r)
            by.setdefault((r["kind"], int(r["ncars"])), []).append(r)
        mirror_by = {k[1]: v for k, v in by.items() if k[0] == "mirror"}
        for (kind, nc), lst in by.items():
            if kind == "mirror":
                continue
            for a_extra in (0.0, 100000.0):
                for mir in (((False, True) + ((2,) if pre == "s2" and n in short else ())) if kind == "road" and nc >= 1 else (False,)):
                    res = []
                    for j, r in enumerate(lst):
                        mr = (short[n][nc][j] if mir == 2 else mirror_by[nc][j]) if mir and nc in mirror_by else None
                        f = m.frame(r, a_extra, mirror=mr)
                        f.update(alt=m_px.frame(r, a_extra, mirror=mr)["T"], r=r, fps=fps_of(f["T"]))
                        res.append(f)
                        frames_csv.write(f"{pre},{n},{kind},{int(r['idx'])},{nc},{int(a_extra)},{int(mir)},{f['T']:.0f},{ms(f['T']):.2f},{f['bound']},"
                                         f"{f['fps']:.1f},{f['C']:.0f},{f['GU']:.0f},{f['S']:.0f},{f['RS']:.0f},{int(r['faces_row'])},"
                                         f"{int(r['px_span'])},{int(r['tex_miss'])},{int(r['tested'])},{int(r['x'])},{int(r['y'])},{int(r['z'])}\n")
                    results[(pre, n, kind, nc, int(a_extra), mir)] = res
    frames_csv.close()

    def summary(pre, n, nc, a_extra=0, mir=False, kind="road"):
        res = results[(pre, n, kind, nc, a_extra, mir)]
        T = [f["T"] for f in res]
        w = max(res, key=lambda f: f["T"])
        return {
            "n": len(res), "mean": ms(mean(T)), "p95": ms(pct(T, 0.95)), "max": ms(max(T)),
            "le20": 100.0 * sum(1 for t in T if t <= BUDGET) / len(T), "gt25": 100.0 * sum(1 for t in T if ms(t) > 25) / len(T),
            "fps": mean([f["fps"] for f in res]), "fps_min": min(f["fps"] for f in res), "worst": w, "alt_max": ms(max(f["alt"] for f in res)),
            "alt_mean": ms(mean([f["alt"] for f in res])),
        }

    def table(title, pre, tracks, nc, a_extra=0, mir=False):
        P(f"### {title}")
        P()
        P("| Track | frames | mean ms | p95 ms | worst ms | within 20 ms | over 25 ms | fps (mean) | fps (worst) | worst frame: bound, faces looked at / pixels / misses / tested |")
        P("|---|---|---|---|---|---|---|---|---|---|")
        for n in tracks:
            if (pre, n, "road", nc, a_extra, mir) not in results:
                continue
            s = summary(pre, n, nc, a_extra, mir)
            r = s["worst"]["r"]
            P(f"| {n} | {s['n']} | {s['mean']:.1f} | {s['p95']:.1f} | {s['max']:.1f} | {s['le20']:.0f} % | {s['gt25']:.0f} % | "
              f"{s['fps']:.1f} | {s['fps_min']:.1f} | {s['worst']['bound']}, {int(r['faces_row'])} / {int(r['px_span'])} / {int(r['tex_miss'])} / {int(r['tested'])} |")
        P()

    # --- the summary the document prints
    P("# Summary (player + 3 opponents; A = start-frame physics)")
    P()
    P("| Track | mean ms | p95 ms | worst ms | within 20 ms | pictures/s mean | pictures/s worst | physics +1.35 ms: mean ms, within 20 ms | with mirror: mean ms | mirror, 15,000 view: mean ms | S1 view distances: mean ms | far x 0.8: mean ms |")
    P("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for n in range(1, 8):
        s0 = summary("s2", n, 4)
        s1_ = summary("s2", n, 4, 100000)
        sm = summary("s2", n, 4, 0, True)
        ss = summary("s2", n, 4, 0, 2) if ("s2", n, "road", 4, 0, 2) in results else None
        sd = summary("s2s1", n, 4)
        sf = summary("s2d", n, 4)
        P(f"| {n} | {s0['mean']:.1f} | {s0['p95']:.1f} | {s0['max']:.1f} | {s0['le20']:.0f} % | {s0['fps']:.1f} | {s0['fps_min']:.1f} | "
          f"{s1_['mean']:.1f}, {s1_['le20']:.0f} % | {sm['mean']:.1f} | {ss['mean'] if ss else float('nan'):.1f} | {sd['mean']:.1f} | {sf['mean']:.1f} |")
    P()
    P("| Track | cars 0: mean ms | 1 | 4 | 10 | worst ms with 10 | within 20 ms with 10 | grid with 10 cars: ms |")
    P("|---|---|---|---|---|---|---|---|")
    for n in range(1, 8):
        v = [summary("s2", n, c) for c in (0, 1, 4, 10)]
        g = results[("s2", n, "grid", 10, 0, False)][0]
        P(f"| {n} | {v[0]['mean']:.1f} | {v[1]['mean']:.1f} | {v[2]['mean']:.1f} | {v[3]['mean']:.1f} | {v[3]['max']:.1f} | {v[3]['le20']:.0f} % | {ms(g['T']):.1f} |")
    P()
    P("# Forecast along the road (near chase camera; ms per frame; cars: 1 = the player's, 4 = and three opponents)")
    P()
    for nc, title in ((0, "no car at all (the study's measure)"), (1, "the player's car only"), (4, "the player's car and 3 opponents"), (10, "the player's car and 9 opponents")):
        P(f"## Screamer 2, {title}")
        P()
        table("A = start-frame physics", "s2", range(1, 8), nc)
    P("## Screamer 1 along its road, the same method (cars: 4)")
    P()
    table("A = start-frame physics", "s1", range(1, 7), 4)
    P("## Sensitivities (3 opponents: 4 cars)")
    P()
    table("Physics +100,000 clocks (1.35 ms; driving, heavier S2 car model)", "s2", range(1, 8), 4, 100000)
    table("With the rear-view mirror (second pass)", "s2", range(1, 8), 4, 0, True)
    table("S1 view distances instead of the track's TRACK_DATA far plane", "s2s1", range(1, 8), 4)
    table("Far plane and reach x 0.8 (the original's MID detail)", "s2d", range(1, 8), 4)
    table("Mirror with a view of 15,000 units only (far plane 15,000, reach 18,000)", "s2", range(1, 8), 4, 0, 2)

    # --- the worst frame of every track: where the time goes
    P("## The worst frame of each track (player + 3 opponents): where the time goes, ms")
    P()
    P("| Track | at road point | T | bound | before the cull | cull | GU: cache misses | GU: faces looked at | GU: waiting for the units behind | GU: reflections | tail | RS busy | frame in the table: faces looked at, pixels, misses, placements tested |")
    P("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for pre, tr in (("s1", range(1, 7)), ("s2", range(1, 8))):
        for n in tr:
            res = results[(pre, n, "road", 4, 0, False)]
            w = max(res, key=lambda f: f["T"])
            r = w["r"]
            P(f"| {pre.upper()} {n} | {int(r['idx'])} | {ms(w['T']):.1f} | {w['bound']} | {ms(w['A']):.1f} | {ms(w['C']):.1f} | {ms(MISS_GU * r['gu_miss']):.1f} | "
              f"{ms(m.g2[0] * r['faces_row']):.1f} | {ms(m.g2[1] * m.rs(r)):.1f} | {ms(REFL * r['refl_verts']):.1f} | {ms(m.tail):.1f} | {ms(m.rs(r)):.1f} | "
              f"{int(r['faces_row'])}, {int(r['px_span'])}, {int(r['tex_miss'])}, {int(r['tested'])} |")
    P()

    # --- start grid
    P("## Start grid (the player's car and nine opponents, camera behind the player)")
    P()
    P("| Track | T ms | bound | GU | cull | faces looked at | pixels | tex misses | note |")
    P("|---|---|---|---|---|---|---|---|---|")
    for pre, lab in (("s1", "S1 synthetic grid"), ("s2", "S2 grid")):
        for n in range(1, 8):
            k = (pre, n, "grid", 10, 0, False)
            if k in results:
                f = results[k][0]
                r = f["r"]
                P(f"| {lab} {n} | {ms(f['T']):.1f} | {f['bound']} | {ms(f['GU']):.1f} | {ms(f['C']):.1f} | {int(r['faces_row'])} | {int(r['px_span'])} | {int(r['tex_miss'])} | model |")
    for d in rows:
        P(f"| S1 {d['n']} measured | {ms(d['T']):.1f} | GU | {ms(d['geometry']):.1f} | {ms(d['cull']):.1f} | {int(d['faces_row'])} | {int(d['px_span'])} | {d['misses']} | simulation |")
    P()

    # --- cars: cost per car
    P("## Cost of cars (means over the road; model)")
    P()
    P("| Track | 0 cars: ms | player's car | +3 opponents | +9 opponents | faces looked at 0 / 1 / 4 / 10 |")
    P("|---|---|---|---|---|---|")
    for pre, tr in (("s1", range(1, 7)), ("s2", range(1, 8))):
        for n in tr:
            v = [summary(pre, n, c)["mean"] for c in (0, 1, 4, 10)]
            fr = [mean([f["r"]["faces_row"] for f in results[(pre, n, "road", c, 0, False)]]) for c in (0, 1, 4, 10)]
            P(f"| {pre.upper()} {n} | {v[0]:.1f} | {v[1]:.1f} | {v[2]:.1f} | {v[3]:.1f} | {fr[0]:.0f} / {fr[1]:.0f} / {fr[2]:.0f} / {fr[3]:.0f} |")
    P()

    # --- statistics per track (equal footing)
    P("## Frame statistics along the road (means; no car / player + 3)")
    P()
    keys = ["tested", "grp_skipped", "groups", "visible", "ts_pieces", "verts", "faces_row", "back", "ts_faces", "clip_any", "walk_tris", "px_span", "tex_miss", "tex_lines", "gu_miss", "fade_night", "fade_fog"]
    P("| Set | cars | " + " | ".join(keys) + " | tex misses / pixel |")
    P("|---|---|" + "---|" * (len(keys) + 1))
    for pre, tr in (("s1", range(1, 7)), ("s2", range(1, 8))):
        for n in tr:
            for c in (0, 4):
                lst = [f["r"] for f in results[(pre, n, "road", c, 0, False)]]
                vals = [mean([r[k] for r in lst]) for k in keys]
                P(f"| {pre.upper()} {n} | {c} | " + " | ".join(f"{v:.0f}" for v in vals) + f" | {mean([r['tex_miss'] for r in lst]) / mean([r['px_span'] for r in lst]):.3f} |")
    P()

    P("## Faces drawn per frame along the road, no car (min / mean / max) and placements tested")
    P()
    P("| Set | faces drawn | pieces handed on | placements tested | in view | pixels | tex misses / pixel | distinct texture lines |")
    P("|---|---|---|---|---|---|---|---|")
    for pre, tr in (("s1", range(1, 7)), ("s2", range(1, 8))):
        for n in tr:
            lst = [f["r"] for f in results[(pre, n, "road", 0, 0, False)]]
            mm = lambda k: f"{int(min(r[k] for r in lst))} / {int(mean([r[k] for r in lst]))} / {int(max(r[k] for r in lst))}"
            P(f"| {pre.upper()} {n} | {mm('ts_faces')} | {mm('gu_pieces')} | {mm('tested')} | {mm('visible')} | {mm('px_span')} | "
              f"{mean([r['tex_miss'] for r in lst]) / mean([r['px_span'] for r in lst]):.3f} | {int(mean([r['tex_lines'] for r in lst]))} |")
    P()

    # --- pacing
    P("## Pictures per second for a constant frame time (the swap rule)")
    P()
    P("| frame time ms | 18 | 20 | 21 | 22 | 24 | 26 | 28 | 30 | 33 | 36 | 40 |")
    P("|---|---|---|---|---|---|---|---|---|---|---|---|")
    P("| pictures/s | " + " | ".join(f"{1000.0 / swap_period(t):.1f}" for t in (18, 20, 21, 22, 24, 26, 28, 30, 33, 36, 40)) + " |")
    P()
    txt = "\n".join(out)
    print(txt)
    open(f"{build}/stats/out/forecast.md", "w", encoding="utf-8").write(txt + "\n")


if __name__ == "__main__":
    main()
