#!/bin/sh
# EXPERIMENT: frames of a Screamer 2 track image (tools/s2/make_track_s2.py) from the firmware's own
# code natively in Docker (as tools/render.sh does for Screamer 1), tools/s2/render_s2.c.
# Usage: tools/s2/render_s2.sh <image> <out prefix> <camera>...      (cameras: see render_s2.c)
#   S2PATCH=0: the firmware's limits as they are (to see what stops it); default 1: the copy of
#   firmware/track.c in build/s2host/ gets the PATCHES below (this is the "small change" list).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
src="$root/build/s2host/src"
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
if [ "${S2PATCH:-1}" = 1 ]; then
    # PATCH 1: more objects and placements than Screamer 1's tracks have (S2: 705 / 3742 against 390 / 2359)
    sed -i 's/^#define MAX_PIECES  2560 /#define MAX_PIECES  4096 /; s/^#define MAX_OBJ     512 /#define MAX_OBJ     1024/' "$src/track.c"
    # PATCH 3: a car as a track piece (S2CAR in make_track_s2.py) has twelve blocks; track.c draws eight
    sed -i 's/^#define MAX_BLOCKS  8 /#define MAX_BLOCKS  16/' "$src/track.c"
    # PATCH 2 (S2CLASS=0): the mesh class word at +0x32c does not exist in Screamer 2's shorter header
    if [ "${S2CLASS:-0}" = 0 ]; then
        sed -i 's|int32_t cl = rd32(obj + 0x32C); |int32_t cl = 0; (void)obj; |' "$src/track.c"
    fi
fi
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); else mount=$root/build; fi
img=$(basename "$1")
cp "$1" "$root/build/s2host/$img"
prefix=$(basename "$2")
outdir=$(dirname "$2")
shift 2
MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/b" --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2host/src && gcc -O2 -w -o ../render_s2 render_s2.c track.c geom.c setup.c raster.c blit.c dlog.c crc32.c dat.c console.c -lm &&
    cd /b/s2host && ./render_s2 $img $prefix $*"
mv "$root/build/s2host/$prefix"*.rgb "$outdir/"
