#!/bin/sh
# Host test of the race's replay (menus row 10): the firmware's own code on the PC in Docker (firmware/race.c, s2ghost.c, s2cam.c, s2car.c on the track images of
# tools/s2/make_track_s2.py, with the address and undefined-behaviour sanitizers): the arcade's race, four cars, is the same race whatever the steps of a frame,
# and a recorded race run again from its controls equals the recorded one bit for bit in every step, on all seven tracks. See the header of
# tools/s2/check_replay_s2.c. Needs the game's folder for the car's numbers and the track images (made on first use, as check_ghost_s2.sh does).
# Usage: tools/s2/check_replay_s2.sh [game folder]       default $GAME_DIR or "C:/GOG Games/Screamer 2"
# Everything it writes stays in build/s2/. Exit code 0 if everything passed.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
out="$root/build/s2"
src="$out/replaysrc"
mkdir -p "$out"
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
for n in 1 2 3 4 5 6 7; do
    [ -f "$out/car$n.bin" ] || S2CARS=1:1,1:2,1:3,1:4,1:5 "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$out/car$n.bin" > /dev/null
done
rm -rf "$src"
mkdir -p "$src"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/check_ghost_s2.c" "$root/tools/s2/check_replay_s2.c" "$src/"
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
LIST="road.c track.c geom.c setup.c raster.c car.c drive.c opp.c ghost.c efx.c scenery.c sound.c speech.c blit.c dlog.c crc32.c dat.c console.c menu.c champ.c intro.c tb_hud.c tb_spl.c tb_mmp.c hud.c s2car.c s2params.c s2ghost.c race.c s2cam.c s2cockpit.c"
MSYS_NO_PATHCONV=1 docker run --rm -e PILOT_TRACE=${PILOT_TRACE:-} -e ASAN_OPTIONS=detect_leaks=0 -e S2_GAME=/game -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2/replaysrc && gcc -O2 -g -w -fsanitize=address,undefined -fno-sanitize-recover -o ../check_replay_s2 check_replay_s2.c $LIST -lm &&
    cd /b/s2 && ./check_replay_s2 /b/s2"
