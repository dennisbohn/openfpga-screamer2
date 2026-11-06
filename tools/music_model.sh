#!/bin/sh
# The music ring against the loads (docs/development.md "The menus' music"): firmware/music.c and firmware/slot.c, the code that runs on the
# Pocket, on the PC against a model of the APF's data slot, the mixer's music ring and the CPU's clock in simulated time
# (tools/host/music_hw.c). Scenarios of the menus' loads, a random mix of them and negative controls (tools/host/music_model.c). No game
# files are needed. Exit code 0 if all tests passed.
# Usage: tools/music_model.sh
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
image="verilator/verilator:v5.050"
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub

src="$root/build/host/musicmodel"
rm -rf "$src"
mkdir -p "$src/host"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$src/"
cp "$root/tools/host/music_model.c" "$root/tools/host/music_hw.c" "$root/tools/host/music_hw.h" "$src/host/"
"$py" -I "$root/tools/host/music_hw_patch.py" "$src/hw.h"

if command -v cygpath >/dev/null 2>&1; then
    mount=$(cygpath -wa "$root/build")
else
    mount=$root/build
fi
MSYS_NO_PATHCONV=1 docker run --rm -e MHW_SEED -e MHW_TRACE -v "$mount:/b" --entrypoint sh "$image" -c "
    cd /b/host/musicmodel && gcc -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined -w -o ../music_model host/music_model.c host/music_hw.c music.c slot.c dlog.c console.c &&
    /b/host/music_model"
