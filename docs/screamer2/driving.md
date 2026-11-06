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
| 0x16878 (part) | the front wheels' steering and the wheels' turning as drawn; the car object's flags (`look`: brake lights, wheels' blur, damage marks) |

Left out of `s2car.c` because one car does not need them: the other cars (the solver 0x245a3, the outlines 0x22d35, 0x20a18),
the driver of the computer's cars (0x17726), laps and the position along the road (0x153c6): these are in `race.c`
(section "The race" below), `s2fx.c` (the wheel smoke and the shadows: docs/development.md "Wheel smoke and shadows"), `s2props.c` (the obstacles and the sparks of touches and wall knocks: "Obstacles and sparks"); the dust of the road kinds,
sound, the road kind's look (0x1f1ef), the other camera views are not done. The car's damage takes power away, softens
a spring and shows on the car (the marks of the four sides, "The cars' look" below).

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
  them by name, 0x2f519). The image holds the four cars of the race, each its own team's, and `image_model()` finds a
  car's model by the livery word of the cars section's rows (team x 256 + car); a car not in the image takes a model of
  the same car number, an older image (one stand-in row, team 1's five cars) the car's number as the index. The wheels'
  places differ from team to team by a few units to 40 (car 3: rear axle at z 432 in team 1, 392 in the others), so the
  race's cars have their own; `check_car.sh` runs on images of team 1's five cars (`S2CARS=1:1,1:2,1:3,1:4,1:5`), as before.
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
| 0x245a3, 0x2348b, 0x20a18, 0x20970, 0x22d35, 0x19aaa | the cars touching: the swept solver, its gate, the push apart, the knock (below, "Cars touching") |
| 0x14e20, 0x1b36f, 0x29571, 0x1f756 | the computer's car in the car model: its handling, wheel lock x 1.3, half the road's roughness in the grip, its own rule at a wall under speed 30, the engine's speed less the pace |

**The laps are counted on the line**, not on markers: Screamer 2 has no `MAPn.MRK`; the game's lap counter is the car's place on the line
(`MAPnOPP.SPL`, `+0x4b8` in nodes), and the start / finish line is node 0, where the grid is. The cars start a little behind it
(lap -1); crossing it the first time does not count, so a race is three laps from the line plus the run to it, and the lap clock starts at GO.

**The driver** (0x17726) aims the car at the point of the line `pace` units ahead of the road under it (22,100 in the first six
seconds, then 170 x its speed held to 10 .. 130, so 1,700 to 22,100 units) from a point one step ahead on its way, and steers the wheels by the angle between the two, times 2, 3 or 5
by which axle drives. The throttle: full minus how far its heading is off the line's, lifted when its speed is over the line's speed share
(`+0xc` of the node: percent under the top speed; the corners), cut by the pace and the lock. It looks for slower cars between itself and the
point it aims at (from a fifth of the way back of where it aims from: a car alongside counts) and goes round the one nearest by the side that turns it the least (within an eighth of a turn), and lifts behind one it cannot pass. On the grid it
revs the engine now and then. All of it from the game's own text, nothing invented but the floats.

**Cars touching** (0x245a3 with 0x2348b, 0x20a18, 0x22d35; `touch_solver()`, `push_apart()`, `collide_slice()` in `race.c`; the section "Cars touching,
against the original" below says how it was checked). The game does not test the cars at the end of a step: after every car has been through the
tyres, the walls and the ground and the step on the ground (`s2car_move()`), its solver sweeps the cars' outlines (the four wheel corners 1.4 times as far from
their mean, the last step's against this step's, every corner of one car against every edge of the other) for the first touch of the step. A touch is a share
of the step. Both cars are flagged (`+0x4c` bit 8, `+0x30` = 1), put back to where they were at that share (place, turns, corners, the wheels' places and
springs), knocked (0x19aaa: the side's mark, with DAMAGES on the corner's and the car's damage, the force scaled by the level's row of `MISC.DTR`'s `DAMAGES`, pro 5,
normal 2.5, rookie 1) and each car's four corners get a push: the speed of the corner against the edge's at the point, along the line from the car's middle
to the point in full and across it weighted by the corner's angle from that line (up to 0.3), so the closing speed does not simply change owners. The
rest of the step is then run for the two cars alone, with what is left of its length (`s2car_move_again()`; not at all when that is 0.005 s or less, the
touch then stays at the put-back place), after the walls and ground of every car have been looked at again (0x16824) -- all of that is the game's, once a
step, for any two cars. 0x22d35 then pushes apart two cars that still overlap (a corner of one inside the other's outline: back along the line between the
middles, the corners only; two cars both under speed 5 are turned away from each other). Only then the cars do what they do with the step's controls
(`s2car_finish()`: the wheels' picture, the clocks, the engine, the throttle and the steering, and for a computer the driver, which therefore sees every car
as the physics left it). The sparks of the touch are thrown by the solver (`s2car_sparks`, `s2props.c`); the sound's own call is not done: the player's touch is heard through `race_knock_take()` with the game's knock to the
unit. The computers' steps are in slices for the viewer (`race_others_slice()`): each car's passes, the touch (in pieces: the solver, the walls again, a
car's rest of the step each), each car's rest. The player's car has done all of its step before the first slice; where it is one of the two it is put back
and its passes are run again with its newest controls and without its engine, wheels' picture and outputs being worked out again (one step's change).

**The computers' bodies on the Pocket** came apart (spinning and floating next to their wheels) because `car.c` took the wheels of a car
model to be its blocks that sit off the middle of the car (Screamer 1's rule). In AUTO02 and AUTO03 the body blocks (`MV_m`, `faut`,
`raut`) sit 1 to 4 units off, so they were turned by the wheels' spin like hubs; the ride's cars had no spin and AUTO01's body sits exactly
on the middle, so nobody had seen it. The game finds the wheels by name (the table at 0x112f00: `flx fli blx rli frx fri brx rri`, hubs `x`
turn and steer, tyres `i` only steer); `cmodel_build()` does that now when a model has the eight, and the old rule only for Screamer 1's
models. The game's fourth body block `fai` leans with the body now too (it was taken as a wheel). The race's overlap was not the cause:
`track_draw_cam()` makes every car's blocks and pushes the pieces into the geometry unit's registers (`hw_piece`) before it returns, the
hardware reads only the image's meshes afterwards, and the computers' states are changed later.

### The cars' look: brake lights, wheels' blur, damage marks

A car's materials have 128 pictures (frames), and the picture a face shows is `flags >> 9` of the car object's flags (`STARTL.EXE`:
`0x7ec0b` phase = flags >> 2, `0xbd748` frame = frames x phase >> 14). The flags are made once a step by `0x16878` (`0x1737b` to `0x174bf`),
`s2car_state.look` here (`wheel_pictures()`, the step's order as the game's: the brake it sees is the last step's):

| Flag | Picture bit | Set when |
|---|---|---|
| `0x200` | 0 | the brake is down: `+0x1e4 + +0x1e8` is not 0 (the pedal or, for a computer's car, its driver; not the slowing down) |
| `0x400`, `0x8000` | 1, 6 | a pair of wheels turns more than 0.29 turns a step (`speed / +0x5c`; the driven pair at the engine's speed): `0x400` for the pair the drive goes to, `0x8000` for the other, with four-wheel drive `0x400` for the rear pair alone |
| `0x2000 0x800 0x4000 0x1000` | 4, 2, 5, 3 | the marks of sides 0 to 3 (car `+0xb8`), set by the knock `0x19aaa` once a side's count is 1 and a knock is over 2000 |

In every car file the textures change with those bits only: the rear panel (`rauto`) with bit 0 (picture 1: the rear lights bright) and
bit 3 (the crumpled rear), the front body with bit 2, the flanks with bits 4 and 5 (`0x4000` the left flank from behind: scraped, its number scratched),
the hubs with bit 1 and bit 6 (the rear-drive cars have the rear hubs on bit 1, the front-drive cars the front ones: the same order as the flags).

**The bug on the Pocket:** the firmware gave the cars Screamer 1's flags, `0x4000` when not braking, 0 when braking. In Screamer 2 `0x4000`
is the mark of side 2, so a car that was not braking showed the scraped left flank (picture 32, "damaged" bodywork next to the rear light), the
picture went whole when the player braked, and the brake (`0x200`, picture 1) was never asked for: the lights never lit (a car in the viewer
showed the scraped flank as well). The damage marks are set by `knock()` with the damage on (the GAME SETTINGS row DAMAGES, off at the first start as in the game: `s2car_damages`, which the viewer sets
from the save when a race is set up; off, a knock marks and hurts nothing, as the game's 0x19aaa returns at once); a car against a car knocks as a wall does ("Cars touching").

### Checks of the race

- `tools/s2/check_car.sh`: the player's car is unchanged by the additions (the output is the same line for line as before): the computer's
  rules are all behind `s2car_state.ai`, the hold behind `.hold`. `check_params.sh` and `check_track_build_s2.sh` pass.
- **The cars' look** (`check_looks()` in `tools/s2/render_s2.c`, every run): the player's car on the start line is drawn from four sides: standing it
  has look 0 and texture phase 0; braking it has `0x200` and the rear lights brighter (614 to 687 pixels on the seven tracks, nothing else but the
  rear: not from the front); each of the four marks changes the car from some side; each wheel flag changes the hubs; at full throttle the rear
  (driven) wheels blur from step 15 and the front ones from step 84 to 101, the brake lights never; in a race of 1,100 steps no car on the grid has a
  look and the computers show their brake lights (48 to 449 car steps). The snapshots are of a copy of the car at rest with the look set (a car held on
  the grid creeps, which moves the whole picture). It fails 11 checks with the rule of Screamer 1. `LOOK_DUMP=/b/s2host/look_` writes the pictures.
  **Which cars it is run for.** The check is of the image's model 0 with its own physics (the cars section's livery word names its team and car) and the race of
  the image's four models (`race_grid`); it used to take team 1's first car for the physics of every image, and failed for **10 of the 20 cars** (car 2 and car 3 of
  every team and car 5 of teams 3 and 4) with one check: "the wheels' flag 0x8000 changes nothing of the car". Those ten are the **four-wheel-drive** cars. Their four hubs
  wear one material (`.MLB` entry of the hubs `flx blx frx brx`: the picture changes with bit 1, flag 0x400), and the game's own code sets only 0x400 for a car whose two
  drive shares (record +0x90, +0x94) are both 0.8 or less (`STARTL.EXE` 0x173ef .. 0x17471: the third branch, `fast_back ? 0x400 : 0`), so on such a car 0x8000 is
  never set and there is nothing for it to change; the other cars have one pair of hubs on bit 1 (the driven pair) and the other on bit 6 (flag 0x8000). The running
  original agrees: its car records for the four cars of the arcade race (HORIZON, ...) hold the shares 0.5 / 0.5, 0.5 / 0.5, 1 / 0, 0 / 1 (`dosbox_arcade.py --cars 1 race`, the
  data object at 0x119300 + 0xcc0 a car) and its CAR page prints "4wd" for them. The check now asks for flag 0x8000 only of the cars whose physics can set it. (Its image's
  track number is the last digit 1 to 7 of the image's *file name*: an image called `c2_1.bin` is track 1, `w1_2.bin` is track 2 with a track-1 image, and the car goes
  wild.) All 20 cars pass with their own physics (each as the player's car on track 1: models, hubs, brakes, marks, blur, the race of the image's four cars).
- **The cars as drawn** (`tools/s2/render_s2.c`, every run): each car model of the image is drawn with and without the wheels' spin and
  steering, and exactly four blocks (the hubs) may change with the spin and four (the front wheels) with the steering; in a `g` run every
  car's pose each step must be a number and a car's (tilt under 1.2 rad, body lean under 0.5, steering under 1). It failed for AUTO02 and
  AUTO03 (7 and 6 blocks turned) before the wheels were found by name, see below. `RACE_CAM=<car>` puts the picture behind that car.
- `tools/s2/render_s2.sh <image> <prefix> g<steps>[,<every>]` runs the race natively (the firmware's own `race.c` and `s2car.c`, the
  images of the seven tracks) and prints, every `every` steps, per car the laps done, place, mph and node along the line, a line
  per lap done, and at the end the player's lap, place and times and the number of cars put back on the line. `RACE_GAS=1` makes the
  player hold the gas, `RACE_ROBOT=1` lets the game's driver drive his car as well (it is marked finished with no end to its time).
  25,000 steps (500 s) on every track, the game's normal level (the table is of 2026-11-05, with the cars touching as the game's solver does and the driver
  of "The driver" above; the first run of the section had them touching by the push apart, and lap times a few seconds off these):
  every computer car does its three laps, finishes and stops; no car was put back on the line, on any track, in either run.

  | Track | line (units) | computers' best laps, player parked (s) | computers' best laps, player driven by the driver (s) | all four finish at (s) |
  |---|---|---|---|---|
  | 1 | 1,019,900 | 98.7, 98.3, 97.6 | 79.1, 79.3, 78.8 | 246 (player, 1st), 254, 256, 257 |
  | 2 | 896,700 | 91.0, 91.0, 94.7 | 78.2, 77.9, 75.4 | 242, 248, 248, 252 (player, 4th) |
  | 3 | 1,171,100 | 118.7, 117.8, 120.5 | 96.1, 95.6, 96.5 | 293 (player, 1st), 302, 305, 308 |
  | 4 | 1,481,300 | 132.1, 132.1, 130.6 | 106.5, 106.4, 105.7 | 321 (player, 1st), 335, 337, 340 |
  | 5 | 1,288,300 | 133.3, 131.4, 131.9 | 119.4, 120.4, 123.2 | 376, 379 (player, 2nd), 379, 394 |
  | 6 | 1,375,400 | 129.2, 129.4, 128.8 | 107.5, 104.3, 105.7 | 320 (player, 1st), 329, 334, 338 |
  | 7 | 812,600 | 77.6, 77.7, 83.1 | 68.6, 67.8, 67.1 | 218, 219, 220 (player, 3rd), 221 |

  The two columns differ by a quarter because of the game's catch-up (0x2da06): a computer car 200,000 units or more ahead of the
  player gets the level's largest handicap (3.6 against 2.0 level with him), which takes that much off the engine's speed and, through the driver,
  puts the throttle (1 less the bend's size x the speed share x the handicap x 0.3) under 0.7 in a moderate bend, where the driver stops rounding
  it up to full; with the player parked at the back the computers are far ahead. A real race is the second column. Track 1 with the player parked: the computers
  average 93 on the car's speed scale (the dial / 1.513), OpenScreamer's port had measured 97 for the same level with a parked player (`NOTES2.md`
  S2-5n; its cars ran at 25 steps a second). The original ran at 17 pictures a second with whole-number places that hold a car back (this
  page, "The car's place stays a float"), so its cars' laps are longer than these; they were not compared with a recording of the original.

### Cars touching, against the original

**The fault (2026-11-05).** On tracks 2, 3, 4, 6 and 7 the first touch of two computer cars, 5 to 12 s after the lights, shoved a car by up to about
31 units in one frame where the original does not. Two causes, both in the port of the game's routines, neither a constant of the physics:
(1) the touch itself was not the game's: the port tested the outlines once, at the end of a step, and pushed the two apart along the axis of least overlap
by half the overlap each (up to 31 units at 60 units of closing a step) and swapped the closing speed along it; the game's solver (0x245a3) finds the
share of the step at which the outlines first touch, puts the two cars back to that moment, gives the corners a push and runs the rest of the step, so
nothing is ever inside anything; (2) the driver (0x17726) did not steer round a car alongside it: the game counts a slower car as in its way from -0.2 of
the way from its own point to the aim (a signed compare of the float's bits), the port from 0; two computers side by side therefore drove into each other
where the game's steered away (it is also what made the free-running cars part from the reference before any touch).
The order of the frame mattered too: the game steps every car's tyres, walls and ground before any car's driver runs, the port stepped car by car; now the
computers' passes are `s2car_move()` for all, the touch, then each one's `s2car_finish_driven()` (the engine and driver).

**How it was checked.** The reference is OpenScreamer's hand-written routines (the same ones `check_car.sh` holds the player's car against; checked there to
the bit against the converted routines of the game's program, and for the physics against DOSBox), run for four cars at 20 ms steps:
`tools/s2/ref_trace/ref_run.sh race` writes every car's whole record after every frame. `tools/s2/check_aicontact_s2.sh` finds the frames at which the
reference's solver stopped a step (102 touches: tracks 1 to 7, the three levels, the player's car standing or holding the gas, three choices of cars; on
tracks 2, 3, 4, 6 and 7 together 83 of them) and starts the firmware's race from the reference's state six frames before each (the places cut to whole numbers,
as the game's; `race_replay_s2.c`), so that both start from one state and what parts them is the code. Errors are distances in units between a car of the firmware and
the same car of the reference.

| 102 touches, 210 windows | the code before (`7e45786`) | now |
|---|---|---|
| the cars the solver stops are the reference's | (it had none) | 102 of 102 |
| error in the 10 frames from the touch: median, 75 %, 90 %, worst | 202, 400, 602, 1,135 | 0.0, 7.0, 17, 590 |
| a car's move in one frame off by (the shove): median, 90 %, worst | 20.9, 73, 113 | 0.0, 2.2, 95 |
| frames within 2 units of the reference after the touch: median | 0 | 27 (60 of the 102 for 20 frames or more, 22 for 100 or more) |
| error 150 frames (3 s) after the touch: median, 75 % | 3,158, 6,567 | 211, 967 |
| windows with no touch: frames within 2 units: median, 25 % | 38, 13 | 82, 35 (of 175) |
| windows with no touch: worst error in 150 frames: median, 75 % | 135, 1,034 | 54, 182 |

In 96 of the 102 touches the error at the touch frame is exactly 0 (the put-back, the push and the rest of the step are the reference's), in 79 still 3 frames later, 67 stay within 2 units
for the 10 frames (example: track 2, frame 295: the car behind takes 87.9 -> 54.8 and the one in front 54.6 -> 88.2, the reference's own numbers). The 3 s after a touch are not the same run:
a corner a hair from the edge of the road is cut to the other whole number and a wall answers differently, and a touch amplifies what float32 does differently from the
game's 53-bit sums: the same firmware built with another rounding (fused multiply-add) parts from itself by a median of 168 units in 150 frames after a touch (62 touches; with no touch: median 0, 75 % 15),
the firmware from the reference by 211. The five touches over 25 units in the first 10 frames are such cases (track 4, level 0, frame 903: a corner on the road's edge a frame before; track 7, frame 463:
the touch at 0.7498 of the step, 0.7500 being where the game stops running the rest of the step); the rest of the table is what the code is held to (`PASS` or `FAILED` at the end of the script).
`FW=<tree>` runs the same on an older firmware.

**Against the original itself, in DOSBox** (`tools/s2/dosbox_race_s2.py`, one race: track 2, the arcade defaults of the private game folder, level rookie, the player's car standing,
30 s, 1,501 frames, every car's whole record each). (1) In this DOSBox the original runs 50 pictures a second and its physics step is **0.022 s** (1.1 x the 0.02 the clock goes
by), not the 0.02 of the model and the traces above: `driving.md`'s "The step" says why that is the model's own choice; a firmware step of 0.02 cannot be laid beside the
original's frames. (2) The reference run with the recorded steps (`--compare`) is the original's to the unit for **287 frames (5.7 s)**: the grid, the release at 3 s, the
three computers' start and 2.7 s of driving (error 0.0 for all three computers), then it parts (4 units at 6.0 s, 70 to 350 at 8 s: DOSBox's emulated FPU and the reference's
x87 do not give the last bit of the same sine). (3) The original's first touch (cars 1 and 2, 8.58 s, frames 428 to 430) comes after that, so the reference does not reproduce it
step for step; there is no touch of the original laid beside the solver's numbers, only the reference's own (above). What the original's touch looks like: the cars are put back
in the first frame (the move of the car behind 231 -> 220, the car in front's 196 -> 188), the speeds change over the three frames (94 -> 74 and 81 -> 94 km/h), and no
car's move changes by more than 32 units from one frame to the next; the old code's shove was 20.9 units at the median and 113 at most. The other tracks were not recorded.

**Approximated, or not done.** (a) The player's car does its whole step (`race_step_player()`) before the computers' first slice, so a touch that puts it back runs its
passes again with the controls of the whole step and without its engine, wheels' picture and outputs worked out a second time (four touches of the set have the player
in them: the errors are like the others). (b) The sparks are thrown from the touch as the game's `0x2aeb9` is called (the point, the mean of the two points' pushes, the knock), checked by running the firmware's step from the game's state before each batch of a recording (docs/development.md "Obstacles and sparks"). (c) The damage the knock does is scaled by the level as the game does; a wall's knock of the player's car is not
(it has been scaled by 1, the rookie level's, at every level: with the level normal, the default, the game hurts it 2.5 times as much -- not changed here: the player's car).
(d) The sums are float32: the reach of the driver's avoidance (the widths x 1.2, a double in the game) is worked out as 12 x the widths / 10 for the same rounding.

**The computers' step of 7.2 ms.** Not found in the opponent code, and not reproduced on the PC. The loops are short: in 8,000 steps of a race on each of the seven tracks the
driver's line_step takes 2 turns on the mean and 5 at most, locate 1.02 and 3 at most, a ground look tries 6 faces on the mean and 98 at most (counted in a copy of the code
of before); timed natively a computer's step has outliers of 10 to 50 times the mean, in single ground looks of 20 to 40 us against a mean of 0.1 us at places that differ from run
to run: not a count of turns, but data that is not in the cache. What can make 7.2 ms on the Pocket against 3.0 on the mean is a frame that is late (the computers then take two
steps when the picture before took over 20 ms: `DRIVE_MAX_STEPS`), a cold cache on the ground tables of a part of the track, or, since the solver above, a touch. None of it is
measured on the Pocket.

**What the solver costs** (PC, natively, 8,000 steps of the race robot on tracks 2, 4, 6 and 7; the Pocket's figures are those by the ratio of the computers' mean, 4.9 us here, 3.0 ms
there). A frame in which the solver finds a touch (0.03 to 1.1 % of the frames) takes the computers' part to 9 or 10 us, about 3 ms more on the Pocket: the sweep of the two
cars, their passes again and the walls and ground of the cars again, in pieces of at most one car's step. A frame without a touch costs 0.2 to 0.5 us more (4 to 8 %, about 0.15 to
0.3 ms): the nearness of the six pairs, and the computers' work in two slices a car. The new code's stack: `touch_solver()` 688 bytes in a frame, over the 336 of the slice, about 300
bytes deeper than the old deepest path (8 KB of stack, 4.5 KB used by the race). The simulation (`tools/s2/sim_race.sh`) was not run for this change.

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
**This version was not simulated**; the numbers above are of the same race with its steps before the scene
(`-DRACE_OVERLAP=0`, identical results). On the Pocket (track 1, `RACE_OVERLAP` 1) the user measured 12.5 ms mean and 14.4 ms worst,
50 pictures a second, `ph1.0/1.1 o3.0/3.5`: the computers' steps are 3.0 ms there. That frame is about 3.5 ms over the ride's forecast
without physics (docs/development.md), about the cost of the steps, so on track 1 the overlap hides little: it can only hide what the
rasterizer would have waited for, and track 1 is not rasterizer-bound. Tracks 4 and 7, the rasterizer-bound ones, were not measured; if
nothing is hidden there the worst frame would be about 15.4 + 0.8 + 3.0 = 19.2 ms. `debug.txt` shows what happened: the visit's line ends `ph<mean>/<worst> o<mean>/<worst>` (ms of the
player's part and the scene set-up, and of the computers' part) after the frame's own mean and worst; a worst frame under about 17 ms on tracks 4 and 7 means
the overlap works. If it does not (the CPU and the units contend for the SDRAM), `-DRACE_OVERLAP=0` or a cheaper computer car (the
physics step is the cost: its four ground looks and about 20 `atanf`) are the next steps; the lap and driver code is about 0.1 ms a car.

**Later (2026-11-04, `performance.md` section 7).** The simulation of the version with the steps after the scene (`RACE_OVERLAP` 1) showed, on track 4's grid frame, that the geometry
unit has 0.4 ms of work left when the last piece is handed on: the steps (3.2 ms) are not hidden, and the frame took 14.7 ms (the
Pocket: 14.5 ms mean, 19.6 worst; 13.8 mean in the earlier run with the steps before the scene). The steps now run in slices, whenever the geometry unit's buffer is full
(`RACE_OVERLAP` 2), and cost 2.9 ms since the physics remembers its sines and cosines (`firmware/trigmemo.h`; `check_car.sh` output equal).

**The start's judder (2026-11-05).** On the Pocket the computer's car beside the player's jerked forward and back as the cars pulled away. The physics
is not at fault: the simulation of the firmware (`tools/s2/sim_race.sh`, track 1, the gas held) and the host's car agree to 1.2 units over the 69
frames after the green light, and the speed rises evenly. The drawing was: a car's blocks were made from its state when the drawing came to
it (`race_make`), and the first slice of the computers' steps, the nearest car's own, runs about then. A probe in the simulation (not kept) found that car
drawn after its step in 20 of the 69 frames and before it in 49, with no rhythm: what it advanced on screen from one frame to the next was 0 in 7
frames and two steps in 8 (0 to 126 units against 76 a step); the cars farther off are made before the first slice and were always one step old.
`road_race_scene` now copies every car's pose before any slice (`race_looks`): all three computers are one step old, always (69 of 69 frames, one step
in each of the 68 intervals; the frame time is the same, 10.3 ms mean and 11.4 worst, and the slices still run in the scene). The physics and
`check_car.sh` are unchanged.

## Open points

- The deviations above are chaotic: a wall, a jump or the creep of a nearly stopped car amplifies a last-bit difference, so
  after the first contact a run is the same kind of run but not the same run. The numbers before the first contact are
  what the check measures.
- The pose of a tilted car (`s2car_look`) and the wheels' sign of turning have not been seen on a slope, and the camera
  of view 3 only in the numbers.
- Not done: the road kind's effects on the look, the dust, the camera's other views.
- The race (section above): the split times and
  "BEST LAP!" messages; the records (best times kept between races); the levels (pro / normal / rookie, `race_level`) and the number of laps (`race_laps`) and the player's team and car
  (`race_grid`) are set by the start menu (`firmware/s2menu.c`, docs/development.md "Start menu"); the computers' cars are the other three
  teams' cars of their own numbers; the game's end screen (REPLAY / RACE AGAIN / EXIT, the table of best times) is a text.
- Not compared with a recording of the original's computer cars: the lap times above are the model's own (the original ran at 17
  pictures a second with whole-number places; its cars were slower).
