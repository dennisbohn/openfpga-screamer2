#!/bin/sh
# Host test of the attract race (menus row 9): the firmware's own code on the PC in Docker (firmware/race.c, s2ghost.c, s2cam.c, s2car.c on the track images of
# tools/s2/make_track_s2.py, with the address and undefined-behaviour sanitizers). See the header of tools/s2/check_attract_s2.c for what it checks: the attract race
# (four computer cars, nobody to drive car 0) on all seven tracks, its cameras' rotation, that it leaves the recorder and the normal race alone.
# The second part builds tools/s2/normal_hash.c against the base commit's firmware/ ($BASE, default the one this branch grew from) and against the working tree and
# compares what they print: a normal race and the replay's cameras must be bit for bit the same race in both; with -DNEG=1 (race_attract set) the output must differ.
# Usage: tools/s2/check_attract_s2.sh [game folder]       default $GAME_DIR or "C:/GOG Games/Screamer 2"; BASE=<commit> for another base
# Everything it writes stays in build/s2/. Exit code 0 if everything passed.
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
game=${1:-${GAME_DIR:-C:/GOG Games/Screamer 2}}
base=${BASE:-30841cc}
out="$root/build/s2"
src="$out/attractsrc"
bsrc="$out/attractbase"
mkdir -p "$out"
py=python3
"$py" -c "" 2>/dev/null || py=python     # Windows: python3 may be the Store's stub
for n in 1 2 3 4 5 6 7; do
    [ -f "$out/car$n.bin" ] || S2CARS=1:1,1:2,1:3,1:4,1:5 "$py" -I "$root/tools/s2/make_track_s2.py" "$game" "$n" "$out/car$n.bin" > /dev/null
done
# the Pro row's middle value of SKILLS, read from MISC.DTR here (not by the firmware's reader)
pace=$("$py" -I - "$game" "$root" <<'PY'
import sys
sys.path.insert(0, sys.argv[2] + "/tools")
import datfile
t = datfile.extract(datfile.read(sys.argv[1] + "/DATA/GAME.DAT"), "MISC.DTR").decode("latin-1").split("\n")
rows = [l for l in t if l.strip().startswith("f") and not l.strip().startswith("*")]
print(rows[0].split()[3][1:])
PY
)
echo "Pro row's middle value of SKILLS: $pace"
patch_hw() {
"$py" -I - "$1/hw.h" <<'PY'
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
}
rm -rf "$src" "$bsrc"
mkdir -p "$src" "$bsrc"
cp "$root"/firmware/*.c "$root"/firmware/*.h "$root/tools/s2/check_ghost_s2.c" "$root/tools/s2/check_attract_s2.c" "$root/tools/s2/normal_hash.c" "$src/"
patch_hw "$src"
git -C "$root" archive "$base" firmware | tar -x -C "$bsrc"
cp "$bsrc"/firmware/*.c "$bsrc"/firmware/*.h "$bsrc/"
rm -rf "$bsrc/firmware"
cp "$root/tools/s2/normal_hash.c" "$bsrc/"
git -C "$root" show "$base:tools/s2/check_ghost_s2.c" > "$bsrc/check_ghost_s2.c"       # (the base's own: s2gh_rec_begin has taken the set-up since)
patch_hw "$bsrc"
if command -v cygpath >/dev/null 2>&1; then mount=$(cygpath -wa "$root/build"); game_mount=$(cygpath -wa "$game"); else mount=$root/build; game_mount=$game; fi
LIST="road.c track.c geom.c setup.c raster.c car.c drive.c opp.c ghost.c efx.c scenery.c sound.c speech.c blit.c dlog.c crc32.c dat.c console.c menu.c champ.c intro.c tb_hud.c tb_spl.c tb_mmp.c hud.c s2car.c s2params.c s2ghost.c race.c s2cam.c s2cockpit.c"
MSYS_NO_PATHCONV=1 docker run --rm -e ASAN_OPTIONS=detect_leaks=0 -e ATTRACT_DUMP=/b/s2 -e S2_GAME=/game -v "$mount:/b" -v "$game_mount:/game:ro" --entrypoint sh verilator/verilator:v5.050 -c "
    cd /b/s2/attractsrc && gcc -O2 -g -w -fsanitize=address,undefined -fno-sanitize-recover -o ../check_attract_s2 check_attract_s2.c $LIST -lm &&
    gcc -O2 -w -o ../normal_hash_new normal_hash.c $LIST -lm &&
    gcc -O2 -w -DNEG=1 -o ../normal_hash_neg normal_hash.c $LIST -lm &&
    cd /b/s2/attractbase && gcc -O2 -w -o ../normal_hash_base normal_hash.c $LIST -lm &&
    cd /b/s2 && ./check_attract_s2 /b/s2 $pace &&
    ./normal_hash_base /b/s2 > normal_hash_base.txt && ./normal_hash_new /b/s2 > normal_hash_new.txt && ./normal_hash_neg /b/s2 > normal_hash_neg.txt &&
    if cmp -s normal_hash_base.txt normal_hash_new.txt; then echo \"normal race and replay cameras: base $base and working tree print the same\"; else echo 'FAIL: a normal race or the replay cameras differ from the base'; diff normal_hash_base.txt normal_hash_new.txt | head; exit 1; fi &&
    if cmp -s normal_hash_base.txt normal_hash_neg.txt; then echo 'FAIL: the negative control (race_attract set) did not change the race'; exit 1; else echo 'negative control: with race_attract set the fingerprint differs from the base (the comparison sees a change)'; fi"
