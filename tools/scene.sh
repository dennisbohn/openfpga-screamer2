#!/bin/sh
# The ARCADE title's cars (firmware/s2arcscene.c, s2arcade.c) against the running original: the recording and the raw DOSBox screenshots that
# `python -I tools/s2/dosbox_scene.py` took (build/check/dosbox_scene: every change of the original's camera record and four cars' object records, 45 s of the four takes; never committed)
# are turned into rows (tools/scene_shots.py) and indices (tools/pages3d_idx.py), and tools/host/scene_match.c runs the firmware's clock and compares the cars' places, headings and the
# camera with every recorded row, then draws the firmware's title in the state of each screenshot and compares the pictures pixel for pixel (see the file's header). Prints the sums and
# one line per screenshot. Windows: Docker as tools/menu_screens.sh.
# Usage: [TEAM=<1..4>] [DUMP=<part of a screenshot's name>] tools/scene.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
#        TEAM: the team whose cars the screenshots show (1 on the script's private CHOICE.DAT); DUMP: the pictures (mine, ref, diff) of those frames in build/check/scene
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
shots="$root/build/check/dosbox_scene"
out="$root/build/check/scene"
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
[ -f "$shots/frames.jsonl" ] || { echo "no recording: python -I tools/s2/dosbox_scene.py first"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python
mkdir -p "$out/idx"
rm -f "$out"/idx/* "$out"/*.rgb "$out"/*.png
"$py" -I "$root/tools/pages3d_idx.py" "$shots" "$out/idx"
"$py" -I "$root/tools/scene_shots.py" "$shots" "$out/rows.txt"

src="$root/build/host/scene"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/scene_match.c" "$src/host/"
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
LIST="dlog.c console.c zip.c dat.c crc32.c s2gfx.c s2scene.c s2showroom.c s2env.c s2params.c s2arcscene.c s2anim.c s2save.c blit.c geom.c setup.c raster.c track.c car.c track_build.c smg.c tb_mmp.c tb_gfx.c tb_hud.c"
MSYS_NO_PATHCONV=1 docker run --rm -e DUMP="$DUMP" -e TEAM="$TEAM" -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh "$image" -c '
    cd /b/host/scene && gcc -O2 -w -ffunction-sections -fdata-sections -Wl,--gc-sections -D"ZIP_ADDR(p)=zip_addr(p)" -o ../scene_match host/scene_match.c '"$LIST"' -lm &&
    cd /b/check/scene && /b/host/scene_match /game/SCREAMER2.zip /b/check/scene/rows.txt /b/check/scene/idx /b/check/scene/p' | tee "$out/match.log"
for f in "$out"/*.rgb; do
    [ -f "$f" ] || continue
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
