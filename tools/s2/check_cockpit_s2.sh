#!/bin/sh
# Host test of the views from the car (firmware/s2cockpit.c, s2dash.c, tb_hud.c's boxes; docs/screamer2/hud.md "The views from the car"): tools/s2/check_cockpit_s2.c natively in Docker against
# an independent reading of the game's files in Python (tools/s2/check_cockpit_s2.py: the pictures, CRUSCOTTI, the needles of 0x46755 in double precision) and, if it exists, against the
# recording of the original's camera records (tools/s2/dosbox_cockpit.py: build/check/dosbox_cockpit/cockpit_t1.bin and .crusc.bin). The negative control (NEGATIVE=1) must fail.
# Usage: tools/s2/check_cockpit_s2.sh [game folder]      default $GAME_DIR or "C:/GOG Games/Screamer 2"; about 1 minute
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
py=python3
"$py" -c "" 2>/dev/null || py=python
work="$root/build/s2/cockpit"
mkdir -p "$work" "$root/build/s2/cockpitsrc"
"$py" -I "$root/tools/s2/check_cockpit_s2.py" "$game" "$work"
src="$root/build/s2/cockpitsrc"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/check_cockpit_s2.c" "$src/"
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
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); else mount=$root/build; fi
LIST="road.c track.c geom.c setup.c raster.c car.c drive.c opp.c ghost.c efx.c scenery.c sound.c speech.c blit.c dlog.c crc32.c dat.c console.c menu.c champ.c intro.c tb_hud.c tb_spl.c tb_mmp.c hud.c s2car.c s2params.c s2ghost.c race.c s2cam.c s2cockpit.c"
rec=/b/check/dosbox_cockpit/cockpit_t1.bin
crusc=/b/check/dosbox_cockpit/cockpit_t1.crusc.bin
args="/b/s2/cockpit"
[ -f "$root/build/check/dosbox_cockpit/cockpit_t1.bin" ] && args="$args $rec $crusc"
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -e S2_GAME=/game -v "$mount:/b" --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2/cockpitsrc && gcc -O2 -g -w -fsanitize=address,undefined -fno-sanitize-recover -o ../check_cockpit_s2 check_cockpit_s2.c $LIST -lm &&
    cd /b/s2 && ./check_cockpit_s2 $args"
echo "the negative control (it must fail):"
if MSYS_NO_PATHCONV=1 docker run --rm -e NEGATIVE=1 -e ASAN_OPTIONS=detect_leaks=0 -v "$mount:/b" --entrypoint sh verilator/verilator:v5.050 -c "cd /b/s2 && ./check_cockpit_s2 $args" > "$work/negative.txt"; then
    echo "NEGATIVE CONTROL PASSED: the checks do not check"; exit 1
fi
tail -1 "$work/negative.txt"
if [ -f "$root/build/check/dosbox_cockpit/cockpit_t1.shots.txt" ]; then
    "$py" -I "$root/tools/s2/check_cockpit_shots.py" "$game" "$root/build/check/dosbox_cockpit" 1
    if NEGATIVE=1 "$py" -I "$root/tools/s2/check_cockpit_shots.py" "$game" "$root/build/check/dosbox_cockpit" 1 > "$work/negative_shots.txt"; then
        echo "NEGATIVE CONTROL OF THE SCREENSHOTS PASSED: the checks do not check"; exit 1
    fi
    echo "the screenshots' negative control (a needle at the wrong part) fails: $(grep -c FAIL "$work/negative_shots.txt") shots"
fi
