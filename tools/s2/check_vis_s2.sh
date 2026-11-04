#!/bin/sh
# Host test of the track's class keys and walks (firmware/track.c), tools/s2/check_vis_s2.c, natively in Docker on the images
# tools/s2/make_track_s2.py builds: the meshes' class keys, and that the walk over the camera cell's list leaves out no piece
# within 6,000 units of a camera round the road that the whole walk takes (docs/screamer2/draw_distance.md).
# Usage: tools/s2/check_vis_s2.sh [game folder] [tracks, default "1 2 3 4 5 6 7"]
#   game folder   the original game (unzipped), default $GAME_DIR or "C:/GOG Games/Screamer 2"
# Everything it writes stays in build/s2/ and build/s2vis/ (game data, never committed).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
tracks=${2:-1 2 3 4 5 6 7}
image="verilator/verilator:v5.050"
out="$root/build/s2"
src="$root/build/s2vis/src"
mkdir -p "$out" "$src"
[ -f "$game/GFX/MAP1H01.DAT" ] || { echo "$game does not look like Screamer 2's folder (no GFX/MAP1H01.DAT)"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
for n in $tracks; do
    [ -f "$out/vis$n.bin" ] || "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$out/vis$n.bin" > /dev/null
done

# the firmware's sources with a hw.h that has no hardware behind it (as tools/s2/render_s2.sh does)
rm -rf "$src"
mkdir -p "$src"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/check_vis_s2.c" "$src/"
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
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -w "$root/build"); game_mount=$(cygpath -w "$game"); else mount=$root/build; game_mount=$game; fi
car=$(cd "$root/firmware" && ls s2car*.c | grep -v '^s2car_stub.c$' | tr '\n' ' '); car="$car s2params.c race.c"
LIST="road.c geom.c setup.c raster.c blit.c dlog.c crc32.c dat.c console.c car.c drive.c opp.c ghost.c efx.c scenery.c sound.c speech.c menu.c champ.c intro.c hud.c tb_hud.c tb_spl.c tb_mmp.c $car"
runs=""
for n in $tracks; do runs="$runs echo \"track $n:\"; ./check_vis_s2 ../s2/vis$n.bin || fail=1;"; done
MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/b" -v "$game_mount:/game:ro" -e S2_GAME=/game --entrypoint sh "$image" -c "
    cd /b/s2vis/src && gcc -O2 -w -o ../check_vis_s2 check_vis_s2.c $LIST -lm &&
    cd /b/s2vis && fail=0; $runs exit \$fail
"
echo "class keys and walks: ok"
