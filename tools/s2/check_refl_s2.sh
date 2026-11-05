#!/bin/sh
# The picture the cars' windows reflect, by the kind of the ground under the car (firmware/s2env.c, s2_env_phase()), against STARTL.EXE's own code: tools/s2/check_refl_s2.py
# follows the game's chain of compares (0x1f372) for every kind and the firmware's function must give the same phase and rate for each of the 256. Needs the flat image
# (tools/dis.sh -f makes it into build/exe/) and Docker as the other checks; reads the game folder only through that image.
# Usage: tools/s2/check_refl_s2.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2"
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
out="$root/build/s2"
mkdir -p "$out"
py=python3
"$py" -c "" 2>/dev/null || py=python
GAME_DIR="$game" sh "$root/tools/dis.sh" -f 0x1f372 0x1f376 > /dev/null          # (makes build/exe/startl_fix.bin if it is not there)
"$py" -I "$root/tools/s2/check_refl_s2.py" "$root/build/exe/startl_fix.bin" | tr -d '\r' > "$out/refl_exe.txt"
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -w "$root"); else mount=$root; fi
MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/work" -w /work --entrypoint sh verilator/verilator:v5.050 -c "
    gcc -O2 -Wall -Wextra -o build/s2/check_refl_s2 tools/s2/check_refl_s2.c firmware/s2env.c && build/s2/check_refl_s2 > build/s2/refl_fw.txt"
if cmp -s "$out/refl_exe.txt" "$out/refl_fw.txt"; then
    echo "PASS: s2_env_phase() gives the phase and rate of STARTL.EXE's chain for all 256 kinds ($(awk '$2 != "0"' "$out/refl_exe.txt" | wc -l) kinds with a picture of their own: $(awk '$2 != "0" { printf "%s ", $1 }' "$out/refl_exe.txt"))"
else
    echo "FAIL: the firmware's table differs from the program's"; diff "$out/refl_exe.txt" "$out/refl_fw.txt" | head; exit 1
fi
