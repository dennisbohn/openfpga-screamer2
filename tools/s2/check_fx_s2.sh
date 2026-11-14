#!/bin/sh
# The wheel smoke and the shadows (firmware/s2fx.c) against the running original's memory: tools/s2/check_fx_s2.c replays a recording of the game's own race
# (tools/s2/dosbox_smoke_s2.py makes it: build/check/dosbox_smoke/smoke_t<track>.bin) through the firmware's code and compares the puffs (which are alive, where they
# are born, where they stand, how long they live) with the game's records, natively in Docker under the address and undefined-behaviour sanitizers.
# Usage: tools/s2/check_fx_s2.sh [track = 1] [game folder]            (the recording must exist: Windows, python -I tools/s2/dosbox_smoke_s2.py <track>)
#        tools/s2/check_fx_s2.sh render <track> <first> <last> <out prefix>   the firmware's pictures of the recorded frames first .. last, from the recorded camera and
#                                                                     cars: <out prefix><frame>.rgb (NOFX=1: without the effects), then python -I tools/rgb2png.py
# FXREC=<folder>: another folder than build/check/dosbox_smoke for the recording
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
mode=check
[ "$1" = render ] && { mode=render; shift; }
track=${1:-1}
if [ $mode = render ]; then first=$2; lastf=$3; outp=$4; game=${5:-${GAME_DIR:-C:/GOG Games/Screamer 2}}; else game=${2:-${GAME_DIR:-C:/GOG Games/Screamer 2}}; fi
recd="${FXREC:-$root/build/check/dosbox_smoke}"
bin="$recd/smoke_t$track.bin"
[ -f "$bin" ] || { echo "no recording $bin (python -I tools/s2/dosbox_smoke_s2.py $track)"; exit 1; }
size=$(wc -c < "$bin" | tr -d ' ')
# the frame's size: with the objects' records (the table of 170 entries) or the first recordings' (120 entries, no records)
nobj=170
fb=$((12 + 4 * 3264 + 28 * nobj + 2796 + 4 * 17 * 64))
[ $((size % fb)) -eq 0 ] || { nobj=120; fb=$((12 + 4 * 3264 + 28 * nobj + 2796)); }
[ $((size % fb)) -eq 0 ] || { nobj=120; fb=$((12 + 4 * 3264 + 28 * nobj + 2796 + 4 * 17 * 64)); }
[ $((size % fb)) -eq 0 ] || { echo "the recording's size $size is no whole number of frames"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python
out="$root/build/s2"
[ -f "$out/ref$track.bin" ] || "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$track" "$out/ref$track.bin" > /dev/null
src="$out/fxsrc"
rm -rf "$src"
mkdir -p "$src"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/check_fx_s2.c" "$src/"
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
LIST="road.c track.c geom.c setup.c raster.c blit.c dlog.c crc32.c dat.c console.c car.c tb_hud.c tb_mmp.c s2car.c s2params.c race.c s2ghost.c s2fx.c"
san="-fsanitize=address,undefined -fno-sanitize=shift -fno-sanitize-recover"      # (geom.c's model shifts negative numbers left, as the hardware does: not flagged)
[ $mode = render ] && san=""            # (the picture is drawn through fixed mappings of the SDRAM: no sanitizer shadow there)
args="$mode /b/s2/ref$track.bin $track /b/check/$(basename "$recd")/smoke_t$track.bin $fb $nobj"
[ $mode = render ] && args="$args $first $lastf /b/s2/fx_"
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -e NOFX -e FXONLY -v "$mount:/b" -v "$game_mount:/game:ro" -e S2_GAME=/game --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2/fxsrc && gcc -O1 -g -w $san -o ../check_fx_s2 check_fx_s2.c $LIST -lm &&
    cd /b/s2 && ./check_fx_s2 $args &&
    if [ $mode = check ]; then if SHIFT=3 ./check_fx_s2 $args > /dev/null 2>&1; then echo 'NEGATIVE CONTROL NOT FOUND'; exit 1; else echo 'negative control (the cars three frames ahead of the puffs) found: it fails'; fi; fi &&
    if [ $mode = check ]; then cd /b/s2/fxsrc && gcc -O1 -g -w -o ../check_fx_s2_cost check_fx_s2.c $LIST -lm && cd /b/s2 && COST=1 ./check_fx_s2_cost $args | grep \"the effects' pieces\"; fi"
if [ $mode = render ]; then mkdir -p "$(dirname "$outp")"; for f in "$out"/fx_*.rgb; do mv "$f" "$outp$(basename "$f" | sed "s/^fx_//")"; done; fi
