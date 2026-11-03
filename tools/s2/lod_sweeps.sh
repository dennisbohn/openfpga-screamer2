#!/bin/sh
# The road sweeps of docs/screamer2/performance.md, "Detail levels and LOD at HIGH", then the report
# (tools/s2/lod_report.py). Screamer 2 tracks 1..7, near chase camera, 0 / 1 / 4 cars (the player's and
# three opponents), the rear-view mirror's pass too in the first two sets:
#   c   HIGH as the original has it: far plane TRACK_DATA's, pieces to far + 8192, every block at level 0
#   l   the same with the original's level of detail (LOD=3: placed meshes and cars)
#   ls  placed meshes only (LOD=1); lc  cars only (LOD=2)
#   dc  MEDIUM: far plane x 0.8, pieces to 0.8 far + 8192, no level of detail; dl  MEDIUM with it
# Needs: tools/s2/framestats.sh build (the counters, with perf_patch.py's level-of-detail edits);
# build/s2/track<n>.bin (tools/s2/make_track_s2.py <Screamer 2 folder> n build/s2/track<n>.bin); for the model
# the S1 start frames as tools/s2/perf_sweeps.sh needs them. Output: build/stats/lod/*.csv, lod_report.md.
set -e
cd "$(dirname "$0")/../.."
out=build/stats/lod
mkdir -p $out
sweep() { sh tools/s2/framestats.sh sweep "$@" 2>&1 | grep "road points" || true; }
for n in 1 2 3 4 5 6 7; do
    far=50000; [ $n = 4 ] && far=60000
    reach=$((far + 8192))
    dfar=$((far * 8 / 10)); dreach=$((dfar + 8192))
    img=build/s2/track$n.bin
    MIRROR=1 FAR=$far REACH=$reach LOD=0 sweep s2 $img s2c_t$n $out/s2c_t$n.csv -160 0,1,4
    MIRROR=1 FAR=$far REACH=$reach LOD=3 sweep s2 $img s2l_t$n $out/s2l_t$n.csv -160 0,1,4
    FAR=$far REACH=$reach LOD=1 sweep s2 $img s2ls_t$n $out/s2ls_t$n.csv -160 0,1,4
    FAR=$far REACH=$reach LOD=2 sweep s2 $img s2lc_t$n $out/s2lc_t$n.csv -160 0,1,4
    FAR=$dfar REACH=$dreach LOD=0 sweep s2 $img s2dc_t$n $out/s2dc_t$n.csv -160 0,1,4
    FAR=$dfar REACH=$dreach LOD=3 sweep s2 $img s2dl_t$n $out/s2dl_t$n.csv -160 0,1,4
done
python -I tools/s2/lod_report.py build
