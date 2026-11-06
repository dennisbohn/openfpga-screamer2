#!/bin/sh
# Frame statistics from the firmware's own C models in Docker (docs/screamer2/performance.md).
#   tools/s2/framestats.sh build
#       copies firmware/ to build/stats/src_s2, puts the counters of perf_patch.py into the copies and builds
#       framestats_s2 (tools/s2/framestats.c); NOLOD=1: every block at level 0 (-DTRACK_NO_LOD); NOVIS=1: without
#       the cells' visibility lists, everything in reach looked at (-DTRACK_NO_VIS)
#   tools/s2/framestats.sh sweep s2 <track image> <label> <out.csv> <step> <cars,...> [grid]   (ROAD_S=185,370: the viewer's frames at those places instead)
#       the frames along the road, see framestats.c
#   tools/s2/framestats.sh race <track image> <label> <out.csv> <steps> <every> [track]
#       a race on the PC (the game's driver drives all four cars), a frame every <every> steps, see racestats.c
#       ATTRACT=1: the attract race instead (nobody drives car 0, the replay's trackside cameras: menus row 9)
#       (needs the game's folder for the physics: GAME_DIR, default C:/GOG Games/Screamer 2;
#       PIECES=<file>: also every piece of every frame with the counters it added, for the pipeline model)
# The firmware is never changed; the counters live in the copies. Game data only under build/.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
st="$root/build/stats"
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); else mount=$root/build; fi
img=verilator/verilator:v5.050
docker_sh() { MSYS_NO_PATHCONV=1 docker run --rm "$@"; }
# the car physics behind firmware/s2car.h: the real s2car*.c when there is s2car.c, else the stand-in (firmware/Makefile)
car=s2car_stub.c
if [ -f "$root/firmware/s2car.c" ]; then car=$(cd "$root/firmware" && ls s2car*.c | grep -v '^s2car_stub.c$' | tr '\n' ' '); car="$car s2params.c"; fi
LIST="road.c track.c geom.c setup.c raster.c car.c drive.c opp.c ghost.c efx.c scenery.c sound.c speech.c blit.c dlog.c crc32.c dat.c console.c menu.c champ.c intro.c tb_hud.c tb_spl.c tb_mmp.c hud.c $car"

cmd=$1
shift
case "$cmd" in
build)
    # (Screamer 1's images are not read any more: the track image is version 7. NOLOD=1: every block at level 0)
    v=s2
    src="$st/src_$v"
    rm -rf "$src"
    mkdir -p "$src"
    cp "$root"/firmware/*.c "$root"/firmware/*.h "$root"/tools/s2/perf.h "$root"/tools/s2/perf.c        "$root"/tools/s2/framestats.c "$root"/tools/s2/racestats.c "$src/"
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
    python -I "$root/tools/s2/perf_patch.py" "$src"
    docker_sh -v "$mount:/b" --entrypoint sh $img -c "
        cd /b/stats/src_s2 && gcc -O2 -w -include perf.h -DRS_TRACE ${NOLOD:+-DTRACK_NO_LOD} ${NOVIS:+-DTRACK_NO_VIS} -o ../framestats_s2 framestats.c perf.c $LIST -lm &&
        gcc -O2 -w -include perf.h -DRS_TRACE ${NOLOD:+-DTRACK_NO_LOD} ${NOVIS:+-DTRACK_NO_VIS} -o ../racestats_s2 racestats.c perf.c $LIST race.c s2ghost.c s2cam.c s2cockpit.c -lm"
    ;;
sweep)
    v=$1; image=$2; label=$3; out=$4; step=$5; cars=$6; grid=$7
    cp "$image" "$st/$(basename "$image")"
    docker_sh ${YAW_OFF:+-e YAW_OFF=$YAW_OFF} ${FAR:+-e FAR=$FAR} ${REACH:+-e REACH=$REACH} ${MIRROR:+-e MIRROR=$MIRROR} ${ROAD_S:+-e ROAD_S=$ROAD_S} -v "$mount:/b" --entrypoint sh $img -c "cd /b/stats && ./framestats_$v $(basename "$image") $label $(basename "$out") $step $cars $grid"
    mv "$st/$(basename "$out")" "$out"
    ;;
race)
    image=$1; label=$2; out=$3; steps=$4; every=$5; tn=$6
    cp "$image" "$st/$(basename "$image")"
    game=${GAME_DIR:-C:/GOG Games/Screamer 2}
    if command -v cygpath >/dev/null 2>&1; then game_mount=$(cygpath -wa "$game"); else game_mount=$game; fi
    docker_sh -v "$mount:/b" -v "$game_mount:/game:ro" -e S2_GAME=/game ${ATTRACT:+-e ATTRACT=1} ${PIECES:+-e PIECES=$(basename "$PIECES")} --entrypoint sh $img -c "cd /b/stats && ./racestats_s2 $(basename "$image") $label $(basename "$out") $steps $every $tn"
    mv "$st/$(basename "$out")" "$out"
    [ -z "$PIECES" ] || mv "$st/$(basename "$PIECES")" "$PIECES"
    ;;
*)
    echo "usage: framestats.sh build | race <image> <label> <out.csv> <steps> <every> [track] | sweep s2 <track image> <label> <out.csv> <step> <cars,...> [grid]"
    exit 2
    ;;
esac
