#!/bin/sh
# Host test of the race's sound (firmware/s2snd.c): the firmware's reading of DATA/SOUND.DAT (GAME.DTR, the 33 samples
# with place, length, rate, volume, loop and the CRC-32 of the bytes) and of CD_TRACKS compared line by line with an
# independent reading in Python (tools/s2/check_sound.py, tools/datfile.py), and the rules (an engine's rate, volume
# and pan from revs and distance, the tyres, the knocks) compared over a sweep with the game's routine as OpenScreamer's
# notes have it in double precision (the constants checked in STARTL.EXE): see the header of tools/s2/check_sound.c.
# Usage: tools/s2/check_sound.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2" (the unzipped game)
# Everything it writes stays in build/s2/ (the dumps hold the game's numbers: never commit them).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
out="$root/build/s2"
mkdir -p "$out"
[ -f "$game/DATA/SOUND.DAT" ] || { echo "$game does not look like Screamer 2's folder (no DATA/SOUND.DAT)"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
"$py" -I "$root/tools/s2/check_sound.py" "$game" > "$out/sound_py.txt"

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
        firmware/s2snd.c firmware/s2params.c firmware/dat.c tools/s2/check_sound.c -o build/s2/check_sound -lm &&
    build/s2/check_sound /game > build/s2/sound_c.txt
"
if cmp -s "$out/sound_py.txt" "$out/sound_c.txt"; then
    echo "SOUND.DAT: $(head -1 "$out/sound_c.txt"), $(grep -c '^sample ' "$out/sound_c.txt") rows and CD_TRACKS equal in C and Python"
else
    diff "$out/sound_py.txt" "$out/sound_c.txt" | head -20
    echo "MISMATCH between the firmware's reading and the independent one"
    exit 1
fi
