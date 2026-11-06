#!/bin/sh
# The CAR page's and the CAR CUSTOMIZATION page's room scenes (firmware/s2garage.c) against the running original: the recordings and raw DOSBox screenshots that
# `python -I tools/s2/dosbox_garage.py [car|custom|all]` took (build/check/dosbox_garage, never committed; keep a run's folder under another name, build/check/dosbox_garage_<name>,
# to compare it later: every folder of that name here is compared) are turned into rows (tools/garage_rows.py) and indices (tools/pages3d_idx.py), and tools/host/garage_match.c steps the
# firmware's camera from each recorded state, counts the states it reproduces and draws the scene at each screenshot's camera (see the file's header). Prints the counts, one line per
# screenshot and the sums. Windows: Docker as tools/menu_screens.sh.
# Usage: [ONLY=<part of a name>] [DUMP=<part of a name>] tools/garage.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
#        DUMP: the pictures (mine, ref, diff) of those screenshots in build/check/garage
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
out="$root/build/check/garage"
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python
mkdir -p "$out"
rm -rf "$out"/idx_* "$out"/rows_*.txt "$out"/*.rgb "$out"/*.png
runs=""
for d in "$root"/build/check/dosbox_garage "$root"/build/check/dosbox_garage_*; do
    [ -f "$d/frames.jsonl" ] || continue
    k=$(basename "$d")
    mkdir -p "$out/idx_$k"
    "$py" -I "$root/tools/garage_rows.py" "$d" "$out/rows_$k.txt"
    "$py" -I "$root/tools/pages3d_idx.py" "$d" "$out/idx_$k" > /dev/null
    runs="$runs $k"
done
[ -n "$runs" ] || { echo "no recording: python -I tools/s2/dosbox_garage.py first"; exit 1; }

src="$root/build/host/garage"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/garage_match.c" "$root/tools/host/garage_arcade.c" "$root/tools/host/garage_custom.c" "$src/host/"
"$py" -I - "$src/hw.h" <<'PY'
import sys
p = sys.argv[1]
t = open(p).read()
t = t[:t.index("/* Cache maintenance")] + """static inline void dcache_invalidate(void) {}
static inline void icache_sync(void) {}
static inline uint32_t rdcycle(void) { return 0; }
"""
open(p, "w").write(t)
PY
if command -v cygpath >/dev/null 2>&1; then
    mount=$(cygpath -wa "$root/build")
    game_mount=$(cygpath -w "$game")
else
    mount=$root/build
    game_mount=$game
fi
LIST="dlog.c console.c zip.c dat.c crc32.c s2gfx.c s2scene.c blit.c geom.c setup.c raster.c track.c car.c track_build.c smg.c tb_mmp.c tb_gfx.c tb_hud.c tb_spl.c s2end.c s2load.c s2save.c champ.c s2league.c \
      s2room.c s2garage.c s2showroom.c s2env.c s2params.c s2arcscene.c s2anim.c"
MSYS_NO_PATHCONV=1 docker run --rm -e DUMP="$DUMP" -e ONLY="$ONLY" -e RUNS="$runs" -e VERBOSE="$VERBOSE" -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh "$image" -c '
    cd /b/host/garage && gcc -O2 -w -ffunction-sections -fdata-sections -Wl,--gc-sections -D"ZIP_ADDR(p)=zip_addr(p)" -o ../garage_match host/garage_match.c host/garage_arcade.c host/garage_custom.c '"$LIST"' -lm &&
    cd /b/check/garage &&
    for k in $RUNS; do
        echo "== $k"; TEAM=1 /b/host/garage_match /game/SCREAMER2.zip /b/check/garage/rows_$k.txt /b/check/garage/idx_$k /b/check/garage/p_$k
    done' | tee "$out/match.log"
for f in "$out"/*.rgb; do
    [ -f "$f" ] || continue
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
