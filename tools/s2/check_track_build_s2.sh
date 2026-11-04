#!/bin/sh
# Host test of the Screamer 2 track build on the Pocket (firmware/track_build.c, track_build_s2()): the
# image built natively in Docker from the containers a track needs (GFX/MAPnH01.DAT, DATA/MAPn.DAT,
# GFX/ENVPn01.DAT) and the cars of the grid (each from its team's GFX/CARS<t>01.DAT, unpacked by
# s2_car_unpack()) must be byte-identical with the one tools/s2/make_track_s2.py builds from the same files
# (the reference), for each of the seven tracks, with three grids: the race's default (team 1 car 1, team 2 car 2,
# team 3 car 3, team 4 car 4), one out of order (bonus cars, a car twice) and the five cars of team 1 (the images of
# the physics check, check_car.sh). The S1 pattern is tools/check_track_build.sh. Then tools/s2/check_cars_s2.sh:
# the cars loader (firmware/s2teams.c) on the ZIP, stored and deflated.
# Usage: tools/s2/check_track_build_s2.sh [game folder] [tracks, default "1 2 3 4 5 6 7"]
#   game folder   the original game (its GFX and DATA folders, as unzipped), default $GAME_DIR or
#                 "C:/GOG Games/Screamer 2"
# Everything it writes stays in build/s2/ (game data, never committed): the references as ref<n>.bin (default
# grid) and ref<n>.g<k>.bin (the others).
# Before the build check, tools/s2/check_env_s2.py checks the references' windows (routine 160) against the
# original's reflection picture and tools/s2/check_cars_s2.py their cars against the cars' own files.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
tracks=${2:-1 2 3 4 5 6 7}
image="verilator/verilator:v5.050"
out="$root/build/s2"
grids="1:1,2:2,3:3,4:4 3:5,1:2,4:1,3:5 1:1,1:2,1:3,1:4,1:5"
mkdir -p "$out"
[ -f "$game/GFX/MAP1H01.DAT" ] || { echo "$game does not look like Screamer 2's folder (no GFX/MAP1H01.DAT)"; exit 1; }

py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
for n in $tracks; do
    k=0
    for g in $grids; do
        ref="$out/ref$n.bin"; [ $k -eq 0 ] || ref="$out/ref$n.g$k.bin"
        S2CARS=$g "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$ref" > /dev/null
        [ $k -ne 0 ] || "$py" -I "$root/tools/s2/check_env_s2.py" "$game" "$ref" "$n"      # the windows' reflection
        "$py" -I "$root/tools/s2/check_cars_s2.py" "$game" "$ref" "$n" "$g"                # the cars' materials
        k=$((k + 1))
    done
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
     firmware/tb_hud.c firmware/crc32.c firmware/s2teams.c firmware/zip.c tools/s2/check_track_build_s2.c"
runs=""
for n in $tracks; do
    k=0
    for g in $grids; do
        ref="ref$n.bin"; [ $k -eq 0 ] || ref="ref$n.g$k.bin"
        runs="$runs echo \"track $n, grid $g:\"; build/s2/check_track_build_s2 /game build/s2/$ref $n $g || exit 1;"
        k=$((k + 1))
    done
done
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -v "$mount:/work" -v "$game_mount:/game:ro" -w /work --entrypoint sh "$image" -c "
    gcc -O2 -g -Wall -Wextra -Wdouble-promotion -fsanitize=address,undefined -fno-sanitize-recover \
        $src -lm -o build/s2/check_track_build_s2 &&
    $runs true
"

sh "$root/tools/s2/check_cars_s2.sh" "$game"
