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

The core reads the container files inside the ZIP unchanged. The firmware reads three containers whole
with `zip_read` (`GFX/MAPnH01.DAT`, `DATA/MAPn.DAT`, `GFX/ENVPn01.DAT`), unpacks their entries with
`firmware/dat.c` and builds each track's image at load time (`track_build_s2()` in
`firmware/track_build.c`): the placements, the palette, the drivable parts per grid cell, the road points
and walls, the background and TRACK_DATA's far plane, and the cars of the race with their material
libraries merged after the track's (the face's material is a signed 16-bit offset from the MLB's start, so the
cars' material records are collected in a section in front of the MLB and the cars' faces point there; the
texture records and pixels stay in the cars' own `.MLB` files, appended after the track's), and the picture of
the cars' windows (below). The cars are the four of the grid, each its own team's ("Teams" below). The image is 3.5 to 5.4 MB of the 7 MB window at `0x1210_0000`. There is no camera path, no markers file (Screamer 2 counts the laps on
the line of `MAPnOPP.SPL`, which the firmware reads from `DATA/MAPn.DAT` itself, `firmware/race.c`) and none of the original's menus (a plain start menu, "Start menu" below). Nothing is converted on the PC.

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

After the files screen (A, B or START, or 5 s once the read test passed) the core shows the start menu
(next section); WATCH there loads the chosen track and shows it from the game's near chase camera over a car that drives
the road at 150 km/h in a loop, with the player's car and three more ahead (`firmware/viewer.c`, `firmware/road.c`). Frames go through the
hardware path (geometry unit, setup unit, rasterizer, blitter) with the original's level of detail at
DETAIL HIGH (`track_lod_level()`: placed meshes switch at 6,000 and 9,000 units, cars at 2,000 and 6,000;
far plane from TRACK_DATA, 50,000, track 4 60,000; pieces out to far + 8,192). The ride is silent (the race has sound and music,
"Sound" below). Start starts the driving mode ("Driving mode" below); the way back to the ride is the start menu's WATCH.

### Start menu

A list in the console's text style (`firmware/s2menu.c`, 40 x 25 characters), the stand-in for the original's options room (the original's own menu screens are in
docs/screamer2/menus.md). Everything is open from the start: all seven tracks, all four teams and their five cars, no championship.

| Row | |
|---|---|
| ARCADE | the original's arcade pages (below), then the race |
| NUMBER OF LAPS | 1, 3, 5, 10 (the original's options; `race_laps`; a GAME SETTINGS row) |
| ARCADE ENEMIES SKILL | ROOKIE, NORMAL, PRO (the original's levels; `race_level = 2 - skill`, the row of `SKILLS` the computers' pace is read from; a GAME SETTINGS row) |
| START RACE | the race at once with the choices as they are; left / right change the track (1 ENGLAND, 2 COLUMBIA, 3 EGYPT, 4 CALIFORNIA, 5 FINLAND, 6 SWITZERLAND, 7 THE TUNNEL: the files' numbers, names read from `STRINGS.TXT` in `DATA/LOCAL.DAT` of the ZIP, the built-in text is the same) |
| WATCH | the viewer's ride (for timing) |
| GAME SETTINGS, HI SCORES | the original's two pages (`firmware/s2pages.c`, "End of the race and the two pages" below); B or Start (A) opens one, A or Select leaves it |

| Button (menu) | |
|---|---|
| d-pad up / down | row (wraps) |
| d-pad left / right | the row's value (wraps); on START RACE the track; a docked controller's stick works as the d-pad |
| L / R | the track three back / on, from any row: every track is at most two moves from every other, so with B or Start at most three presses (track 4 from track 1: R, then B or Start on START RACE) |
| A, B or Start | the row's action: the arcade pages on ARCADE, WATCH on its row, the GAME SETTINGS and HI SCORES pages on theirs, the race on the others; X: WATCH |

**The arcade pages** (`firmware/s2arcade.c`, details in docs/screamer2/menus.md section 6): title (ARCADE_L, ACCELERATE TO CONTINUE blinking) -> TRACK (the tracks' pictures with their banners,
the lit frame) -> CAR (a 2D stand-in for the 3D page: team, car and its name plate) -> TRANSMISSION (AUTOMATIC / MANUAL) -> the race. B or Start go on (the picture fades out over half a
second, with the page's click), A or Select go back (a cut): the TRACK page's A leaves to the start menu, the CAR page's to TRACK, the TRANSMISSION page's to CAR.

| Button (arcade pages) | |
|---|---|
| B or Start | forward |
| A or Select | back |
| d-pad left / right | TRACK: the track before / after (the original's order England, Egypt, California, Finland, Switzerland, Columbia, The Tunnel); CAR: the car; TRANSMISSION: automatic / manual |
| d-pad up / down | CAR: the team (the original chooses it on an earlier page, not built) |
| L / R | TRACK: three places back / on, round |

The choices are kept: in the session, and across power-ups in the nonvolatile `CHOICE.DAT` (data slot 23), Screamer 2's save in a layout of its
own (`firmware/s2save.c`, docs/screamer2/menus.md section 7.1; `s2menu_store()` sets the track, team and car, and the laps, level and gearbox settings, also after the arcade pages
are left with A), which the
Pocket writes back when the core exits (the first start with the new firmware reads the old 1,120-byte file once and turns it into the 19,348-byte one). After the files screen the menu shows the last choices, so the usual start is two presses (A on the
files screen, then B or Start with the cursor on START RACE; the cursor starts on ARCADE and stays where it was left in a session). The grid is applied when the track image is built (`load_track()`), so every start from the menu loads the track again
(3 to 5 s; the cars that did not change are not read again). The log gets one line per start: `menu: race T4 CONDOR 1 3L NORMAL AUTO`.
`tools/menu_screens.sh` runs the menu's code on the PC: tests of the keys, the grid, the kept choices and the texts read from the real ZIP,
and pictures of the list in `build/check/menu/`; the arcade pages are compared with a model (below, Tools).

Back to the menu: EXIT in the race's pause menu (Start, "Loading screen and pause menu" below), Select + B in the ride (the music stops), and EXIT on the end screen
of a finished race ("End of the race and the two pages" below). The simulation (`SIM_*`) has no menu: it takes the track from the testbench as before.

| Button (ride) | |
|---|---|
| d-pad left / right | previous / next track (1 to 7), the track is loaded again (about 5 to 10 s) |
| A | the cars: 4 (the realistic field), 0, 1 (the player's) in turn |
| B | timing overlay on / off (default on) |
| Select + B | back to the menu |
| Start | the driving mode |

### Driving mode (the race)

Start puts the player and three computer cars on the game's start grid (the player's car last, the others in front of him,
two rows of two, 2,000 units apart, as the game's `0x2f519` places them), holds them for 3 seconds with the start lights (3, 2,
1, then GO!), and lets the player race the laps of the menu (3 by default) against them (`firmware/race.c`, docs/screamer2/driving.md, "The race"). The buttons are Screamer 1's
(`core/input.json`, README "Controls": B accelerate, Y brake, d-pad steering, L / R the gears), the camera is
the car physics' chase camera. The physics steps 20 ms at a time by real elapsed time (a video
frame is 20 ms; at most 4 steps per frame, as Screamer 1's race), so a slow frame makes the picture slower, not
the cars. The four cars (`firmware/s2car.c`) each wear their own team's model and paint: by default the player's is team 1's car 1
(CONDOR), the computers' are team 2's car 2, team 3's car 3 and team 4's car 4; the start menu sets the player's team and car and the
computers get the other three teams ("Teams" below). Speed in mph, gear and the gearbox (AUTO / MAN) are drawn top right, below it
`LAP 1/3` and `POS 1/4`, top left under the timing overlay `TIME`, `LAP` (this lap's clock) and `BEST`
(mm:ss:hundredths). After his last lap the player's car is the game's driver's (it slows to a stop) and the original's end screen comes over the race
("End of the race and the two pages" below).

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
| After the finish | the end screen: d-pad left / right choose RACE AGAIN or EXIT, B or Start take it (after the four seconds of CONGRATULATIONS) |
| Start | the pause menu (RESUME, RESTART, EXIT; not once the player has finished), the original's Esc; B or Start take the item, A or Select resume |

### Loading screen and pause menu

Both are the original's pages on the 2D toolkit (`firmware/s2gfx.c`, `s2page.c`, `s2load.c`, `s2pause.c`; docs/screamer2/menus.md section 6 has the details and
the SDRAM map). **LOADING:** the track's `LOAD_nL` picture with the track's banner, BEST TIME and BEST LAP (the first rows of the save's arcade lists for the race's length: `--:--:-- MIL` for a track nobody has driven) and
LOADING... is put up before `load_track()` and stays through the track's build and the race's set-up: the display is driven by the hardware from the front
buffer and its palette, so the CPU can be busy for the 3 to 5 s of `T1 load` and the picture does not change. It then fades out in 0.4 s. The log has
`ui: loading screen up after N ms` (the picture's read, unpacking and drawing: 35 ms in the simulation, where a data slot is 3 times faster than the Pocket's, so
about 60 ms there), and once at the start `ui: 111 sprites, 6 fonts ... ms` (the toolkit's assets, before the start menu: 169 ms in the simulation, about 0.3 s on the Pocket).
The Pocket's log of 2026-11-04 (the build before this one) has the time the picture covers: track 4 `T4 load 4487 ms` (read 2,413 KB 2,117 ms, build 1,742 ms, road 424 ms)
and `race init 160 ms`, 4.65 s in all. **Pause:** Start in the race stops it over the darkened picture; the race loop is not
run, so the clock, the cars and the frame statistics stand still (the first frame after it takes one 20 ms step); the effects (engines, tyres) ease out over a
second and come back whole, the music plays on. RESTART is the race again from the grid (the track stays loaded), EXIT the start menu. The pause adds nothing to a
frame of the race but the test of one button bit: the race in the simulation (220 frames from the grid, track 4 and track 1, the code shifted by the new objects) took 10.7 ms
mean and 11.5 worst, and 10.2 and 11.1, against 10.8 / 11.5 and 10.3 / 11.1 of the build before. Opening the menu takes 74 ms in the simulation (`pause: still and
palette N ms` in the log, once: the darkening table and the copy of the screen, then the screen stands); a frame of the menu itself is one blitter list (the still and
a few letters, about 1.5 ms of the CPU's time). The screens drawn by the hardware in the simulation equal the PC's drawing pixel for pixel (LOADING, track 4: 0 of 64,000
pixels differ; the pause's picture was looked at: darkened race, box, dim items).

### End of the race and the two pages

Row 3 of the menus (docs/screamer2/menus.md section 6 has the positions and rules, read from `STARTL.EXE`). **End of the race** (`firmware/s2end.c`): once the player has done his laps, four
seconds of CONGRATULATIONS ! (first place only), `1ST POSITION` and his total time over the race that goes on behind it, then the track's banner, a darkened band with RACE AGAIN / EXIT (d-pad left /
right choose, B or Start take; the race from the grid again, or the start menu) and the track's 15 best times for the race's length, the player's new row lit and scrolled into view. At that moment his total time
and best lap go into the save's lists (name from the save, team and car of the grid, arcade), and the LOADING screen of the next race shows the track's best total time and best lap from them. Nothing is
drawn or run for it before the finish; the draw is after the rasterizer, like the old FINISH text it replaces (which stays for a core without the toolkit). **GAME SETTINGS** and **HI SCORES** (`firmware/s2pages.c`):
the start menu's last two rows open them. GAME SETTINGS: up / down choose a row, B or Start (or left / right) step its value, A or Select leave; the eight settings are kept in the save and TRANSMISSION, NUMBER OF LAPS,
ARCADE ENEMIES SKILL, DAMAGES (off by default as in the game: no damage marks or damage from a knock until it is on) and MUSIC act in the race, DETAIL LEVEL, MULTIPLAYER ENEMIES and TIME ATTACK GHOST CAR only keep their value.
HI SCORES TABLE: up / down choose a row of the list of 15, left / right the box above (TOTAL TIME / LAP TIME, the kind of game, the laps, the track, VIEW REPLAY), B or Start step the value in that box, A or
Select leave; VIEW REPLAY stays dark (no replays yet). Both are tested on the PC against references made from the game's files (`tools/menu_screens.sh`).
**Cost** (simulation of 2026-11-05; the simulation has no menus, so a build with `tools/fw.sh EXTRA_CFLAGS=-DS2_SIM_END=1 TAG=simend` draws the end screen over frames 2 to 9 of the simulated race and the log says
`end screen frame N: <us>`; `tools/s2/sim_race.sh <dir> <track 4 image> 4 14 <name>`): the first frame 70 ms (the darkening table of the race's palette, once at the finish: about 4 frames of the race
stand still while the car has stopped), then 3.3 ms a frame (23,000 pixels darkened, the banner, the choices and a list of six rows with their cars through the blitter), so a frame of the finished race is about 14 ms, under the 20 ms of 50 a second.
Before the finish nothing changed in a frame, but the code of the viewer, the car and the toolkit moved, and the I-cache is 8 KB and direct mapped: the race on track 4 (220 frames from the grid, the commands of "Simulation" above,
`tools/s2/sim_race.sh`) took 10.9 ms mean and 11.8 ms worst (no frame late, 50.0 a second) against 10.7 / 11.5 of the build before (the same simulation, rebuilt from `6c2945e`); the two edits alone gave 10.8 / 11.6 (the car's
DAMAGES gate with the new toolkit objects) and 10.8 / 11.9 (the viewer's end screen call), a padding of 40, 200 or 600 bytes behind `main.c`'s code 10.8 / 11.8, 10.9 / 11.9 and 10.9 / 11.8: layout, not work.

A car (any of the four) with no drivable ground under it for 3 s (`off_track` of the state), or more than 3,000 units below the
lowest road point of the track, or a computer's car that has not moved for 6 s, is put back onto the line by itself (the nearest point
of the game's line, heading along the road, standing; its laps and times stay): `race.c`, `FALL_UNITS` in `viewer.c`. The grid hold is the
car's gearbox in neutral (`s2car_state.hold`): on the grid the throttle revs the engine and nothing else. The log gets
`race init <n> ms` (the first start of a run), and the visit's last line the mean and worst ms of the player's step and the
scene set-up (`ph<mean>/<worst>`, the first mark of the frame timeline) and of the computers' three steps (`o<mean>/<worst>`).

The computers' steps (the cars' physics, the driver, the cars touching) cost 3.2 ms a frame on the CPU (2.6 ms since the sine and
cosine of the physics are remembered, performance.md section 7), more than a third of what the lone car's whole frame took. The viewer
therefore runs them while the hardware has work (`race_others_begin()`, `race_others_slice()`, `RACE_OVERLAP` in `viewer.c`, 2 by
default): in slices (one car's step, or the cars touching) whenever handing a piece to the geometry unit had to wait 1,200 cycles
for room in its buffer (`hw_idle` in `frame.h`: the buffer holds 107 pieces and is then full), and what is left after the last
piece, while the rasterizer finishes the picture. `-DRACE_OVERLAP=1` runs them all after the last piece, `-DRACE_OVERLAP=0` before
the scene as one step with the player's. Where the hardware is the bound, which the worst frames are, the CPU waits for it anyway and
the steps cost nothing; the computers' cars are drawn from the step before or the one just taken, 20 ms apart. See
docs/screamer2/driving.md, "The race", and performance.md section 7 for the numbers.

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
`debug.txt` (4 KB, not a ring: when it is full later lines are lost, so the lines are short): its first line (128 bytes; the
boot loader's `firmware.bin` and `program.bin` must be of the same build) is the
live statistics of the current visit (`Screamer Pocket T1 c4 fr1234 avg10.4 max12.8 late0 fps49.9`: track, cars or `drv`
in the driving mode, frames, mean and worst ms, frames over 20 ms, pictures a second), followed by what the longest frame was made of
(` | a1.3 c1.4 s7.1 o3.4 w0.0 f873 p219 n77`: ms before the scene (the player's step, the sound, the backdrop), in the cull, in the
pieces' loop (waiting for the geometry unit included), in the computers' steps, waiting for the rasterizer at the end; faces and
pieces of that frame; in the race the player's place on the line, as a node of it); one line per track load (`T1 load
5100 ms: read 2973 KB ... ms, build 4494 KB ... ms (unpack ...), road 1440 pts ... ms, 2000 pl`; on the first load `read` is 255 KB
and the ms of the cars' unpacking more than later ones, the cars staying unpacked); one line when a visit
ends (another track, the cars or the mode changed; a visit of under 50 frames leaves none), `T1 c4 249fr avg10.4
max12.8@512k late0 50.0/s f1180 p95` (track, cars or drv, frames, mean and worst ms and where on the road in thousand
units (not in drv), frames over 20 ms, pictures a second, faces and pieces a frame, in the race `ph` / `o` and the same parts of the
longest frame as the first line has); the frame timeline (`tl_*`: cycles and
the units' busy clocks at each stage, 1 KB) once, 5 s into the first visit. A visit that ends with the core being quit
has no line: the first line is its last state. The load no longer checks the image's CRC-32s (they cost 1.4 s; the
native check of the build has them, and the firmware's builder leaves them 0).

Known gaps (accepted): the cars' windows show the original's environment picture (routine 160 reads the
first material of `ENV.MLB`, `GFX/ENVPn01.DAT`; no hardware change, see "The windows" below) but the picture
neither slides with the car's travel nor changes by the road's kind (the tunnels' ceiling frames of tracks 3, 4, 6, 7:
the viewer's objects carry no reflection accumulators and phase 0), the palette of tracks 2 and 5 is the track's,
not `ENV.COL`'s, no shadows, smoke, props or animated scenery (nor its sounds), no fade by depth, no night or fog, no mirror, the S1
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
192.3 KB), and 6 to 20 KB read from the ZIP at the load (`GFX/ENVPn01.DAT`, at
`0x12D4_0000`). `tools/s2/check_env_s2.py` (run by `check_track_build_s2.sh`) compares every texel the reflection can
address (u, v in -512 to 511) with the original's wrap-128 read.

Teams: the game loads one car per car of the race (`STARTL.EXE` 0x2f519: `GFX/CARS<team>0<look>.DAT`, its `AUTO<1 + model>`:
`.MLB` with the paint and the sponsors' textures, `.3DX` with the meshes of body and wheels), so a race of four needs four
cars and none of the 16 others of the four teams (CONDOR, ANGELS, WASP, ZEUS: 1 to 4). The image holds those four, car
model k of the image being grid slot k (`race_grid` in `firmware/race.h`: team and car per slot as the files number them,
default team 1 car 1 for the player, team 2 car 2, team 3 car 3, team 4 car 4; the race draws slot k with model k, `road.c`
unchanged), each with its own material library: the material records of all four in the section in front of the MLB, their
libraries after the track's, the faces moved onto the records. The cars section has a row for each model with the car's
name in its livery word (team x 256 + car), by which `firmware/s2car.c` finds the car's wheel places in its own mesh (the
four teams' cars differ by a few units to 40 in the wheels' places). Colours 0 to 119 are the same in every car's `.COL` and in the tracks',
and the cars' textures use only those, so the track's palette serves (`check_cars_s2.py`). The image format stays version 7; an
older image (one stand-in row, the five cars of team 1) draws and drives as before.
`firmware/s2teams.c` reads the cars at the first track load and keeps them unpacked (4 x up to 224 KB at `0x12D6_0000`) for all
tracks: from a stored ZIP only the container's last 4 KB (its directory) and the two entries of each car, 239 KB of entries
and 255 KB read in all instead of `CARS101.DAT` whole (762 KB); from a deflated ZIP the four containers whole (2.0 MB inflated, the
slow path). Changing `race_grid` and loading a track again reads only the cars that changed. Sizes: a car (`.MLB` and
`.3DX` unpacked, padded) is 152 to 193 KB (day) or 164 to 204 KB (night), so the image grows or shrinks by what the cars cost and
no four cars of any team make it larger than 0.82 MB of cars (the five cars of team 1 were 0.87 MB):

| Track | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| image before (team 1's five cars), bytes | 4,695,040 | 5,236,224 | 5,224,448 | 5,415,936 | 4,863,744 | 5,542,144 | 3,694,592 |
| image now (the four teams' cars), bytes | 4,544,512 | 5,085,696 | 5,073,920 | 5,265,408 | 4,713,216 | 5,391,616 | 3,544,064 |
| worst grid (four of the dearest cars), at most | 4.65 MB | 5.19 MB | 5.17 MB | 5.37 MB | 4.81 MB | 5.49 MB | 3.64 MB |

All of them in the 7 MB window (7,340,032 bytes); the builder's scratch needs 0.25 to 0.57 MB (track 4), and sits above the cars'
blobs from `0x12E4_0000` (1.75 MB). The load takes less time than before: the first track load reads 0.20 s of cars at the
Pocket's 1.2 MB/s (before: 0.6 s for the whole `CARS101.DAT`) and unpacks 713 KB once, 0.23 s at 3 MB/s (before: 863 KB for every
build, 0.27 s), so the first load is about 0.4 s faster and every later one 0.27 s faster; only a deflated ZIP reads more
(1.2 MB more to inflate, once). Checks: `tools/s2/check_track_build_s2.sh` (the image of every track byte for byte against
`make_track_s2.py` for three grids: the default, one out of order with a bonus car and a car twice, and team 1's five cars),
`tools/s2/check_cars_s2.py` (every material and texture frame of the cars in the image equals the cars' own file, the mesh equals the
`.3DX` but for the faces' material words, the palette), `tools/s2/check_cars_s2.sh` (the loader on a stored and a deflated ZIP made of
the eight team containers, with the bytes it reads counted).

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

### Sound (the race)

The race has the game's music and effects (`firmware/s2sound.c`, rules in `firmware/s2snd.c`); the ride is silent. The
rules are read from the original's program (`STARTL.EXE`, addresses below; OpenScreamer's `docs/NOTES2.md` S2-5zf has
the sound manager) and tested natively (`tools/s2/check_sound.sh`).

| What | When | The rule (STARTL.EXE) |
|---|---|---|
| Music | with the race: the track is set up (before the lights), round and round, fades out over 3 s once the player has finished; stops when the race is left | the CD track is `CD_TRACKS` of `DATA/GAME.DAT`'s `MISC.DTR`, `9 8 6 7 4 3 5` for tracks 1 to 7 (`0x2ebc9..0x2ebdc`: `CD_TRACKS[track - 1]`, the repeat flag set); the CD image `GAME.GOG` and its cue sheet `GAME.DAT` are the optional pair (data slots 22 and 21), `firmware/music.c` streams it through the mixer's ring |
| Engines | all four cars, from the first frame (the grid's revs too) | per car a voice: sample = the car's model (`M_PORSCH`, `M_CELICA`, `M_ACUV1`, `M_MK5` for the four cars; `M_STOCK` .. for a team's fifth model), the same recording's tunnel version (`T_*`, sample + 0x19) on road kinds 7, 13, 34, 43 (`0x1fe3b..0x1ff20`); rate = the sample's own x (2 x revs / most revs)^2, plus that x (last distance - distance) / 50,000 x 5; volume = the sample's x ((50,000 - distance) / 50,000)^2; pan = 32768 - 32767 x cos((1 - across / 50,000) x 1.87): the listener is the picture's camera (`0x1ff20..0x1ffb3` fills the call's record, the routine is `0xaf338`; the constants were read in the program: 2.0, 5.0, 1.87, 32767, 32768) |
| Tyres | the player's car | squeal where the grip (record +0xc34) is under 0.93, volume (0.93 - grip) / 0.1 of 29,952, the sample `SKID` + the road kind's last sound number (`7 + n`); without one, the rolling sound of the kind (`0x12 + n`, `TIRES1..6`: grass and the like, none on asphalt) at 655 x the speed (50 at most); a voice that is not restarted while the sample stays (`0x1b36f`, the sounds at `0x1b7e3..0x1b8da`) |
| Wall | the player's car | `HIT1..3` (0xc..0xe) for a knock harder than 500, 4,000, 10,000 (`0x29571`, the sounds at `0x2a301..0x2a34b`; the physics' `hit_hard`) |
| Car against car | the player's car | `CRASH1..3` (0xf..0x11) harder than 0, 5,000, 10,000 (`0x245a3`, the thresholds at `0x27abd`; the firmware's cars touch with the closing speed of `race.c collide()`, which is not the game's knock to the unit) |
| Landing | the player's car | `GROUND1` (0x12) when all four wheels were in the air and any is down again (`0x1c45e`) |
| Start lights, gears | - | nothing: the original has no sound for them (every call to the sound manager in the race code is one of the above; the cones' and the scenery's sounds are not run) |

`DATA/SOUND.DAT` (1.4 MB) is a `.DAT` container of `.RAW` samples (8-bit unsigned mono, stored) and `.DTR` tables;
`GAME.DTR` lists the 33 samples of the race by number with their own rate (8,000 to 22,050 Hz), volume and loop, in a section
`SOUND_DATA` (`sNAME.RAW xID xLOOPLEN xLOOPCOUNT xVOLUME`, then `dRATE dBITS dCHANNELS dFORMAT`). It is read whole from the ZIP once,
when the viewer starts (about a second on a stored ZIP, longer on a deflated one), to `0x11C0_0000` (docs/soc.md), and the
mixer plays the samples straight from there. The mixer's step is the rate over 48,000 (2.16), at most 4 samples a value. `MENU.DTR` and the
`AMBIENT*.DTR` of the tracks (birds, mill, crowd, ...) belong to the menu and to the scenery's animations, which are not run.

Voices of the mixer: 0 to 3 the engines, 5 the player's tyres, 6 to 9 the knocks (round robin). Volumes: the sample's volume (`0x3fff`
for the engines, `0x5fff` for `M_ACUV1`, `0x7fff` for the rest) times the game's factor, then the effects' master `S2SND_FX_MASTER` `0x2000`
of `0x7fff` (Screamer 1's `sound.c` has two thirds of the game's, `0x4000`; with that the four engines and the tyres alone clipped
the sum in the first simulation, 686 samples in 8 s, so it is halved; a full sample at the full volume then moves the output by 63
per 8-bit step, the music stays at `0x4000`), pan with the far side faded linearly. The game starts a new voice for every frame of a contact and lets the old
ones play on; here a knock starts every 3 frames at most (a harder one at once). The engine of a car at rest has a floor of 200 Hz.

The cost: `s2snd_frame()` (the engines, tyres, knocks, the fade) and `music_frame()` (the ring) are timed with the cycle counter
and the mean and worst go into the log (`sound: 250 fr, effects 7211/7519, music 643/950 cycles mean/max`) five seconds into the
race and when it is left. In the simulation of the race on track 4 (330 frames, 4 cars, the music's ring filling): the effects
7,211 cycles on the mean and 7,519 at the worst, 0.10 ms; the ring 643 and 950 cycles, 0.01 ms. They sit before the scene in the frame, so the first mark of the
timeline (`ph` in the log) holds them; that run: 13.6 ms mean and 15.1 ms worst a frame, no frame over 20 ms, 50.0 a second, with the
computers' steps after the scene (`RACE_OVERLAP` 1). The ring was read as the song played: 51 reads of 32 KB or less for 330 frames,
by the log's `data slot read 22` lines, at the rate of the song. Track 1, 260 frames: effects 7,027 / 7,356 cycles, ring 638 / 956,
12.6 ms mean and 13.6 ms worst a frame, none late. Stack of the program 4.5 KB. (The runs: the real ZIP, a made-up CD image of
tones and the commands of "Simulation" above with `SIM_WAV=build/sim/race1.wav`; 330 and 260 frames take 13 and 10 minutes.)

What the output showed (2026-11-04, `SIM_WAV`): the music is the track's song: track 4 reads CD track 7 (its first read at the
sector of track 7) and track 1 CD track 9, whose two test tones (1,800 and 2,700 Hz, amplitude 12,000) came out at 5,950 of the 6,000 the
music volume gives. The engines were there from the first frame of the race and louder with the cars let go. At the effects' master `0x4000` (first run, the
music silent in it, a track without a tone) 686 samples clipped in 8 s; at `0x2000` (second run, with the tones) the peaks were 21,800
and 27,200, nothing clipped, the engines' rms 3.5k left and 5.2k right (the game's pan leans right: a source straight ahead is
at 42,400 of 65,535) against the test tone's 4.2k.

The music and the track load: the load takes the data slot for 3 to 5 s (the ZIP's reads), the ring is read from a data slot too,
and the ring holds 1.5 s: so the music stops when the race is left (`s2snd_stop()` in `leave()`, which waits for a running read of
the ring) and starts again with the next race, which costs the 170 ms of its first fill while the loading screen is up. A
restart of the race on the same track (Select + Start) keeps its song. A frame as slow as 1 s would still not run the ring dry.

What is open: the original's volume balance is not known (the CD drive's analogue mix is not in the program; the two thirds are
Screamer 1's choice), the pan law of the effects is HMI's as Screamer 1's `sound.c` has it, the car against car knock is the
firmware's closing speed, the cones' knock and the scenery's sounds (`ANIMATIONS` rows with a sound, `AMBIENT*.DTR`) are not
done, no menu sounds in the start menu (the clicks of `MENU.DTR` exist, `s2sound_menu()`, the pause menu uses them), the pause eases the effects out (above).

## PC renderer

`tools/s2/render_s2.sh <image> <out prefix> v<s>[,<cars>] ...` draws the viewer's scene (`road.c`, `track.c`,
`car.c` and the C models of the units) natively in Docker: `v0.3f,4` is 30 % round the road with four cars;
`NOLOD=1` draws every block at level 0 (to compare), `d<k>` the lone car after k steps of a script of its own, `g<k>[,<n>]` the race for k
steps (the firmware's own `race.c`; the player parked, `RACE_GAS=1` holding the gas, `RACE_ROBOT=1` driven by the game's driver, `RACE_CAM=<car>`
the picture from behind that car) with a line per car every n steps and per lap done; every run also checks that the car models and the cars'
poses hold together (a block that is not a wheel must not turn with the wheels: the exit code is 3 if one does) and the cars' look (brake lights,
wheels' blur, damage marks as drawn, `docs/screamer2/driving.md`; `LOOK_DUMP=/b/s2host/look_` keeps its pictures), `r<i>` and `c<x>,<y>,<h>,<yaw>,<pitch>` are other
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
| `tools/menu_screens.sh`, `tools/host/menu_screen.c`, `tools/host/ui_screen.c`, `tools/ui_reference.py`, `tools/ui_arcade_ref.py` | the 2D toolkit (`s2gfx.c`, `s2page.c`, `s2load.c`, `s2pause.c`, the arcade pages `s2arcade.c`) on the PC with the blitter's C model and scripted buttons, against references made from the game's files by `ui_reference.py` (its own LZW, `.SPR` / `.FNT` and text readers): the 28 `L` pictures of `GRAF1.DAT` (CRC-32 of pixels and palette, on the stored ZIP and on a deflated one made on the fly), 20 text and sprite cases pixel for pixel, the loading screens of the seven tracks, the pause menu (keys, clicks, the easing of the effects, the darkened still pixel for pixel, the darkening table against a double-precision search) on three of the repository's race pictures quantized to their track's palette, the page runner's key repeat and fades, the 15 samples of `MENU.DTR`; pictures in `build/check/menu/` (`menu_load*.png`, `menu_pause*.png`). The arcade pages: 17 scripted runs of the four pages (button scripts of one character a frame, the page's result, the choices and sounds it leaves, the frames it takes) with 105 frames compared pixel for pixel and palette entry for palette entry with `ui_arcade_ref.py`'s model (picture, sprites, text, the eased scroll in float32, the blinking, the fade; made from the game's files and a reading of `STARTL.EXE`), with 3, 5 and 7 tracks open and a bonus car or not, the whole chain forward and back (sounds, kept choices) and every track reachable from every other by scripted presses; pictures of the frames marked in `ui_arcade_ref.py` (`menu_<case>_<tick>.png`). The pages of row 3 (`s2pages.c`, `s2end.c`): the references `tools/ui_reference.py` makes (the game's fonts and sprites drawn by its own routines: four states of GAME SETTINGS, nine of HI SCORES, the end screens of three runs frame by frame, 21 best-times rows for the lists) against the firmware's frames pixel for pixel (the darkening applied with the firmware's own table, which is checked against a search in double precision for every palette used), scripted buttons for the keys, clicks, scrolling, the settings in the save, the best-time flow (rank, ties, a slower time than the fifteenth, the lap list) and the start menu's two rows; the seven `GAMEL.SPR` banners from a stored and a deflated ZIP. And the start menu `firmware/s2menu.c` with the real ZIP: tests (every track within two moves, the grid for each team and car, the kept choices, the 7 texts read from `DATA/LOCAL.DAT`) and pictures of the list, `build/check/menu/*.png` |
| `tools/s2/check_save_s2.sh`, `tools/s2/check_save_s2.c` | the save `firmware/s2save.c` natively in Docker under the sanitizers, with a fake data slot and the SDRAM window of `CHOICE.DAT` at its address: the defaults and the layout's bytes (against the table in docs/screamer2/menus.md 7.1), the interface and its clamping, the unlocks, the best-times ranking against a plain model (ties, the 15 rows, kinds / tracks / lists apart, time attack's one total list, 20,000 random insertions), a round trip through a "power-off", the migration of the old layout (with and without its marker, values out of range), and broken files: empty, short, foreign, random, oversized, a failing read, every byte of a valid file damaged in turn (the damaged part falls back to the defaults and the other keeps its data), damage with a matching checksum but values out of range, random damage; after every load the save is sealed and in range and nothing outside the layout was touched |
| `tools/s2/dosbox_arcade.py [extra tracks] [--check]` | the original's arcade pages in the game's own DOSBox (Windows, SendInput; the owner's game folder is not written to: a private copy `build/dosgame` of junctions and its own `CONFIG` with a `CHOICE.DAT` that has the extra tracks open): types its way through TEAM SELECTION, the name and the OPTIONS ROOM to ARCADE, takes Ctrl+F5 screenshots (raw 320 x 200 palette indices, `build/check/dosbox/`) on the title, the TRACK page (a key press, then screenshots at once: the easing in flight), the CAR page and the TRANSMISSION page, and compares each with the frames of `ui_arcade_ref.py`'s model (the TRACK page whole; for the others the parts the model draws, the original also has 3D cars there). The window must stay in front for the 3 minutes; `--check` repeats the comparison on the last run's screenshots |
| `tools/files_screens.sh`, `tools/files_screens.py`, `tools/host/files_screen.c` | the files screen of `firmware/files.c` on the PC for the real ZIP and made-up ones (missing file, deflated, unsupported method, other top folder, not a ZIP, not there): `build/check/screens/*.png` |
| `tools/s2/make_track_s2.py`, `tools/s2/check_track_build_s2.sh`, `tools/s2/check_track_build_s2.c`, `tools/s2/check_env_s2.py`, `tools/s2/check_cars_s2.py` | the Screamer 2 track image on the PC (the reference; `S2CARS=team:car,...` chooses the grid's cars) and the firmware's builder `track_build_s2()` compared with it byte for byte, seven tracks and three grids, natively under the sanitizers; the windows' reflection picture and the cars' materials, textures and colours in the image against the original's |
| `tools/s2/check_cars_s2.sh`, `tools/s2/check_cars_s2.c` | the cars' loader `firmware/s2teams.c` natively under the sanitizers on a stored and a deflated ZIP of the team containers: the cars equal the containers' own entries, the bytes read are counted, a repeated or partly changed grid reads nothing or only the changed cars |
| `tools/s2/check_params.sh`, `tools/s2/check_params.py`, `tools/s2/check_params.c` | the car's numbers: `firmware/s2params.c` (CAR_PARAMS of `DATA/GAME.DAT`, TRACK_DATA with the road kinds of `DATA/MAPn.DAT`) natively against an independent reading in Python, every field of 20 cars and 7 tracks |
| `tools/s2/check_sound.sh`, `tools/s2/check_sound.py`, `tools/s2/check_sound.c` | the race's sound rules `firmware/s2snd.c` natively in Docker: `DATA/SOUND.DAT`'s `GAME.DTR` (the 33 samples: where their bytes are in the container, length, rate, volume, loop, CRC-32 of the bytes) and `CD_TRACKS` read by the firmware's code and by an independent reading in Python, line by line; an engine's rate, volume and pan over a sweep of revs, distance, last distance and side (223,560 cases), the tyres' rule (13,650 cases) and the knocks' thresholds against the game's routine as OpenScreamer's notes have it in double precision (the constants checked in `STARTL.EXE`) |
| `tools/s2/check_car.sh`, `tools/s2/check_car.c`, `tools/s2/car_scenarios/` | the car physics `firmware/s2car.c` natively in float32 on the track images (built with the five cars of team 1, `S2CARS=1:1,1:2,1:3,1:4,1:5`: the wheels of every car number as the traces had them) against the reference traces of OpenScreamer's routines (the folder `ref/` next to the repository, or `$REF_DIR`; scenarios in its `scn/`, traces in `traces*/`), the place cut as the game does and kept as a float; per quantity the largest and the mean difference; also the car alone from each track's grid place and the trigonometry and ground looks a step costs. `car_scenarios/` holds the scenarios heading towards the origin (`a_origin` ..., traces made with `tools/s2/ref_trace/ref_run.sh` into `ref/traces_benign/`) |
| `tools/s2/check_vis_s2.sh`, `tools/s2/check_vis_s2.c` | the track's drawing key and walks natively in Docker (`docs/screamer2/draw_distance.md`): the key `-(sqrt(d^2 >> 3) >> 2)` against a double-precision reading, every mesh's class key (header entry 43) against the file bytes, and from cameras round the road of each track no piece within 6,000 units that the whole walk takes may be left out by the camera cell's list |
| `tools/s2/report_carsort_s2.sh`, `tools/s2/report_carsort_s2.c` | a report (not a check) on the painter's order of a computer car against the static pieces: the race robot's cameras with a computer car taken from later steps, a per-pixel depth map of every polygon (patched temporary copies of `track.c` and `geom.c`) and the count of poses where the car is drawn through a nearer piece (`docs/screamer2/draw_distance.md`, "A car behind a wall") |
| `tools/s2/render_s2.sh`, `tools/s2/render_s2.c` | the viewer's scene, the lone car and the race on the PC from the firmware's own code (`NOLOD=1`: no level of detail; `g<k>`: the race, laps and places of the four cars as a table) |
| `tools/s2/map_shift.py <old.map> <new.map>` | which objects of the program moved between two builds (`build/firmware/program.map`): the I-cache is 8 KB and direct mapped, so code added in front of the frame's code shifts what follows (`docs/screamer2/performance.md` section 7); judge the shift by the race in the simulation (`tools/s2/sim_race.sh`) |
| `tools/s2/framestats.sh`, `tools/s2/perf_patch.py`, `tools/s2/perf_model.py`, `tools/s2/lod_report.py` | the study's counting build (counters in copies of the firmware) and cost model, `docs/screamer2/performance.md` |
| `tools/s2/framestats.sh race`, `tools/s2/racestats.c`, `tools/s2/perf_race.py`, `tools/s2/check_cull_s2.sh <commit>`, `tools/s2/sim_race.sh` | the race's frame time (`docs/screamer2/performance.md` section 7): a race on the PC with the game's driver at all four cars and the counters per frame (and per piece), the frame times along a lap from them, a check that the cull of this tree hands on the same pieces in the same order and draws the same pictures as the tree of another commit (7 tracks), and the viewer's race in the simulation with its frame timeline; `tools/fw.sh PROF=1` builds the cycle counters of `firmware/prof.h` for it, `CODE_PAD=n` / `CODE_PAD2=n` move the code to see what the caches' alignment costs |
| `tools/fmcheck.sh [song ...]`, `tools/fmsongs.py`, `tools/host/fmdump.c`, `tools/host/fmref.c` | the FM music on the PC: `fmsongs.py` reads the songs of `DATA/MUSIC.DAT` independently (tracks, messages, lengths); `fmdump.c` runs `firmware/fm.c` against a model of the hardware's write queue; `fmref.c` is openscreamer's driver with the chip replaced by a log; `fmcheck.sh` runs all of them (Docker) and compares the register writes tick by tick |
| `tools/dis.sh [-h] [-f] <from> <to>` | disassembles part of `STARTL.EXE` (`-h`: `STARTH.EXE`) at the addresses of OpenScreamer's notes; `tools/unle.py` (from OpenScreamer) unpacks the program into `build/exe/` on first use |
| `tools/dosram.py` | reads the memory of the original game running in DOSBox, to compare with the firmware |
| `tools/release_zip.py` | after `tools/package.sh`: the release archive `build/release/Chainy.Screamer2_<version>.zip` (version from `core/core.json`), refusing to include game data |
| `tools/pocketlog.py [debug.txt]` | reads the Pocket's debug log (default: the SD card at I:) as a short table |
| `tools/platform_image.py` | makes the platform image (`.bin`) from a PNG and back |
| `tools/gen_pll_sdram.sh` | regenerates the SDRAM clock PLL (Intel `ip-generate` in the Quartus image) |
| `tools/sta_sdram.tcl`, `tools/check_budget.py` | after every build: the four SDRAM I/O paths, resource use and I/O registers (called by `tools/build.sh`) |
| `tools/make_budget_svg.py`, `tools/make_timeline_svg.py` | draw `docs/resources.svg` and `docs/frame_timeline.svg`; the timeline from a race simulation (`tools/s2/sim_race.sh <dir> build/s2/track4.bin 4 220 <name>`, about 9 minutes), then `python tools/make_timeline_svg.py build/sim/<name>.log docs/frame_timeline.svg "<scene text>"`; refresh it after every change that moves the frame time |
| `tools/vexriscv/` | the CPU's configuration and generator, see [tools/vexriscv/README.md](../tools/vexriscv/README.md) |
