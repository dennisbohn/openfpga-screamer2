#!/bin/sh
# What the rear-view mirror's pass adds to a frame of the race, counted on the PC (docs/screamer2/hud.md "The views from the car", tools/s2/mirrorstats.c):
#   tools/s2/mirrorstats.sh [steps [every [tracks...]]]     default 3000 steps (a minute), a frame every 10, tracks 1 .. 7
# Needs build/s2/track<n>.bin (python -I tools/s2/make_track_s2.py <game folder> n build/s2/track<n>.bin) and the game's folder for the physics ($GAME_DIR, default C:/GOG Games/Screamer 2).
# The firmware is never changed; the counters live in a copy (tools/s2/perf_patch.py). Writes build/stats/mirror_t<n>.csv and prints the means.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
st="$root/build/stats"
steps=${1:-3000}
every=${2:-10}
[ $# -gt 2 ] && { shift 2; tracks="$*"; } || tracks="1 2 3 4 5 6 7"
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); else mount=$root/build; fi
img=verilator/verilator:v5.050
game=${GAME_DIR:-C:/GOG Games/Screamer 2}
if command -v cygpath >/dev/null 2>&1; then game_mount=$(cygpath -wa "$game"); else game_mount=$game; fi
src="$st/src_m"
rm -rf "$src"
mkdir -p "$src"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root"/tools/s2/perf.h "$root"/tools/s2/perf.c "$root"/tools/s2/mirrorstats.c "$src/"
python -I - "$src/hw.h" <<'PY'
import sys
p = sys.argv[1]
t = open(p).read()
t = t[:t.index("/* Cache maintenance")] + """static inline void dcache_invalidate(void) {}
static inline void icache_sync(void) {}
#define csr_read(csr)   0u
static inline uint32_t rdcycle(void) { return 0; }
static inline int32_t fixmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
static inline uint32_t opaque(uint32_t x) { return x; }
static inline float opaquef(float x) { return x; }
"""
open(p, "w").write(t)
PY
python -I "$root/tools/s2/perf_patch.py" "$src"
LIST="road.c track.c geom.c setup.c raster.c car.c blit.c dlog.c crc32.c dat.c console.c tb_hud.c tb_mmp.c s2car.c s2params.c race.c s2ghost.c s2cam.c s2cockpit.c"
MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/b" --entrypoint sh $img -c "cd /b/stats/src_m && gcc -O2 -w -include perf.h -DRS_TRACE -o ../mirrorstats_s2 mirrorstats.c perf.c $LIST -lm"
for n in $tracks; do
    cp "$root/build/s2/track$n.bin" "$st/track$n.bin"
    MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/b" -v "$game_mount:/game:ro" -e S2_GAME=/game --entrypoint sh $img -c "cd /b/stats && ./mirrorstats_s2 track$n.bin t$n mirror_t$n.csv $steps $every $n" 2>&1 | grep -v "^$" || true
done
python -I - "$st" $tracks <<'PY'
import csv, sys
st = sys.argv[1]
# the CPU's part of a pass by the constants of tools/s2/perf_race.py (counted in the simulation of the race on the grid): the cull 25,000 + 126 a cell looked at + 150 a placement tested
# + 110 a piece in view, 920 cycles a static piece and 6,300 a car's block handed on; at 74.25 MHz
def cpu(r, p):
    pieces, objb = float(r[p + "pieces"]), float(r[p + "obj_blocks"])
    cull = 25000 + 126 * float(r[p + "cells"]) + 150 * float(r[p + "tested"]) + 110 * float(r[p + "visible"])
    return (cull + 920 * (pieces - objb) + 6300 * objb) / 74250.0
print("what the mirror's pass adds to a frame of the race (tools/s2/mirrorstats.sh; the frames after step 200, one in 10, the game's driver driving all four cars)")
print("track frames | main: pieces faces pixels | mirror: pieces faces pixels | mirror / main: pieces faces pixels | CPU ms (model): main, mirror")
for n in sys.argv[2:]:
    rows = [r for r in csv.DictReader(open("%s/mirror_t%s.csv" % (st, n))) if int(r["step"]) > 200]
    def m(k): return sum(float(r[k]) for r in rows) / max(len(rows), 1)
    a = [m("pieces"), m("faces"), m("px_drawn")]
    b = [m("m_pieces"), m("m_faces"), m("m_px_drawn")]
    ca = sum(cpu(r, "") for r in rows) / len(rows)
    cb = sum(cpu(r, "m_") for r in rows) / len(rows)
    print("%5s %6d | %7.1f %7.0f %8.0f | %7.1f %7.0f %8.0f | %5.2f %5.2f %5.2f | %5.2f %5.2f" % (n, len(rows), a[0], a[1], a[2], b[0], b[1], b[2], b[0] / a[0], b[1] / a[1], b[2] / a[2], ca, cb))
PY
