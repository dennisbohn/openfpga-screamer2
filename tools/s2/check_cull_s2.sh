#!/bin/sh
# The cull must hand the same pieces to the geometry unit, in the same order, as before a change of it
# (docs/screamer2/performance.md, section 7). Counting copies of the firmware of a reference commit and of this tree
# (tools/s2/framestats.sh build, perf_patch.py: a hash of the sorted list of pieces, and the pieces, faces, triangles
# and vertices counted by the C models) draw the same frames: a race of each track on the PC (the game's driver drives
# the player's car too, a frame every 15 steps) and the road sweeps with 0, 1 and 4 cars; every frame's hash and
# counters must be equal.
# Usage: tools/s2/check_cull_s2.sh <reference commit> [tracks]       (default tracks 1 2 3 4 5 6 7)
# Needs: build/s2/track<n>.bin (python -I tools/s2/make_track_s2.py <Screamer 2 folder> n build/s2/track<n>.bin),
# the game's folder for the physics (GAME_DIR, default C:/GOG Games/Screamer 2). About 2 minutes.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
ref=$1
shift
tracks=${*:-1 2 3 4 5 6 7}
[ -n "$ref" ] || { echo "usage: check_cull_s2.sh <reference commit> [tracks]"; exit 2; }
cd "$root"
old=build/ref_tree
rm -rf "$old"
mkdir -p "$old/build/stats/out" "$old/build/s2"
git archive "$ref" | tar -x -C "$old"
# the reference tree is counted with this tree's tools (its own perf_patch.py is the one of its time)
cp tools/s2/perf.h tools/s2/perf.c tools/s2/perf_patch.py tools/s2/framestats.sh tools/s2/racestats.c tools/s2/framestats.c "$old/tools/s2/"
python -I - "$old/tools/s2/perf_patch.py" <<'PY'
import sys
p = sys.argv[1]
t = open(p, newline="").read()
# the cull's own counters need the new cull's text: the reference tree has the old one
i = t.find('        ("            if(o1 | o2 | o3 | o4 | o5 | o6) continue;')
j = t.find('        ("    uint32_t t_pieces = now();')
if i >= 0 and j > i:
    t = t[:i] + t[j:]
open(p, "w", newline="").write(t)
PY
sh "$old/tools/s2/framestats.sh" build > /dev/null
sh tools/s2/framestats.sh build > /dev/null
bad=0
for n in $tracks; do
    img=build/s2/track$n.bin
    cp "$img" "$old/build/s2/"
    sh "$old/tools/s2/framestats.sh" race "$old/$img" r$n "$old/build/stats/out/race$n.csv" 6000 15 $n > /dev/null 2>&1
    sh tools/s2/framestats.sh race "$img" r$n build/stats/out/race$n.csv 6000 15 $n > /dev/null 2>&1
    sh "$old/tools/s2/framestats.sh" sweep s2 "$old/$img" s$n "$old/build/stats/out/sweep$n.csv" -120 0,1,4 > /dev/null 2>&1
    sh tools/s2/framestats.sh sweep s2 "$img" s$n build/stats/out/sweep$n.csv -120 0,1,4 > /dev/null 2>&1
    python -I - "$n" "$old/build/stats/out" build/stats/out <<'PY' || bad=$((bad + 1))
import csv, sys
n, a, b = sys.argv[1:4]
cols = ("vis_hash", "ts_pieces", "ts_faces", "ts_triangles", "ts_vertices", "ts_objects", "gu_pieces", "faces_row", "tris_tex", "px_drawn")
fail = 0
for kind in ("race", "sweep"):
    ra = list(csv.DictReader(open(f"{a}/{kind}{n}.csv")))
    rb = list(csv.DictReader(open(f"{b}/{kind}{n}.csv")))
    diff = sum(1 for x, y in zip(ra, rb) if any(x[c] != y[c] for c in cols))
    mean = lambda r, c: sum(float(x[c]) for x in r) / max(1, len(r))
    print(f"track {n} {kind:5}: {len(ra)} / {len(rb)} frames, {diff} differ; placements tested {mean(ra, 'tested'):.0f} -> {mean(rb, 'tested'):.0f}, cells looked at {mean(ra, 'groups'):.0f} -> {mean(rb, 'groups'):.0f}")
    fail += diff + (len(ra) != len(rb))
sys.exit(1 if fail else 0)
PY
done
if [ $bad -ne 0 ]; then echo "FAIL: the cull changed the pieces drawn on $bad track(s)"; exit 1; fi
echo "ok: the same pieces in the same order in every frame"
