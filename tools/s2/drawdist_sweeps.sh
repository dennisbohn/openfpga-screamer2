#!/bin/sh
# The road sweeps of docs/screamer2/draw_distance.md, then the report (tools/s2/drawdist_report.py): Screamer 2
# tracks 1..7, near chase camera, 0 / 1 / 4 cars (the player's and three opponents), 160 frames along each road,
# the firmware's far plane, reach and level of detail:
#   before  without the cells' visibility lists (framestats.sh build with NOVIS=1: everything in reach is looked at)
#   after   as the firmware has them now (the camera cell's list names the cells that are looked at)
# Needs: build/s2/track<n>.bin (python -I tools/s2/make_track_s2.py <Screamer 2 folder> n build/s2/track<n>.bin) and,
# for the model, the S1 start frames as tools/s2/perf_sweeps.sh needs them (build/sim/t1..6.log,
# build/stats/start1..6.csv). About 1 minute. Output: build/stats/dd/*.csv, drawdist_report.md.
set -e
cd "$(dirname "$0")/../.."
out=build/stats/dd
mkdir -p $out
sweep() { sh tools/s2/framestats.sh sweep "$@" 2>&1 | grep "road points" || true; }
for v in before after; do
    if [ $v = before ]; then NOVIS=1 sh tools/s2/framestats.sh build; else sh tools/s2/framestats.sh build; fi
    for n in 1 2 3 4 5 6 7; do
        sweep s2 build/s2/track$n.bin ${v}_t$n $out/${v}_t$n.csv -160 0,1,4
    done
done
python -I tools/s2/drawdist_report.py build
