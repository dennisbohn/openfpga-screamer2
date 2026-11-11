"""Swap and catch-up model of the race loop: 512 lines of 39.0625 us per picture, swaps in the blanking (lines 210..511 and 0..9),
the frame counter counts at line 0, a frame takes steps = video frames since its predecessor began (at most 4), each step after the first costs X ms."""
import re, statistics as st, sys
LINE = 20.0 / 512
def base_series(path):
    d = open(path, "rb").read().rstrip(b"\0").decode("latin1")
    out = []
    for l in d.split("\n"):
        m = re.match(r"L(\d+\.\d) ([\d.]+) k1 ", l)
        if m: out.append(float(m[2]))
    return out
def blank(line): return line >= 210 or line < 10
def next_swap(req):                       # req in lines (float); the first blanking line start at or after it
    t = int(req) + (0 if req == int(req) else 1)
    while not blank(t % 512): t += 1
    return t
def run(base, policy, X=4.7, tail=0.4, seconds=60, thr=15.8, cap=3):
    t = 210; prev_counter = t // 512; owed = 0; last_work = 0.0
    swaps = []; works = []; steps_log = []; i = 0; lag_max = 0
    while t * LINE < seconds * 1000:
        counter = t // 512; dvf = counter - prev_counter; prev_counter = counter
        if not swaps: dvf = 1
        if policy == "now":
            steps = min(max(dvf, 1), 4)
        else:
            owed += dvf
            steps = min(owed, 4)
            if steps > 1 and last_work > thr and owed <= cap: steps = 1
            owed -= steps
            lag_max = max(lag_max, owed)
        work = base[i % len(base)] + (steps - 1) * X
        i += 1
        last_work = work
        t = next_swap(t + (work + tail) / LINE)
        swaps.append(t * LINE); works.append(work); steps_log.append(steps)
    gaps = [swaps[k + 1] - swaps[k] for k in range(len(swaps) - 1)]
    return dict(pics=len(swaps) / (swaps[-1] / 1000), mean_work=st.mean(works), max_work=max(works), k2=sum(s > 1 for s in steps_log) / len(steps_log) * 100,
                gaps40=sum(g > 30 for g in gaps), lag=lag_max)
if __name__ == "__main__":
    base = base_series("build/sim/replay2_log.txt")[:]
    print("base frames:", len(base), "mean %.1f max %.1f" % (st.mean(base), max(base)))
    print(f"{'scenario':38} {'policy':7} {'pics/s':>7} {'mean ms':>8} {'max ms':>7} {'k>1 %':>6} {'gaps>30ms/min':>14} {'lag':>4}")
    for label, shift in (("replay window as logged (18.1 ms mean)", 0.0), ("-1.0 ms on every frame (17.1)", -1.0), ("-2.0 ms (16.1)", -2.0), ("-3.0 ms (15.1)", -3.0), ("+1.0 ms (19.1)", 1.0)):
        b = [x + shift for x in base]
        for pol in ("now", "smooth"):
            r = run(b, pol)
            print(f"{label:38} {pol:7} {r['pics']:7.1f} {r['mean_work']:8.2f} {r['max_work']:7.1f} {r['k2']:6.1f} {r['gaps40']:14d} {r['lag']:4d}")
