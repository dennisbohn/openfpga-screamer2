#!/bin/sh
# Host test of the geometry driver's cut of the blocks over the unit's limits (firmware/geom.c gu_split, gu_cut: any block over 512 vertices or 1,024 faces a row is drawn as runs of
# its face row, in order): the firmware's own code natively in Docker under the address and undefined-behaviour sanitizers on the game's own data (the track maps, the arcade scene,
# a car, a race track): the runs are compared with their block face by face and row by row, with the unit's limits and with limits far below any block (see the header of
# tools/s2/check_cut_s2.c). Needs the game's folder; reads it only.
# Usage: tools/s2/check_cut_s2.sh [game folder]       default $GAME_DIR or "C:/GOG Games/Screamer 2"
# Everything it writes stays in build/s2/. Exit code 0 if everything passed.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
out="$root/build/s2"
src="$out/cutsrc"
[ -f "$game/GFX/MAPS1.DAT" ] || { echo "$game does not look like Screamer 2's folder (no GFX/MAPS1.DAT)"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
rm -rf "$src"
mkdir -p "$src"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/check_cut_s2.c" "$src/"
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
sed -i 's|"../../firmware/|"|' "$src/check_cut_s2.c"
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); game_mount=$(cygpath -wa "$game"); else mount=$root/build; game_mount=$game; fi
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2/cutsrc && gcc -O2 -g -w -fsanitize=address,undefined -fno-sanitize-recover -o ../check_cut_s2 check_cut_s2.c geom.c setup.c raster.c dat.c -lm &&
    cd /b/s2 && ./check_cut_s2 /game"
