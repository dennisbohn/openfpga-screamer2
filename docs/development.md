# Development

How to build the core, run the simulations and use the tools. Run everything from the
repository root in a POSIX shell (Git Bash works on Windows). Docker is needed for Quartus,
the firmware toolchain and Verilator; Python 3 for the scripts.

## Build

```sh
tools/fw.sh          # firmware: build/firmware/firmware.bin and program.bin
tools/build.sh       # Quartus: core/output/bitstream.rbf_r
tools/package.sh     # SD card layout in build/sdcard/, copy its contents to the card's root
tools/deploy.sh /i   # package and copy to the SD card mounted at /i (or I:)
```

Quartus Prime Lite 18.1.1 runs in the image `raetro/quartus:pocket`, the version of Analogue's
core template. `tools/build.sh` ends with three summary lines (`tools/buildsum.py`): the
time and the resource use, the smallest slack of every timing check, and the logic of every
unit, each with the change against the previous build. The full log is `build/quartus.log`.
The Pocket needs the bitstream bit-reversed, `tools/reverse_rbf.py` does that.

The firmware is built by GCC 14 and picolibc from Debian trixie in Docker
(`firmware/docker/Dockerfile`); `tools/fw.sh` builds the image on first use. See
[firmware/README.md](../firmware/README.md).

A release holds the bitstream, the core's JSON files, the platform files and the firmware,
never game data. `tools/deploy.sh <sd card> [game folder]` additionally copies your own game
files (default folder `C:/GOG Games/Screamer 2`, or `$GAME_DIR`): `SCREAMER2.zip`, `GAME.GOG`
and `GAME.DAT`, to `Assets/screamer2/common/`, and leaves everything else on the card alone.

## Game data

The game's files reach the core as ONE ZIP in one data slot (the APF allows 32 slots, and Screamer 2
has about 140 files): `SCREAMER2.zip` holds the original `DATA`, `GFX` and `ANIMS` folders, entries
stored (method 0, fast) or deflated (method 8, loads slowly; the firmware inflates them and checks the
CRC-32). The music image `GAME.GOG` and its cue sheet `GAME.DAT` stay separate slots. How a player makes
the ZIP is in `core/dist/common/README.txt` (Windows 11 Explorer, `tar`, 7-Zip). The firmware reads the
ZIP with `firmware/zip.c` (the central directory only: Windows writes sizes 0 into the local headers;
names are matched on the end of the path, ignoring case, so a folder in front does not matter).
`tools/check_zip.sh` tests the reader natively against the real ZIP and broken ones.

The core reads the container files inside the ZIP unchanged. The firmware reads four containers whole
with `zip_read` (`GFX/MAPnH01.DAT`, `GFX/CARS101.DAT`, `DATA/MAPn.DAT`, `GFX/ENVPn01.DAT`), unpacks their entries with
`firmware/dat.c` and builds each track's image at load time (`track_build_s2()` in
`firmware/track_build.c`): the placements, the palette, the drivable parts per grid cell, the road points
and walls, the background and TRACK_DATA's far plane, and the cars AUTO1..5 of team 1 with their material
libraries merged after the track's (the face's material is a signed 16-bit offset from the MLB's start, so the
cars' material records are collected in a section in front of the MLB and the cars' faces point there; the
texture records and pixels stay in the cars' own `.MLB` files, appended after the track's), and the picture of
the cars' windows (below). The image is 3.7 to 5.5 MB of the 7 MB window at `0x1210_0000`. There is no camera path, no markers file (Screamer 2 counts the laps on
the line of `MAPnOPP.SPL`, which the firmware reads from `DATA/MAPn.DAT` itself, `firmware/race.c`) and no menu. Nothing is converted on the PC.

`tools/s2/make_track_s2.py <game folder> <n> <out.bin>` builds the same image on the PC. It only serves
as the reference for `tools/s2/check_track_build_s2.sh [game folder]`, which compiles the firmware's
builder natively (address and undefined-behaviour sanitizers) and compares the result byte for byte, for
the seven tracks. The game folder is the unzipped original (its `GFX` and `DATA` folders): the ZIP only
holds the same files. (`tools/make_track.py` and `tools/check_track_build.sh` are Screamer 1's builder
and its reference, no longer reached by the Screamer 2 program.)

`L` and `H` in `MAPnL01.DAT` / `MAPnH01.DAT` are the game's DETAIL sets, not resolutions: the game
loads the `L` set only for LOW and the `H` set from MID on (more scenery, same textures). The core uses
the `H` sets (DETAIL HIGH), day, 256 colours (`..01`).

The core lists its files on the screen (green there, red missing and needed, grey missing and
optional) and says in plain words what is wrong with the ZIP (not found, not a ZIP, unsupported
compression: repack with Store; a deflated ZIP gets a notice that loading is slow). `GAME.GOG` and
`GAME.DAT` are optional: they only bring the music. `tools/files_screens.sh` draws these screens on the
PC (`build/check/screens/*.png`).

## Viewer (milestone 2)

After the files screen (A, B or START, or 5 s once the read test passed) the core loads track 1 and shows
it from the game's near chase camera over a car that drives the road at 150 km/h in a loop, with the
player's car and three more ahead (`firmware/viewer.c`, `firmware/road.c`). Frames go through the
hardware path (geometry unit, setup unit, rasterizer, blitter) with the original's level of detail at
DETAIL HIGH (`track_lod_level()`: placed meshes switch at 6,000 and 9,000 units, cars at 2,000 and 6,000;
far plane from TRACK_DATA, 50,000, track 4 60,000; pieces out to far + 8,192). No menu, race screen, sound
or music. Start switches between this ride and the driving mode (next section).

| Button (ride) | |
|---|---|
| d-pad left / right | previous / next track (1 to 7), the track is loaded again (about 5 to 10 s) |
| A | the cars: 4 (the realistic field), 0, 1 (the player's) in turn |
| B | timing overlay on / off (default on) |
| Start | the driving mode |

### Driving mode (the race)

Start puts the player and three computer cars on the game's start grid (the player's car last, the others in front of him,
two rows of two, 2,000 units apart, as the game's `0x2f519` places them), holds them for 3 seconds with the start lights (3, 2,
1, then GO!), and lets the player race three laps against them (`firmware/race.c`, docs/screamer2/driving.md, "The race"). The buttons are Screamer 1's
(`core/input.json`, README "Controls": B accelerate, Y brake, d-pad steering, L / R the gears), the camera is
the car physics' chase camera. The physics steps 20 ms at a time by real elapsed time (a video
frame is 20 ms; at most 4 steps per frame, as Screamer 1's race), so a slow frame makes the picture slower, not
the cars. The four cars are the same model (`firmware/s2car.c`): the player's AUTO01, the computers' AUTO02 to AUTO04
(teams 2 to 4: their car numbers are the game's; the models are team 1's, the image holds only those). Speed in mph, gear and the gearbox (AUTO / MAN) are drawn top right, below it
`LAP 1/3` and `POS 1/4`, top left under the timing overlay `TIME`, `LAP` (this lap's clock) and `BEST`
(mm:ss:hundredths). After his third lap the player's car is the game's driver's (it slows to a stop) and `FINISH`, his place and total
time are on the screen.

| Button (drive) | |
|---|---|
| d-pad left / right | steering (-1 / 0 / +1: there is no analog value; a docked controller's stick works as the d-pad) |
| B / Y | gas / brake (1 or 0) |
| A | handbrake (held) |
| L / R | gear down / up (on the step the button goes down; the gearbox is automatic until d-pad up makes it manual) |
| d-pad up | automatic / manual gearbox |
| X | timing overlay on / off (B is the gas here; Screamer 1's X, the camera, has no use yet) |
| Select + left / right | previous / next track (the track is loaded again, a new race) |
| Select + Start | a new race from the grid |
| Start | back to the ride |

A car (any of the four) with no drivable ground under it for 3 s (`off_track` of the state), or more than 3,000 units below the
lowest road point of the track, or a computer's car that has not moved for 6 s, is put back onto the line by itself (the nearest point
of the game's line, heading along the road, standing; its laps and times stay): `race.c`, `FALL_UNITS` in `viewer.c`. The grid hold is the
car's gearbox in neutral (`s2car_state.hold`): on the grid the throttle revs the engine and nothing else. The log gets
`race init <n> ms` (the first start of a run), and the visit's last line the mean and worst ms of the player's step and the
scene set-up (`ph<mean>/<worst>`, the first mark of the frame timeline) and of the computers' three steps (`o<mean>/<worst>`).

The computers' steps (the cars' physics, the driver, the cars touching) cost 3.4 ms a frame on the CPU, more than a third
of what the lone car's whole frame took. The viewer therefore runs them after the pieces are handed to the hardware, while the
rasterizer finishes the picture (`race_step_others()`, `RACE_OVERLAP` in `viewer.c`, 1 by default; `-DRACE_OVERLAP=0` runs
them before the scene as one step with the player's): where the rasterizer is the bound, which the worst frames are, the CPU
waits for it anyway and the steps cost nothing; the computers' cars are drawn from the step before, 20 ms old. See
docs/screamer2/driving.md, "The race", for the numbers.

The physics sits behind `firmware/s2car.h` (`s2car_init`, `s2car_step` for one 20 ms step, `s2car_look`,
`s2car_camera`; the header is the contract with the car physics). `s2car.c` is the game's car model (docs/screamer2/driving.md). Until `firmware/s2car.c`
exists the Makefile builds the kinematic stand-in `firmware/s2car_stub.c`: speed from gas, brake and drag (top
speed 200 km/h), a yaw rate from the wheel angle, the car on the surface under it (`track_under`) and tilted by it,
five gears of 40 km/h, a camera behind and above as the ride's. It is not a model of the game's car (no
grip, weight, reverse, walls or fall: with no ground under it the car keeps its height). With `s2car.c` present
the Makefile takes it and every other `firmware/s2car*.c` instead (`S2CAR_SRCS`, also settable on the command
line); `tools/s2/render_s2.sh` and `framestats.sh` do the same. `yaw` grows to the left (the view turns left),
so steering left (-1) must increase it. The race (`firmware/race.h`) uses more of
the physics' side of the header (`s2car_init_grid`, `s2car_init_at`, `s2car_step_driven`, the `hold` and `ai` fields of the state):
the stand-in does not have those, so a build without `s2car.c` does not link.

The overlay (top left, drawn after the 3D picture) shows the mean and the worst time of a frame over
the last second in ms, the track, the cars and the faces of the frame. A frame's time runs from its
start (the swap before it is done) to the end of the rasterizer's work, not including the wait for the next
vertical blanking, so 10 ms still shows 50 pictures a second; the camera advances by the real time
(video frames of 1/50 s), so a frame over 20 ms makes the picture slower but not the movement.
`debug.txt` (4 KB, not a ring: when it is full later lines are lost, so the lines are short): its first line is the
live statistics of the current visit (`Screamer Pocket T1 c4 fr1234 avg10.4 max12.8 late0 fps49.9`: track, cars or `drv`
in the driving mode, frames, mean and worst ms, frames over 20 ms, pictures a second); one line per track load (`T1 load
5100 ms: read 2973 KB ... ms, build 4494 KB ... ms (unpack ...), road 1440 pts ... ms, 2000 pl`); one line when a visit
ends (another track, the cars or the mode changed; a visit of under 50 frames leaves none), `T1 c4 249fr avg10.4
max12.8@512k late0 50.0/s f1180 p95` (track, cars or drv, frames, mean and worst ms and where on the road in thousand
units (not in drv), frames over 20 ms, pictures a second, faces and pieces a frame); the frame timeline (`tl_*`: cycles and
the units' busy clocks at each stage, 1 KB) once, 5 s into the first visit. A visit that ends with the core being quit
has no line: the first line is its last state. The load no longer checks the image's CRC-32s (they cost 1.4 s; the
native check of the build has them, and the firmware's builder leaves them 0).

Known gaps (accepted): the cars' windows show the original's environment picture (routine 160 reads the
first material of `ENV.MLB`, `GFX/ENVPn01.DAT`; no hardware change, see "The windows" below) but the picture
neither slides with the car's travel nor changes by the road's kind (the tunnels' ceiling frames of tracks 3, 4, 6, 7:
the viewer's objects carry no reflection accumulators and phase 0), the palette of tracks 2 and 5 is the track's,
not `ENV.COL`'s, no shadows, smoke, props or animated scenery, no fade by depth, no night or fog, no mirror, the S1
see-through table (64 rows) where Screamer 2 has 112, the sky and ground colour of `TRACK_DATA` only
where the backdrop picture ends.

The windows: the game draws a car's glass (routine 160, a material of 8 x 8 with a placeholder picture in the car's
`.MLB`) with the picture of the first material of the track's `ENV.MLB` (128 x 128, 1 to 3 frames; the frame of an
object is n x phase >> 16) and masks the texel's address with `0x7f7f` (`STARTL.EXE` 0xcd771), where the hardware
masks u and v with 0xff and reads a window's u, v as whole texels (0xcd118 shifts the reflection's value left by 3: one
texel each, as `firmware/geom.c`). So the builder stores each frame tiled 2 x 2 as 256 x 256 (64 KB) after the cars'
libraries, inside the MLB section, with a texture record of its own, and the records of the cars' routine-160
materials point to it (`track_build_s2()`, `make_track_s2.py`; image version stays 7: no new section). That costs
64 KB a frame in the image (tracks 1, 2, 5: one frame, 64.3 KB; tracks 4, 6, 7: two, 128.3 KB; track 3: three,
192.3 KB; the largest image, track 6, is 5.5 MB), and 6 to 20 KB read from the ZIP at the load (`GFX/ENVPn01.DAT`, at
`0x12D4_0000`). `tools/s2/check_env_s2.py` (run by `check_track_build_s2.sh`) compares every texel the reflection can
address (u, v in -512 to 511) with the original's wrap-128 read.

What to expect: `docs/screamer2/performance.md`, section 6, forecasts for the four-car case with the level of
detail, mean / worst ms of the game's frame including its physics (about 1.7 ms) and the race screen's
tail: track 1 10.1 / 12.2, 2 10.6 / 13.0, 3 10.5 / 13.6, 4 12.7 / 15.8, 5 11.1 / 15.6, 6 10.1 / 13.3, 7 12.0 /
17.4. The viewer has neither, so expect about 1 to 2 ms less; a mean within ±10 % of that and no frame
over 20 ms on any track confirms the forecast.

Simulation of the viewer (8 frames at the start of track 1, about 6 minutes of Verilator once the model is built): the
firmware takes the finished image from the testbench instead of building it (the build is checked
natively, above), `tools/s2/make_track_s2.py` makes it:

```sh
python -I tools/s2/make_track_s2.py <game folder> 1 build/s2/track1.bin
# build/gamedata/ needs SCREAMER2.zip, GAME.GOG and GAME.DAT (see tools/simcheck.sh)
SIM_QUICK=1 SIM_GAME=build/gamedata SIM_IMAGE=build/s2/track1.bin SIM_TRACK=1 SIM_RACE_FRAMES=8 \
    tools/sim.sh soc build/firmware/firmware.bin build/sim/viewer 1500 > build/sim/viewer.log
python -I tools/rgb2png.py build/sim/viewer.rgb build/sim/viewer.png
```

The run of 2026-11-04: no trap, 6.1 ms mean and 6.3 ms worst for the 8 frames (the cost model gives 6.0 ms for
them without the game's own physics), the picture equal to the PC render of the same camera
(`tools/s2/render_s2.sh build/s2/track1.bin <prefix> v1482.4,4`) in every pixel outside the overlay's box.

The driving mode (the race) in the simulation: Start held (`SIM_KEYS=8000`) starts the viewer in the driving mode, on scripted
buttons (`sim_keys()` in `viewer.c`: the gas all the time), one physics step per frame; `SIM_RACE_FRAMES` is the number of frames, the log
gets a line per 25 frames (speed, gear, heading, place of the player's car) and, at the end, the visit's line with the
frame cost. 150 frames are the grid's hold (the race clock is the frame count x 20 ms), so 220 frames are 70 frames of racing:

```sh
python -I tools/s2/make_track_s2.py <game folder> 4 build/s2/track4.bin
SIM_QUICK=1 SIM_GAME=build/gamedata SIM_IMAGE=build/s2/track4.bin SIM_TRACK=4 SIM_RACE_FRAMES=220 SIM_KEYS=8000 \
    tools/sim.sh soc build/firmware/firmware.bin build/sim/race4 4000 > build/sim/race4.log
python -I tools/rgb2png.py build/sim/race4.rgb build/sim/race4.png
```

The run of 2026-11-04, track 4 (the race with its steps run before the scene, as `-DRACE_OVERLAP=0`): no trap, 220 frames at 50.2 a
second, 13.8 ms mean and 14.6 ms worst (876 faces and 218 pieces a frame), no frame over 20 ms; the first mark of the frame timeline (the
four cars' steps, the driver, the cars touching, the scene's objects) 316,875 cycles = 4.27 ms, against 0.88 ms for the lone car, so
the three computers cost 3.4 ms; the race clock released the cars at frame 150 (13, 27 mph 25 and 50 frames later). Stack of
the program 4.5 KB. The frame was rasterizer-bound (the CPU waited 1.7 ms for it at the end); the version with the computers'
steps after the scene (`RACE_OVERLAP` 1, what the Pocket gets) was not simulated.

The lone car's run with the stand-in physics, from an earlier version of the viewer (scripted gas for 2.6 s, left, right, then the
brake: the buttons of `tools/s2/render_s2.c` `d<k>`, which still has that script): no trap, 160 frames at 50.3 a second, 5.5 ms mean and 6.4 ms worst (332 faces,
60 pieces a frame; the ride's 4-car frame took 6.1 ms; the physics step with the stand-in about 0.1 ms), the speed
1, 17, 30, 42, 52, 60 mph on the gas and 29 mph 0.5 s into the brake, the heading turning left then right; the
hardware's last frame equals the PC render of the same step (`render_s2.sh build/s2/track1.bin <prefix> d161`) in every
pixel below the overlay's rows. The load line of this run: 654 ms with the image from the testbench (the 1.4 s CRC
check is gone).

## PC renderer

`tools/s2/render_s2.sh <image> <out prefix> v<s>[,<cars>] ...` draws the viewer's scene (`road.c`, `track.c`,
`car.c` and the C models of the units) natively in Docker: `v0.3f,4` is 30 % round the road with four cars;
`NOLOD=1` draws every block at level 0 (to compare), `d<k>` the lone car after k steps of a script of its own, `g<k>[,<n>]` the race for k
steps (the firmware's own `race.c`; the player parked, `RACE_GAS=1` holding the gas, `RACE_ROBOT=1` driven by the game's driver) with a
line per car every n steps and per lap done, `r<i>` and `c<x>,<y>,<h>,<yaw>,<pitch>` are other
cameras; `tools/rgb2png.py` makes PNGs. `tools/s2/framestats.sh build` and `sweep s2 <image> <label> <out.csv>
<step> <cars,...>` count what a frame asks of the units along the road (`ROAD_S=185,371` the viewer's own
places), see `docs/screamer2/performance.md`. For Screamer 1: `tools/render.sh` runs the firmware's C code
natively (`tools/host/render.c`); it needs the game files in `build/gamedata/` and its images are version 6,
which the renderer no longer reads. The header of the script lists its switches.

## Simulation

Verilator runs in Docker (`verilator/verilator:v5.050`). Each testbench lives in
`sim/<name>/`: `files.f` names the top module and the sources, `tb.cpp` drives them.

```sh
# Two framebuffers: load two pictures from the game data over the bridge, swap them by key,
# capture 13 frames and check 12 of them pixel by pixel (one is interrupted by a reset).
python tools/make_screen.py "C:/GOG Games/Screamer/GFX/GRAF.DAT" LOAD2L.PCX \
    build/sim/screen.bin build/preview/LOAD2L.png
python tools/make_screen.py "C:/GOG Games/Screamer/GFX/GRAF.DAT" LOAD1L.PCX \
    build/sim/screen2.bin build/preview/LOAD1L.png
tools/sim.sh display build/sim/screen.bin build/sim/screen2.bin build/sim/display_frame

# The CPU system with firmware and a model of the SDRAM: self-tests, memory test, test band,
# log over the bridge, then two resets with reloaded firmware.
tools/fw.sh
tools/sim.sh soc build/firmware/firmware.bin build/sim/soc
# Fault test: an instruction fetch behind the boot RAM, the trap handler has to report it.
tools/fw.sh FAULT=IFETCH
tools/sim.sh soc build/firmware-IFETCH/firmware.bin build/sim/soc_ifetch 80 c0000001
# Counter-checks: one fault in a memory model at a time, the memory test must fail exactly
# the expected tests.
tools/sim_faults.sh

# Random test of the SDRAM port and controller against the SDRAM model: seed, clocks.
tools/sim.sh sdram 1 2000000
# Self-test of the SDRAM model: one correct and one wrong sequence per rule.
tools/sim.sh sdram_model
# The mixer (audio.v) against its C model firmware/mixer.c: random voices, loops, steps and
# volumes; every output value as it leaves over I2S is compared. Values, seed.
tools/sim.sh audio 20000 1
# The FM music (opl.v with the real JTOPL chip): the write queue's timing, the chip's pitch
# against the register values, the gain, run = 0.
tools/sim.sh opl
# A song on the real chip into a WAV file (events from tools/host/fmdump.c, "events"), to
# listen to and to measure the level: tools/sim.sh opl play <events> <seconds> <out.wav> [gain]
```

The SDRAM model in `sim/models/sdram_model.v` checks the datasheet timings in clocks and can be
built with injected faults for the counter-checks (`SIM_DEFINES`, see `tools/sim.sh`).

`tools/simcheck.sh [--full] [--save] [name]` runs the SoC simulation with the game files from
`build/gamedata/` (`SCREAMER2.zip`, which the testbench reads where the firmware asks, and small
`GAME.GOG` and `GAME.DAT`) and summarizes it in a few lines (`tools/simsum.py`). Without `--full` it
is the quick run (no resets); `--full` behaves like the Pocket. Without a track image (`SIM_IMAGE`) the
program stops at the files screen, so the check is its title bar and the log (`SCREAMER2.zip: ...`, `game
files: ...`, one line per file of the read test). With `SIM_IMAGE` the viewer runs (see above) and the
testbench only checks the log (the track's `T1 load ...` line), the picture is `build/sim/<name>.rgb`. The audio of the
run (I2S output, 48 kHz stereo) goes to `build/sim/audio.wav`.

## Tools

| Tool | What it does |
|---|---|
| `tools/dat_sizes.py <file.DAT>...` | lists a game container with unpacked sizes (`-v`: every file) |
| `tools/datfile.py` | reads the containers (shared by the other scripts) |
| `tools/check_zip.sh`, `tools/check_zip.py`, `tools/check_zip.c` | the ZIP reader `firmware/zip.c` natively in Docker: the real `SCREAMER2.zip` (every file's size and CRC-32 against Python's `zipfile`), archives made on the fly (deflated at levels 1, 6, 9, wrong CRC, damaged, cut, other methods, wrong top folder, too many files), a fuzz of damaged copies under the address sanitizer, and the speed of the inflate on the PC |
| `tools/files_screens.sh`, `tools/files_screens.py`, `tools/host/files_screen.c` | the files screen of `firmware/files.c` on the PC for the real ZIP and made-up ones (missing file, deflated, unsupported method, other top folder, not a ZIP, not there): `build/check/screens/*.png` |
| `tools/s2/make_track_s2.py`, `tools/s2/check_track_build_s2.sh`, `tools/s2/check_track_build_s2.c`, `tools/s2/check_env_s2.py` | the Screamer 2 track image on the PC (the reference) and the firmware's builder `track_build_s2()` compared with it byte for byte, seven tracks, natively under the sanitizers; the windows' reflection picture in the image against the original's |
| `tools/s2/check_params.sh`, `tools/s2/check_params.py`, `tools/s2/check_params.c` | the car's numbers: `firmware/s2params.c` (CAR_PARAMS of `DATA/GAME.DAT`, TRACK_DATA with the road kinds of `DATA/MAPn.DAT`) natively against an independent reading in Python, every field of 20 cars and 7 tracks |
| `tools/s2/check_car.sh`, `tools/s2/check_car.c`, `tools/s2/car_scenarios/` | the car physics `firmware/s2car.c` natively in float32 on the track images against the reference traces of OpenScreamer's routines (the folder `ref/` next to the repository, or `$REF_DIR`; scenarios in its `scn/`, traces in `traces*/`), the place cut as the game does and kept as a float; per quantity the largest and the mean difference; also the car alone from each track's grid place and the trigonometry and ground looks a step costs. `car_scenarios/` holds the scenarios heading towards the origin (`a_origin` ..., traces made with `tools/s2/ref_trace/ref_run.sh` into `ref/traces_benign/`) |
| `tools/s2/render_s2.sh`, `tools/s2/render_s2.c` | the viewer's scene, the lone car and the race on the PC from the firmware's own code (`NOLOD=1`: no level of detail; `g<k>`: the race, laps and places of the four cars as a table) |
| `tools/s2/framestats.sh`, `tools/s2/perf_patch.py`, `tools/s2/perf_model.py`, `tools/s2/lod_report.py` | the study's counting build (counters in copies of the firmware) and cost model, `docs/screamer2/performance.md` |
| `tools/fmcheck.sh [song ...]`, `tools/fmsongs.py`, `tools/host/fmdump.c`, `tools/host/fmref.c` | the FM music on the PC: `fmsongs.py` reads the songs of `DATA/MUSIC.DAT` independently (tracks, messages, lengths); `fmdump.c` runs `firmware/fm.c` against a model of the hardware's write queue; `fmref.c` is openscreamer's driver with the chip replaced by a log; `fmcheck.sh` runs all of them (Docker) and compares the register writes tick by tick |
| `tools/dis.sh [-h] [-f] <from> <to>` | disassembles part of `STARTL.EXE` (`-h`: `STARTH.EXE`) at the addresses of OpenScreamer's notes; `tools/unle.py` (from OpenScreamer) unpacks the program into `build/exe/` on first use |
| `tools/dosram.py` | reads the memory of the original game running in DOSBox, to compare with the firmware |
| `tools/release_zip.py` | after `tools/package.sh`: the release archive `build/release/Chainy.Screamer2_<version>.zip` (version from `core/core.json`), refusing to include game data |
| `tools/pocketlog.py [debug.txt]` | reads the Pocket's debug log (default: the SD card at I:) as a short table |
| `tools/platform_image.py` | makes the platform image (`.bin`) from a PNG and back |
| `tools/gen_pll_sdram.sh` | regenerates the SDRAM clock PLL (Intel `ip-generate` in the Quartus image) |
| `tools/sta_sdram.tcl`, `tools/check_budget.py` | after every build: the four SDRAM I/O paths, resource use and I/O registers (called by `tools/build.sh`) |
| `tools/make_budget_svg.py`, `tools/make_timeline_svg.py` | draw `docs/resources.svg` and `docs/frame_timeline.svg` |
| `tools/vexriscv/` | the CPU's configuration and generator, see [tools/vexriscv/README.md](../tools/vexriscv/README.md) |
