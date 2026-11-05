#!/bin/sh
# Report on the painter's order of a computer car against the static pieces (tools/s2/report_carsort_s2.c), natively in Docker
# on the images tools/s2/make_track_s2.py builds, with the firmware's own track.c and geom.c patched in temporary copies to
# hand every polygon to a depth map (nothing of the firmware changes). Not a check: the original's order has the same
# cases (docs/screamer2/draw_distance.md, "A car behind a wall").
# Usage: tools/s2/report_carsort_s2.sh [game folder] [tracks, default "1"] [first step, default 300] [last step, default 1700] [every, default 10]
#   game folder   the original game (unzipped), default $GAME_DIR or "C:/GOG Games/Screamer 2"
# Everything it writes stays in build/s2/ and build/s2carsort/ (game data, never committed).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
tracks=${2:-1}
klo=${3:-300}
khi=${4:-1700}
kst=${5:-10}
image="verilator/verilator:v5.050"
out="$root/build/s2"
src="$root/build/s2carsort/src"
mkdir -p "$out"
[ -f "$game/GFX/MAP1H01.DAT" ] || { echo "$game does not look like Screamer 2's folder (no GFX/MAP1H01.DAT)"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
for n in $tracks; do
    [ -f "$out/vis$n.bin" ] || "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$out/vis$n.bin" > /dev/null
done

rm -rf "$src"
mkdir -p "$src"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/report_carsort_s2.c" "$src/"
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
# the depth map's hooks: geom.c hands each polygon on, track.c says which piece or object it is drawing
sed -i 's/^#include "geom.h"/#include "geom.h"\nvoid dbg_poly(const struct su_view *v, const struct su_fixed *c, int n);/; s/^\( *\)pixels += su_polygon8(/\1dbg_poly(v, c, nc);\n\1pixels += su_polygon8(/' "$src/geom.c"
sed -i 's/^#include "crc32.h"/#include "crc32.h"\nextern int dbg_tag;/; s/^\( *\)const int vi = vis\[n\] \& 0xFFFF;/\1const int vi = vis[n] \& 0xFFFF; dbg_tag = vi;/' "$src/track.c"
grep -q "dbg_poly(v, c, nc)" "$src/geom.c" && grep -q "dbg_tag = vi" "$src/track.c" || { echo "the patches did not apply (geom.c / track.c changed?)"; exit 1; }
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -w "$root/build"); game_mount=$(cygpath -w "$game"); else mount=$root/build; game_mount=$game; fi
car=$(cd "$root/firmware" && ls s2car*.c | grep -v '^s2car_stub.c$' | tr '\n' ' '); car="$car s2params.c race.c s2ghost.c"
LIST="road.c geom.c setup.c raster.c blit.c dlog.c crc32.c dat.c console.c car.c drive.c opp.c ghost.c efx.c scenery.c sound.c speech.c menu.c champ.c intro.c hud.c tb_hud.c tb_spl.c tb_mmp.c $car"
runs=""
for n in $tracks; do runs="$runs echo \"track $n:\"; ./report_carsort_s2 ../s2/vis$n.bin $klo $khi $kst;"; done
MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/b" -v "$game_mount:/game:ro" -e S2_GAME=/game --entrypoint sh "$image" -c "
    cd /b/s2carsort/src && gcc -O2 -w -o ../report_carsort_s2 report_carsort_s2.c $LIST -lm &&
    cd /b/s2carsort && $runs true
"
