#!/bin/sh
# Host test of the Screamer 2 track build on the Pocket (firmware/track_build.c, track_build_s2()): the
# image built natively in Docker from the three containers a track needs (GFX/MAPnH01.DAT,
# GFX/CARS101.DAT, DATA/MAPn.DAT) must be byte-identical with the one tools/s2/make_track_s2.py builds from
# the same files (the reference), for each of the seven tracks. The S1 pattern is tools/check_track_build.sh.
# Usage: tools/s2/check_track_build_s2.sh [game folder] [tracks, default "1 2 3 4 5 6 7"]
#   game folder   the original game (its GFX and DATA folders, as unzipped), default $GAME_DIR or
#                 "C:/GOG Games/Screamer 2"
# Everything it writes stays in build/s2/ (game data, never committed): the references as ref<n>.bin.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
tracks=${2:-1 2 3 4 5 6 7}
image="verilator/verilator:v5.050"
out="$root/build/s2"
mkdir -p "$out"
[ -f "$game/GFX/MAP1H01.DAT" ] || { echo "$game does not look like Screamer 2's folder (no GFX/MAP1H01.DAT)"; exit 1; }

py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
for n in $tracks; do
    "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$out/ref$n.bin" > /dev/null
done

# Git Bash on Windows: hand Docker Windows paths and stop MSYS rewriting /work.
if command -v cygpath >/dev/null 2>&1; then
    mount=$(cygpath -w "$root")
    game_mount=$(cygpath -w "$game")
else
    mount=$root
    game_mount=$game
fi
src="firmware/track_build.c firmware/dat.c firmware/tb_mmp.c firmware/tb_spl.c firmware/tb_gfx.c \
     firmware/tb_hud.c firmware/crc32.c tools/s2/check_track_build_s2.c"
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -v "$mount:/work" -v "$game_mount:/game:ro" -w /work --entrypoint sh "$image" -c "
    gcc -O2 -g -Wall -Wextra -Wdouble-promotion -fsanitize=address,undefined -fno-sanitize-recover \
        $src -lm -o build/s2/check_track_build_s2 &&
    for n in $tracks; do echo \"track \$n:\"; build/s2/check_track_build_s2 /game build/s2/ref\$n.bin \$n || exit 1; done
"
