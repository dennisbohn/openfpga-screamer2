#!/bin/sh
# Run the SoC simulation with the game's files and print a short summary (tools/simsum.py).
# Usage: tools/simcheck.sh [--full] [--save] [name]
#   quick (default): no resets (SIM_QUICK). --full: as on the Pocket, with the two resets after the
#   first run. --save: this run becomes the baseline the summary compares with. The log goes to
#   build/sim/<name>.log (name default "check"), the sound of the run to build/sim/audio.wav
#   (SIM_WAV names another file).
# The program so far reads the game's ZIP and lists its files (firmware/files.c), then reads the
# first race's small files whole (the read test: only the ones below 100 KB in the simulation). Needs
# build/firmware-debug/ (tools/fw.sh DEBUG=1: the log lines the summary reads come from that build;
# SIM_FW names another folder) and the files core/data.json names in build/gamedata/ (SIM_GAME):
# SCREAMER2.zip (any ZIP of the game's folders, stored or deflated: the testbench reads it where the
# firmware asks), and GAME.GOG and GAME.DAT at the top (the firmware only asks their sizes: a small
# file named GAME.GOG will do).
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
quick=1; save=""; name=check
for a in "$@"; do
    case "$a" in
        --full) quick="" ;;
        --save) save=--save ;;
        *) name=$a ;;
    esac
done
cd "$root"
[ -f build/gamedata/SCREAMER2.zip ] || { echo "build/gamedata/ has no game files (see the comment above)"; exit 2; }
start=$(date +%s)
rc=0
SIM_QUICK=$quick SIM_GAME=${SIM_GAME:-build/gamedata} SIM_WAV=${SIM_WAV:-build/sim/audio.wav} \
    sh tools/sim.sh soc \
    "${SIM_FW:-build/firmware-debug}/firmware.bin" "build/sim/$name" 400 > "build/sim/$name.log" 2>&1 || rc=$?
echo "simulation ${quick:+(quick) }exit $rc after $(( $(date +%s) - start )) s, log build/sim/$name.log"
python -I tools/simsum.py "build/sim/$name.log" $save
exit $rc
