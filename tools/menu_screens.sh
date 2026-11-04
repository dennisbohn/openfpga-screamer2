#!/bin/sh
# The start menu (firmware/s2menu.c) on the PC from the firmware's own code: tests of its logic (keys, the grid, the kept
# choices, the texts read from the real ZIP's DATA/LOCAL.DAT) and pictures of the list in several states, in
# build/check/menu/*.png (never committed). Exit code 0 if all tests passed.
# Usage: tools/menu_screens.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
out="$root/build/check/menu"
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
mkdir -p "$out"
rm -f "$out"/*.rgb "$out"/*.png

# the firmware's sources with hw.h's RISC-V instructions made plain C (as tools/files_screens.sh does)
src="$root/build/host/menu"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/menu_screen.c" "$src/host/"
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
    cd /b/host/menu && gcc -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined -w -D'ZIP_ADDR(p)=zip_addr(p)' -o ../menu_screen host/menu_screen.c s2menu.c s2save.c champ.c console.c dlog.c zip.c dat.c crc32.c &&
    cd /b/check/menu && /b/host/menu_screen /game/SCREAMER2.zip /b/check/menu/menu"
status=$?
for f in "$out"/*.rgb; do
    [ -f "$f" ] || continue
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
ls "$out"/*.png 2>/dev/null
exit $status
