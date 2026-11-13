#!/bin/sh
# sim_race.sh with the testbench named (a CPU variant's: sim/soc_<variant>/files.f).
# Usage: tools/s2/sim_race_tb.sh <testbench> <dir with firmware.bin and program.bin> <track image> <track 1..7> <frames> <name>
cd "$(dirname "$0")/../.." || exit 1
start=$(date +%s)
SIM_QUICK=1 SIM_GAME=build/gamedata SIM_IMAGE=$3 SIM_TRACK=$4 SIM_RACE_FRAMES=$5 SIM_KEYS=8000 \
    sh tools/sim.sh "$1" "$2/firmware.bin" "build/sim/$6" 4000 > "build/sim/$6.log" 2>&1
echo "exit $? after $(( $(date +%s) - start )) s" > "build/sim/$6.done"
