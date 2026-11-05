#!/bin/sh
# The track pages' 3D maps (firmware/s2trackpage.c, s2trackmap.c) against the running original: the raw DOSBox screenshots that
# `python -I tools/s2/dosbox_arcade.py --timeattack` (build/check/dosbox_ta) and `--league` (build/check/dosbox_league: its "trackmap" frames) took, never committed, are turned
# into indices (tools/pages3d_idx.py) and tools/host/trackmap_match.c looks for the angle of the map's turn at which the firmware's page draws each one, then counts the pixels
# that differ (see the file's header). Two builds are compared: the firmware's (the six blocks with more than 512 vertices cut into pieces, as the geometry unit takes at most
# 512) and one with a model of the unit that takes any number (GU_MAX_VERT=1024: the whole blocks, as the original draws them). Prints one line per screenshot and a summary.
# Windows: Docker as tools/menu_screens.sh.
# Usage: [ONLY=<part of a name or label>] [DUMP=<part of a name or label>] tools/trackmap.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
#        DUMP: the pictures (mine, ref, diff) of those frames in build/check/trackmap
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
out="$root/build/check/trackmap"
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python
mkdir -p "$out/idx_ta" "$out/idx_league" "$out/idx_taend" "$out/idx_taclosed"
rm -f "$out"/idx_*/* "$out"/*.rgb "$out"/*.png
have=0
if ls "$root"/build/check/dosbox_ta/*.png > /dev/null 2>&1; then
    "$py" -I "$root/tools/pages3d_idx.py" "$root/build/check/dosbox_ta" "$out/idx_ta"
    cp "$root/build/check/dosbox_ta/labels.txt" "$out/labels_ta.txt"
    have=1
fi
if ls "$root"/build/check/dosbox_ta_closed/*.png > /dev/null 2>&1; then
    mkdir -p "$out/idx_taclosed"
    "$py" -I "$root/tools/pages3d_idx.py" "$root/build/check/dosbox_ta_closed" "$out/idx_taclosed"
    cp "$root/build/check/dosbox_ta_closed/labels.txt" "$out/labels_taclosed.txt"
    have=1
fi
if ls "$root"/build/check/dosbox_ta_race/*.png > /dev/null 2>&1; then
    mkdir -p "$out/idx_taend"
    "$py" -I "$root/tools/pages3d_idx.py" "$root/build/check/dosbox_ta_race" "$out/idx_taend"
    cp "$root/build/check/dosbox_ta_race/labels.txt" "$out/labels_taend.txt"
    cp "$root/build/check/dosbox_ta_race/ram_end.txt" "$out/ram_taend.txt"
    have=1
fi
if ls "$root"/build/check/dosbox_league/*.png > /dev/null 2>&1; then
    "$py" -I "$root/tools/pages3d_idx.py" "$root/build/check/dosbox_league" "$out/idx_league"
    cp "$root/build/check/dosbox_league/labels.txt" "$out/labels_league.txt"
    have=1
fi
[ $have = 1 ] || { echo "no screenshots: python -I tools/s2/dosbox_arcade.py --timeattack (and --league) first"; exit 1; }

src="$root/build/host/trackmap"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/trackmap_match.c" "$src/host/"
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
LIST="dlog.c console.c zip.c dat.c crc32.c s2gfx.c s2scene.c s2trackmap.c blit.c geom.c setup.c raster.c track.c car.c s2end.c s2load.c s2save.c champ.c s2league.c"
MSYS_NO_PATHCONV=1 docker run --rm -e DUMP="$DUMP" -e ONLY="$ONLY" -e TA_RAM=/b/check/trackmap/ram_taend.txt -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh "$image" -c "
    cd /b/host/trackmap && gcc -O2 -w -ffunction-sections -fdata-sections -Wl,--gc-sections -D'ZIP_ADDR(p)=zip_addr(p)' -o ../trackmap_match host/trackmap_match.c $LIST -lm &&
    gcc -O2 -w -ffunction-sections -fdata-sections -Wl,--gc-sections -DGU_MAX_VERT=1024 -D'ZIP_ADDR(p)=zip_addr(p)' -o ../trackmap_whole host/trackmap_match.c $LIST -lm &&
    cd /b/check/trackmap &&
    for k in ta taclosed taend league; do
        TA_OPEN=7; [ \$k = taclosed ] && TA_OPEN=3; export TA_OPEN
        [ -d idx_\$k ] && [ -f labels_\$k.txt ] && ls idx_\$k/*.idx > /dev/null 2>&1 || continue
        echo '== the cut blocks (the firmware): '\$k; /b/host/trackmap_match /game/SCREAMER2.zip /b/check/trackmap/idx_\$k /b/check/trackmap/labels_\$k.txt /b/check/trackmap/p_\$k
        echo '== the whole blocks (a unit without the limit): '\$k; /b/host/trackmap_whole /game/SCREAMER2.zip /b/check/trackmap/idx_\$k /b/check/trackmap/labels_\$k.txt /b/check/trackmap/w_\$k
    done" | tee "$out/match.log"
for f in "$out"/*.rgb; do
    [ -f "$f" ] || continue
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
