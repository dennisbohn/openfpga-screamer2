#!/bin/sh
# Pictures of the files screen (firmware/files.c) from the firmware's own code on the PC, in
# build/check/screens/*.png: the real SCREAMER2.zip with and without the music files, and
# archives made here that miss a file, are deflated or BZip2, have another top folder, hold other
# files, are no ZIP or are not there; two after the read test (the screen on its second frame) (tools/files_screens.py). Needs tools/check_zip.sh run once
# (it writes the real archive's file list).
# Usage: tools/files_screens.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
out="$root/build/check/screens"
man="$root/build/check/zip/real.man"
[ -f "$man" ] || { echo "$man is missing: run tools/check_zip.sh first"; exit 1; }
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
mkdir -p "$out"
"$py" -I "$root/tools/files_screens.py" "$man" "$out"

# the firmware's sources with hw.h's RISC-V instructions made plain C (as tools/render.sh does)
src="$root/build/host/files"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/files_screen.c" "$src/host/"
"$py" -I - "$src/hw.h" <<'EOF'
import sys
p = sys.argv[1]
t = open(p).read()
t = t[:t.index("/* Cache maintenance")] + """static inline void dcache_invalidate(void) {}
static inline void icache_sync(void) {}
static inline uint32_t rdcycle(void) { return 0; }
"""
open(p, "w").write(t)
EOF

if command -v cygpath >/dev/null 2>&1; then
    mount=$(cygpath -wa "$root/build")
    game_mount=$(cygpath -w "$game")
else
    mount=$root/build
    game_mount=$game
fi
MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh "$image" -c "
    cd /b/host/files && gcc -O1 -w -Wno-error=implicit-function-declaration -D'ZIP_ADDR(p)=zip_addr(p)' -o ../files_screen host/files_screen.c files.c console.c dlog.c zip.c crc32.c &&
    cd /b/check/screens &&
    run() { echo \"== \$1\"; /b/host/files_screen \"\$2\" \$1.rgb \$3 \$4; }
    run all /game/SCREAMER2.zip music
    run nomusic /game/SCREAMER2.zip
    for n in missing nonight deflated unsupported topfolder foreign notzip; do run \$n \$n.zip music; done
    run deflated_loaded deflated.zip music load
    run all_loaded /game/SCREAMER2.zip music load
    run nofile -
"
for f in "$out"/*.rgb; do
    "$py" -I "$root/tools/rgb2png.py" "$f" "${f%.rgb}.png" > /dev/null
    rm "$f"
done
ls "$out"/*.png
