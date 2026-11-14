# Architecture

Overview of the core's hardware: the partition, the memory layout, how the original's drawing is
reproduced, and the measured resource budget. The CPU and bus are described in `docs/soc.md`, the
memory system in `docs/memory.md`, the fill rule in `docs/original_filler.md`, and the geometry
formats in `docs/original_geometry.md`. The game's side of Screamer 2 (the files, the menus, the
race, the sound's rules, the tools) is in `docs/development.md` and `docs/screamer2/`.

The hardware began as that of the sister core, [Screamer for the Analogue Pocket](https://github.com/dennisbohn/openfpga-screamer),
and is developed separately since (this core reads a second controller and has no FM chip). This document grew
with the sister core's: where a passage does not name Screamer 2, its code addresses
(in backticks, `0x83a68`, ...) and measurements may be those of Screamer 1's `STARTL.EXE` and
tracks. How Screamer 1's game runs on the units (its car, opponents, menus, championship,
commentary) is in that project's `docs/architecture.md`. "OpenScreamer `docs/NOTES.md`" is the
notes of the OpenScreamer port.

## Pocket hardware (main FPGA)

Cyclone V 5CEBA4F23C8: 18,480 ALMs, 308 M10K (10 Kbit each), 66 DSP blocks, 4 PLLs.
Separate memory buses: SDRAM 64 MB, PSRAM `cram0` and `cram1` 16 MB each, SRAM 256 KB (all
16 bits wide). Only the SDRAM is used; the SRAM and PSRAM controllers were removed (only the
memory test needed them; hints for re-adding them are in `docs/memory.md`).

## Partition

![Architecture: the CPU selects and sorts the parts and writes them into the geometry unit's buffer; geometry unit, clipper, setup unit and rasterizer draw the frame into the back framebuffer in block RAM while video_out shows the front one](architecture.svg)

Where the time of a frame goes, CPU sections and unit utilisation over 20 ms (drawn from the
log by `tools/make_timeline_svg.py`, also from the Pocket's `debug.txt`):
[frame_timeline.svg](frame_timeline.svg).

The CPU selects the parts in range, sorts them back to front and writes them into the geometry
unit's buffer. Geometry unit, clipper, setup unit and rasterizer work on them concurrently; the
rasterizer reads texels from the SDRAM through its texture cache and draws into the back
framebuffer while `video_out` shows the front one. The buffers are swapped in the vertical
blanking.

| Memory | Content |
|---|---|
| SDRAM | Program, geometry, game state, textures of the current track (`MAPn.MLB`, up to 3 MB, through the 16 KB texture cache) |
| SDRAM | Sound effects (`DATA/SOUND.DAT`, unpacked; the mixer reads the samples directly) |
| SDRAM | Music: the ring of the CD stream (256 KB) |
| Block RAM | Two framebuffers (2 x 64 KB) and two palettes, boot RAM, CPU caches, geometry unit buffer, texture and mesh caches, transparency table (`MAPn.SMG`), the mixer's lines (32 x 32 bytes) |
| SRAM, cram0, cram1 | not used |

One texture cache line costs about 30 clocks from the SDRAM and about 130 from the (asynchronous)
PSRAM; hence everything is in the SDRAM, including the sounds, which the mixer reads slowly and
evenly. For each voice the mixer keeps a 32-byte line around its position in block RAM and
requests the next one as soon as the position moves into it; a whole output sample (1547 clocks)
passes before it is read, which absorbs any wait.

## Game data on the SD card

The original files, unchanged (`core/data.json`): `SCREAMER2.zip` in one data slot, the game's
`DATA`, `GFX` and `ANIMS` folders in one ZIP (the APF allows 32 slots, and the game has about 140
files); the music image `GAME.GOG` and its cue sheet `GAME.DAT` in two more; and two nonvolatile
slots for the saves, `CHOICE.DAT` and `ATTACK.DAT`, in the Pocket's save folder. All of them are
`deferload`: the firmware reads the ZIP's directory and fetches the entries it needs
(`firmware/zip.c`), unpacks the game's containers (`dat.c`, LZW) and builds each track's image
when it is loaded (`track_build.c`). `docs/development.md`, "Game data", has the details;
`docs/soc.md` the addresses.

## Design decisions

- **Language:** own code in Verilog (like the template). VexRiscv is generated once from a Scala
  configuration as Verilog; custom CPU instructions go through its CFU plugin.
- **50 Hz, one game step per frame:** the original has no fixed rate; its physics runs with the
  measured frame time, limited to 0.02-0.2 s, so it never runs faster than 50 steps per second
  (OpenScreamer `docs/NOTES.md`, driving model). Video therefore runs at exactly 50 Hz
  (480 x 512 clocks at 12.288 MHz; the Pocket's screen takes 47-61 Hz) and each frame is one
  20 ms physics step: the game at its best rate, without intermediate frames or judder. The clock
  for countdowns and lap times ticks at 100 Hz in the original and has its own counter register.
  One frame is 1.485 million clocks.
- **VGA version:** 320x200 with an 8-bit palette like the game's VGA version. The original takes
  its tracks from both sets: `MAPnL` at DETAIL LOW, `MAPnH` from MID (`STARTL.EXE` like
  `STARTH.EXE`, OpenScreamer `docs/NOTES.md` section 27). How far is drawn is set by DETAIL as in the original:
  parts up to 52000 units, faces clipped at depth 27808 (LOW, MID) or 43808 (HI). The SVGA
  version draws just as far (same table at `ds:0x21d8` in `STARTH.EXE`, same comparison with
  52000^2/4 at `0x8340b`); it only doubles the pixels (focal length 128, result x 2, centre
  column 319). The core always uses the full H set and the HI distances; DETAIL is only displayed. Cockpit, race info and other static pictures are sprites from the L entries of
  `GAME.DAT` (`COCKnnAL`, `COCKnnBL`, `CDFRONTL`, `GAMEL`, `IMPLET_L`, `IMPRES_L`), copied 1:1
  pixel-exact into the framebuffer by the blitter. In `GAME.DAT`, L and H mean the 320 x 200 and
  the SVGA pictures, not the DETAIL sets of the `MAPn` files.
- **Two framebuffers in block RAM** instead of PSRAM: the rasterizer writes the back one at one
  pixel per clock, overdraw costs nothing, and no PSRAM controller or framebuffer arbitration is
  needed (saves logic, the scarce resource). Each buffer has its own palette, so palette changes
  (fades) match the frame exactly. The drawing side also reads (smoke and shadows blend with the
  frame content, routine 224): four explicit 8-bit memory lanes with a read/write port
  (`fb_lane.v`), because Quartus 18.1 otherwise silently builds memory with true byte enables
  from registers; `tools/check_budget.py` checks the 128 M10K on every build. The swap happens at
  the start of a line in the vertical blanking, at most once per blanking (`video_out.v`): a
  frame that takes slightly more than 20 ms does not immediately drop to 25 fps.
- **Exact per-pixel perspective correction:** a reciprocal unit (table plus one Newton step,
  2-3 DSP) delivers one reciprocal per clock, so it costs no time. OpenScreamer's software renderer
  computes only every eighth pixel exactly, hence its kinks in near textures. The fixed-point bit
  widths were determined on the C model against floating point.
- **Number formats:** rasterizer and geometry in fixed point (screen coordinates with at least
  4 fractional bits); the physics on the CPU in `float` like the original.

### Visibility without a depth buffer (painter's algorithm)

The CPU sorts whole objects by the distance of their centre from the camera and emits them back
to front (original `0x83164`: `dist^2 >> 2`). Within an object the order is in the data: each
mesh block has sixteen precomputed face lists (header +0x28..+0x64), each sorted back to front for
one viewing direction: a 45-degree sector around the block's y axis each, for an eye above
(lists 0..7) and below (8..15) its x-z plane. The CPU picks the list like the game (`0x8a598`);
the geometry unit does not sort and drops back faces by the normal. As in the game, the list is
chosen by the camera in the block's frame plus the block's centre (`0x83511` scales this by 1.2,
which does not change the list).

The game tests back faces with the same point (`0x835a0`), but its filler (`0xd55ff`) draws
nothing of a face whose corners run the other way on screen, i.e. one facing away from the lens
eye (128 behind the camera). The geometry unit's back-face test therefore uses this eye (with the
point alone, the nose of the first car in the car selection looked through the front tyre). The
track data rely on this order (grass faces below the road, two objects at one spot); OpenScreamer
needs special rules for it with its depth buffer. Clipping at the near plane remains necessary.
A depth buffer would cost about 125 M10K (320x200x16 bit fits only the 8-Kbit modes), and every
hidden pixel would be rewritten, which makes framebuffer bandwidth more important.

**Object order as in the original** (read from `STARTL.EXE`): `0x83164` builds the list from the
grid cells, per cell run 1 before run 0, with `dist^2 >> 2`; `0x8a6a8` appends the moving objects;
`0x8a7b0` turns this into the key -(sqrt(dist^2 >> 2) rounded >> 2), roughly distance/8; `0x832c0`
sorts; the farthest is drawn first. A moving object (car) counts **800 closer**, 6400 units;
otherwise the long road piece it stands on overpaints the car, on a lap with the near chase
camera at every fourth spot. A mesh with a class in the last word of its object header (+0x32c)
counts farther away: class 3 by 6000 (on track 1 only `akk1tap`, the grass under the road),
1 by 1500, 2 by 800, 4 by 200. OpenScreamer draws with a depth buffer and has its own rules for
this (grass "twice as far", objects above their road); the core draws like the original and needs
only its key.

### Geometry chain

Measured on the CPU (fly-through of track 1): about 3000 clocks per triangle, almost half of it
per-triangle preparation (the planes of Q, U, V with divisions), a quarter clipping and
projection, vertex transformation only 3 %. Hence the units, in this order:

- **Setup unit** (`setup.v`, C model `firmware/setup.c`): the CPU hands over polygons in camera
  view (five floats per corner); the hardware projects, splits into a fan and computes the
  rasterizer's values in fixed point while the rasterizer draws the previous triangle.
- **Clipper** (`clipper.v`, in front of the setup unit): with the setup unit alone the CPU time in
  the heaviest frame fell from 10.4 to 5.8 million clocks, 58 % of it clipping. Clipping is as in
  the original at the near plane (depth 128) and the far plane of the DETAIL level, plus a guard
  band at 14 x depth only so that screen coordinates fit 16 bits (the rasterizer clips at the
  screen edge). The lens is the original's: focal length 128, eye height at 65 % of the frame
  height, and the eye 128 units behind the camera (the division at `0x83420` reads
  `TABS\PERSP01.TAB`; screen point = centre + 128 * x / (128 + z), z from the camera).
- **Geometry unit** (`geom.v`, C model `firmware/geom.c`): the CPU selects and sorts the parts and
  writes 19 words per part (matrix, origin, eye, mesh, face list; layout in `docs/soc.md`) into
  the unit's buffer (2048 words, 107 parts, block RAM). The unit reads the mesh itself from the
  SDRAM, transforms the vertices, drops back faces, looks up the materials and hands the faces to
  the clipper. Because of the buffer the CPU does not wait for each part. It needs about 20 % of
  a frame; the rest is left for cars and game logic.
- **Texture prefetch:** counters in the rasterizer showed 76 % of its time waiting for texture
  lines, and a cache study with the C model's texel addresses showed half as many misses as
  counted: both caches fetched each missing line twice (fixed). Tiling the texture memory brings
  little (most misses are first accesses, about 7,800 lines per frame). So the rasterizer
  prefetches (after Igehy, Eldridge, Proudfoot 1998): tags are checked ahead of a queue of 64
  pixels, a missing line is requested at once (up to 16 in flight, into a line buffer), only the
  pixel that needs it waits at the queue head, and the line enters the cache once all pixels
  before it have read the old line. The next textured triangle starts as soon as the previous
  one's pixels are in the queue. In the heaviest frame the rasterizer is busy 0.49 instead of
  0.65 million clocks.
- **Cadence:** the geometry unit sets the pace: it is busy from the first part to the end of the
  frame (including waits for clipper and memory); frame time is roughly the CPU's selection plus
  the geometry unit.

**Faster chain at the start** (all ten cars in view: 248 parts, 2,473 faces; SoC simulation with
`SIM_DEFINES="-DGEOM_STATS"`): a frame took about 1.6 million clocks (25 fps) before the
following changes, with the geometry unit busy 1.15 million clocks (37 % waiting for reads that
cost 5 clocks even as cache hits, 23 % for SDRAM misses), the rasterizer waiting 289,000 clocks
for texture lines and the CPU 414,000 for the geometry unit.

1. A geometry-unit read puts the address straight to the cache RAMs and the next step takes the
   value (2 clocks on a hit; on a miss only that step waits); two more multipliers compute the
   three rows of a vertex at once (6 instead of 12 clocks).
2. The line port has up to two lines in the SDRAM controller; the texture loader requests two
   (`docs/memory.md`).
3. The firmware builds the cars only when they are due in the draw sequence (the near ones last,
   while the geometry unit works on the far scenery) and skips groups of 16 parts whose outline
   is out of range during culling.

Result: 996,000 clocks (67 %), geometry unit 812,000, rasterizer waits 193,000 clocks for
textures, CPU 28,000 for the geometry unit. Bigger caches bring little: a study with the start
frame's accesses (C model) gave for geometry 8,738 misses with 4 KB, 6,368 with 16 KB; for
textures 11,200 with 16 KB, 10,261 with 32 KB, and more (13,895) with 8x4 tiled textures. Most are
first accesses to about 360 KB of texture and 280 KB of mesh data per frame.

### Corners on whole pixels, fill rule

The original's projection (`0x83420` ff.) keeps x as 16.16 and uses only the upper half word, and
all fill routines (`0xd5350` textures, `0xd8844` one colour) read the corners as whole pixels from
the table at `0x54e94`. Corners of adjacent parts that are a few units apart in the world (the
stored angles are up to 2.5 units short) mostly land on the same pixel. The setup unit and the
rasterizer therefore truncate corners to whole pixels instead of rounding to 1/16 pixel.

The fillers run column by column from the smallest to the largest corner x, both columns
included (`0xda330`: each edge pushes N + 1 pairs, the pair at the vertex is overwritten), and
fill each column from the clipped top to the clipped bottom edge, both drawn (`0xda478`:
`sar 16`, then count + 1 pixels). A pixel (x, r) is thus drawn if the polygon touches the segment
from (x, r) to (x, r + 1), the lower end excluded. This makes every polygon about half a pixel
fuller all round than the "pixel centre inside the triangle" rule; neighbours overlap at the
seam, also where a corner lies on the other's edge (T junction), and asphalt stays closed. With
the centre rule and corners on whole pixels, 61 hole pixels remained at the four steepest slants
of track 1, with the original's rule 9, which are columns between parts that do not touch. The
rasterizer stays line by line (the framebuffer holds four pixels of a line in one word) and
computes the same set: per line both edges at the top and bottom edge. See
`docs/original_filler.md`.

### Handoff between frames (rules for CPU and graphics)

- The swap is requested only when the last pixel of the frame is written: an end marker runs
  through the whole graphics pipeline, only then does `swap_req_toggle` flip.
- Palette changes are in frame N's command list and are written into the back buffer's palette
  when frame N begins. The CPU never writes the palette directly, because it is already a frame
  ahead.
- The drawer knows its back buffer from its own toggle (back = not `req`) and waits only for the
  acknowledgement; it does not read `front` across the clock domain.
- A reset must not fake a swap: it sets request and acknowledgement to 0, after which buffer 0 is
  in front until the drawer swaps (checked in `sim/display` and `sim/soc`).

## The blitter, moving parts, the see-through and reflection routines

### The blitter and the background

A blitter (`blit.v`) copies pixel by pixel 1:1 into the framebuffer, without geometry, setup and
rasterizer: commands with x, y, w, h (copy, text) or as a line, in a queue behind the frame's
parts, executed as soon as the 3D units are done. The race screen and the pages' pictures, text
and sprites go through it. A picture is cut into boxes around its pixels (`tb_hud.c`), so that a
whole race screen costs little of a frame; a separate buffer for it (64 KB per buffer) or a mask
for the rasterizer would gain too little.

**Background** (`MAPn.BCK`, a 512x256 PCX, drawn before the scene with `0x83a68`): a pure copy,
the column `(2047 - heading) / 2 mod 512` at the left edge (twice around the image per turn), the
top line at eye height + 22 - 256 + 128 * sin(pitch), 320 bytes per line at a 512-byte line pitch
(a line that reaches past the image end reads into the next); below, up to the view's end, the
colour from TRACK_DATA (`color`, times four at `0x1337d`). The tilted version `0x83b44` (four
textured quads, rotated by the roll angle) is called nowhere in `STARTL.EXE`: when the camera rolls
in a banked curve, the background stays upright, as here. Eye height is 130
(`0x4c610(320, 200, 130)`), the projection centre in x is column 159 (`0x83431`), not 160. The
blitter copies the image (opaque, one pixel per clock) and fills the rest (four pixels per clock)
while the CPU selects the scene; the first part for the geometry unit waits until it is done.

**Moving parts of the track** (from the EXE: `0x8ad14`, `0x8acf0`, `0x8ac58`; format per
OpenScreamer's notes S2-5b, valid for both games): a mesh can have several blocks, and a block with
a position set (+0x0C) is moved from there or, if the mesh has images (+0x10), rotated per image:
12 words per image are its whole matrix (the stored corner as a row times matrix, 16383 = 1, then
the offset), the image is images x clock >> 16, one turn of the animation clock in 3.33 s. On
track 1 only the `H` set has such a mesh, `V220` at the start: the plinth with the video wall
(block 0, one fixed image) and the helmet on it (block 1, 80 images). The firmware splits such a
part into one job per block, the farthest centre first (`0x8ac58`). The images turn z-up into
y-up, a mirroring; the game takes the other side as the back face for such blocks (`0x8acf0`:
`jge` instead of `jl`), and so does the geometry unit (bit 3 in word 18 of a job, `docs/soc.md`).

### Routine 224 (smoke, shadows) and routine 160 (reflections)

**Routine 224:** the game's filler (`0xcd9d0`) turns each pixel into `SMG[texel x 256 + pixel]`,
without light or depth; this is the only SMG lookup in the program. It is used by smoke (all four cars,
`0x204ae`, `0x2073b`: eight records of each of two kinds a car, a puff at each rear corner every 0.08 s
while the car is on the ground and the tyres slide or the road kind says so, 30 frames, turned to the
screen; each cloud offset sideways by ((2 d & T) - d) * (x & 7) and backwards by the same * (z & 7), d the
road kind's dispersion, x and z the car's integer position, T the 100 Hz counter; the camera's car's puffs
stay in the car's frame), by shadows (`0x1f4a7`: 30 units away from the camera, ground - 2; class 4 sorts
200 farther back), by headlight cones and lamp glow of the night looks, by a helicopter's rotor on tracks
2, 3 and 6; never by glass. The rasterizer reads the
pixel from the framebuffer and looks it up in a 16 KB table (16 M10K, `RS_SMG`); texel 0 leaves the
pixel alone (row 0 swaps only colours of equal value, which saves 23-75 % of the pixels for smoke).
Screamer 2's table has 112 rows made at the race start; the 64 rows of the unit are enough: the image
builder (`firmware/smg.c`, docs/development.md "Wheel smoke and shadows") gives each distinct texel the
see-through textures use a row (40 to 62 of 62), copies the textures with their texels as those rows,
turns routine 208 into 224 and puts the table (the game's rows for those texels) at the end of the MLB
section; no hardware change.
In blend mode the original's point rule applies, otherwise the shared edges of a polygon's
triangles would be blended twice.

**Routine 160 (reflections on the cars):** the windows show a reflection that moves with travel
and view. For each vertex of a block with flag 0x2000 (the body, 84-128 vertices with a normal)
the original computes on the x87 the reflection r = 2 (e.n) n - e of the view direction e at the
normal n (`0xc2230`) and takes u = round(127 r.x) + 127 - offx, v = round(127 r.z) + 127 - offz as
the texel, without material size, wrapping at 256. In the core this is done by the geometry unit
after the block's vertices, in integer arithmetic (`firmware/geom.h`): the normal from the float
bits to Q14, with the matrix into view space (the same step as for the vertices), the vertex
normalised to 17 bits, 1/|p| from a table of 128 and one Newton step on its three multipliers
(now 18x18), about 26 clocks per vertex; u, v in a RAM of 512 (1 M10K), from which the routine-160
faces take their texels; the rasterizer lets them wrap at 256 (size bit 29). Compared with the
floating-point form about 1 % of the values differ by one. What the original computes per object
stays on the CPU (`track.c`): the offsets offx, offz from three accumulators per object (`0xc1fc0`:
the distance moved since the last draw, rotated into the view, times the factor at `0xc2428`) and
the image. In Screamer 1 `0x1813c` sets for the player's car, by the kind of the last marker reached, car
+0x7b0 = 0 / 0x8000 / 0xff00 (kind 0 / 1 / 2) and the factor 0.002 or 0.03, `0x1607c` copies it to
the object set (+0x3c), the image drawn is n x phase >> 16. In track 1's tunnel (markers 3 and 4,
kind 1) image 1 of the texture, the tunnel ceiling lights, replaces the sky; opponents keep image 0
(they do not call `0x1813c`). Measured against Screamer 1's original (DOSBox, start grid, near chase
camera; the sister core's PC renderer with the camera and the accumulators from its memory): of the 128
body vertices 95 are equal, the rest differ by one; in the rear window 65 of 125 pixels are equal
(84 % equal to a neighbour); with the stored u, v it was 9. The cost in the simulation:
geometry unit +36,000 clocks, start frame 1,338,092 clocks = 18.0 ms.

In Screamer 2 the picture is not the glass material's own (a placeholder of 8 x 8) but the first material of the
track's `ENV.MLB`, 128 x 128, which the game's filler wraps at 128 (`& 0x7f7f`). The unit and the rasterizer stay as
they are: the image builder stores each frame of that picture tiled 2 x 2 as 256 x 256 and points the windows'
materials at it (docs/development.md, "The windows"). The race's picture is the track's (`ENVP<track><look>1.DAT`), the CAR page's the team's (`ENVM<team>1.DAT`, one picture for the four teams); in the race each car's
object slides it by the way the car moved and picks its frame by the kind of the ground under its fourth wheel (`s2_env_phase()`), as the original's 0x1f1ee does.

## Sound

**Mixer in hardware** (after OpenScreamer's notes section 17 and the EXE): the game's driver runs
at 11025 Hz and takes the stored sample without smoothing (index += pitch 16.16). `audio.v` does
the same with 48 kHz at the output: per output value one pass through 32 voices (four clocks per
voice, one data path, one DSP), the byte at the position times volume left and right, the sum
limited to 16 bits, then the position advanced by pitch x 11025 / 48000 (the firmware converts).
The registers are in MLABs, per voice a line of 32 bytes in one M10K, fetched through the line port;
the output clock is the template's (MCLK 12.288 MHz), everything on `clk_74a`. The C model
`firmware/mixer.c` computes the same value by value (`tools/sim.sh audio`, 60,000 random values).

Which samples play on the voices, their rates, volumes and pan are the game's side
(`firmware/s2snd.c`, `s2sound.c`; `docs/development.md`, "Sound (the race)").

**CD music:** the core reads GOG's CD image from the SD card unchanged: the cue sheet `GAME.DAT`
says where the tracks begin in `GAME.GOG` (raw, 2352 bytes per sector). In the mixer one stream of
16-bit stereo at 44.1 kHz, linearly interpolated to 48 kHz, from a 256 KB ring in the SDRAM
(`docs/soc.md`, "Sound"), which `firmware/music.c` keeps filled with read commands that the CPU
only starts: the Pocket does the rest through the bridge while the frame is drawn. Which track
plays where is the game's (`CD_TRACKS`; the menus' song is CD track 2): `docs/development.md`,
"Sound (the race)" and "The menus' music". Screamer 1's FM music (the OPL2 chip for its MIDI songs) was
in this core up to release 1.1.0 and never played: Screamer 2 has no such songs.

## Resource usage

Measured (Quartus build of 2026-11-14, the first without Screamer 1's FM chip; timing holds at 74.25 MHz: setup +0.95 ns, hold
+0.02 ns; fitter seed 7): 10,986 of 18,480 ALMs (59 %), 282 of 308 M10K (92 %, of which 16 the table of
routine 224), 31 of 66 DSP (47 %). The FM chip had cost 646 ALMs, 10 M10K and 3 DSP in the build before
(11,470 ALMs, 292 M10K, 34 DSP). From build to build the
logic fluctuates by a few hundred ALMs: placement packs differently densely, and where timing is
tight the fitter rebuilds logic and registers (physical synthesis).

![Space budget in the FPGA: logic 59 %, block RAM 92 %, DSP 47 %](resources.svg)

The picture is produced by `tools/make_budget_svg.py`: the measured rows from the fitter report
(`core/src/fpga/output_files/ap_core.fit.rpt`, table "Resource Utilization by Entity"). Regenerate
after every build; the script also writes this table.

| Component | ALMs | M10K | DSP |
|---|---|---|---|
| Analogue frame (bridge, PLLs) | 465 | 2 | - |
| Framebuffer x2 + video | 124 | 130 | - |
| VexRiscv + 2x 8 KB cache | 1,253 | 19 | 4 |
| FPU (RV32F) | 1,501 | 5 | 4 |
| CFU (16.16 multiplication) | 8 | - | 3 |
| Boot RAM 32 KB + log | - | 36 | - |
| Geometry unit | 1,257 | 23 | 4 |
| Clipper | 955 | 6 | 2 |
| Setup unit | 1,493 | 7 | 4 |
| Rasterizer | 1,246 | 2 | 5 |
| Textures: pipeline + cache | 696 | 47 | 4 |
| Blitter (background, HUD) | 483 | 2 | - |
| SDRAM controller + ports | 319 | 2 | - |
| Audio: mixer 32 voices + music, I2S | 568 | 1 | 1 |
| Bus, registers, arbiter | 617 | - | - |
| **built** | **10,985 (59 %)** | **282 (92 %)** | **31 (47 %)** |

Block RAM is the scarce resource, and logic becomes tight too: from about 80 % utilisation it is
hard on the Cyclone V to hold the clock frequency. This shows at build time (timing analysis;
`tools/build.sh` reports it and tries further fitter seeds), not as instability on the Pocket, as
long as timing holds. The biggest items are setup unit and FPU (about 1,500 ALMs each). The setup
unit computes bit-exactly like the C model in fixed point, hence wide: a corner is 144 bits, a
polygon 576, intermediate values and products up to 65 bits. There are three arithmetic units in
series so that they work simultaneously (clipper, corners, triangles), each with one multiplier
and a fixed sequence: in front of each multiplier sits a select (47 bits wide from 28 steps in
the triangle part), behind it register fields for the intermediate values (about 1,100 bits) which
are selected again. Variable shifters for normalising before the reciprocal table and scaling the
results add to this (the setup unit has eight, 32 to 65 bits wide, the clipper three, a few hundred
ALUTs each), and products wider than a DSP block (47x18, 38x18) add their partial products in
logic. There is no depth buffer, see "Visibility without a depth buffer".

How the logic was kept small:

- The project file from Analogue's template set synthesis to speed (`OPTIMIZATION_TECHNIQUE SPEED`,
  `MUX_RESTRUCTURE OFF`); without restructuring, every wide register that is only held in many
  states of a sequence, and every partial select with too wide an index, became its own wide
  select. The core uses `BALANCED` and `MUX_RESTRUCTURE ON`: 14,046 instead of 15,646 ALMs, and
  better timing.
- Units are written so that wide registers have an enable and small selects instead of being held
  in every step, one shared adder serves all read accesses (or counters), matrix, origin and eye of
  the part and rasterizer registers 7 to 18 sit in small block RAMs, and values are narrower where
  corners lie on whole pixels. The clipper's 4-bit edge number selected from the 576-bit input like a
  shifter over 16 places (1,101 ALUTs alone); now the edge end comes from the buffer, the plane
  distance is one adder with selected operands, one shifter instead of two. Each rewrite was
  checked clock for clock against the old version with 30,000 to 200,000 random cases (the clipper
  with 90,000 random polygons) and in the simulation gave the same pixels in the same clocks.
- The setup unit's 576-bit input buffer stays in registers (in block RAM it would save 190 ALMs but
  cost 19 M10K, the scarcer resource).

VexRiscv and FPU come from a synthesis of the CPU alone for the 5CEBA4F23C8 with Quartus 18.1.1:
RV32IM with 2 x 8 KB cache 1,268-1,278 ALMs, 19 M10K, 4 DSP, Fmax about 115 MHz; with FPU
3,052-3,061 ALMs, 24 M10K, 8 DSP, Fmax 98-111 MHz (median 105). The FPU makes float calculations
13-32x faster than soft-float.
