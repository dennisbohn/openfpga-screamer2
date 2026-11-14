#!/bin/sh
# Host test of the computers' set-ups (menus row 12; docs/screamer2/driving.md "The set-up"): the numbers the cars of firmware/race.c are made of in the arcade (the standard set-up) and in
# the championship (the original's 0x14e20 tunes the computers' cars where the mode word has no bit 1) against the original's routines (OpenScreamer's, tools/s2/ref_trace/ref_run.sh race
# with OS_S2GAME=1 and 2: every car's whole record after every frame), all seven tracks, with a control that the two modes differ. See tools/s2/check_setup_ai_s2.c.
# Usage: tools/s2/check_setup_ai_s2.sh [game folder] [reference folder]
#   game folder       the unzipped original, default $GAME_DIR or "C:/GOG Games/Screamer 2"
#   reference folder  default $REF_WORK or build/ref of this repository: build-src/ holds the reference's program built by tools/s2/ref_trace/ref_run.sh build (with s2_ref.patch of this
#                     repository: it lets OS_S2GAME set the kind of game of the test race), the traces go to traces_race/
# Everything it writes stays in build/s2/ (images, the sources) and the reference folder (the traces, which hold the game's numbers: never commit them).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
ref=${2:-${REF_WORK:-$root/build/ref}}
out="$root/build/s2"
src="$out/aisetupsrc"
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
exe="$ref/build-src/port/build/screamer-remastered.exe"
[ -f "$exe" ] || exe="$ref/build-src/port/build/screamer-remastered"
[ -f "$exe" ] || { echo "no reference program in $ref/build-src/port/build (tools/s2/ref_trace/ref_run.sh build)"; exit 1; }
mkdir -p "$out" "$ref/traces_race"
export S2_GAME="$game" REF_WORK="$ref"
for n in 1 2 3 4 5 6 7; do
    [ -f "$out/aisetup$n.bin" ] || S2CARS=1:2,2:3,3:4,4:1 "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$out/aisetup$n.bin" > /dev/null
    for mode in 1 2; do          # the race of four cars (level 1, the player's car standing) for 3 frames, in the kind of game 1 and 2
        [ -f "$ref/traces_race/aisetup.t$n.g$mode.bin" ] || {
            OS_S2GAME=$mode bash "$root/tools/s2/ref_trace/ref_run.sh" race "$n" 1 park 3 1 > /dev/null
            cp "$ref/traces_race/race.t$n.L1.park.c1.ref.bin" "$ref/traces_race/aisetup.t$n.g$mode.bin"
        }
    done
done
rm -rf "$src"
mkdir -p "$src"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/check_setup_ai_s2.c" "$src/"
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
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); game_mount=$(cygpath -wa "$game"); ref_mount=$(cygpath -wa "$ref"); else mount=$root/build; game_mount=$game; ref_mount=$ref; fi
LIST="road.c track.c geom.c setup.c raster.c car.c blit.c dlog.c crc32.c dat.c console.c tb_hud.c tb_mmp.c s2car.c s2params.c s2ghost.c"
runs=""
for n in 1 2 3 4 5 6 7; do
    runs="$runs && echo track $n && ./check_setup_ai_s2 /b/s2/aisetup$n.bin $n /ref/traces_race/aisetup.t$n.g1.bin /ref/traces_race/aisetup.t$n.g2.bin"
done
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -e S2_GAME=/game -v "$mount:/b" -v "$game_mount:/game:ro" -v "$ref_mount:/ref:ro" --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2/aisetupsrc && gcc -O2 -g -w -fsanitize=address,undefined -fno-sanitize-recover -o ../check_setup_ai_s2 check_setup_ai_s2.c $LIST -lm &&
    cd /b/s2 && true $runs"
