#!/bin/sh
# Host test of the cars touching (firmware/race.c: the game's solver 0x245a3 and its push 0x20a18, the order of the game's frame, the computers' driver 0x17726)
# against the original's routines. The reference (OpenScreamer's, checked there against the game in DOSBox) runs a race of four cars at 20 ms steps with the
# player's car standing or holding the gas (tools/s2/ref_trace/ref_run.sh race: every car's whole record after every frame, the places as whole numbers as
# the game has them). For each frame at which its solver stopped a step (a touch) the firmware's race (firmware/race.c with its car physics, natively in Docker,
# tools/s2/race_replay_s2.c, built with S2_WHOLE_PLACES so that its places are whole numbers too) is started from the reference's state six frames before and
# run on for 175 frames; the two are compared car by car (tools/s2/aicontact_s2.py): the cars the solver stopped, the error at the touch and 3 frames after
# it, the worst in the 10 and the 150 frames (3 s) after it, how long the error stays within 2 units, and the "jerk", the largest difference of a car's move
# in one frame in the six after the touch (the shove). Also windows with no touch asked for (the driver and the physics alone), which show how far the two
# runs part without any touch in 3 s (float32 against the original's 53-bit sums, a corner on the edge of the road cut to the other whole number).
# Usage: tools/s2/check_aicontact_s2.sh [game folder] [reference folder]
#   game folder       the unzipped original, default $GAME_DIR or "C:/GOG Games/Screamer 2"
#   reference folder  default $REF_DIR or ../ref next to the repository: build-src/ (the reference's program, built by ref_run.sh build), the traces go to traces_race/
#   FW=<folder>       the firmware to test (the folder holding firmware/), default this repository's; an older tree (a git archive of a commit) shows what it did before
#   TRACKS="1 2 .."   the tracks (default all seven)       LEVELS="0 1 2"   the computers' levels, 0 pro, 1 normal, 2 rookie (the player's car stands)
#   GAS=1             also the player's car holding the gas (level 1)       CARS="0 3"   also other cars for the player (car 0 .. 4, level 1: the other three
#                     cars follow, the reference's own rule); default "0 3"      KEEP=1   keep the reference's traces (33 MB each) in <reference folder>/traces_race/
# Everything it writes stays in build/s2/aicontact/ (images, replays and the table, which hold the game's numbers: never commit them).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
ref=${2:-${REF_DIR:-$root/../ref}}
fw=${FW:-$root}
tracks=${TRACKS:-"1 2 3 4 5 6 7"}
levels=${LEVELS:-"0 1 2"}
cars=${CARS-"0 3"}
out="$root/build/s2/aicontact"
work="$out/src"
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
exe="$ref/build-src/port/build/screamer-remastered.exe"
[ -f "$exe" ] || exe="$ref/build-src/port/build/screamer-remastered"
[ -f "$exe" ] || { echo "no reference program in $ref/build-src/port/build (tools/s2/ref_trace/ref_run.sh build)"; exit 1; }
mkdir -p "$out" "$ref/traces_race"

# the jobs "track:level:mode:car"; the player's car number c (0 .. 4) gives the reference's grid: team 1 car c + 1, then the next three cars in turn, one of each team
jobs=""
for n in $tracks; do
    for lv in $levels; do jobs="$jobs $n:$lv:park:1"; done
    [ -n "$GAS" ] && jobs="$jobs $n:1:gas:1"
    for c in $cars; do jobs="$jobs $n:1:park:$c"; done
done
grid_of() { echo "1:$(($1 + 1)),2:$((($1 + 1) % 4 + 1)),3:$((($1 + 2) % 4 + 1)),4:$((($1 + 3) % 4 + 1))"; }
for j in $jobs; do
    n=${j%%:*}; c=${j##*:}
    [ -f "$out/ai${n}c$c.bin" ] || S2CARS=$(grid_of "$c") "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$out/ai${n}c$c.bin" > /dev/null
done

# the reference's races
export S2_GAME="$game" REF_WORK="$ref"
name_of() { echo "race.t$1.L$2.$3.c$4.ref.bin"; }
for j in $jobs; do
    IFS=: read -r n lv mode c <<EOF
$j
EOF
    [ -f "$ref/traces_race/$(name_of "$n" "$lv" "$mode" "$c")" ] || bash "$root/tools/s2/ref_trace/ref_run.sh" race "$n" "$lv" "$mode" 2500 "$c" > /dev/null
done

# the firmware, started from the reference's states (one program, one run of all of them)
rm -rf "$work"
mkdir -p "$work"
cp "$fw"/firmware/*.c "$fw"/firmware/*.h "$root/tools/s2/race_replay_s2.c" "$work/"
"$py" -I - "$work/hw.h" <<'PY'
import sys
p = sys.argv[1]
t = open(p).read()
t = t[:t.index("/* Cache maintenance")] + """static inline void dcache_invalidate(void) {}
static inline void icache_sync(void) {}
#define csr_read(csr)   0u
static inline uint32_t rdcycle(void) { return 0; }
static inline int32_t fixmul(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
static inline uint32_t opaque(uint32_t x) { return x; }
static inline float opaquef(float x) { return x; }
"""
open(p, "w").write(t)
PY
script="$out/runs.sh"
: > "$script"
for j in $jobs; do
    IFS=: read -r n lv mode c <<EOF
$j
EOF
    trace="$ref/traces_race/$(name_of "$n" "$lv" "$mode" "$c")"
    k0s=""
    while read -r frame cs; do
        [ $((frame - 6)) -gt 0 ] && [ $((frame + 176)) -lt 2500 ] && k0s="$k0s $((frame - 6))"
    done <<EOF
$("$py" -I "$root/tools/s2/aicontact_s2.py" events "$trace")
EOF
    gas=""; [ "$mode" = gas ] && gas="REPLAY_GAS=1"
    tag="t$n.L$lv.$mode.c$c"
    [ -n "$k0s" ] && echo "RACE_LEVEL=$lv $gas ./race_replay /o/ai${n}c$c.bin $n /r/$(name_of "$n" "$lv" "$mode" "$c") 175 /o/ev.$tag. $k0s -g $(grid_of "$c")" >> "$script"
    echo "RACE_LEVEL=$lv $gas ./race_replay /o/ai${n}c$c.bin $n /r/$(name_of "$n" "$lv" "$mode" "$c") 175 /o/win.$tag. 300 700 1100 1500 1900 -g $(grid_of "$c")" >> "$script"
done
if command -v cygpath >/dev/null 2>&1; then
    mount=$(cygpath -wa "$out"); game_mount=$(cygpath -wa "$game"); ref_mount=$(cygpath -wa "$ref/traces_race"); fw_mount=$(cygpath -wa "$work")
else
    mount=$out; game_mount=$game; ref_mount=$ref/traces_race; fw_mount=$work
fi
LIST="road.c track.c geom.c setup.c raster.c blit.c dlog.c crc32.c dat.c console.c car.c tb_hud.c tb_mmp.c s2car.c s2params.c s2ghost.c"
MSYS_NO_PATHCONV=1 docker run --rm -e S2_GAME=/game -v "$fw_mount:/src" -v "$mount:/o" -v "$game_mount:/game:ro" -v "$ref_mount:/r:ro" --entrypoint sh verilator/verilator:v5.050 -c "
    cd /src && gcc -O2 -w -DS2_WHOLE_PLACES=1 -o /o/race_replay race_replay_s2.c $LIST -lm && cd /o && sh /o/runs.sh"

# the comparison
table="$out/table.txt"
: > "$table"
for j in $jobs; do
    IFS=: read -r n lv mode c <<EOF
$j
EOF
    trace="$ref/traces_race/$(name_of "$n" "$lv" "$mode" "$c")"
    tag="t$n.L$lv.$mode.c$c"
    "$py" -I "$root/tools/s2/aicontact_s2.py" events "$trace" | while read -r frame cs; do
        [ $((frame - 6)) -gt 0 ] && [ $((frame + 176)) -lt 2500 ] || continue
        echo "event track=$n level=$lv $mode car=$c $("$py" -I "$root/tools/s2/aicontact_s2.py" compare "$trace" "$out/ev.$tag.$((frame - 6)).bin" $((frame - 6)) "$frame" "$cs")" >> "$table"
    done
    for k0 in 300 700 1100 1500 1900; do
        echo "window track=$n level=$lv $mode car=$c $("$py" -I "$root/tools/s2/aicontact_s2.py" window "$trace" "$out/win.$tag.$k0.bin" "$k0")" >> "$table"
    done
done
cat "$table"
if [ -z "$KEEP" ]; then
    for j in $jobs; do
        IFS=: read -r n lv mode c <<EOF
$j
EOF
        rm -f "$ref/traces_race/$(name_of "$n" "$lv" "$mode" "$c")"
    done
fi
"$py" -I "$root/tools/s2/aicontact_s2.py" summary "$table"
