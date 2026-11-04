#!/bin/sh
# Host test of the Screamer 2 car physics (firmware/s2car.c, float32) against reference traces: OpenScreamer's
# converted game routines (x87, 53-bit sums) driven by the same scripted keys, one CSV row per 20 ms step
# (README of the reference folder: columns and scenarios). The firmware's car is built natively in Docker with
# the firmware's track.c on the track image of tools/s2/make_track_s2.py and run through the same scenario; for each
# quantity the largest and the mean difference from the reference over the steps are printed
# (tools/s2/check_car.c). "Flat" scenarios replace the ground by a level world of one road kind and take the walls
# and road points away, which tests the car alone.
# Usage: tools/s2/check_car.sh [game folder] [reference folder] [scenario ...]
#   game folder       the unzipped original, default $GAME_DIR or "C:/GOG Games/Screamer 2"
#   reference folder  default $REF_DIR or ../ref next to the repository: scn/<name>.txt and traces/<name>.t<track>
#                     [.flat<kind>].team<T>.model<M>.dt20.csv (the traces hold the game's numbers: never committed)
#   scenario          names as in scn/ (a_full_throttle, b_steer_lr, c_brake, d_offroad, e_wall, f_jump, a_flat,
#                     b_flat, c_flat, g_manual, d_surface); default: all that have a trace
# Everything it writes stays in build/s2/ (images, our traces as car_<name>.csv, the log car_check.txt).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
ref=${2:-${REF_DIR:-$root/../ref}}
[ $# -gt 2 ] && { shift 2; names="$*"; } || names="a_full_throttle b_steer_lr c_brake d_offroad e_wall f_jump a_flat b_flat c_flat g_manual d_surface"
out="$root/build/s2"
src="$out/carsrc"
mkdir -p "$out"
ls "$ref"/traces*/*.dt20.csv > /dev/null 2>&1 || { echo "no reference traces in $ref/traces"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub

# the images (the car models, the ground, the walls and road points): one per track used
for n in 1 3 4 7; do
    [ -f "$out/ref$n.bin" ] || "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$out/ref$n.bin" > /dev/null
done

rm -rf "$src"
mkdir -p "$src"
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

if command -v cygpath >/dev/null 2>&1; then
    mount=$(cygpath -wa "$root/build"); game_mount=$(cygpath -wa "$game"); ref_mount=$(cygpath -wa "$ref")
else
    mount=$root/build; game_mount=$game; ref_mount=$ref
fi
LIST="track.c geom.c setup.c raster.c blit.c dlog.c crc32.c dat.c console.c car.c drive.c opp.c ghost.c efx.c scenery.c sound.c speech.c menu.c champ.c intro.c hud.c tb_hud.c tb_spl.c tb_mmp.c s2params.c s2car.c"

# the runs: one line each into a script for the container
script="$out/car_runs.sh"
: > "$script"
seen=""
for name in $names; do
    for t in "$ref"/traces*/$name.t*.dt20.csv; do
        [ -f "$t" ] || continue
        base=$(basename "$t" .dt20.csv)
        case " $seen " in *" $base "*) continue;; esac
        seen="$seen $base"
        rel=${t#"$ref"/}
        track=$(echo "$base" | sed 's/.*\.t\([0-9]\)\..*/\1/')
        team=$(echo "$base" | sed 's/.*\.team\([0-9]\)\..*/\1/')
        model=$(echo "$base" | sed 's/.*\.model\([0-9]\)$/\1/')
        flat=$(echo "$base" | sed -n 's/.*\.flat\([0-9]*\)\..*/\1/p'); [ -n "$flat" ] || flat=-1
        img=1; case $track in 3|4|7) img=$track;; esac
        echo "echo '== $base'; ./check_car /game ref$img.bin /ref/scn/$name.txt /ref/$rel $track $team $model $flat car_$base.csv || echo FAILED" >> "$script"
    done
done

MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/b" -v "$game_mount:/game:ro" -v "$ref_mount:/ref:ro" --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2/carsrc && gcc -O2 -w -o ../check_car check_car.c $LIST -lm -Wl,--wrap=track_ground_s2,--wrap=track_dirs,--wrap=track_walls &&
    cd /b/s2 && sh car_runs.sh" 2>&1 | tee "$out/car_check.txt"
