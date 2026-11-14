#!/bin/sh
# Host test of the championship's rules (firmware/s2league.c): the firmware's own code on the PC in Docker with the address and
# undefined-behaviour sanitizers: the leagues' tables against STARTL.EXE's, the grid, the places of a race (the player who left is last), the
# points, the standings (ties), what winning opens (with the real s2save). See the header of tools/s2/check_league_s2.c. No game files are
# needed. Exit code 0 if everything passed.
# Usage: tools/s2/check_league_s2.sh
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
image="verilator/verilator:v5.050"
mkdir -p "$root/build/s2"

# Git Bash on Windows: hand Docker Windows paths and stop MSYS rewriting /work.
if command -v cygpath >/dev/null 2>&1; then
    mount=$(cygpath -w "$root")
else
    mount=$root
fi
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -v "$mount:/work" -w /work --entrypoint sh "$image" -c "
    gcc -O2 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover \
        firmware/s2league.c firmware/s2save.c firmware/crc32.c tools/s2/check_league_s2.c -o build/s2/check_league_s2 -lm &&
    build/s2/check_league_s2
"
