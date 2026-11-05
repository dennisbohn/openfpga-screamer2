#!/bin/sh
# The 3D pages (TEAM SELECTION, ENTER YOUR NAME, the CAR page: firmware/s2pages3d.c, s2showroom.c, s2arcade.c) against the running original: the raw DOSBox
# screenshots that `python -I tools/s2/dosbox_arcade.py --pages3d` took (build/check/dosbox3d/*.png, never committed) are turned into indices
# (tools/pages3d_idx.py) and tools/host/pages3d_match.c looks for the state of the firmware's page that draws each one: where the objects stand and the angle
# each has, then compares the pictures pixel for pixel (see the file's header). Prints one line per screenshot. Windows: Docker as tools/menu_screens.sh.
# Usage: [ONLY=<part of a name>] [DUMP=<part of a name>] tools/pages3d.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"; DUMP: pictures (mine, ref, diff) of that frame in build/check/pages3d
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
shots="$root/build/check/dosbox3d"
out="$root/build/check/pages3d"
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
ls "$shots"/*.png > /dev/null 2>&1 || { echo "no screenshots in build/check/dosbox3d: python -I tools/s2/dosbox_arcade.py --pages3d first"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python
mkdir -p "$out/idx"
rm -f "$out"/idx/* "$out"/*.rgb "$out"/*.png
"$py" -I "$root/tools/pages3d_idx.py" "$shots" "$out/idx"

src="$root/build/host/pages3d"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/pages3d_match.c" "$src/host/"
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
MSYS_NO_PATHCONV=1 docker run --rm -e DUMP="$DUMP" -e ONLY="$ONLY" -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh "$image" -c "
    cd /b/host/pages3d && gcc -O2 -w -ffunction-sections -fdata-sections -Wl,--gc-sections -D'ZIP_ADDR(p)=zip_addr(p)' -o ../pages3d_match host/pages3d_match.c \
        dlog.c console.c zip.c dat.c crc32.c s2gfx.c s2scene.c s2showroom.c s2params.c blit.c geom.c setup.c raster.c track.c car.c -lm &&
    cd /b/check/pages3d && /b/host/pages3d_match /game/SCREAMER2.zip /b/check/pages3d/idx /b/check/pages3d/p" | tee "$out/match.log"
for f in "$out"/*.rgb; do
    [ -f "$f" ] || continue
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
# the 2D parts of the same screenshots (titles, names, the plate's sprites and digits) against the frames menu_screens.sh drew
if [ -f "$root/build/check/menu/menu_car3d_0.png" ]; then
    "$py" -I "$root/tools/pages3d_2d.py" "$out/match.log" "$shots" "$root/build/check/menu"
fi
