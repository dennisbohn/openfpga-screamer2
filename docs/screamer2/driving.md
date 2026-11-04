# Driving: the car model of Screamer 2 in the firmware

The player's car of the viewer's driving mode (`firmware/s2car.c`, contract `firmware/s2car.h`) is the game's own car
model: the record of 0xCC0 bytes of one car, advanced by the same routines in the same order, in single precision, with
the car's numbers read from the game's own files. This page says where it comes from, what was left out, where it
deliberately differs from the original, and how it was checked.

## Sources

The reference is OpenScreamer's hand-written text of the car (`port/src/s2r_car.h`, checked there against the converted
game routines to the last bit and against the original in DOSBox; `docs/NOTES2.md` S2-5f to S2-8). Ported, with the
game's addresses (the comment at each routine in `s2car.c` names it):

| Routine | What |
|---|---|
| 0x2baa4 (one car) | the frame: tyres, walls, ground, ground step, then the controls |
| 0x1b36f | tyres on the ground: grip from the four wheels' road kinds, the slide, the four tyres, the corners, the road's heading |
| 0x19cac | one tyre (drive, brake, slope, grip, skid) |
| 0x28f96, 0x29571 | the ground under the four wheels, the car against a wall (the nearest wall, the way out, the spin, the knock) |
| 0x1b92b, 0x1bef1, 0x1c4fb, 0x1e529, 0x1c7f8, 0x1d9fc, 0x19810 | the step on the ground: springs, body lean, speed and way, the angles and corners |
| 0x1f756, 0x1f697, 0x174ca, 0x1756f, 0x19409, 0x1f3ce | engine and gearbox (automatic or manual), throttle, brake, steering, clocks |
| 0x14e20, 0x2f519 | the car made of its set-up and put on the grid |
| 0x13f38 | the chase camera (view 3, the one a race starts in): 1300 behind, 200 over the car, looking down 0.274, its turn led towards the way the car goes and eased by speed |
| 0x16878 (part) | the front wheels' steering and the wheels' turning as drawn |

Left out of `s2car.c` because one car does not need them: the other cars (the solver 0x245a3, the outlines 0x22d35, 0x20a18),
the driver of the computer's cars (0x17726), laps and the position along the road (0x153c6): these are in `race.c`
(section "The race" below, with a simpler touch between cars); cones, sparks, smoke and dust,
sound, the road kind's look (0x1f1ef), the other camera views are not done. The car's damage is kept (it takes power away and softens
a spring) but nothing shows it.

## The car's numbers

- `CAR_PARAMS` of `DATA/GAME.DAT`'s `CARS.DTR` (4 teams x 5 cars, the fifth the team's bonus car), `TRACK_DATA` of the
  track's `MAPnDAT.DTR` (the best tyre pressure per tyre set, six `ROAD KINDS` rows: grip for slick, rain and snow tyres,
  roughness class): `firmware/s2params.c`, tested field by field against an independent reading in Python
  (`tools/s2/check_params.sh`: 20 cars, 42 road kind rows, equal).
- The set-up is the game's arcade race: handling, pressure, springs and brakes 2 (the middle), the tyres "the track's
  own" (`-1`, `14e20` picks slick, rain or snow by the track: 1 1 2 0 2 0 0 for tracks 1 to 7). So on track 1 the
  player's car runs on rain tyres (grip 1.4 on asphalt against 1.1 for slicks): that is what the original does by
  default.
- Each placement's road kind is in the track image (one byte a placement, an extra section at the end, offset in the
  header word 0x74; `tb_mmp_kinds()` and `make_track_s2.py`, byte for byte equal on all seven tracks).
- The ground under a point is the game's `0x28ce6` (`track_ground_s2()` in `track.c`): the nine cells, the pieces'
  bounding circles, the point turned into the piece's frame as a whole number and tried five times 4 units on, the
  faces in order, a triangle of the face for the plane (not the face's stored normal), integers with 32-bit wrap.
- Walls and road points: the nearest of the nine cells round the point (the cell of a wall or road point is the cell of
  its middle on all seven tracks, checked; the lists in the image are in cell order, so the lookup is a binary search).
- The wheels' places in the car are read from the car model's blocks named `flx`, `blx`, `frx`, `brx` (the game finds
  them by name, 0x2f519). The image holds team 1's five cars; the models of cars 1 to 4 are the same in all four teams,
  car 5 (the team's own) is not, so a car 5 of another team gets team 1's wheel places.
- The car starts on the first node of the track's `MAPnOPP.SPL` (the grid place of the only car: 1000 units to the side),
  heading along the nearest road point's heading.

## Precision

The game runs on the x87 set to 53 bits (every sum one rounding to a double, a float stored where the game stores a
DWORD). The CPU here has float32 only. Everything is float32, every sum rounded on its own, in the game's order where it
is known, with fused multiply-add forbidden (`#pragma GCC optimize ("-ffp-contract=off")`): a fused product would round
once where the game rounds twice. Nothing in the car needs a double: the places are about 1e5 to 3e5 (a float has a
64th to a 16th of a unit there) and are always added in a way that rounds the big number once (the corners as the
middle plus a small sum, the middle as one corner plus a small mean). `sinf`, `cosf`, `atanf` and `sqrtf` stand for the
x87's 64-bit functions; their last bit can differ.

## The car's place stays a float

The game keeps the middle of the car as whole numbers and cuts the fraction off towards the map's origin every frame
(`0x1c7f8`, `0x1e529`); the cut-off comes back as speed. It ran at 10 to 20 frames a second, where that loses little. At
50 steps a second it eats up to a unit a step and the result depends on where the car is and which way it faces
(measured with the reference traces of the same routines at 20 ms, `ref/README.txt`): level ground from track 1's grid
place tops out at 104 (the car's own top speed is 127), heading towards (0, 0) at 127, heading away from (0, 0) the car
does not move at all, and braking towards the origin cannot get below about 17. That is a compromise of 1995, not the
feel of the car, so `s2car.c` keeps the place as a float (words 0x4d8 and 0x4e0, and their copies at 0x734). The heights
stay whole numbers (the spring and the wheels' heights are in units of the springs' travel, 30). `-DS2_WHOLE_PLACES=1`
cuts the place as the game does; the host test uses it to follow the reference step for step.

## The step

One call of `s2car_step` is 20 ms; the viewer calls it once per video frame since the last picture, at most
`DRIVE_MAX_STEPS`, as Screamer 1 does. The game itself has no fixed step (it runs once per picture with the time the last
pictures took, clamped to 0.02 to 0.2 s, and several terms depend on it: a tyre keeps `1 - grip x step` of its sideways
speed, the easing tables are `(0.5 + 0.005 i)^(50 x step)`). The tables here are those of 50 steps a second. The
numbers are therefore the original's at 50 pictures a second, which the original never ran at: the traces at 20 ms and at
the port's pace of OpenScreamer (0.0621 s a step) differ, e.g. speed after 0.5 s on track 1 17.6 against 9.45 (and the
level-ground top speed 104 against 125, which is the cut-off again).

## Conventions

- Lengths in the track's units, angles in radians, `speed_kmh` is the dial's (`car + 0x500` x 1.513), `speed_mph` its
  mph. The game's `car + 0x500` is 17.6 at 0.5 s from the grid of track 1; the reference traces use that number as "speed".
- `yaw`: forward is `(sin yaw, -cos yaw)` in the track's (x, y); it grows to the left. `input.steer` is -1 for the left
  d-pad, +1 for the right: -1 turns the car left (yaw grows). The game's own left key raises `car + 0x660` and the
  heading the same way, so the reference traces' `in_left` is `steer = -1` without any change of sign.
- `pitch` + nose up, `roll` + the car's +X side up (the game's), `s2car_look` converts to `car_look` (pitch negated, as
  the port's own `tilt` has it) and the steering and the wheels' turn as `car.c` draws them. The matrix `rot` in the
  state is an extra for other users and is not what the viewer draws; check the signs of the tilt in the first picture of
  a bank, they are the one thing of the pose that was not seen drawn.
- Keys (the d-pad): the whole lock to the side held, as the game's keyboard control (type 1). With `stick = 1` the
  steering input is an analog axis through the game's curve and the car steers 1.3 times as far (type 0).
- The gearbox of the player's car is automatic (the arcade default); `manual = 1` and `gear_up` / `gear_down` give the
  game's manual gearbox (0.5 s between changes).

## Checks

| Command | What |
|---|---|
| `tools/s2/check_params.sh` | CAR_PARAMS (20 cars) and TRACK_DATA (7 tracks, 42 road kind rows): C reader against an independent Python reader |
| `tools/s2/check_track_build_s2.sh` | the image with the road kinds, byte for byte against `make_track_s2.py`, seven tracks |
| `tools/s2/check_car.sh` | the car against the reference traces of the same routines in OpenScreamer, run natively in float32 on the image of each track, with the place cut as the game does and kept as a float |

### Against the reference (`tools/s2/check_car.sh`, 20 ms steps, the car of the arcade default: team 1, car 2)

The reference traces are OpenScreamer's routines (x87) driven by the same scripted keys, one row per step (82 files: 20
cars on level ground, steering, braking, a wall, the grass, a jump, 20 road kinds, the manual gearbox, tracks 1, 3, 4 and
7). Our car runs the same script natively in float32 on the real image of the track. Two builds: the place cut as the
game does (`S2_WHOLE_PLACES=1`, which has to agree with the reference) and the place as a float (the firmware).

With the game's whole-number places (the check of the port itself):

| Scenario | Result |
|---|---|
| level ground from track 1's grid, full throttle 20 s (cars 1 and 2: gears 1 to 4, 104 at the top) | position equal to 1 unit and speed equal to 0.00 km/h over all 1000 steps; cars 3 and 4: 5 and 25 units after 340 and 225 steps; car 5 of team 1 (rear drive, 1.1 x power) leaves the reference at step 27 (on the real track the reference itself runs away to 1e9 km/h at step 978) |
| steering left and right (b), braking (c), the manual gearbox (g), 20 road kinds (d) | equal to 0 to 1 units over 400 to 1000 steps; the gearbox 3; 17 of the 20 kinds equal, three (track 2: 26 and 33, track 6: 28) leave the reference after 111 to 172 steps by 23 to 1643 units |
| the real track | track 1 equal to 2 units for 252 steps and the wall at step 247 hit on the same step with the same response (heading to 1e-4); track 7 follows through its wall at step 226 for 321 steps; track 4 for 178 (a wall at 186); braking to a stop on tracks 1 and 7 equal to 0 units; track 3, whose grid is on a slope, leaves the reference after 49 steps (a place's cut-off falls the other way by a unit; speeds within 4 % until a wall, step 378) |
| a jump (track 4, f) | the car is off the ground from step 269 to 316 in both, the same 48 steps |
| after a wall or a long run | the same kind of run, not the same run: the first contact moves the car by whole units and a last bit decides the rest; 5 s later two runs are 50 to 100 km/h apart |

With the place kept as a float (what the Pocket runs), on level ground:

| Scenario | Float places | Reference |
|---|---|---|
| from track 1's grid, full throttle | 126.6 at the top | 104.0 (the cut-off holds it back) |
| towards the origin (benign for the reference), full throttle | 126.6 | 127.0 |
| the same with steering left 50 to 200, right 200 to 350 | 126.5 | 127.8 |
| braking from the top towards the origin | stops (0) | cannot get below 22.9 |

So the top speed is the model's own (127) within 0.3 %, the acceleration (17.6 at 0.5 s, 39.6 at 1 s, 85.1 at 2 s) is the
reference's to its last digit while the place's cut-off does not matter, and the creep that holds the original back at 50
steps a second is gone.

Cost of a step, counted natively per step (tracks 1 to 7 alike): 4 ground looks (each the game's 9-cell search), about
20 `atanf`, 2 `sinf`, 4 `cosf` and the `sincosf` of the corners' turns. On the Pocket's CPU (the simulation of the
driving mode, `SIM_KEYS=8000`, track 1, 160 frames, run of 2026-11-04 with the real physics): no trap, 50.0 pictures a
second, 9.0 ms mean and 9.5 ms worst for the frame (492 faces and 140 pieces a frame: the draw-distance fix made the
scene bigger than the 332 faces and 60 pieces of the stand-in's run, 5.5 ms), the first mark of the frame timeline (one
physics step, the chase camera and the objects of the scene) 65,600 cycles = 0.88 ms, of which the stand-in took about
0.1 ms. The Pocket and the host agree: 10, 22, 33, 44 mph after 25, 50, 75, 100 steps and the place to 1 unit over 100 steps
(the picolibc and glibc `sinf` differ in the last bit; a steering flick shows it later). Stack of the program: 4.4 KB.

## The race

`firmware/race.c` (contract `firmware/race.h`, used by the viewer's driving mode) puts the player and three computer cars on
the grid and runs a three-lap race with the model above for all four cars. The pieces are the game's, ported from the same
OpenScreamer text (`s2r_car.h`) in single precision:

| Routine | What |
|---|---|
| 0x2f519 | the grid: a race of n cars gives car i the place n - 1 - i (the player, car 0, the last); place g is 1000 units to one side of the first node of the line (the sides in turn) and 2000 x g behind it, heading along the road there. The cars are held: the gearbox in neutral (`s2car_state.hold`, the game's race flag `0x11f0a8 & 0x20`) until the race clock passes 3 s |
| 0x1099a, 0x4a831, 0x108f5, 0x2d59c, 0x309b0 | the line of `MAPnOPP.SPL` (one spline per track, 36 to 48 nodes of 0x38 bytes: two handles each, `+0xc` the speed share, `+0x34` the way from node 0): a quadratic B-spline per half node, a place on it moved by a distance, the line measured once at load by stepping 100 units at a time |
| 0x153c6 | where a car is along the line: four points half a node apart round where it was, the share along each stretch, the nearest, stepping half a node to the nearer side; round the end of the line a lap is done (or undone); the nodes with the laps in them (`+0x4b8`), the way in units (`+0x4bc`) and the car's distance from the line, signed (`+0x4c0`) |
| 0x201ee | a lap done (a lap not done before counts; the car's best lap; the last lap: the car has finished (`+0x4c` gets the bits 1 and 2) and gets a fifth of a second for each car behind it) |
| 0x2da06 | places by the distance along the line (the finished cars keep theirs); the computers' pace (`+0x48`, what the engine takes off their speed: `SKILLS` of `MISC.DTR`, 3 rows x 7 numbers, the arcade race's level 1 = normal, from 0 before the sixth second, then by how far ahead of the player the car is) |
| 0x17726 | the computer's driver |
| 0x14e20, 0x1b36f, 0x29571, 0x1f756 | the computer's car in the car model: its handling, wheel lock x 1.3, half the road's roughness in the grip, its own rule at a wall under speed 30, the engine's speed less the pace |

**The laps are counted on the line**, not on markers: Screamer 2 has no `MAPn.MRK`; the game's lap counter is the car's place on the line
(`MAPnOPP.SPL`, `+0x4b8` in nodes), and the start / finish line is node 0, where the grid is. The cars start a little behind it
(lap -1); crossing it the first time does not count, so a race is three laps from the line plus the run to it, and the lap clock starts at GO.

**The driver** (0x17726) aims the car at the point of the line `pace` units ahead of the road under it (22,100 in the first six
seconds, then 170 x its speed held to 10 .. 130, so 1,700 to 22,100 units) from a point one step ahead on its way, and steers the wheels by the angle between the two, times 2, 3 or 5
by which axle drives. The throttle: full minus how far its heading is off the line's, lifted when its speed is over the line's speed share
(`+0xc` of the node: percent under the top speed; the corners), cut by the pace and the lock. It looks for slower cars between itself and the
point it aims at and goes round the one nearest by the side that turns it the least (within an eighth of a turn), and lifts behind one it cannot pass. On the grid it
revs the engine now and then. All of it from the game's own text, nothing invented but the floats.

**Cars touching.** The game's solver (0x245a3, 18 KB) sweeps the cars' outlines (the wheel corners 1.4 times as wide) through the
step for the first touch and runs the rest of the step again; it also sparks, knocks the springs and the sound. Here (`collide()`
in `race.c`) two outlines that overlap at the end of a step are pushed apart along the axis of least overlap and swap their closing speed
along it (equal masses, as the game's push does): the same outlines, tested once a step, no damage, sparks or sound. A car that is hit
at speed from behind therefore takes that speed, as in the game. A step moves a car by 280 units at most (the top speed), a quarter of an
outline's length (1,165), so the overlap found at the end of a step is shallow.

### Checks of the race

- `tools/s2/check_car.sh`: the player's car is unchanged by the additions (the output is the same line for line as before): the computer's
  rules are all behind `s2car_state.ai`, the hold behind `.hold`. `check_params.sh` and `check_track_build_s2.sh` pass.
- `tools/s2/render_s2.sh <image> <prefix> g<steps>[,<every>]` runs the race natively (the firmware's own `race.c` and `s2car.c`, the
  images of the seven tracks) and prints, every `every` steps, per car the laps done, place, mph and node along the line, a line
  per lap done, and at the end the player's lap, place and times and the number of cars put back on the line. `RACE_GAS=1` makes the
  player hold the gas, `RACE_ROBOT=1` lets the game's driver drive his car as well (it is marked finished with no end to its time).
  25,000 steps (500 s) on every track, the game's normal level:
  every computer car does its three laps, finishes and stops; no car was put back on the line, on any track, in either run.

  | Track | line (units) | computers' best laps, player parked (s) | computers' best laps, player driven by the driver (s) | all four finish at (s) |
  |---|---|---|---|---|
  | 1 | 1,019,900 | 98.4, 99.0, 99.6 | 78.6, 79.2, 79.4 | 235 (player, 1st), 244, 247, 248 |
  | 2 | 896,700 | 90.9, 91.2, 94.7 | 77.3, 79.2, 79.3 | 251, 251, 253 (player, 3rd), 254 |
  | 3 | 1,171,100 | 117.0, 117.7, 118.3 | 96.2, 96.8, 96.8 | 295 (player, 1st), 299, 300, 303 |
  | 4 | 1,481,300 | 130.8, 131.4, 131.7 | 105.2, 105.2, 106.7 | 312 (player, 1st), 329, 332, 334 |
  | 5 | 1,288,300 | 130.3, 131.7, 133.9 | 117.5, 117.6, 118.6 | 359, 362, 362 (player, 2nd), 362 |
  | 6 | 1,375,400 | 128.5, 129.2, 129.2 | 105.5, 106.5, 106.5 | 316 (player, 1st), 327, 329, 330 |
  | 7 | 812,600 | 77.6, 77.7, 80.7 | 67.3, 69.4, 70.1 | 216, 217, 219, 220 (player, 4th) |

  The two columns differ by a quarter because of the game's catch-up (0x2da06): a computer car 200,000 units or more ahead of the
  player gets the level's largest handicap (3.6 against 2.0 level with him), which takes that much off the engine's speed and, through the driver,
  puts the throttle (1 less the bend's size x the speed share x the handicap x 0.3) under 0.7 in a moderate bend, where the driver stops rounding
  it up to full; with the player parked at the back the computers are far ahead. A real race is the second column. Track 1 with the player parked: the computers
  average 93 on the car's speed scale (the dial / 1.513), OpenScreamer's port had measured 97 for the same level with a parked player (`NOTES2.md`
  S2-5n; its cars ran at 25 steps a second). The original ran at 17 pictures a second with whole-number places that hold a car back (this
  page, "The car's place stays a float"), so its cars' laps are longer than these; they were not compared with a recording of the original.

### The race on the Pocket's CPU

Simulation of the viewer on track 4 (docs/development.md, 220 frames, the first 150 of them the grid's hold): the first mark of the frame
timeline of the last frame (70 frames after GO), the step of the four cars with the driver, the cars touching and the scene's objects, is 4.27 ms,
against 0.88 ms for the lone car: **the three computers cost 3.4 ms a frame** (three physics steps of about 0.9 ms; the rest is the locate
along the line, the driver and the cars touching, not measured apart). That frame took 13.8 ms mean and 14.6 ms worst at the grid, 50.2 pictures a second, no frame late.

The Pocket's worst frames are rasterizer-bound (docs/screamer2/performance.md): track 4's worst frame while driving the lone car was 15.4
ms on the Pocket, and the physics comes before the first piece is handed on, so 3.4 ms more of it, and 0.8 ms for the three cars drawn,
would make about 19.6 ms there, too close to the 20 ms of a picture. So the computers' steps run after the scene is handed to the hardware,
while the rasterizer finishes the picture (`race_step_others()`, `RACE_OVERLAP` in `viewer.c`): the CPU waited 1.7 ms for the rasterizer at
the end of the simulated frame, and on the worst frames it waits several ms. By that estimate the worst frame of track 4 stays near 16 to 17 ms.
**This version was not simulated or run on the Pocket**; the numbers above are of the same race with its steps before the scene
(`-DRACE_OVERLAP=0`, identical results). `debug.txt` shows what happened: the visit's line ends `ph<mean>/<worst> o<mean>/<worst>` (ms of the
player's part and the scene set-up, and of the computers' part) after the frame's own mean and worst; a worst frame under about 17 ms on tracks 4 and 7 means
the overlap works. If it does not (the CPU and the units contend for the SDRAM), `-DRACE_OVERLAP=0` or a cheaper computer car (the
physics step is the cost: its four ground looks and about 20 `atanf`) are the next steps; the lap and driver code is about 0.1 ms a car.

## Open points

- The deviations above are chaotic: a wall, a jump or the creep of a nearly stopped car amplifies a last-bit difference, so
  after the first contact a run is the same kind of run but not the same run. The numbers before the first contact are
  what the check measures.
- The pose of a tilted car (`s2car_look`) and the wheels' sign of turning have not been seen on a slope, and the camera
  of view 3 only in the numbers.
- Not done: the road kind's effects on the look, the dust, damage shown, the camera's other views.
- The race (section above): the game's swept solver for touching cars, sparks, sounds and damage from a touch; the split times and
  "BEST LAP!" messages; the records (best times kept between races); the levels (pro / normal / rookie are a constant, `LEVEL` in
  `race.c`) and the number of laps (`RACE_LAPS`) are not in a menu; the computers' cars are teams 2 to 4's cars 2 to 4 whatever the
  player's car is; the game's end screen (REPLAY / RACE AGAIN / EXIT, the table of best times) is a text; the other cars' models of
  teams 2 to 4 are team 1's (the image holds those).
- Not compared with a recording of the original's computer cars: the lap times above are the model's own (the original ran at 17
  pictures a second with whole-number places; its cars were slower).
