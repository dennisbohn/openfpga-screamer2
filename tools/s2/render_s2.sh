#!/bin/sh
# Frames of a Screamer 2 track image (tools/s2/make_track_s2.py) from the firmware's own code natively in
# Docker (as tools/render.sh does for Screamer 1), tools/s2/render_s2.c: the viewer's scene (firmware/road.c).
# Usage: tools/s2/render_s2.sh <image> <out prefix> <camera>...      (cameras: see render_s2.c)
#   NOLOD=1: every block at level 0 (-DTRACK_NO_LOD); default: the original's level of detail, as the firmware
#   NOVIS=1: without the cells' visibility lists (-DTRACK_NO_VIS): everything in reach is looked at, as before them
# Writes <out prefix><k>.rgb (320 x 200 x 3) per camera; python -I tools/rgb2png.py makes a PNG of one.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
src="$root/build/s2host/src"
rm -rf "$src"
mkdir -p "$src"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/render_s2.c" "$src/"
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
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); else mount=$root/build; fi
img=$(basename "$1")
cp "$1" "$root/build/s2host/$img"
prefix=$(basename "$2")
outdir=$(dirname "$2")
shift 2
# the car physics behind firmware/s2car.h: the real s2car*.c when there is s2car.c, else the stand-in (firmware/Makefile)
car=s2car_stub.c
if [ -f "$root/firmware/s2car.c" ]; then car=$(cd "$root/firmware" && ls s2car*.c | grep -v '^s2car_stub.c$' | tr '\n' ' '); car="$car s2params.c race.c s2ghost.c"; fi
LIST="road.c track.c geom.c setup.c raster.c blit.c dlog.c crc32.c dat.c console.c car.c drive.c opp.c ghost.c efx.c scenery.c sound.c speech.c menu.c champ.c intro.c hud.c s2anim.c s2env.c smg.c track_build.c tb_gfx.c tb_hud.c tb_spl.c tb_mmp.c $car"
# the real car physics reads its numbers from the unzipped game (DATA/GAME.DAT, DATA/MAPn.DAT): $GAME_DIR, default as elsewhere
game=${GAME_DIR:-C:/GOG Games/Screamer 2}
if command -v cygpath >/dev/null 2>&1; then game_mount=$(cygpath -wa "$game"); else game_mount=$game; fi
MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/b" -v "$game_mount:/game:ro" -e S2_GAME=/game -e NOROTOR -e RACE_GAS -e RACE_ROBOT -e RACE_CAM -e LOOK_DUMP --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2host/src && gcc -O2 -w ${PITCH:+-DPITCH_SIGN=$PITCH} ${NOLOD:+-DTRACK_NO_LOD} ${NOVIS:+-DTRACK_NO_VIS} -o ../render_s2 render_s2.c $LIST -lm &&
    cd /b/s2host && ./render_s2 $img $prefix $*"
mv "$root/build/s2host/$prefix"*.rgb "$root/build/s2host/$prefix"*.idx "$outdir/"
