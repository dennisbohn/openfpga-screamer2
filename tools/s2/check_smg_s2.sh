#!/bin/sh
# The see-through table in 64 rows (firmware/smg.c) against tools/s2/smg_ref.py, its independent reading, on the game's palettes and on made-up cases, natively in Docker
# under the address and undefined-behaviour sanitizers: the rows of the table (every texel of the seven tracks, random palettes: the search outwards from the colour's red
# value must give what the plain search over all 256 colours gives), the copies of the textures and the plan of the rows (made-up libraries with every sort of material,
# textures shared with materials that draw them as colours, more than 62 texels), the originals' pixels left whole, a damaged expectation found (negative control).
# Usage: tools/s2/check_smg_s2.sh [game folder]       default $GAME_DIR or "C:/GOG Games/Screamer 2"      (about 2 minutes: the cases are made in Python)
# Writes build/s2/smg_cases.bin (made from the game's files: never committed). The image check, tools/s2/check_track_build_s2.sh, compares the whole image the two build.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
mkdir -p "$root/build/s2"
[ -f "$game/GFX/MAP1H01.DAT" ] || { echo "$game does not look like Screamer 2's folder (no GFX/MAP1H01.DAT)"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python
"$py" -I "$root/tools/s2/check_smg_s2.py" "$game" "$root/build/s2/smg_cases.bin"
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -w "$root"); else mount=$root; fi
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -v "$mount:/work" -w /work --entrypoint sh "$image" -c "
    gcc -O2 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover -Ifirmware firmware/smg.c tools/s2/check_smg_s2.c -o build/s2/check_smg_s2 &&
    build/s2/check_smg_s2 build/s2/smg_cases.bin"
