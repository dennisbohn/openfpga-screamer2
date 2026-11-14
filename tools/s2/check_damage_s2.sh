#!/bin/sh
# Host test of the damage a wall's knock does with DAMAGES on (firmware/s2car.c knock(), STARTL.EXE 0x19aaa): the original multiplies EVERY knock's force by the level's row of
# MISC.DTR's DAMAGES (pro 5, normal 2.5, rookie 1; 0x19b7b: force x [0x11cce0 + 4 x [0x130d4c]]) after the side's count and mark, which go by the force as it came; so the damage a wall
# leaves on the player's car and the computers' goes by the level. The same run (tools/s2/car_scenarios/wall_damage.txt: full throttle, a flick at 2 s into a wall) is made with the
# scale at 1, 2.5 and 5 and the car's damage, its corners' and the sides' counts and marks are compared with the formulas of 0x19aaa (the damage grows by share x force x scale / 50000
# a corner, / 100000 the car's, clamped at 2): the damage must go with the scale (5 x where nothing is clamped), the sides' counts and the marks must not.
# The negative control is the same run with the firmware's s2car.c of the commit before this change (BASE; its knock leaves a wall's scale at 1): the damage then does not go with the
# scale, which the same verdict calls a failure.
# Usage: tools/s2/check_damage_s2.sh [game folder]       default $GAME_DIR or "C:/GOG Games/Screamer 2". Everything it writes stays in build/s2/. Exit code 0 if it passed.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
BASE=92b405f
out="$root/build/s2"
src="$out/damagesrc"
base="$out/damagebase"
[ -f "$game/DATA/GAME.DAT" ] || { echo "$game does not look like Screamer 2's folder"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
mkdir -p "$out/scn"
cp "$root"/tools/s2/car_scenarios/*.txt "$out/scn/"
for n in 1 4; do
    [ -f "$out/car$n.bin" ] || S2CARS=1:1,1:2,1:3,1:4,1:5 "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$out/car$n.bin" > /dev/null
done
rm -rf "$src" "$base"
mkdir -p "$src" "$base"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/check_car.c" "$src/"
"$py" -I - "$src/hw.h" <<'PY'
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
sed -i 's|"../../firmware/|"|' "$src/check_car.c"
cp "$src"/* "$base/"
git -C "$root" show "$BASE:firmware/s2car.c" > "$base/s2car.c"
git -C "$root" show "$BASE:firmware/s2car.h" > "$base/s2car.h"
printf 'float s2car_damage_scale = 1.0f;\n' > "$base/shim.c"          # (the commit before has no such variable: the test's own, which nothing reads)
printf 'extern float s2car_damage_scale;\n' > "$base/shim.h"
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); game_mount=$(cygpath -wa "$game"); else mount=$root/build; game_mount=$game; fi
LIST="track.c geom.c setup.c raster.c blit.c dlog.c crc32.c dat.c console.c car.c tb_hud.c tb_mmp.c s2params.c s2car.c"
WRAP="-Wl,--wrap=track_ground_s2,--wrap=track_dirs,--wrap=track_walls,--wrap=sinf,--wrap=cosf,--wrap=atanf"
script="$out/damage_runs.sh"
: > "$script"
for scale in 1 2.5 5; do
    echo "echo '== knock new track 1 scale $scale'; CHECK_CAR_KNOCK=1 CHECK_CAR_DAMAGE=$scale ./check_damage_new /game car1.bin scn/wall_damage.txt - 1 0 1 -1 | grep '^damage'" >> "$script"
done
for n in 1 4; do
    for scale in 1 2.5 5; do
        for who in new base; do
            echo "echo '== run $who track $n scale $scale'; CHECK_CAR_DAMAGE=$scale ./check_damage_$who /game car$n.bin scn/wall_damage.txt - $n 0 1 -1 | grep '^damage'" >> "$script"
        done
    done
done
MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2/damagesrc && gcc -O2 -w -o ../check_damage_new check_car.c $LIST -lm $WRAP &&
    cd /b/s2/damagebase && gcc -O2 -w -DCHECK_CAR_BASE -include shim.h -o ../check_damage_base check_car.c shim.c $LIST -lm $WRAP &&
    cd /b/s2 && sh damage_runs.sh" > "$out/damage_check.txt" 2>&1 || { cat "$out/damage_check.txt"; exit 1; }
"$py" -I - "$out/damage_check.txt" <<'PY'
import sys

runs, cur = {}, None
for line in open(sys.argv[1]):
    if line.startswith("== "):
        cur = tuple(line.split()[1:])
        continue
    if line.startswith("damage:"):
        f = line.split()
        runs[(cur[0], cur[1], cur[3], float(cur[5]))] = {"car": float(f[2]), "corners": [float(x) for x in f[4:8]], "sides": [float(x) for x in f[9:13]], "marks": f[14]}

bad = 0
# 1. the formulas of 0x19aaa (STARTL.EXE 0x19aaa .. 0x19c57, read once more here): knock A (side 0, corner 0, share 0, force 40000) fills side 0's count to 1 and, the force being over 2000, puts
# its mark 0x2000 (the counts and the marks go by the force as it came); knock B (side 1, corner 2, share 0.4, force 15000) adds (1 - 0.4) x 15000 / 20000 = 0.45 to side 1's count and, the
# force times the scale, share x force / 50000 to corner 2's damage, share x force / 100000 to the car's (a front corner) and (1 - share) x force / 100000 more (side 1)
for scale in (1.0, 2.5, 5.0):
    r = runs[("knock", "new", "1", scale)]
    want = {"car": 0.4 * 15000 * scale / 100000 + 0.6 * 15000 * scale / 100000, "corners": [0.0, 0.0, 0.4 * 15000 * scale / 50000, 0.0], "sides": [1.0, 0.45, 0.0, 0.0]}
    ok = abs(r["car"] - want["car"]) < 1e-5 and all(abs(a - b) < 1e-5 for a, b in zip(r["corners"], want["corners"])) and all(abs(a - b) < 1e-5 for a, b in zip(r["sides"], want["sides"])) and r["marks"] == "2000"
    print("knock at the scale %g: car %.5f (want %.5f), corner 2 %.5f (want %.5f), side counts %s, marks %s: %s" % (scale, r["car"], want["car"], r["corners"][2], want["corners"][2], r["sides"], r["marks"], "ok" if ok else "NOT AS 0x19aaa HAS IT"))
    bad += not ok
# 2. the wall: the run into a wall at the three scales. The firmware's damage goes with the scale (the car is hurt more, and then drives differently, so the numbers are not simple
# multiples); the negative control, the commit before, leaves a wall's scale at 1: the same run, the same damage at all three
for track in (1, 4):
    n = [runs[("run", "new", str(track), s)] for s in (1.0, 2.5, 5.0)]
    o = [runs[("run", "base", str(track), s)] for s in (1.0, 2.5, 5.0)]
    goes = 0 < n[0]["car"] < n[1]["car"] < n[2]["car"]
    flat = o[0] == o[1] == o[2] and o[0]["car"] > 0
    print("track %d wall run, the car's damage at the scale 1 / 2.5 / 5: firmware %.4f %.4f %.4f (goes with the scale: %s); commit before %.4f %.4f %.4f (the same at all three: %s)" %
          (track, n[0]["car"], n[1]["car"], n[2]["car"], goes, o[0]["car"], o[1]["car"], o[2]["car"], flat))
    bad += (not goes) + (not flat)
print("DAMAGES scale of a wall's knock:", "PASS" if not bad else "FAIL")
sys.exit(1 if bad else 0)
PY
