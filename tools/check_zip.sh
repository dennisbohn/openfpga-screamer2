#!/bin/sh
# Host test of the ZIP reader (firmware/zip.c): compiled natively in Docker with a stand-in for
# the data slot read that holds to the APF's rules, run on the real SCREAMER2.zip (every file
# found, its size and CRC-32 equal to what Python's zipfile says, whole and in pieces) and on
# broken archives made on the fly (cut off, compressed, wrong top folder, too many files, ...), then
# 30000 damaged copies of a small archive (tools/check_zip.c).
# Usage: tools/check_zip.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
# Everything it writes stays in build/check/zip/ (made-up bytes only); the game's ZIP is read only.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
out="$root/build/check/zip"
[ -f "$game/SCREAMER2.zip" ] || { echo "$game/SCREAMER2.zip is missing: name the game's folder"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
mkdir -p "$out"
"$py" -I "$root/tools/check_zip.py" "$game/SCREAMER2.zip" "$out"

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
        tools/check_zip.c firmware/crc32.c -o build/check/check_zip &&
    build/check/check_zip build/check/zip /game/SCREAMER2.zip
"
