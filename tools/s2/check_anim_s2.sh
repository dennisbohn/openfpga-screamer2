#!/bin/sh
# Host test of the animated scenery (firmware/s2anim.c, docs/development.md "Animated scenery"): the firmware's model of the tracks' ANIMATIONS rows and MAPn.PTH paths
# natively in Docker under the address and undefined-behaviour sanitizers, against
#   - tools/s2/anim_ref.py, an independent reading of the original's routines (STARTL.EXE 0x30ec2, 0x31060, 0x2cee4, 0x126ad, 0x129d5) in double precision: a tour of the player
#     over every track's rows, three laps of 20,000 frames, shown / heard equal, places and angles within 1 (a truncation falling the other way in float); a negative control
#     (the same comparison with a deliberately wrong reach) must fail;
#   - the track image's copy of MISC.MLB (the materials of the animated meshes, in front of the MLB: track_build_s2()) against the container's: tools/s2/check_anim_s2.py misc;
#   - the running original's own numbers, if a recording of dosbox_anim.py is there (build/check/dosbox_anim_t<n>/frames.jsonl): its rows and object records frame by frame.
# Usage: tools/s2/check_anim_s2.sh [game folder]    default $GAME_DIR or "C:/GOG Games/Screamer 2" (the unzipped game)
# Everything it writes stays in build/s2/ (the states hold the game's numbers: never commit them).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
image="verilator/verilator:v5.050"
out="$root/build/s2"
mkdir -p "$out"
[ -f "$game/DATA/MAP1.DAT" ] || { echo "$game does not look like Screamer 2's folder (no DATA/MAP1.DAT)"; exit 1; }
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
chk="$root/tools/s2/check_anim_s2.py"
for t in 1 2 3 4 5 6 7; do
    "$py" -I "$chk" scenario "$game" $t "$out/anim_scn_t$t.txt" "$out/anim_exp_t$t.txt"
    "$py" -I "$chk" limits "$game" $t
done
for t in 1 2 3 4 5 6 7; do
    "$py" -I "$root/tools/s2/make_track_s2.py" "$game" $t "$out/anim_image_t$t.bin" > /dev/null
    "$py" -I "$chk" misc "$game" "$out/anim_image_t$t.bin" $t
done
"$py" -I "$chk" misc "$game" "$out/anim_image_t3.bin" 3 negative
dos=""
for t in 1 2 3 4 5 6 7; do
    d="$root/build/check/dosbox_anim_t$t"
    [ -f "$d/frames.jsonl" ] || continue
    "$py" -I "$chk" dosbox_scenario "$d" "$out/anim_dos_scn_t$t.txt"
    dos="$dos $t"
done

# Git Bash on Windows: hand Docker Windows paths and stop MSYS rewriting /work.
if command -v cygpath >/dev/null 2>&1; then
    mount=$(cygpath -w "$root")
    game_mount=$(cygpath -w "$game")
else
    mount=$root
    game_mount=$game
fi
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -v "$mount:/work" -v "$game_mount:/game:ro" -w /work --entrypoint sh "$image" -c "
    gcc -O2 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover \
        firmware/s2anim.c firmware/s2params.c firmware/dat.c firmware/track_build.c firmware/smg.c firmware/s2env.c firmware/tb_mmp.c firmware/tb_spl.c firmware/tb_gfx.c \
        firmware/tb_hud.c firmware/crc32.c tools/s2/check_anim_s2.c -o build/s2/check_anim -lm &&
    for t in 1 2 3 4 5 6 7; do build/s2/check_anim /game \$t < build/s2/anim_scn_t\$t.txt > build/s2/anim_c_t\$t.txt 2> build/s2/anim_c_t\$t.err || exit 1; done &&
    for t in $dos; do build/s2/check_anim /game \$t < build/s2/anim_dos_scn_t\$t.txt > build/s2/anim_dos_c_t\$t.txt 2> /dev/null || exit 1; done
"
fail=0
for t in 1 2 3 4 5 6 7; do
    "$py" -I "$chk" compare "$game" $t "$out/anim_c_t$t.txt" "$out/anim_exp_t$t.txt" || fail=1
done
"$py" -I "$chk" compare "$game" 4 "$out/anim_c_t4.txt" "$out/anim_exp_t4.txt" negative || fail=1
for t in $dos; do
    "$py" -I "$chk" dosbox "$game" $t "$root/build/check/dosbox_anim_t$t" "$out/anim_dos_c_t$t.txt" || fail=1
done
[ $fail = 0 ] && echo "animated scenery: PASS" || { echo "animated scenery: FAIL"; exit 1; }
