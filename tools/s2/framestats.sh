#!/bin/sh
# Frame statistics from the firmware's own C models in Docker (docs/screamer2/performance.md).
#   tools/s2/framestats.sh build
#       copies firmware/ to build/stats/src_s1 and src_s2, puts the counters of perf_patch.py into
#       the copies (src_s2 also Screamer 2's constants) and builds framestats_s1, framestats_s2
#       (tools/s2/framestats.c) and render_s1 (tools/host/render.c with the counters)
#   tools/s2/framestats.sh sweep s1|s2 <track image> <label> <out.csv> <step> <cars,...> [grid]
#       the frames along the road, see framestats.c
#   tools/s2/framestats.sh startimg s1|s2 <image> <n> <label>
#       the same on any track image (Screamer 1's track n files for the props and effects)
#   tools/s2/framestats.sh start <n>   (Screamer 1 only; GAME_DIR as tools/render.sh)
#       the game's own start frame of track n (the grid with ten cars, near chase camera, frame 2:
#       what the simulation logs) with the counters, into build/stats/start<n>.csv
# The firmware is never changed; the counters live in the copies. Game data only under build/.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${GAME_DIR:-C:/GOG Games/Screamer}
st="$root/build/stats"
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); else mount=$root/build; fi
img=verilator/verilator:v5.050
docker_sh() { MSYS_NO_PATHCONV=1 docker run --rm "$@"; }
LIST="track.c geom.c setup.c raster.c car.c drive.c opp.c ghost.c efx.c scenery.c sound.c speech.c blit.c dlog.c crc32.c dat.c console.c menu.c champ.c intro.c tb_hud.c tb_spl.c tb_mmp.c hud.c"

cmd=$1
shift
case "$cmd" in
build)
    for v in s1 s2; do
        src="$st/src_$v"
        rm -rf "$src"
        mkdir -p "$src"
        cp "$root"/firmware/*.c "$root"/firmware/*.h "$root"/tools/s2/perf.h "$root"/tools/s2/perf.c \
           "$root"/tools/s2/framestats.c "$root"/tools/host/render.c "$src/"
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
        python -I "$root/tools/s2/perf_patch.py" "$src" $([ $v = s2 ] && echo s2)
    done
    docker_sh -v "$mount:/b" --entrypoint sh $img -c "
        cd /b/stats/src_s1 && gcc -O2 -w -include perf.h -DRS_TRACE -o ../framestats_s1 framestats.c perf.c $LIST -lm &&
        gcc -O2 -w -include perf.h -DRS_TRACE -o ../render_s1 render.c perf.c $LIST -lm &&
        cd /b/stats/src_s2 && gcc -O2 -w -include perf.h -DRS_TRACE -o ../framestats_s2 framestats.c perf.c $LIST -lm &&
        gcc -O2 -w -include perf.h -DRS_TRACE -o ../render_s2 render.c perf.c $LIST -lm"
    ;;
sweep)
    v=$1; image=$2; label=$3; out=$4; step=$5; cars=$6; grid=$7
    cp "$image" "$st/$(basename "$image")"
    docker_sh ${FAR:+-e FAR=$FAR} ${REACH:+-e REACH=$REACH} ${MIRROR:+-e MIRROR=$MIRROR} -v "$mount:/b" --entrypoint sh $img -c "cd /b/stats && ./framestats_$v $(basename "$image") $label $(basename "$out") $step $cars $grid"
    mv "$st/$(basename "$out")" "$out"
    ;;
start)
    n=$1
    [ -f "$root/build/check/track$n.bin" ] || python -I "$root/tools/make_track.py" "$game/GFX/MAP${n}H.DAT" "$game/DATA/MAP$n.DAT" \
        "$game/DATA/GEN.DAT" "$root/build/check/track$n.bin" "$root/build/check/track$n.png" > /dev/null
    cp "$root/build/check/track$n.bin" "$st/track$n.bin"
    cp "$game/GFX/MAP${n}H.DAT" "$game/DATA/MAP$n.DAT" "$st/"
    docker_sh -e PERF_OUT=/b/stats/start$n.csv -e PERF_LABEL=s1_track$n -v "$mount:/b" --entrypoint sh $img -c \
        "cd /b/stats && ./render_s1 track$n.bin MAP${n}H.DAT MAP$n.DAT start$n.rgb 2 2 $n 0"
    ;;
startimg)
    # the game's start frame on a given image (a Screamer 2 image with S2START_ROAD, for the simulation):
    # the props and effects from Screamer 1 track n's files, as the simulation has them
    v=$1; image=$2; n=$3; label=$4
    cp "$image" "$st/$(basename "$image")"
    cp "$game/GFX/MAP${n}H.DAT" "$game/DATA/MAP$n.DAT" "$st/"
    docker_sh -e PERF_OUT=/b/stats/$label.csv -e PERF_LABEL=$label -v "$mount:/b" --entrypoint sh $img -c         "cd /b/stats && ./render_$v $(basename "$image") MAP${n}H.DAT MAP$n.DAT $label.rgb 2 2 $n 0"
    ;;
*)
    echo "usage: framestats.sh build | sweep ... | start <n> | startimg s1|s2 <image> <n> <label>"
    exit 2
    ;;
esac
