#!/usr/bin/env bash
# Reference traces of Screamer 2's car physics, out of the user's OpenScreamer (see README.md).
#
#   OPENSCREAMER=<openscreamer checkout>  S2_GAME=<game folder>  REF_WORK=<work folder>  ./ref_run.sh <command>
#
#   build                          copy the checkout to $REF_WORK/build-src, apply s2_ref.patch, build the port
#   run <scenario> <track> [team model [step]]
#                                  one trace -> $REF_WORK/traces/<scenario>.t<track>.team<team>.model<model>.<step>.csv
#                                  scenario: a file name in scenarios/ without .txt; team 0..3, model 0..4 (default 0 and 1);
#                                  step: dt20 (20 ms, default) or dtport (the port's own pace, 1.1 / 17.7 s)
#   all                            the whole set (see README.md)
#   check                          run `all` twice and compare the two sets byte for byte
#
# Nothing in the user's checkout is touched; the traces are derived from the game and are not committed.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
: "${OPENSCREAMER:?set OPENSCREAMER to the OpenScreamer checkout}"
: "${S2_GAME:?set S2_GAME to the Screamer 2 game folder}"
WORK=${REF_WORK:-$HERE/work}
SRC=$WORK/build-src
OUT=$WORK/traces
MSYS=${MSYS:-/c/msys64}

build() {
    mkdir -p "$WORK"
    if [ ! -d "$SRC/.git" ]; then git clone -q --local "$OPENSCREAMER" "$SRC"; fi
    git -C "$SRC" rev-parse HEAD > "$WORK/openscreamer.commit"
    if [ ! -f "$SRC/port/src/s2_ref.h" ]; then git -C "$SRC" apply "$HERE/s2_ref.patch"; fi
    if [ -d "$MSYS/ucrt64/bin" ]; then      # Windows, MSYS2 UCRT64 (gcc + SDL2); SDL2 from the system, not built lean
        export PATH=$MSYS/ucrt64/bin:$PATH
        "$MSYS/mingw32/bin/cmake.exe" -S "$SRC/port" -B "$SRC/port/build" -G "MinGW Makefiles" \
            -DCMAKE_C_COMPILER="$MSYS/ucrt64/bin/gcc.exe" -DCMAKE_MAKE_PROGRAM="$MSYS/ucrt64/bin/mingw32-make.exe" \
            -DOS_SDL_LEAN=OFF -DCMAKE_BUILD_TYPE=Release
        "$MSYS/mingw32/bin/cmake.exe" --build "$SRC/port/build" -j 8
    else                                    # Linux: SDL2, cmake, a C compiler (untested)
        cmake -S "$SRC/port" -B "$SRC/port/build" -DCMAKE_BUILD_TYPE=Release
        cmake --build "$SRC/port/build" -j 8
    fi
}

exe() { if [ -f "$SRC/port/build/screamer-remastered.exe" ]; then echo "$SRC/port/build/screamer-remastered.exe"; else echo "$SRC/port/build/screamer-remastered"; fi; }

# directive of a scenario file: @name value
directive() { awk -v k="@$2" '$1 == k { print $2; exit }' "$1"; }

run() {
    local scn=$1 track=$2 team=${3:-0} model=${4:-1} step=${5:-dt20} dest=${6:-$OUT}
    local file=$HERE/scenarios/$scn.txt
    [ -f "$file" ] || { echo "no scenario $scn" >&2; return 1; }
    mkdir -p "$dest" "$WORK/home" "$WORK/run"
    local steps auto put robot flat
    steps=$(directive "$file" steps); auto=$(directive "$file" auto); put=$(directive "$file" put); robot=$(directive "$file" robot)
    flat=${FLAT:-$(directive "$file" flat)}      # @flat <road kind>: a level world of that kind, no walls (FLAT=<kind> overrides)
    local out=$dest/$scn.t$track${flat:+.flat$flat}.team$team.model$model.$step.csv
    local -a env=(SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy OS_NOMUSIC=1 OS_NOSOUND=1 "OS_S2PHYS=${steps:-1000}"
                  "OS_TRACK=$track" "OS_CAR=$model" "OS_S2TEAM=$team" "OS_S2REF=$file" "OS_S2REFOUT=$out")
    case $step in
        dt20) env+=(OS_S2STEP=0.02) ;;
        dtport) env+=(OS_S2RAW=0.05649718)         # the port's own pace: step 1.1 / 17.7 (its default), the picture's time 1 / 17.7
                steps=$(( ${steps:-1000} * 100 / 311 ))   # (the same seconds: 0.0621 s a step)
                env=("${env[@]/OS_S2PHYS=*/OS_S2PHYS=$steps}") ;;
        *) echo "step: dt20 or dtport" >&2; return 1 ;;
    esac
    [ -n "$auto" ] && env+=("OS_S2AUTO=$auto")
    [ -n "$put" ] && env+=("OS_S2REFPUT=$put")
    [ -n "$robot" ] && env+=(OS_S2REFROBOT=1)
    [ -n "$flat" ] && env+=("OS_S2FLAT=$flat" OS_S2NONEAR=1)
    ( export PATH=$MSYS/ucrt64/bin:$PATH; cd "$WORK/run"
      env "${env[@]}" "$(exe)" -game 2 -data "$S2_GAME" -home "$WORK/home" -renderer software > "$WORK/run/last.log" 2>&1 ) \
        || { echo "run failed: $scn track $track (see $WORK/run/last.log)" >&2; return 1; }
    echo "$out"
}

all() {
    local dest=${1:-$OUT} f s t k pair team model
    for f in "$HERE"/scenarios/*.txt; do
        s=$(basename "$f" .txt)
        if [ -n "$(directive "$f" surfaces)" ]; then                       # d_surface: one run per road kind, on its own track
            for pair in $(awk '$1 == "@surfaces" { $1 = ""; print; exit }' "$f"); do FLAT=${pair#*:} run "$s" "${pair%%:*}" 0 1 dt20 "$dest"; done
            continue
        fi
        for t in $(awk '$1 == "@tracks" { $1 = ""; print; exit }' "$f"); do run "$s" "$t" 0 1 dt20 "$dest"; done
    done
    # (h) every car of every team in (a) on track 1, and on level ground
    for team in 0 1 2 3; do for model in 0 1 2 3 4; do
        [ $team = 0 ] && [ $model = 1 ] && continue
        run a_full_throttle 1 $team $model dt20 "$dest"; run a_flat 1 $team $model dt20 "$dest"
    done; done
    # the port's own pace (step 1.1/17.7 s): the same scenarios (their step numbers are 20 ms steps and are kept in time)
    for s in a_full_throttle b_steer_lr c_brake e_wall; do for t in 1 3 4 7; do run $s $t 0 1 dtport "$dest"; done; done
    for s in a_flat b_flat c_flat g_manual; do run $s 1 0 1 dtport "$dest"; done
    run d_offroad 1 0 1 dtport "$dest"; run f_jump 4 0 1 dtport "$dest"
}

check() {
    rm -rf "$WORK/traces_a" "$WORK/traces_b"
    all "$WORK/traces_a" > /dev/null; all "$WORK/traces_b" > /dev/null
    local bad=0 n=0
    for f in "$WORK"/traces_a/*; do
        n=$((n + 1))
        cmp -s "$f" "$WORK/traces_b/$(basename "$f")" || { echo "DIFFERENT: $(basename "$f")"; bad=$((bad + 1)); }
    done
    echo "$n traces, $bad different between two runs"
}

cmd=${1:-}; shift || true
case $cmd in
    build) build ;;
    run) run "$@" ;;
    all) all ;;
    check) check ;;
    *) sed -n '2,15p' "$0" ;;
esac
