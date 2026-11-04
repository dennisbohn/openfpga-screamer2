#!/bin/sh
# Host test of the cars' loader on the Pocket (firmware/s2teams.c, s2_cars_load()), natively in Docker under the
# address and undefined-behaviour sanitizers: a ZIP of the eight team containers (GFX/CARS<t><look>1.DAT), stored
# and deflated, made here; the loader's cars must equal the containers' own entries (tools/s2/check_cars_s2.c),
# with the bytes it reads from each kind of ZIP counted. Run by check_track_build_s2.sh.
# Usage: tools/s2/check_cars_s2.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
# Everything it writes stays in build/s2/ (the two ZIPs hold game data: never commit them).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
out="$root/build/s2"
mkdir -p "$out"
[ -f "$game/GFX/CARS101.DAT" ] || { echo "$game does not look like Screamer 2's folder (no GFX/CARS101.DAT)"; exit 1; }

py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
"$py" -I - "$game" "$out" <<'EOF'
import sys, zipfile
game, out = sys.argv[1], sys.argv[2]
for name, method, prefix in (("cars_stored.zip", zipfile.ZIP_STORED, ""), ("cars_deflated.zip", zipfile.ZIP_DEFLATED, "Screamer 2/")):
    with zipfile.ZipFile(out + "/" + name, "w", method) as z:
        for t in range(1, 5):
            for look in (0, 1):
                z.write(f"{game}/GFX/CARS{t}{look}1.DAT", f"{prefix}GFX/CARS{t}{look}1.DAT")
EOF

# Git Bash on Windows: hand Docker Windows paths and stop MSYS rewriting /work.
if command -v cygpath >/dev/null 2>&1; then
    mount=$(cygpath -w "$root")
    game_mount=$(cygpath -w "$game")
else
    mount=$root
    game_mount=$game
fi
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -v "$mount:/work" -v "$game_mount:/game:ro" -w /work --entrypoint sh "$image" -c "
    gcc -O2 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover \
        tools/s2/check_cars_s2.c firmware/s2teams.c firmware/dat.c firmware/crc32.c -o build/s2/check_cars_s2 &&
    build/s2/check_cars_s2 /game build/s2/cars_stored.zip build/s2/cars_deflated.zip
"
