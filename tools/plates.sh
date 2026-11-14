#!/bin/sh
# WELCOME's flying plates (firmware/s2plates.c, the page in s2leaguepages.c) against the running original: the recording and the raw DOSBox screenshots that
# `python -I tools/s2/dosbox_desk.py welcome` took (build/check/dosbox_welcome: every change of the plates' records and the four engine objects' records, 40 screenshots; never
# committed) are turned into rows and states (tools/desk_shots.py) and indices (tools/pages3d_idx.py), and tools/host/plates_match.c runs the firmware's flight and compares it with
# every recorded row, then draws the firmware's page in the state of each screenshot and compares the pictures (see the file's header). Prints the sums and one line per screenshot.
# Exit code 0 if every recorded row is a frame of the flight and the flight ends in the frame the original's does. Windows: Docker as tools/menu_screens.sh.
# Usage: [SHOTS=<folder of a recording>] [DUMP=<part of a screenshot's name>] [ROLL=-1] tools/plates.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
#        DUMP: the pictures (mine, ref, diff) of those frames in build/check/plates; ROLL=-1: the other way round for the plates' roll (the sign was found by this test)
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
shots="${SHOTS:-$root/build/check/dosbox_welcome}"
out="$root/build/check/plates"
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
[ -f "$shots/frames.jsonl" ] || { echo "no recording: python -I tools/s2/dosbox_desk.py welcome first"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python
mkdir -p "$out/idx"
rm -f "$out"/idx/* "$out"/*.rgb "$out"/*.png
"$py" -I "$root/tools/pages3d_idx.py" "$shots" "$out/idx" > /dev/null
"$py" -I "$root/tools/desk_shots.py" welcome-rows "$shots" "$out/rows.txt"
"$py" -I "$root/tools/desk_shots.py" welcome "$shots" "$out/shots.txt"

src="$root/build/host/plates"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/plates_match.c" "$src/host/"
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
LIST="dlog.c console.c zip.c dat.c crc32.c s2gfx.c s2scene.c blit.c geom.c setup.c raster.c track.c car.c s2save.c s2league.c s2end.c s2load.c"
DEFS=""
[ -n "$ROLL" ] && DEFS="-DPLATE_ROLL_SIGN=${ROLL}.0f"
set +e
MSYS_NO_PATHCONV=1 docker run --rm -e DUMP="$DUMP" -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh "$image" -c '
    cd /b/host/plates && gcc -O2 -w '"$DEFS"' -ffunction-sections -fdata-sections -Wl,--gc-sections -D"ZIP_ADDR(p)=zip_addr(p)" -o ../plates_match host/plates_match.c '"$LIST"' -lm &&
    cd /b/check/plates && /b/host/plates_match /game/SCREAMER2.zip /b/check/plates/rows.txt /b/check/plates/shots.txt /b/check/plates/idx /b/check/plates/p' > "$out/match.log" 2>&1
status=$?
cat "$out/match.log"
for f in "$out"/*.rgb; do
    [ -f "$f" ] || continue
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
exit $status
