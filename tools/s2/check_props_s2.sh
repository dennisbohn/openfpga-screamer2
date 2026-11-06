#!/bin/sh
# The obstacles (cones, fences, chests, crates) and the sparks of the race (firmware/s2props.c, docs/development.md "Obstacles and sparks") on the PC in Docker, natively under the
# address and undefined-behaviour sanitizers: tools/s2/check_props_s2.c has the firmware's own code run
#   model  the arcade's race, the game's driver driving all four cars, with the props run from the race and without them: the race (every car's whole record but the word the
#          obstacles' count is kept in) is the same at every step; where the obstacles stand, what they do
#   rec    the recording of the running original (tools/s2/dosbox_props_s2.py makes it: Windows, DOSBox in front for about two minutes): the cars' records of every frame go
#          through the firmware's code and the game's obstacle and spark records are compared with it (see the header of the C file), with a negative control
# Usage: tools/s2/check_props_s2.sh model [steps = 6000] [tracks = "1 3 4 6"] [game folder]
#        tools/s2/check_props_s2.sh rec <track> [game folder]            (the recording build/check/dosbox_props/props_t<track>.bin must exist)
#        tools/s2/check_props_s2.sh pic <track> <camera>... [game folder]   pictures (see tools/s2/render_s2.sh's cameras, and 'p' there)
# Everything it writes stays in build/s2/. Exit code 0 if everything passed.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
mode=${1:-model}
shift || true
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
out="$root/build/s2"
src="$out/propssrc"
mkdir -p "$out"
case $mode in
model) steps=${1:-6000}; tracks=${2:-"1 3 4 6"}; game=${3:-${GAME_DIR:-C:/GOG Games/Screamer 2}} ;;
rec) tracks=$1; game=${2:-${GAME_DIR:-C:/GOG Games/Screamer 2}} ;;
*) echo "usage: check_props_s2.sh model|rec ..."; exit 2 ;;
esac
for n in 1 2 3 4 5 6 7; do
    [ -f "$out/car$n.bin" ] || S2CARS=1:1,1:2,1:3,1:4,1:5 "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$out/car$n.bin" > /dev/null
done
rm -rf "$src"
mkdir -p "$src"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/check_props_s2.c" "$src/"
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
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); game_mount=$(cygpath -wa "$game"); else mount=$root/build; game_mount=$game; fi
LIST="road.c track.c geom.c setup.c raster.c blit.c dlog.c crc32.c dat.c console.c car.c drive.c opp.c ghost.c efx.c scenery.c sound.c speech.c menu.c champ.c intro.c hud.c tb_hud.c tb_spl.c tb_mmp.c tb_gfx.c track_build.c smg.c s2env.c s2car.c s2params.c race.c s2ghost.c s2props.c"
run=""
case $mode in
model) for n in $tracks; do run="$run ./check_props_s2 model /b/s2/car$n.bin $n $steps &&"; done ;;
rec) recd="${PROPSREC:-$root/build/check/dosbox_props}"
     bin="/b/check/$(basename "$recd")/props_t$tracks.bin"
     fb=$((24 + 4 * 3264 + 60 * 160 + 2796 + 64 * 160))      # a frame of the recording (tools/s2/dosbox_props_s2.py)
     [ -f "$recd/props_t$tracks.bin" ] || { echo "no recording $recd/props_t$tracks.bin (python -I tools/s2/dosbox_props_s2.py $tracks)"; exit 1; }
     [ $(( $(wc -c < "$recd/props_t$tracks.bin" | tr -d ' ') % fb )) -eq 0 ] || { echo "the recording's size is no whole number of frames of $fb bytes"; exit 1; }
     run="./check_props_s2 rec /b/s2/car$tracks.bin $tracks $bin $fb &&
          if SHIFT=3 ./check_props_s2 rec /b/s2/car$tracks.bin $tracks $bin $fb > /dev/null 2>&1; then echo 'NEGATIVE CONTROL NOT FOUND'; exit 1; else echo 'negative control (the cars three frames ahead of the obstacles) found: it fails'; fi &&" ;;
esac
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -v "$mount:/b" -v "$game_mount:/game:ro" -e S2_GAME=/game --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2/propssrc && gcc -O1 -g -w -fsanitize=address,undefined -fno-sanitize=shift -fno-sanitize-recover -o ../check_props_s2 check_props_s2.c $LIST -lm &&
    cd /b/s2 && $run true"
