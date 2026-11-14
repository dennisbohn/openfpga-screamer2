#!/bin/sh
# Build the CPU firmware (firmware/) in Docker with the pinned toolchain of firmware/docker/
# (image screamer-fw:trixie-20261005, built on first use) and copy firmware.bin (the boot
# loader) and program.bin (the program in the SDRAM) to build/assets/, from where
# tools/package.sh puts them on the SD card.
# Usage: tools/fw.sh                 the release firmware, copied to build/assets/ (no development code: firmware/debug.h)
#        tools/fw.sh DEBUG=1         the development firmware in build/firmware-debug/, not copied: the timing overlay (L + R in a race), the statistics
#                                    and the button record in debug.txt, the shortcut to the desk scenes, the ride, everything open from the start.
#                                    The simulation and the tools that read the log's statistics need this build. The variants below (PROF=, TAG=) are
#                                    debug builds too unless DEBUG=0 is given with them.
#        tools/fw.sh UNLOCK_ALL=1 TAG=open   the release's code with everything open from the start (or DEBUG=1 UNLOCK_ALL=0 TAG=x: a debug build with the original's gating)
#        tools/fw.sh FAULT=IFETCH    a fault test for sim/soc in build/firmware-IFETCH/, not copied
#        tools/fw.sh EXTRA_CFLAGS=-DX=1 TAG=x   other defines, in build/firmware-x/, not copied (e.g. -DHW_FULL_CYCLES=100: the computers' steps run in the scene at once)
#        tools/fw.sh EXTRA_CFLAGS=-DS2_SIM_END=1 TAG=simend   the end of the race drawn over frames 2 to 9 of the simulated race, with its cost in the log ("end screen frame 3: 3341 us")
#        tools/fw.sh EXTRA_CFLAGS=-DS2_SIM_INTRO=1 TAG=intro   the start's intro (the LOADING picture, the attract sequence on track 1) run in the simulation: tools/s2/sim_race.sh build/sim/<dir> build/s2/track1.bin 1 600 <name> (the image of track 1; about 20 minutes)
#        tools/fw.sh EXTRA_CFLAGS=-DS2_SIM_MENUMUSIC=1 TAG=menumusic   the menus' song on the real mixer in the simulation, with a short read, long reads and a chain of chunks while it plays (a made-up CD image: python -I tools/musicwav.py make build/simmusic, the game's ZIP and a dummy file next to it; SIM_GAME=build/simmusic SIM_IMAGE=build/simmusic/dummy.bin SIM_WAV=build/sim/menumusic.wav tools/sim.sh soc build/firmware-menumusic/firmware.bin build/sim/menumusic 4000; python -I tools/musicwav.py check build/sim/menumusic.wav; docs/development.md "The menus' music")
#        tools/fw.sh PROF=1          with the cycle counters of firmware/prof.h, for the simulation, in build/firmware-prof/, not copied
# Output: build/firmware/{firmware,program}.elf, .bin, .lst (disassembly), .map
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
image="screamer-fw:trixie-20261005"
limit=32768                         # boot RAM: firmware.bin may have at most 32 KB
prog_limit=1048576                  # data slot 2 (core/data.json): program.bin

# Git Bash on Windows: hand Docker Windows paths and stop MSYS rewriting /work.
if command -v cygpath >/dev/null 2>&1; then
    mount=$(cygpath -w "$root")
    context=$(cygpath -w "$root/firmware/docker")
else
    mount=$root
    context=$root/firmware/docker
fi

if ! docker image inspect "$image" > /dev/null 2>&1; then
    echo "Building the toolchain image $image (once)"
    MSYS_NO_PATHCONV=1 docker build -t "$image" "$context"
fi

fault=""
prof=""
debug=""
for arg in "$@"; do
    case "$arg" in FAULT=*) fault=${arg#FAULT=} ;; PROF=*) prof=-prof ;; CODE_PAD=*) prof=$prof-pad${arg#CODE_PAD=} ;; CODE_PAD2=*) prof=$prof-padb${arg#CODE_PAD2=} ;; TAG=*) prof=$prof-${arg#TAG=} ;; DEBUG=*) debug=${arg#DEBUG=} ;; esac
done
# the measuring variants are debug builds (their log lines are the debug build's) unless the caller says DEBUG=0
extra=""
case "$*" in *DEBUG=*|*UNLOCK_ALL=*) ;; *) [ -n "$prof" ] && { extra="DEBUG=1"; debug=1; } ;; esac
[ "$debug" = 0 ] && debug=""
[ -n "$debug" ] && [ -z "$prof" ] && prof=-debug
out=firmware${fault:+-$fault}$prof

MSYS_NO_PATHCONV=1 docker run --rm -v "$mount:/work" -w /work/firmware "$image" \
    make --no-print-directory OUT=../build/$out "$@" $extra info

bin="$root/build/$out/firmware.bin"
prog="$root/build/$out/program.bin"
size=$(wc -c < "$bin" | tr -d ' ')
prog_size=$(wc -c < "$prog" | tr -d ' ')
if [ "$size" -gt "$limit" ]; then
    echo "firmware.bin has $size bytes, more than the $limit bytes of the boot RAM"
    exit 1
fi
if [ "$prog_size" -gt "$prog_limit" ]; then
    echo "program.bin has $prog_size bytes, more than the $prog_limit bytes of data slot 2"
    exit 1
fi
if [ -n "$fault$prof" ]; then
    echo "not copied: build/$out/firmware.bin, $size bytes${debug:+ (a debug build)}"
    exit 0
fi
assets="$root/build/assets/screamer2/Chainy.Screamer2"
mkdir -p "$assets"
cp "$bin" "$assets/firmware.bin"
cp "$prog" "$assets/program.bin"
echo "boot RAM: $size of $limit bytes; program.bin: $prog_size bytes; both copied to build/assets/screamer2/Chainy.Screamer2/"
