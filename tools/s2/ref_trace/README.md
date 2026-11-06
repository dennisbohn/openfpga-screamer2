# Reference traces of Screamer 2's car physics

Ground truth for checking the firmware's physics: the player's car, one CSV row per physics step,
produced by OpenScreamer's `-game 2` (the game's own race routines, written by hand and checked to the
bit against the converted ones and against DOSBox: `docs/NOTES2.md` S2-5u, S2-5w, S2-8), headless, from
a scenario file (step -> keys), alone on the track. Nothing here is game data; the traces are derived
from the game and stay untracked (`$REF_WORK/traces`).

## Build and run

Needs `<openscreamer>` (a checkout; it is only read: the script clones it to `$REF_WORK/build-src`),
`<game folder>` (Screamer 2, read only) and, on Windows, MSYS2 UCRT64 (gcc, SDL2, pkg-config) plus
MSYS2's `mingw32/bin/cmake.exe`. About 20 s to build, about 1 s a trace. No window (SDL dummy video and
audio), no threads in the physics, no timers: the traces are byte-identical run to run.

    export OPENSCREAMER=<openscreamer> S2_GAME=<game folder> REF_WORK=<work folder>
    ./ref_run.sh build        # clone, apply s2_ref.patch, build with the system SDL2 (-DOS_SDL_LEAN=OFF)
    ./ref_run.sh run a_full_throttle 1            # one trace: scenario, track [team model [dt20|dtport]]
    ./ref_run.sh all          # the whole set: 102 traces + 102 .meta, ~50 MB, about 1.5 minutes
    ./ref_run.sh check        # `all` twice, compared byte for byte
    ./ref_run.sh setups       # the set-ups' traces (menus row 12): tools/s2/car_scenarios/setups.txt, 22 traces -> $REF_WORK/traces_setup/, the generated scenarios -> $REF_WORK/scn/
    ./ref_run.sh race 2 1 park 2500 1   # a race of four cars (track, level 0..2, park|gas, frames, the player's car 0..4): every car's whole record after every frame
                                        # -> $REF_WORK/traces_race/ (not part of `all`: tools/s2/check_aicontact_s2.sh makes and uses them; 33 MB each)

Built with port version 0.6.52b, OpenScreamer commit `dd0ecf6`, gcc 16.2.0, SDL2 2.32.10.
`s2_ref.patch` adds `port/src/s2_ref.h` (the scenario reader and the CSV writer, `s2_ref_run`), two
lines in `main.c` and one in `s2_hooks.c`/`s2_port.h`; no physics line is touched. Switches it adds:
`OS_S2REF=<scenario> OS_S2REFOUT=<csv>` (with the port's `OS_S2PHYS=<steps> OS_S2STEP=<seconds>
OS_TRACK OS_CAR`), `OS_S2TEAM` (the player's team, which the port's test mode had fixed), `OS_S2RAW`
(the picture's time), `OS_S2REFPUT=x,y,heading`, `OS_S2REFROBOT=1` (the game's own driver).
Existing switches used: `OS_S2FLAT=<road kind>` + `OS_S2NONEAR=1` (level ground of one kind, no walls),
`OS_S2AUTO=0` (manual gearbox).

## The step: 20 ms here, not the port's

The game has no fixed step: it runs the race frame once per picture with the time the last pictures took
(eased, clamped to 0.02 .. 0.2 s). OpenScreamer's port runs 17.7 frames/s with step 1.1/17.7 = 0.0621 s
(the pace measured in DOSBox). The traces are at **0.02 s (50 Hz)** (`dt20`, the game's shortest step), and
the on-track scenarios and the flat ones also at the port's pace (`dtport`: the scenario's step numbers are
20 ms steps and keep their time, so fewer rows). The arithmetic is step-dependent (the place is cut to
whole numbers towards (0,0) every frame and the cut-off comes back as speed; a tyre keeps `1 - grip * step`
of its sideways speed: NOTES2 S2-5l), so traces of the two steps differ a lot (speed after 0.5 s: 17.6 at
20 ms, 9.5 at 62 ms; a flat top speed of 104 at 20 ms, 125 at 62 ms). At 20 ms it also depends on WHERE and
which way the car goes: on level ground a car heading towards (0,0) tops out at 127 and cannot be braked
below 17 (the creep feeds itself), one heading away from (0,0) from (300000, -300000) does not move at all.
The flat scenarios therefore start at track 1's grid place, where the on-track trace starts.

## Files

`<scenario>.t<track>[.flat<kind>].team<T>.model<M>.<dt20|dtport>.csv` and `.csv.meta`. Team 0..3 = the
game's teams 1..4, model 0..4 = cars 1..5 (model 4 is the team's own car; the four teams differ only
there, CAR_PARAMS is `team*5+model`). Default team 0, model 1 (the arcade race's), automatic gearbox, rookie.
`.meta`: step, CAR_PARAMS words (hex/float: word 0 `+0x5c` speed at the most revs, 1 most revs, 2 gearbox
flag, 3 top gear, 4 most steering, 5 engine strength, 6 tyre grip factor, the rest NOTES2 S2-5o/5u), the
track's road-kind records (id, grip for tyre sets 0..2, roughness, flags (`&3 == 2`: grass), mask, rolling
and squeal sounds), the gear ratios `0x112e24`.

Row 0 = the grid state; row k = the state after step k, with the keys of the scenario's latest line at or
below k held during it. The 3 s start hold is skipped (`s2_go` at once). t = k * step.

## Columns (all from the car record at `0x119300`, offsets `+x`; floats printed to 9 digits)

| column | meaning |
|---|---|
| `step t clock` | row, seconds (k*step), the race clock `0x13335c` (adds the picture's time) |
| `in_acc in_brk in_left in_right in_gup in_gdn` | the controls record's words the game read (0 / 1023) |
| `throttle brake_f brake_r` | `+0x1ec`, `+0x1e4`, `+0x1e8`: the controls after the game's easing, 0..1 |
| `steer_target steer steer_shown` | `+0x660` asked, `+0xbc` the front wheels' angle (rad, speed-reduced, eased), `+0x13c` the drawn one |
| `x height y` | place, ints (`+0x4d8 +0x4dc +0x4e0`), height is up; `y` is the game's second ground axis |
| `pitch heading roll` | rad (`+0x4e4 +0x4e8 +0x4ec`); heading about up, + towards +x |
| `vx vh vy` | velocity units/s = (place - last row's) / step, by the harness; row 0 = 0 |
| `speed kmh` | `+0x500` = units moved / step * 3600/400000 (NOTES2 calls it km/h); `kmh` = speed * 1.513 is the port's dial (the English game shows MPH = kmh/1.6) |
| `way way_pitch slip_angle yaw_rate` | direction of travel `+0x508`, its pitch `+0x50c`, wheels' angle against the way `+0x4d4`, turn rate `+0x44` (rad/s) |
| `rpm gear engine_wheel_speed shift_timer` | `+0xc78`, `+0xc7c` (0 neutral, 1..top), `+0x4fc`, `+0x140` (s, a change settling; automatic only counts down) |
| `grip slide` | `+0xc34` = 1 - tyre slip / (111 * top speed) (under 0.93 the tyres smoke and squeal), `+0xc0` the sideways slide term |
| `body_pitch body_roll lift` | the body on its springs `+0xc18 +0xc20`, `+0x4f4` (int; ~28 at rest) |
| `ground ground_pitch ground_roll wheels_down` | mean ground height `+0x4f8`, slopes `+0xc0c +0xc14`, `+0x510` = share of wheels with the spring on the ground (0, 0.25 .. 1) |
| `road_dist road_side road_heading` | `+0x4bc +0x4c0 +0x4c4` (where the car is along the road; 0 on level ground) |
| `w0..w3 _spin _spring _bump _y _ground_y _vup _contact _over _kind _skid _push_x _push_h _push_y _cx _ch _cy` | per wheel (0 front +X, 1 rear +X, 2 front -X, 3 rear -X; record `+0x574 + 0x70 w`): turning rad `+0x10`, spring length `+0x14` (28.5 at rest, travel 30), bump `+0x48`, wheel height `+0x30`, ground height `+0x44`, vertical speed `+0x00`, spring on ground `+4`, ground under it `+8`, road kind id `+0x5c`, skid pick `+0x60` (nonzero = skidding; also 0 when the pick is 0), the tyre's push velocity `+0x544 + 12 w` (x, up, y), the corner's place floats `+0x514 + 12 w` |
| `wall wall_corners knock knock_push spin damage knock_front knock_left knock_right knock_back car_flags touched` | `+0x150` against a wall this step; the four corners off the drivable area as digits w0..w3; `+0x30` (1 on a hit, counts down), `+0x34`; `+0x58` the wall spin; `+0x8` damage (to 2); `+0xc..+0x18` the knocks per side; byte `+0x4c` (bit 3 = a car touch, bits 0/1 finished); `0x11cdb4` steps cut short by car contact |

## Scenarios (`scenarios/`, a line `step keys`: A accelerate, B brake, L left, R right, U gear up, D gear down, - none)

| file | what | runs |
|---|---|---|
| `a_full_throttle` | 20 s throttle, no steering (the road bends: first wall at step 227 / 378 / 186 / 226 on tracks 1 / 3 / 4 / 7; 17.60 at 0.5 s on track 1) | tracks 1 3 4 7; track 1 for all 20 cars; dtport |
| `a_flat` | the same on level ground, road kind 36, the car at track 1's grid place (clean 20 s: gears at steps 1, 45, 76, 129; 17.6 / 39.6 / 85.1 / 104.0 at 0.5 / 1 / 2 / 3 s) | default car; all 20 cars; dtport |
| `b_steer_lr` / `b_flat` | throttle, left 1 s..4 s, right 4 s..7 s | tracks 1 3 4 7 / flat; dtport |
| `c_brake` / `c_flat` | throttle 3 s, then brake held (flat: stopped at step 277) | tracks 1 3 4 7 / flat; dtport |
| `d_offroad` | track 1: a flick right at 2.8 s: grass, a wall, grass, back on asphalt | track 1; dtport |
| `d_surface` | level ground of each road kind (20 distinct kinds of the seven tracks; top speed 80 on the loose ones, 101-113 on the grippy), throttle 5 s then brake | 20 runs |
| `e_wall` | throttle, a flick left at 2 s: a wall at speed | tracks 1 3 4 7; dtport |
| `f_jump` | track 4: the car leaves the ground at 5.4 s at 129 and flies 0.9 s (47 steps with no wheel down, 2400 units down) | track 4; dtport |
| `g_manual` | manual gearbox on level ground: gear ups, downs, a held key shifts once | flat; dtport |

Directives (`@steps @tracks @flat @put @auto @robot @surfaces @game @setup`) are read by `ref_run.sh`, the port reads only the key lines (and tools/s2/check_car.c the `@setup`).

**Set-ups** (`@game 8`, `@setup a,b,c,d,e,f`): the player's CUSTOMIZE set-up (handling, tyre pressure, tyres, front and rear suspension, brakes: docs/screamer2/driving.md "The set-up") goes into the
race record as the original's 0x339e7 puts it for time attack and the free run (the port reads `OS_S2SETUP` for the kinds of game 2, 4 and 8), `OS_S2GAME=<kind of game>` sets the kind (`s2_ref.patch`
makes the test race read it: without the patch it stayed 1). The standard set-up is no `@setup` (2 2 -1 2 2 2, the track's own tyres). `@game 8` is the free run (one car); the traces of the arcade's
default are byte for byte the ones of `all` (`ref_run.sh run a_flat 1` with and without `OS_S2GAME=8`). `OS_S2GAME=2 ref_run.sh race ...` is a championship's race: 0x14e20 tunes the
computers' cars (tools/s2/check_setup_ai_s2.sh).

## Caveats

- Alone on the track (`ncars` 1): no opponents, so `touched` stays 0 and no car-to-car hit is traced.
- Digital keys only (the Pocket's); the port's analog path (`s2_controls_pad`) is not used.
- Not modelled/traced: replays, the camera, sound; damage is traced (`damage`) but its effect on the
  engine is whatever the game does. Weather/night are off (day), level rookie (opponents' handicap only).
- Compiled for the x87 with 53-bit precision as the game's runtime sets it (`-mfpmath=387`
  `-fexcess-precision=standard` for `s2_hooks.c`): bit-exact on x86 only; another CPU may differ in the last bits.
- Flat-world rows: `road_*` are junk (no road); the speed depends on place and heading as said above.
