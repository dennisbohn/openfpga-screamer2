#!/bin/sh
# The start menu (firmware/s2menu.c), the 2D toolkit (s2gfx.c, s2page.c, s2load.c, s2pause.c: loading screen, pause menu, page
# runner), the arcade pages (s2arcade.c) and the pages of menus row 3 (s2pages.c: GAME SETTINGS, HI SCORES; s2end.c: the end of the race and
# the best-time flow) on the PC from the firmware's own code: tests of the menu's logic (keys, the grid, the kept choices, the texts read from
# the real ZIP's DATA/LOCAL.DAT), of the toolkit against independent references made from the game's files (tools/ui_reference.py: .BKK
# pictures, text and sprites, MENU.DTR; and the pages of row 3 drawn from the game's files), of the arcade pages against a model of them
# (tools/ui_arcade_ref.py: scripted buttons, frames pixel for pixel, the chain) and pictures of the screens in several states, in
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
mkdir -p "$out/ref"
rm -f "$out"/*.rgb "$out"/*.png
"$py" -I "$root/tools/ui_reference.py" "$game" "$out/ref" || exit 1

# the firmware's sources with hw.h's RISC-V instructions made plain C (as tools/files_screens.sh does)
src="$root/build/host/menu"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/menu_screen.c" "$root/tools/host/ui_screen.c" "$root/tools/host/pages3d_screen.c" "$root/tools/host/league_screen.c" "$src/host/"
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
    cd /b/host/menu && gcc -O2 -w -c -ffunction-sections -fdata-sections geom.c setup.c raster.c track.c car.c &&
    gcc -O1 -g -ffunction-sections -fdata-sections -Wl,--gc-sections -fsanitize=undefined -fno-sanitize-recover=undefined -w -D'ZIP_ADDR(p)=zip_addr(p)' -o ../menu_screen host/menu_screen.c host/ui_screen.c s2menu.c s2save.c champ.c console.c dlog.c zip.c dat.c crc32.c s2gfx.c s2page.c s2load.c s2pause.c s2arcade.c s2pages.c s2end.c s2league.c s2leaguepages.c s2snd.c s2params.c blit.c s2scene.c s2pages3d.c s2showroom.c geom.o setup.o raster.o track.o car.o -lm &&
    cd /b/check/menu && /b/host/menu_screen /game/SCREAMER2.zip /b/check/menu/menu /b/check/menu/ref /b/check/menu/ref/graf_deflated.zip"
status=$?
for f in "$out"/*.rgb; do
    [ -f "$f" ] || continue
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
ls "$out"/*.png 2>/dev/null
exit $status
