# Draw distance: what the original draws

Study of 2026-11-04 on branch `drawdist` (from `main`, d500c5c). Bug: in the track viewer on track 3 three palm
trees of a later stretch of road hung in the sky above the near ridge; the original (DOSBox, VGA version,
DETAIL HIGH) does not draw them although its camera was a little closer. Labels: **c** read in the code or
data, **m** measured on the PC renderer, **d** derived.

## Answer

The original does not look at every placement within the far plane. Its walk over the map (`0x76d80`) takes
**only the cells that the camera's own cell lists as visible** (the fifth array of every MMP cell, "run 4"),
when that cell has such a list. Our renderer looked at every placement within 58,192 units (far plane + 8192),
so scenery of road sections 6 cells away (47,000 to 49,000 units) was drawn: far palms with no ground under
them, because the ground there is not in the list either (the far-plane cut and the level of detail were
not the cause).

## What the original does (STARTL.EXE, addresses flat as `tools/dis.sh -f` prints them)

| Step | Where | Rule | Sure |
|---|---|---|---|
| The scene routine calls the walk | `0x755b8`..`0x755d0` | after `0x77a78` (the view), `call 0x76d80`; then the moving objects (`0x7e70c`), the keys (`0x7e880`) and the sort (`0x77130`). The other entry (`0x75570`, car screens) calls `0x769ac` and no walk at all | c |
| The window | `0x76b38`..`0x76d7f` (`0x769ac`: the same without the cell) | the camera's square of +-0x2000 and the two corners (-+57433, -56096) turned by the heading (constants at `0x768e0`), as a box of grid cells: `docs/original_geometry.md` 2.1 | c |
| The camera's cell | `0x76b38`: `((x + 2^20) >> 13, (z + 2^20) >> 13)` of the camera record (`[0x7e55c]` +0xc, +0x14); `[cell+0x24]` > 0 is the list's length, `[cell+0x10]` its start (4-byte entries) | stored in `0x769a4` (end) and `0x769a8` (start); no cell, or length <= 0: both 0 | c |
| Which path | `0x76d96`..`0x76da8`: `test [0xae6b0], 1` and the list's end `<> 0` | with both, the **list path** (`0x76dba`..`0x76e92`), else the **window path** (`0x76eb0`, `0x76ee8`: every cell of the window). `[0xae6b0]` is 1 in the image, no code writes it (the only reference to the address is that test; STARTH.EXE: `0xae890`, the same) | c |
| The list path | `0x76dba`..`0x76e92` | rows and columns of the window as before, but a cell is walked only if the list has it: entries are `row << 16 \| column`, sorted, the walk runs along the rows with the list's cursor (`0x76dc0`: a cell is taken when the entry's row is the current row and its column lies in the window) | c |
| A cell | `0x76fdc` (list path), `0x76ee8` (window path) | run 1 first (`[cell+4]`, count `[cell+0x18]`), then run 0 (`[cell]`, `[cell+0x14]`); each record: `(d^2 >> 3) < K`, 64-bit square, K at `0x76f4f`, `0x76fb3`, `0x77039`, `0x7709d`, patched at the race's start to ((far - 0x2000)^2 >> 3) (`0x7f6d4`, `performance.md` section 6) | c |
| Moving objects | `0x7e772` | window box and the same distance test; no list | c |
| `TABS/VIS.TAB` | read whole at `0x4fc84` (161,024 bytes into a reserved buffer in the code object) | **not** the visibility data: its 40,256 values fall smoothly from 25,474 to 0 (a ramp, probably a distance or projection table); the lists are in the map file | c (loader), p (use) |

Not guessed: the same code is in `STARTH.EXE` (SVGA) and the list is data of every cell in the map file. Checked
against the one DOS picture there is: at track 3's road point 653,400 the objects it shows (the palm at left
of centre, the tall trunk with a second palm hidden behind the ridge, the row of bushes, the big palm at the
right edge) are exactly the placements inside the camera cell's list; the three floating palms
(`a3palm`, 47,070 / 49,040 / 49,303 units from the camera, cells (142,131) and (141,131)) are in none of its
cells. The camera's cell is (141,137); its list names 73 cells, rows 133 to 142, columns 134 to 144.

## The lists in the seven maps (H set, `tools/s2/make_track_s2.py` `visibility()`)

| S2 track | cells | cells with a list | entries, mean (max) | reach from the cell, rows / columns (max) | placements in a list's cells, mean of all | road frames whose camera cell has a list |
|---|---|---|---|---|---|---|
| 1 | 457 | 197 (43 %) | 52 (78) | 9 / 8 | 229 of 2000 | 114 of 161 |
| 2 | 388 | 213 (54 %) | 48 (67) | 8 / 8 | 199 of 1576 | 185 of 185 |
| 3 | 510 | 223 (43 %) | 48 (70) | 7 / 9 | 223 of 2346 | 165 of 185 |
| 4 | 843 | 180 (21 %) | 71 (98) | 11 / 9 | 280 of 3742 | 82 of 169 |
| 5 | 558 | 250 (44 %) | 54 (80) | 9 / 10 | 205 of 2116 | 128 of 161 |
| 6 | 655 | 178 (27 %) | 54 (78) | 10 / 8 | 210 of 2467 | 89 of 161 |
| 7 | 251 | 205 (81 %) | 36 (54) | 11 / 8 | 158 of 1099 | 162 of 165 |

(m: the sweeps' road frames, the near chase camera 1,300 behind the car; 161 to 185 frames a track.) Every
list names its own cell and is sorted; 14 to 29 % of the entries name places of the grid where the map has no cell
(nothing there), which the image's table leaves out. Where
the camera's cell has no list (tracks 4 and 6: about half the road frames), the original takes every cell of the
window, and so do we: far scenery within the far plane stays visible there, as in DOS.

## What was wrong in ours, and the fix

`track.c` looked at all placements, 16 at a time with a box test (`groups[]`), and the image did not carry
the cells at all. Now:

- **Image** (`tools/s2/make_track_s2.py` `visibility()`, `firmware/tb_mmp.c` `tb_mmp_vis()`, called from
  `track_build_s2()`; byte-identical for the seven tracks, `tools/s2/check_track_build_s2.sh`): the placements
  section ends with a table, `VIS1`, cells (key = row << 8 | column, first placement, count, list start,
  list length), then the lists as cell numbers. +24 to +26 KB an image (largest image 5.41 MB of 7). The image
  version stays 7: an image without the table (magic missing) is read as before.
- **Firmware** (`firmware/track.c`: the `cells[]` globals and `camera_cell()`; the cell loop in `track_init()`;
  the cull loop at the start of `draw()`): the cells replace the groups of 16 (an image without the table gets
  groups of 16 as cells); `draw()` finds the camera's cell by binary search (about 10 steps a frame) and, if it has
  a list, walks the listed cells (in cell order, so the pieces keep the order of their numbers for the sort's
  ties), else all cells. The box test per cell, the reach, the view test and the keys are the old code.
  `-DTRACK_NO_VIS` (`NOVIS=1` for `framestats.sh` / `render_s2.sh`) switches the lists off for comparisons.

Not done: the window itself (what the cell lists leave inside the window box, the original also cuts to the box).
Over the road frames, of the pieces that pass list, reach and view test, 0.0 to 0.2 % lie in cells outside the
window (track 4, window path: 216 of 25,910, 0.8 %): slivers at the screen's edge 40,000 to 50,000 units out.
The mirror's pass and the cars are unchanged (cars are objects, not cells).

## Evidence (PC renderer, the firmware's own `track.c`)

The viewer's frame on the Pocket was found by sweeping every position the viewer visits on track 3 (185.3 units a
frame, 6,452 frames) and matching the screenshot's picture: 92.8 % of the pixels agree within the palette's
rounding at 653,400 units along the road (camera 107,332 / 75,207 / 4,768, yaw 6.100; the overlay's "faces 632"
is the previous frame, ours counts 633). The three palms are drawn in that frame, with the same wrong sky.

| Frame (track 3, 653,400 units, 4 cars) | blocks handed on | faces looked at | pixels that differ |
|---|---|---|---|
| before (every placement in reach) | 147 | 633 | |
| after (the camera cell's list) | 122 | 595 | 340 of 64,000: the three palms |

By eye, the picture after the fix has the DOS picture's objects: palm at left of centre, tall trunk and
the palm behind the ridge, bushes, big palm at the right edge, no palm in the sky left of centre. Other tracks, 24 road
positions each (`tools/s2/render_s2.sh`, with and without the table): pixels differ in 8, 15, 4, 5, 3, 3 and 0 of the
24 positions of tracks 1 to 7 (most 1 to 2,100 of 64,000); the same symptom as on track 3 is gone in track 5 (a spire
and roofs floating at the top left, position 4 of 24, 240,000 units, 26 s into the viewer) and in track 2 (a mass of
far bushes hanging above the near ridge, position 9, 345,000 units, 37 s; the same at positions 11 and 19); the rest
is distant detail at the horizon (tracks 1, 3, 4, 6). The firmware with an image without the table draws the old picture bit for bit
(checked at the matched frame).

## Frame time (model of `performance.md`, `tools/s2/drawdist_sweeps.sh`)

160 frames along each road, near chase camera, player + 3 opponents, the firmware's LOD and far plane; before = the
same image with the lists off (`NOVIS=1`).

| S2 track | blocks handed on, mean | placements tested, mean | faces looked at, mean (max) | mean ms | worst ms | road within 20 ms |
|---|---|---|---|---|---|---|
| 1 | 153 -> 139 | 521 -> 238 | 1,101 (1,842) -> 1,047 (1,842) | 9.8 -> 8.9 | 11.8 -> 10.6 | 100 % -> 100 % |
| 2 | 176 -> 129 | 637 -> 200 | 1,101 (1,525) -> 971 (1,261) | 10.3 -> 8.6 | 12.5 -> 10.8 | 100 % -> 100 % |
| 3 | 163 -> 140 | 590 -> 223 | 1,295 (2,363) -> 1,179 (2,299) | 10.3 -> 9.2 | 13.5 -> 12.1 | 100 % -> 100 % |
| 4 | 214 -> 194 | 723 -> 360 | 1,569 (2,382) -> 1,485 (2,375) | 12.2 -> 11.0 | 15.3 -> 14.7 | 100 % -> 100 % |
| 5 | 145 -> 130 | 496 -> 202 | 1,208 (1,926) -> 1,142 (1,849) | 10.6 -> 9.6 | 15.1 -> 13.9 | 100 % -> 100 % |
| 6 | 154 -> 137 | 521 -> 213 | 1,239 (2,136) -> 1,155 (2,058) | 9.9 -> 8.9 | 13.1 -> 10.9 | 100 % -> 100 % |
| 7 | 154 -> 107 | 481 -> 146 | 1,269 (2,296) -> 1,049 (2,042) | 11.4 -> 9.7 | 16.7 -> 13.9 | 100 % -> 100 % |

The change never costs time: no frame of the 1,200 got slower. Scenery only: 8.8 -> 8.0, 9.4 -> 7.8, 9.4 -> 8.3, 11.2 ->
10.0, 9.8 -> 8.8, 8.9 -> 7.9, 10.7 -> 9.0 ms. Most of the saving is the cull (fewer placements tested, 150 clocks
each in the model), the rest the pieces and faces that are no longer drawn. (This run's "before" column is 0.1 to 0.6 ms
below the LOD table of `performance.md` section 6 for the same face counts, because the model's inputs differ a little
since that table was made; both columns here are from one run.) Not measured on the Pocket.

## Not found

- A per-piece flag or sort class (+0x3C of the record, the sort bias at `0x7e9a5`): not involved in which pieces are
  drawn; the sort bias only orders them (done 2026-11-04, next section).
- The five distance tests cannot be what removed the palms: they all use the same K (reach 58,192 at HIGH), and
  the palms are at 47,000 to 49,300 units.

## Reproduce

`python -I tools/s2/make_track_s2.py <Screamer 2 folder> 3 build/s2/track3.bin`, `tools/s2/render_s2.sh
build/s2/track3.bin build/pics/x_ v653400,4` (`NOVIS=1` for the old picture), the reads
`GAME_DIR=<folder> tools/dis.sh -f 0x76b38 0x77130`, the sweeps `tools/s2/drawdist_sweeps.sh` (40 s).

## Track-side gaps and the sort key (2026-11-04, branch `trackside`)

Two Pocket screenshots, track 1: (B) at 21 s the stone wall between the grass strip and the brown field on the left of the
road is missing for a stretch and shows up late; (C) at the start line the crowd on the left is painted over the "Muller"
board, the barrier. Both are the sort order, not the walk: **the original sorts by the piece's place plus the mesh's bias,
and ours had the bias at 0 (and S1's scale)**.

**The rule** (c, `STARTL.EXE 0x7e8c0`..`0x7e9b7`): the walk stores d^2 >> 3 of every record it takes (`0x76f46`, 64-bit square
of the whole-number distance to the camera); the key loop makes `key = -(round(sqrt(d^2 >> 3)) >> 2)`, adds 300 for a
moving object (`0x7e991`, flag bit 30 of the list entry) and takes the 16-bit word of the mesh's header entry 43
(`[obj + 0xc4]`, `0x7e9a5`) off it; the entries are sorted as `key << 16 | number` (`0x77130`). Ours had S1's d^2 >> 2 (a key
of distance / 8 instead of / 11.3), +800 for objects (6,400 units against 3,394) and no bias. Entry 43 of the seven tracks'
meshes (`tools/s2/check_vis_s2.sh` prints the counts): `a)NN...` (crowds, galleries, the big backdrop panels `a)03.tcp` of
14,000 units radius) N x 100, `a(NN...` (stands, columns) -N x 100, `akk...` (lawns, squares, carpets under the road,
tracks 3 and 4) 6,000, everything else, the cars included, 0. Track 1: 38 meshes with 100 to 400. A bias of 200 is 2,263
units of distance, 6,000 is 67,900.

**C (people before the barrier)** needs the bias alone (353 of 64,000 pixels with it only, 354 with the scale and the +300 as well):
the crowd (`a)02...`, +200) now sorts 2,263 units behind its place and the "Muller" board is whole (`render_s2.sh
build/s2/track1.bin x g1`). The seven
tracks' start lines differ by 0 to 444 pixels (board and gantry edges). Not compared with a DOS picture of S2 (there is none).

**B (the missing wall)** was found by sweeping cameras round the race robot's line (`RACE_ROBOT=1`, steps 600 to 900, 6 places
across, 5 headings): the best match to the screenshot is the pose (-23207, -80135, 6037, yaw 5.775, pitch -0.274): it draws the screenshot's picture (same bank, same
missing stone course). Everything the original's walk gives is there: the camera cell's list (52 cells) includes every piece of
the wall chain (`a2c6s2s`, 6 pieces); the view test, the level of detail and the back-face test change nothing there (each
switched off in turn). What hides the wall is the backdrop panel `a)031tcp` (radius 14,471, centre 12,674
units away): without its bias it is painted after the wall pieces 12,700 to 16,100 units away and covers their facade. Along the robot's line (51 frames, every 6th step from 600 to 900) the picture changes in 45 frames, up to 294 pixels, mostly
in the wall and the field on the left (1 to 58 elsewhere); at step 720 (camera -26695, -83911, 6471) the panel owns 304 pixels of the wall's stretch before, 54 after, the
wall piece 18 before, 249 after. The late pop-up is the order flipping as the camera nears: the wall piece's key crosses the
panel's. At the pose of the screenshot itself the two keys are 3 units apart (wall piece 486: 16,101 units, the panel's key
equals 16,068): the original's whole-number camera may lie a few units off ours and flip them, so a stretch of this wall can
still be hidden there in the original too.

**Checked** (`tools/s2/check_vis_s2.sh`, 7 tracks): the key against a double-precision reading (117 of 1,000,000 differ by 1,
single precision), the class keys against the file bytes, and the walk: from 216,000 cameras round the roads (every road point,
7 places across, 3 headings) no piece the whole walk takes within 6,000 units is left out by the camera cell's list. Further
out the lists do drop pieces (the original's own): within 9,000 units on 9 and 17 cameras of tracks 2 and 4, within
12,000 on 132, 77, 316, 20 and 21 of tracks 2, 3, 4, 6 and 7; tracks 1 and 5 none. Frame cost: none expected (the cull is untouched: the pieces, faces, triangles and vertices of the 51 route
frames are identical, the key is the same arithmetic with other constants); not measured on the Pocket.

**Open**: the S1 backlog "building drawn through the side barrier" was not looked at on S2's pictures beyond the start lines;
no DOS picture of S2 to compare the order with; the bias of an object (car) mesh is read through `track_class_key()` (0 for the
four car meshes of the image).

## A car behind a wall (2026-11-04, branch `carsort`)

Pocket screenshot, track 1 (race time 14.5 s, drive mode): a computer car ahead round the left bend is drawn over the stone wall
and bank that lie between it and the camera. **The original's order does it too: nothing was changed.**

**The rule** (c, `STARTL.EXE 0x7e70c`..`0x7e9b7`, `0x75570`..`0x75620`): the cars are not drawn by the cell they stand in or per
block. The moving-object walk (`0x7e70c`) appends them to the same list as the map's pieces, flagged in bit 30, with the same
d^2 >> 3 of their position; the key loop (`0x7e8c0`) gives them `-(sqrt(d^2 >> 3) >> 2) + 300` less the bias of their mesh (0 for
the four car meshes), a piece `-(sqrt(d^2 >> 3) >> 2)` less its bias; one sort (`0x77130`), one draw (`0x7ed44`). Nothing in the key
uses a radius or a polygon's depth. (The code after the sorted draw, `0x7e68c` to `0x7815c`, was read only as far as to see that it
transforms and projects one object's vertex list; it is no ordering rule.) So a car comes after every piece whose place is not at least 3,394 units nearer than the car's
(300 key units x 11.3: the head start that keeps the road piece under it from covering it), however near the piece's walls are:
a piece 2,000 units across has its place up to 2,000 units behind its near face. The backdrop panels (`a)03.tcp`, radius 10,000
to 14,000, bias 300) sort 3,394 units further back still, so a car up to 6,788 units behind a panel's place is drawn over all
of it, wall loop included. Ours is the same arithmetic (`depth_key()`, `+ 300`), so the same cases come out.

**Measured** (`tools/s2/report_carsort_s2.sh`, a per-pixel depth map of every polygon the geometry unit is given, patched
copies only; the robot's cameras every 10th step of 300 to 1700, a computer car taken 30 to 130 steps later, 2,538 poses a
track): the car is drawn through a nearer piece in 17 poses of track 1 (cars 14,500 to 23,000 units away, 16 to 46 pixels
each, 15 of them behind the panel `a)032tcp`, the first left-bend wall of the route at steps 740 to 800), 8 of track 2
(trees), 11 of track 5 (trees), 2 each of tracks 6 and 7, none on tracks 3 and 4; a car is never covered by a piece that lies
behind it. Without the sort bias the count for track 1 was 15 (13 behind the panel): the bias widens the band a little (the panel
sorts 3,394 units further back), it is not the cause.

**Not done**: a rule of our own (the car after a piece only where no nearer polygon covers it) needs the depth buffer of the
backlog or a test of the car's bounding box against the pieces' faces; the original has neither. If the user wants cars hidden
behind walls more than he wants the original's picture, that is the way.


## The firmware draws farther than the original (2026-11-10, for a test on the Pocket)

`FAR_SCALE` 1.3 and `LOD_SCALE` 1.0 (`firmware/Makefile` sets them, `track.h` has the original's 1.0 as the default for
the PC tools). Screamer 1 uses 2.0 and 1.2; with 1.2 the Pocket did not keep up on track 4, so the levels stay the
original's here (2026-11-10: first 2.0 and 1.2, then 2.0 and 1.0, then 1.5 and 1.0, all too heavy for track 4; now 1.3 and 1.0): the far plane the faces are cut at and the reach of the pieces are 1.3 times
the image's (track 4: 78,000 and 10,650 more instead of 60,000 and 8,192), and the distances where a block goes to the
next level of detail are the original's (placed 6,000 / 9,000, cars 2,000 / 6,000; with `LOD_SCALE` 1.2: 7,200 / 10,800 and 2,400 / 7,200). Not changed: the cell
lists of this document still decide which cells are looked at where the camera's cell has one (their cells reach 7 to 11
cells, 57,000 to 90,000 units, so a longer reach shows more of them, not floating scenery), and the rear-view mirror keeps
the original's far plane (its glass is 160 x 35 pixels). Not measured, not simulated: the frame time (the owner tests it
on the Pocket, track 4 first) and what pops in. Back to the original's distances:
`tools/fw.sh EXTRA_CFLAGS="-DFAR_SCALE=1.0f -DLOD_SCALE=1.0f" TAG=orig`.
