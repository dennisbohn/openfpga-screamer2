#!/bin/sh
# EXPERIMENT: where does the firmware's own track build (firmware/track_build.c) stop on Screamer 2's
# files? The files are only COPIED under the names the firmware looks for (build/s2/game2/, game
# data, never committed); the reference image is tools/s2/make_track_s2.py's.
# Usage: tools/s2/check_track_build_s2.sh <Screamer 2 folder> [track n, default 1]
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
s2=$1
n=${2:-1}
g="$root/build/s2/game2"
mkdir -p "$g/GFX" "$g/DATA"
cp "$s2/GFX/MAP${n}H01.DAT" "$g/GFX/MAP${n}H.DAT"
cp "$s2/GFX/GAME1.DAT" "$g/GFX/GAME.DAT"
cp "$s2/DATA/MAP$n.DAT" "$s2/DATA/GEN.DAT" "$s2/DATA/LOCAL.DAT" "$g/DATA/"
python "$root/tools/s2/make_track_s2.py" "$s2" "$n" "$root/build/s2/ref$n.bin" H 0 > /dev/null
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -w "$root"); else mount=$root; fi
src="firmware/track_build.c firmware/dat.c firmware/tb_mmp.c firmware/tb_spl.c firmware/tb_gfx.c firmware/tb_hud.c firmware/crc32.c tools/check_track_build.c"
MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/work" -w /work --entrypoint sh verilator/verilator:v5.050 -c "
    gcc -O2 -w $src -lm -o build/s2/check_track_build_s2 &&
    build/s2/check_track_build_s2 build/s2/game2 build/s2/ref$n.bin $n; echo exit \$?"
