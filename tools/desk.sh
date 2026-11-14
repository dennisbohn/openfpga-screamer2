#!/bin/sh
# The championship's desk scene (firmware/s2desk.c) against the running original: the recording and the raw DOSBox screenshots that `python -I tools/s2/dosbox_desk.py report`
# (or `desk`) took (build/check/dosbox_desk: every change of the camera record, the car's object record, the textures' clock and the wheels' phase while the scene ran, its screenshots; never
# committed) are turned into rows and states (tools/desk_shots.py) and indices (tools/pages3d_idx.py), and tools/host/desk_match.c runs the firmware's scene and compares it with every
# recorded row, then draws the firmware's page in the state of each screenshot and compares the pictures (see the file's header). Prints the sums and one line per screenshot. Exit
# code 0 if every recorded row is a frame of the scene and the fade starts in the frame the original's does, and if all 16 scenes of the 4 teams build the image of the whole file with the five cars each (ALL=1). Windows: Docker as tools/menu_screens.sh.
# Usage: [SHOTS=<folder of a recording>] [TEAM=<1..4>] [CAR=<1..5>] [DUMP=<part of a screenshot's name>] tools/desk.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
#        TEAM, CAR: the team and the car of the run (1 and 2 on the script's private CHOICE.DAT); DUMP: the pictures (mine, ref, diff) of those frames in build/check/desk
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
shots="${SHOTS:-$root/build/check/dosbox_desk}"
out="$root/build/check/desk"
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
[ -f "$shots/frames.jsonl" ] || { echo "no recording: python -I tools/s2/dosbox_desk.py report first"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python
mkdir -p "$out/idx"
rm -f "$out"/idx/* "$out"/*.rgb "$out"/*.png
"$py" -I "$root/tools/pages3d_idx.py" "$shots" "$out/idx" > /dev/null
"$py" -I "$root/tools/desk_shots.py" desk-rows "$shots" "$out/rows.txt"
"$py" -I "$root/tools/desk_shots.py" desk "$shots" "$out/shots.txt"

src="$root/build/host/desk"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/desk_match.c" "$src/host/"
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
LIST="dlog.c console.c zip.c dat.c crc32.c s2gfx.c s2scene.c s2env.c s2anim.c s2params.c blit.c geom.c setup.c raster.c track.c car.c track_build.c smg.c tb_mmp.c tb_gfx.c tb_hud.c s2save.c"
set +e
MSYS_NO_PATHCONV=1 docker run --rm -e DUMP="$DUMP" -e TEAM="$TEAM" -e CAR="$CAR" -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh "$image" -c '
    cd /b/host/desk && gcc -O2 -w -ffunction-sections -fdata-sections -Wl,--gc-sections -D"ZIP_ADDR(p)=zip_addr(p)" -o ../desk_match host/desk_match.c '"$LIST"' -lm &&
    cd /b/check/desk && /b/host/desk_match /game/SCREAMER2.zip /b/check/desk/rows.txt /b/check/desk/shots.txt /b/check/desk/idx /b/check/desk/p; s=$?; ALL=1 /b/host/desk_match /game/SCREAMER2.zip x x x x || s=1; exit $s' > "$out/match.log" 2>&1
status=$?
cat "$out/match.log"
for f in "$out"/*.rgb; do
    [ -f "$f" ] || continue
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
exit $status
