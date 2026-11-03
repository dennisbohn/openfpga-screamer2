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
with `zip_read` (`GFX/MAPnH01.DAT`, `GFX/CARS101.DAT`, `DATA/MAPn.DAT`), unpacks their entries with
`firmware/dat.c` and builds each track's image at load time (`track_build_s2()` in
`firmware/track_build.c`): the placements, the palette, the drivable parts per grid cell, the road points
and walls, the background and TRACK_DATA's far plane, and the cars AUTO1..5 of team 1 with their material
libraries merged after the track's (the face's material is a signed 16-bit offset from the MLB's start, so the
cars' material records are collected in a section in front of the MLB and the cars' faces point there; the
texture records and pixels stay in the cars' own `.MLB` files, appended after the track's). The image is
3.5 to 5.4 MB of the 7 MB window at `0x1210_0000`. There is no camera path, no markers, no race screen and
no opponents yet. Nothing is converted on the PC.

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
far plane from TRACK_DATA, 50,000, track 4 60,000; pieces out to far + 8,192). No menu, physics, race
screen, sound or music.

| Button | |
|---|---|
| d-pad left / right | previous / next track (1 to 7), the track is loaded again (about 5 to 10 s) |
| A | the cars: 4 (the realistic field), 0, 1 (the player's) in turn |
| B | timing overlay on / off (default on) |

The overlay (top left, drawn after the 3D picture) shows the mean and the worst time of a frame over
the last second in ms, the track, the cars and the faces of the frame. A frame's time runs from its
start (the swap before it is done) to the end of the rasterizer's work, not including the wait for the next
vertical blanking, so 10 ms still shows 50 pictures a second; the camera advances by the real time
(video frames of 1/50 s), so a frame over 20 ms makes the picture slower but not the movement.
`debug.txt`: its first line is the live statistics of the current visit (`Screamer Pocket T1 c4 fr1234
avg10.4 max12.8 late0 fps49.9`: track, cars, frames, mean and worst ms, frames over 20 ms, pictures a
second), a line `viewer: track 1, cars 4, frames ..., ms mean ... max ... at <units>k, over 20 ms ...,
<pictures>/s, faces ..., pieces ...` 5 s after a track is shown, then every minute, and one when it is
left; the frame timeline (`tl_*`: cycles and the units' busy clocks at each stage) once. The log has 4 KB
and is not a ring: it ends when it is full.

Known gaps (accepted): the cars' windows are white (no environment map: routine 160 reads `ENV.MLB`),
no shadows, smoke, props or animated scenery, no fade by depth, no night or fog, no mirror, the S1
see-through table (64 rows) where Screamer 2 has 112, the sky and ground colour of `TRACK_DATA` only
where the backdrop picture ends.

What to expect: `docs/screamer2/performance.md`, section 6, forecasts for the four-car case with the level of
detail, mean / worst ms of the game's frame including its physics (about 1.7 ms) and the race screen's
tail: track 1 10.1 / 12.2, 2 10.6 / 13.0, 3 10.5 / 13.6, 4 12.7 / 15.8, 5 11.1 / 15.6, 6 10.1 / 13.3, 7 12.0 /
17.4. The viewer has neither, so expect about 1 to 2 ms less; a mean within ±10 % of that and no frame
over 20 ms on any track confirms the forecast.

Simulation of the viewer (8 frames at the start of track 1, about 6 minutes of Verilator once the model is built): the
firmware takes the finished image from the testbench instead of building it (the build is checked
natively, above), `tools/s2/make_track_s2.py` makes it:

```sh
python -I tools/s2/make_track_s2.py "C:/GOG Games/Screamer 2" 1 build/s2/track1.bin
# build/gamedata/ needs SCREAMER2.zip, GAME.GOG and GAME.DAT (see tools/simcheck.sh)
SIM_QUICK=1 SIM_GAME=build/gamedata SIM_IMAGE=build/s2/track1.bin SIM_TRACK=1 SIM_RACE_FRAMES=8 \
    tools/sim.sh soc build/firmware/firmware.bin build/sim/viewer 1500 > build/sim/viewer.log
python -I tools/rgb2png.py build/sim/viewer.rgb build/sim/viewer.png
```

The run of 2026-11-04: no trap, 6.1 ms mean and 6.3 ms worst for the 8 frames (the cost model gives 6.0 ms for
them without the game's own physics), the picture equal to the PC render of the same camera
(`tools/s2/render_s2.sh build/s2/track1.bin <prefix> v1482.4,4`) in every pixel outside the overlay's box.

## PC renderer

`tools/s2/render_s2.sh <image> <out prefix> v<s>[,<cars>] ...` draws the viewer's scene (`road.c`, `track.c`,
`car.c` and the C models of the units) natively in Docker: `v0.3f,4` is 30 % round the road with four cars;
`NOLOD=1` draws every block at level 0 (to compare), `r<i>` and `c<x>,<y>,<h>,<yaw>,<pitch>` are other
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
testbench only checks the log (`viewer: track ...`), the picture is `build/sim/<name>.rgb`. The audio of the
run (I2S output, 48 kHz stereo) goes to `build/sim/audio.wav`.

## Tools

| Tool | What it does |
|---|---|
| `tools/dat_sizes.py <file.DAT>...` | lists a game container with unpacked sizes (`-v`: every file) |
| `tools/datfile.py` | reads the containers (shared by the other scripts) |
| `tools/check_zip.sh`, `tools/check_zip.py`, `tools/check_zip.c` | the ZIP reader `firmware/zip.c` natively in Docker: the real `SCREAMER2.zip` (every file's size and CRC-32 against Python's `zipfile`), archives made on the fly (deflated at levels 1, 6, 9, wrong CRC, damaged, cut, other methods, wrong top folder, too many files), a fuzz of damaged copies under the address sanitizer, and the speed of the inflate on the PC |
| `tools/files_screens.sh`, `tools/files_screens.py`, `tools/host/files_screen.c` | the files screen of `firmware/files.c` on the PC for the real ZIP and made-up ones (missing file, deflated, unsupported method, other top folder, not a ZIP, not there): `build/check/screens/*.png` |
| `tools/s2/make_track_s2.py`, `tools/s2/check_track_build_s2.sh`, `tools/s2/check_track_build_s2.c` | the Screamer 2 track image on the PC (the reference) and the firmware's builder `track_build_s2()` compared with it byte for byte, seven tracks, natively under the sanitizers |
| `tools/s2/render_s2.sh`, `tools/s2/render_s2.c` | the viewer's scene on the PC from the firmware's own code (`NOLOD=1`: no level of detail) |
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
