#!/bin/sh
# The options room (firmware/s2room.c) against the running original: the raw DOSBox screenshots that `python -I tools/s2/dosbox_arcade.py --options` (build/check/dosbox_room: the
# five stations of team 1's room, the camera flying in, the moves), `--options pages` (build/check/dosbox_room_pages: the fade out, the pages ENTER leads to, the return, TEAM
# SELECTION) and `--options team2` .. `team4` (build/check/dosbox_room_t2 .. t4: the other teams' rooms, each station settled) took, never committed, are turned into indices
# (tools/pages3d_idx.py), the camera of the game at each one is read from the polls of its memory (tools/room_shots.py) and tools/host/room_match.c draws the firmware's page in the
# state of the game's camera and counts the pixels that differ (see the file's header). Prints one line per screenshot and a summary. Windows: Docker as tools/menu_screens.sh.
# Usage: [ONLY=<part of a name or label>] [DUMP=<part of a name or label>] tools/room.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
#        DUMP: the pictures (mine, ref, diff) of those frames in build/check/room
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
out="$root/build/check/room"
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python
mkdir -p "$out"
rm -rf "$out"/idx_* "$out"/shots_*.txt "$out"/*.rgb "$out"/*.png
have=0
runs="room room_pages room_t2 room_t3 room_t4"
for k in $runs; do
    if ls "$root"/build/check/dosbox_$k/*.png > /dev/null 2>&1; then
        mkdir -p "$out/idx_$k"
        "$py" -I "$root/tools/pages3d_idx.py" "$root/build/check/dosbox_$k" "$out/idx_$k"
        "$py" -I "$root/tools/room_shots.py" "$root/build/check/dosbox_$k" "$out/shots_$k.txt"
        have=1
    fi
done
[ $have = 1 ] || { echo "no screenshots: python -I tools/s2/dosbox_arcade.py --options (and --options pages, team2 .. team4) first"; exit 1; }

src="$root/build/host/room"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/room_match.c" "$src/host/"
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
LIST="dlog.c console.c zip.c dat.c crc32.c s2gfx.c s2scene.c blit.c geom.c setup.c raster.c track.c car.c track_build.c smg.c tb_mmp.c tb_gfx.c tb_hud.c s2end.c s2load.c s2save.c s2league.c s2env.c"
MSYS_NO_PATHCONV=1 docker run --rm -e DUMP="$DUMP" -e ONLY="$ONLY" -e RUNS="$runs" -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh "$image" -c '
    cd /b/host/room && gcc -O2 -w -ffunction-sections -fdata-sections -Wl,--gc-sections -D"ZIP_ADDR(p)=zip_addr(p)" -DS2_SETTINGS_ORIGINAL -o ../room_match host/room_match.c '"$LIST"' -lm &&
    cd /b/check/room &&
    for k in $RUNS; do
        [ -f shots_$k.txt ] || continue
        team=1; case $k in room_t?) team=$(echo $k | cut -c7);; esac
        echo "== $k (the room of team $team)"; TEAM=$team /b/host/room_match /game/SCREAMER2.zip /b/check/room/shots_$k.txt /b/check/room/idx_$k /b/check/room/p_$k
    done' | tee "$out/match.log"
for f in "$out"/*.rgb; do
    [ -f "$f" ] || continue
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
