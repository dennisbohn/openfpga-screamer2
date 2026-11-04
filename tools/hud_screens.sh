#!/bin/sh
# The race screen (firmware/s2hud.c: Screamer 2's HUD in the chase camera, drawn from GAMEL.SPR and GAMEL.FNT by the blitter) on the PC
# from the firmware's own code, with the blitter's C model, over a made-up race (the car records race.c keeps), against frames made by
# tools/hud_reference.py from the game's files and the original's code: every frame of the scenario (the lights, the dial settling, a
# checkpoint's gap, BEST LAP! blinking, WRONG WAY, a bend's arrow, the 1ST mark over a leader, the finish, the elements moved down for the
# timing overlay) must equal its reference in every pixel. Pictures in build/check/hud/*.png (never committed). Exit code 0 if all passed.
# Usage: tools/hud_screens.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
out="$root/build/check/hud"
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
mkdir -p "$out/ref"
rm -f "$out"/*.rgb "$out"/*.png "$out/ref"/*
"$py" -I "$root/tools/hud_reference.py" "$game" "$out/ref" || exit 1

# the firmware's sources with hw.h's RISC-V instructions made plain C (as tools/menu_screens.sh does)
src="$root/build/host/hud"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/hud_screen.c" "$src/host/"
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
set +e
MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh "$image" -c "
    cd /b/host/hud && gcc -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined -w -D'ZIP_ADDR(p)=zip_addr(p)' -o ../hud_screen host/hud_screen.c s2hud.c s2gfx.c blit.c console.c dlog.c zip.c dat.c crc32.c -lm &&
    cd /b/check/hud && /b/host/hud_screen /game/SCREAMER2.zip /b/check/hud/hud /b/check/hud/ref /b/check/hud/ref/game_deflated.zip"
status=$?
for f in "$out"/*.rgb; do
    [ -f "$f" ] || continue
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
ls "$out"/*.png 2>/dev/null | head -50
exit $status
