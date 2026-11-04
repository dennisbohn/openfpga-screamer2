"""Frame time of the race along a lap, before and after the changes of docs/screamer2/performance.md section 7.

Usage: python -I tools/s2/perf_race.py <old stats dir> <new stats dir> [build dir] [full|pack]
  The stats dirs hold, for n = 1..7, either full<n>.csv (tools/s2/framestats.sh race <image> r<n> <dir>/full<n>.csv 12000 10 <n>: the
  game's driver drives all four cars of a race of 12,000 steps = 4 minutes, a frame every 10 steps; the robot is ahead of the pack
  and sees about one car) or pack<n>.csv (tools/s2/framestats.sh sweep s2 <image> s<n> <dir>/pack<n>.csv -400 4: 400 frames along
  the road, the near chase camera, the player and three computers ahead of him all in view: the case that matches the Pocket), made
  by the counting build of the old firmware and of this one. The build dir has what tools/s2/perf_model.py needs (sim/t1..6.log,
  stats/start1..6.csv).

What it does. The hardware's part of a frame is the model of perf_model.py (geometry unit and rasterizer from the counters, the
time before the first piece and the tail). The CPU's part is built from what the simulation of the race on the grid (track 4,
70 frames after GO, the frame timeline of the old and the new firmware) measured:
  before the first piece   pre (the player's step, the scene, the sound, the preparation) + the cull
  the pieces               920 cycles a static piece, 6,300 a block of a car (drawn, pushed, waits for the buffer not counted)
  the computers' steps     3 x their step, after the last piece (all of it) -- or hidden behind the hardware where the
                           hardware is the bound (the new firmware's slices run while the buffer is full)
  cull                     25,000 + per cell: 106 (old walk: one reach test each) or 126 (new: the rows' cells, the column test,
                           the reach test, the view test) + 150 per placement tested + 110 per piece in view
The frame is the longer of the hardware's and the CPU's path. Old and new differ by the cull (counters of both builds), the
car step (old 74.3k, new 62k cycles: the simulation's `ph` and `o`), and the new firmware's slices.
These are the study's numbers, not the Pocket's: on tracks 1 and 4 the Pocket's means and worsts are the check (printed).
"""
import csv
import os
import statistics as st
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import perf_model as pm                                  # noqa: E402

PRE_OLD, PRE_NEW = 95_400, 95_400 - 12_000             # player's step 74k -> 62k cycles with the sine/cosine memo, the rest as measured
STEP_OLD, STEP_NEW = 74_300, 62_000                      # one computer's car step
CELL_OLD, CELL_NEW = 106, 126
TAIL_CPU = 40_000                                        # race screen, overlay: after the rasterizer, not in `work`
STATIC, OBJ = 920, 6_300


def load_model(build):
    rows = []
    for n in range(1, 7):
        d = pm.parse_log(f"{build}/sim/t{n}.log")
        d.update(pm.read_csv(f"{build}/stats/start{n}.csv")[0])
        rows.append(d)
    return pm.Model(rows)


def frames(path):
    out = []
    for r in pm.read_csv(path):
        pm.derived(r)
        out.append(r)
    return out


def cull(r, cell):
    return 25_000 + cell * r["groups"] + 150 * r["tested"] + 110 * r["visible"]


def frame(m, r, new):
    pre = PRE_NEW if new else PRE_OLD
    step = STEP_NEW if new else STEP_OLD
    c = cull(r, CELL_NEW if new else CELL_OLD)
    nobj = r["obj_blocks"] + r["multi_blocks"]
    nstat = r["gu_pieces"] - nobj
    cpu_pieces = STATIC * nstat + OBJ * nobj
    hw = m.gu(r)                                           # geometry unit busy
    rs = m.rs(r)
    t_first = pre + c
    t_hw = t_first + max(hw, rs) + m.tail                  # the hardware's path
    handoff = t_first + cpu_pieces
    t_cpu_old = handoff + 3 * step + 6_000                 # the computers' steps after the last piece (+ collide)
    if not new:
        t = max(t_hw, t_cpu_old)
    else:
        # slices run while the buffer is full: the CPU is that much ahead of the hardware; what fits is hidden
        ahead = max(0.0, t_hw - m.tail - handoff)          # the hardware still has this much to do when the last piece is handed on
        hidden = min(3 * step + 6_000, ahead * 0.8)       # (not all of it can be used: the slices come one at a time)
        t_cpu = handoff + 3 * step + 6_000 - hidden
        t = max(t_hw, t_cpu)
    return t, c, t_hw, t_first


def main():
    old_dir, new_dir = sys.argv[1], sys.argv[2]
    build = sys.argv[3] if len(sys.argv) > 3 else "build"
    kind = sys.argv[4] if len(sys.argv) > 4 else "full"
    m = load_model(build)
    print("| track | frames | cull old -> new ms (mean / at the worst frame) | mean ms old -> new | worst ms old -> new | worst frame's place on the line (node) | frames over 16 ms old -> new |")
    print("|---|---|---|---|---|---|---|")
    for n in range(1, 8):
        fo = frames(f"{old_dir}/{kind}{n}.csv")
        fn = frames(f"{new_dir}/{kind}{n}.csv")
        if kind == "pack":
            fo = [r for r in fo if r["kind"] == "road" and int(r["ncars"]) == 4]
            fn = [r for r in fn if r["kind"] == "road" and int(r["ncars"]) == 4]
            for r in fo + fn:
                r["node"] = r["idx"]
        to = [frame(m, r, False) for r in fo]
        tn = [frame(m, r, True) for r in fn]
        ms = pm.MS
        mo = st.mean(t[0] for t in to) * ms
        mn = st.mean(t[0] for t in tn) * ms
        wo = max(range(len(to)), key=lambda i: to[i][0])
        wn = max(range(len(tn)), key=lambda i: tn[i][0])
        over_o = sum(1 for t in to if t[0] * ms > 16.0)
        over_n = sum(1 for t in tn if t[0] * ms > 16.0)
        print(f"| {n} | {len(to)} | {st.mean(t[1] for t in to) * ms:.2f} -> {st.mean(t[1] for t in tn) * ms:.2f} "
              f"({to[wo][1] * ms:.2f} -> {tn[wo][1] * ms:.2f}) | {mo:.1f} -> {mn:.1f} | {to[wo][0] * ms:.1f} -> {tn[wn][0] * ms:.1f} "
              f"| {fo[wo]['node']:.0f} | {over_o} -> {over_n} |")


if __name__ == "__main__":
    main()
