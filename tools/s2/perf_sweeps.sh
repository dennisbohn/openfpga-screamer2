#!/bin/sh
# All road sweeps of docs/screamer2/performance.md, then the model (tools/s2/perf_model.py):
#   Screamer 1 tracks 1..6 (the firmware's view distances), Screamer 2 tracks 1..7 (TRACK_DATA's far plane,
#   pieces out to far plane + 8192 as the original sets them (STARTL.EXE 0x7f6d4; the first version of this
#   study assumed 1.2 times the far plane); and with the S1 distances; and at 0.8 times (MEDIUM); the mirror
#   with a 15,000-unit view).
# NOTE (milestone 2): the study's sweeps came from the counting build as it was then. framestats.sh now builds the
# firmware as it is (the level of detail always on, Screamer 2 images only, version 7): the s1 sweeps, the
# LOD=0/1/2 variants and the start frames are gone. What still runs: the s2 sweeps (framestats.sh sweep s2 ...)
# and, with NOLOD=1 at build, the sweeps without the level of detail. The tables of performance.md are the
# study's record.
# Needs: tools/s2/framestats.sh build; build/check/track<n>.bin (tools/make_track.py, made by
# tools/simcheck.sh); build/s2/track<n>.bin (tools/s2/make_track_s2.py <Screamer 2 folder> n
# build/s2/track<n>.bin); for the model also the S1 start frames: build/sim/t<n>.log (SIM_TRACK=n
# tools/simcheck.sh t<n>) and build/stats/start<n>.csv (tools/s2/framestats.sh start n).
# About 2 minutes. Output: build/stats/out/*.csv, build/stats/out/forecast.md (printed).
set -e
cd "$(dirname "$0")/../.."
export MIRROR=1
sweep() { sh tools/s2/framestats.sh sweep "$@" 2>&1 | grep "road points" || true; }
for n in 1 2 3 4 5 6; do
    sweep s1 build/check/track$n.bin s1_t$n build/stats/out/s1_t$n.csv -160 0,1,4,10 grid
done
for n in 1 2 3 4 5 6 7; do
    far=50000; [ $n = 4 ] && far=60000
    FAR=$far REACH=$((far + 8192)) sweep s2 build/s2/track$n.bin s2_t$n build/stats/out/s2_t$n.csv -160 0,1,4,10 grid
    sweep s2 build/s2/track$n.bin s2s1_t$n build/stats/out/s2s1_t$n.csv -160 0,1,4,10 grid
    far=40000; [ $n = 4 ] && far=48000
    reach=$((far + 8192))
    FAR=$far REACH=$reach sweep s2 build/s2/track$n.bin s2d_t$n build/stats/out/s2d_t$n.csv -160 0,1,4,10 grid
    FAR=15000 REACH=18000 sweep s2 build/s2/track$n.bin s2m_t$n build/stats/out/s2m_t$n.csv -160 0,1,4,10
done
python -I tools/s2/perf_model.py build
