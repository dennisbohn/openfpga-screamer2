#!/bin/sh
# The viewer's race in the Verilator simulation on one track, with the frame timeline of the last frame, the visit line and the
# first line of the log (docs/development.md "Driving mode", docs/screamer2/performance.md section 7).
# Usage: tools/s2/sim_race.sh <dir with firmware.bin and program.bin> <track image> <track 1..7> <frames> <name>
#   e.g. cp build/firmware/firmware.bin build/firmware/program.bin build/sim/x/ ;
#        tools/s2/sim_race.sh build/sim/x build/s2/track4.bin 4 220 x4
#   220 frames = 150 on the grid and 70 after GO, the gas held; about 9 minutes (several at a time are fine). The log goes
#   to build/sim/<name>.log (the picture to <name>.rgb, the debug.txt to <name>_log.txt), build/sim/<name>.done says when it ended.
# Needs build/gamedata/ (SCREAMER2.zip, GAME.GOG, GAME.DAT: tools/simcheck.sh) and the Docker image of tools/sim.sh.
cd "$(dirname "$0")/../.." || exit 1
start=$(date +%s)
SIM_QUICK=1 SIM_GAME=build/gamedata SIM_IMAGE=$2 SIM_TRACK=$3 SIM_RACE_FRAMES=$4 SIM_KEYS=8000 \
    sh tools/sim.sh soc "$1/firmware.bin" "build/sim/$5" 4000 > "build/sim/$5.log" 2>&1
echo "exit $? after $(( $(date +%s) - start )) s" > "build/sim/$5.done"
