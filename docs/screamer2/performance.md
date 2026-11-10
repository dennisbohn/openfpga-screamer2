# Screamer 2 on our hardware: how many pictures a second?

Study of 2026-11-03 on branch `perf` (from `main`, 122da04). A cost model of one frame, fitted on measured
Screamer 1 (S1) frames and applied to the road of all seven S2 tracks. Nothing was run on the Pocket;
one S2 frame was run in the simulation as a check. Labels: **(M)** measured, **(D)** derived from
measured numbers, **(A)** assumed.

Section 6 (branch `lod`, the same day) reads the original's DETAIL setting and its level of detail (LOD) and
measures what the LOD saves. It also **corrects one assumption of the first version**: the original considers
placements out to the far plane + 8192 units, not 1.2 times the far plane; every S2 number in sections 2 to 5
and in the answer is recomputed with it (changes: mean -0.1 to -0.4 ms, track 4 worst 24.6 to 23.5 ms).
**With the original's LOD, which it uses at every detail, all seven tracks hold 50 pictures/s in the 4-car
case** (section 6); the tables below are the picture without it.

Section 7 (2026-11-04, branch `perf2`) is about the race as the Pocket ran it (mean 14.5 and worst 19.6 ms on track 4): where the
frame's cycles go in the simulation, what was changed (the cull, the code layout, the car physics' trigonometry) and a forecast.

Later (2026-11-04, `draw_distance.md`): the original also takes only the cells that the camera cell's visibility
list names; the firmware does that now, which removes 0.9 to 1.7 ms more from the model's mean (placements tested
521 -> 238 on track 1) and never costs a frame time. Every table here is without it ("every placement in reach").

## Answer

Near chase camera, the player's car and 3 opponents in view (A), physics as on the S1 grid (M):

| S2 track | mean ms | worst ms | road within 20 ms | pictures/s mean / worst | with 9 opponents: mean ms, within 20 ms |
|---|---|---|---|---|---|
| 1 | 13.7 | 17.9 | 100 % | 50.0 / 50.0 | 16.2, 93 % |
| 2 | 15.1 | 19.9 | 100 % | 50.0 / 50.0 | 17.7, 77 % |
| 3 | 13.9 | 20.5 | 99 % | 50.0 / 48.0 | 16.4, 91 % |
| 4 | **18.6** | **23.5** | **73 %** | 48.9 / **40.0** | 21.1, 33 % |
| 5 | 14.4 | 20.2 | 99 % | 50.0 / 49.1 | 17.0, 89 % |
| 6 | 13.8 | 20.9 | 99 % | 50.0 / 46.4 | 16.1, 93 % |
| 7 | 15.6 | 21.9 | 96 % | 49.9 / 43.8 | 17.9, 73 % |

In words: tracks 1 and 2 hold 50 pictures/s everywhere, 3, 5, 6 nearly everywhere (dips to 46 to 49 at their
heaviest spots); track 7 dips to about 44; **track 4 is the problem**: about three quarters of
the road at 50, the rest at 40 to 48, the worst spot 40. Crowded places cost more: the starting grid
(10 cars) is 20.6 to 21.8 ms on tracks 4, 6, 7 (43 to 47 pictures/s), 19.7 on track 2. The geometry unit is
the bottleneck in every worst frame. Uncertainty: the renderer part is good to about 5 % (below); the
unknown S2 physics can add 1 to 2 ms (track 4 would then be within 20 ms on only 49 % of its road). **The
rear-view mirror, as drawn without LOD, adds 7 to 11 ms and does not fit** (lever 1; with the original's LOD in
the mirror it adds 3 to 5 ms and fits, section 6).

## Method

- **S1 side (M).** The six start frames (grid with ten cars, near chase camera, frame 2) in the Verilator
  simulation with the firmware of release 1.0.2 (`program.bin` as on the Pocket; the Pocket's `debug.txt` of
  track 1 agrees with the simulation to 0.2 %: frame 1,204,436 against 1,207,338 clocks). Per frame: CPU
  phases, the units' busy clocks, texture misses, the timeline marks.
- **Counters (D).** `tools/s2/framestats.sh` builds copies of the firmware's `track.c geom.c setup.c raster.c`
  with counters (`perf_patch.py`; the firmware is not touched) and draws frames natively. It counts placements
  tested and groups skipped, pieces, vertices, faces looked at, back faces, clipped polygons, triangles,
  rows, pixels, and feeds the texel and mesh addresses to cache models. The texture-cache model (16 KB, direct
  mapped) agrees with the hardware's miss counter to 0.5 % on all six S1 frames and to 0.4 % on the S2 frame.
  The PC pictures are the ones the simulation checks (0 pixels differ), faces and pieces equal the logs.
- **Same measure for S1 and S2.** Every road point of the map (MMP array 2), every 2nd to 12th so that a
  track has 160 to 220 frames; the game's near chase camera (1300 behind, 300 up, 0.1885 rad down) over a car
  on the road. Cars (A): the player's own car plus opponents ahead (2,200 units in front, then every 1,700,
  alternately 500 left and right), 12-block meshes AUTO00..06 (S1) and AUTO1..5 of team 1 (S2). Start grid:
  the S1 grid's positions as measured in the game. A synthetic S1 grid comes out 1 to 4 % below the
  simulated frames (no shadows, props or smoke). S2 view distances (A): `TRACK_DATA` far plane 50,000 (track 4:
  60,000), placements out to the far plane + 8192 (STARTL.EXE `0x7f6d4`, section 6; the first version used 1.2
  times the far plane, OpenScreamer's rule); S1 keeps 43,808 and 52,000.

## 1. Calibration on Screamer 1 (M)

| Track | placements tested | faces looked at | pixels | texture misses PC / HW | cull ms | GU ms | RS ms | frame |
|---|---|---|---|---|---|---|---|---|
| 1 | 512 | 3,734 | 108,157 | 11,221 / 11,175 | 1.5 | 12.2 | 6.1 | 1,204,582 = 16.2 ms |
| 2 | 496 | 3,428 | 123,744 | 12,318 / 12,319 | 1.5 | 11.7 | 6.3 | 1,139,349 = 15.3 ms |
| 3 | 480 | 3,340 | 96,189 | 7,907 / 7,908 | 1.4 | 10.4 | 5.2 | 1,051,700 = 14.2 ms |
| 4 | 544 | 3,847 | 116,326 | 14,174 / 14,185 | 1.6 | 12.8 | 6.9 | 1,250,648 = 16.8 ms |
| 5 | 656 | 4,156 | 119,611 | 7,126 / 7,129 | 1.9 | 12.3 | 5.8 | 1,209,503 = 16.3 ms |
| 6 | 896 | 3,602 | 133,303 | 11,849 / 11,871 | 2.3 | 12.5 | 6.9 | 1,262,972 = 17.0 ms |

How the frame is made (the timelines show it): the CPU does music, physics, preparation (`A`), then the
cull (`C`), then hands pieces over; the geometry unit (GU) starts with the first piece and sets the pace;
the rasterizer (RS) is busy only 35 to 41 % of the frame; in five of six frames the CPU finishes its own work
150,000 to 290,000 clocks before the GU (the sixth was held up by the GU's full buffer). In all six frames
*frame = first piece + GU busy + 41,300*, spread 38,387 to 45,881 (blitter copy of the race screen, rasterizer
drain). Model:

| Part | Model | Source |
|---|---|---|
| `A` before the cull | 125,117 clocks (1.7 ms), constant | M, mean of six (109,000 to 138,000) |
| cull `C` | 24,198 + 150 x placements tested + 110 x pieces in view | fitted, error <= 5 % |
| GU busy | 32.5 x read misses (4 KB cache) + 32.4 x reflection vertices + 87.8 x faces looked at + 0.48 x RS (the next row) | 32.5, 32.4 from the GEOM_STATS run and the reflection step (`architecture.md`); the rest fitted |
| RS busy | 0.83 x (pixels + 17.3 x texture misses + 41 x triangles + 6.5 x rows + 18,000) | per-event costs from the rasterizer's own counters; 0.83 the overlap, +-5 % |
| frame | `A` + `C` + GU + 41,300; the largest of this path, the CPU path and the RS path wins | |

**Hold out one frame, fit on five** (central model; the variant with pixels instead of RS in the GU term in
brackets): track 1 -0.3 % (-2.2), 2 +1.2 (+2.4), 3 +1.6 (+1.5), 4 -2.0 (-4.7), 5 -3.2 (+2.7), 6 -0.9 (-0.1).
Worst -3.2 %, rms 1.8 %.

**One S2 frame in the simulation** (track 4, road point 130, the player's car alone, the firmware with the
constants of `comparison.md`; S1 view distances; the track check: 0 pixels differ). Measured 1,397,788
clocks = 18.8 ms; model 18.0 ms (-4.6 %): GU 1,046,165 against 1,022,718 (-2.2 %), RS 733,692 against
766,864 (+4.5 %), cull 150,150 against 144,370. The pixel variant said 16.7 ms (-11 %), which is why the
RS term is the central one: S2 frames have up to twice the texture misses per pixel and the GU waits for
the rasterizer behind it. Of the 0.9 ms gap, 0.8 ms is `A` (182,000 clocks here, the physics of a stand-in car) and the model's tail is
26,000 clocks too long (this image has no race screen).

## 2. Equal footing: faces, pieces and cache misses along the road (D), no car

| Track | faces drawn min / mean / max | pieces handed on, mean | placements tested, mean (max) | pixels, mean | texture misses per pixel | distinct texture lines |
|---|---|---|---|---|---|---|
| S1 1 | 900 / 1,395 / 1,922 | 121 | 428 (576) | 98,194 | 0.121 | 6,307 |
| S1 2 | 510 / 1,122 / 1,792 | 95 | 392 (528) | 84,631 | 0.120 | 5,836 |
| S1 3 | 341 / 1,336 / 2,585 | 101 | 445 (608) | 91,179 | 0.103 | 5,210 |
| S1 4 | 923 / 1,388 / 1,899 | 119 | 531 (736) | 93,168 | 0.129 | 6,427 |
| S1 5 | 718 / 1,408 / 2,693 | 90 | 494 (791) | 63,228 | 0.127 | 3,901 |
| S1 6 | 639 / 1,612 / 3,501 | 121 | 573 (928) | 84,949 | 0.120 | 5,432 |
| S2 1 | 493 / 1,391 / 2,857 | 113 | 521 (704) | 93,181 | 0.152 | 7,427 |
| S2 2 | 545 / 1,536 / 3,135 | 136 | 637 (992) | 104,231 | 0.154 | 8,127 |
| S2 3 | 545 / 1,484 / 3,329 | 124 | 590 (864) | 85,233 | 0.183 | 7,544 |
| S2 4 | 826 / 2,206 / 3,262 | 173 | 722 (1,008) | 113,366 | 0.163 | 9,960 |
| S2 5 | 634 / 1,436 / 3,326 | 105 | 496 (752) | 123,837 | 0.119 | 6,022 |
| S2 6 | 678 / 1,474 / 2,779 | 114 | 521 (752) | 99,154 | 0.128 | 6,881 |
| S2 7 | 482 / 1,584 / 3,623 | 114 | 480 (752) | 102,433 | 0.240 | 6,602 |

S2 draws on average 15 % more faces than S1 (track 4: +60 %) and up to twice the texture misses per pixel.
Along the road (player + 3 opponents) S1 comes out at 10.9 to 12.5 ms mean, S2 at 13.7 to 18.6. A car costs
about 0.7 ms (the player's, +150 faces looked at) and 0.6 ms (each opponent, +230).

## 3. Forecast: more cases (D)

Mean ms along the road. Columns: cars in view (0 = scenery only, the study's measure; 1 = the player's; 4 = and
3 opponents; 10 = and 9), then variants of the 4-car case.

| Track | 0 | 1 | 4 | 10 | physics +1.35 ms | mirror (full view) | mirror, 15,000 view | S1 view distances | far plane x 0.8 | grid, 10 cars |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 11.1 | 11.8 | 13.7 | 16.2 | 15.1 | 21.2 | 17.6 | 13.0 | 12.6 | 17.9 |
| 2 | 12.4 | 13.1 | 15.1 | 17.7 | 16.4 | 23.5 | 19.0 | 13.9 | 13.3 | 19.7 |
| 3 | 11.3 | 12.0 | 13.9 | 16.4 | 15.2 | 21.2 | 17.6 | 13.1 | 12.6 | 16.7 |
| 4 | 15.7 | 16.5 | 18.6 | 21.1 | 19.9 | 29.9 | 22.9 | 15.8 | 16.6 | 21.8 |
| 5 | 11.7 | 12.4 | 14.4 | 17.0 | 15.8 | 21.8 | 18.2 | 13.5 | 13.0 | 17.6 |
| 6 | 11.2 | 11.9 | 13.8 | 16.1 | 15.1 | 21.1 | 17.6 | 13.0 | 12.5 | 20.6 |
| 7 | 13.1 | 13.9 | 15.6 | 17.9 | 16.9 | 22.4 | 19.3 | 14.5 | 13.9 | 20.6 |

Worst frame (4 cars), ms: before the cull 1.7, cull 1.6 to 2.2, GU cache misses 4.6 to 6.6, GU faces 5.6
to 7.3, GU waiting for the units behind it 3.7 to 7.0, tail 0.6; the RS is busy 7.6 to 14.5 ms of the frame (track 7:
42,243 misses). The CPU path is never the longest (75 to 83 % of the frame on S1).

**Pictures per second when frames take longer** (the swap waits for a vertical blanking, 12.2 of the 20 ms
of a picture; the next frame starts at the swap; `video_out.v`, `main.c`; simulated rule, constant frame time):

| frame time ms | <= 20 | 21 | 22 | 24 | 26 | 28 | 30 | 33 | 36 | 40 |
|---|---|---|---|---|---|---|---|---|---|---|
| pictures/s | 50.0 | 46.2 | 42.9 | 40.0 | 37.5 | 33.3 | 33.3 | 25.0 | 25.0 | 25.0 |

The game's physics keeps real time (one step per video frame elapsed, at most four), so a slow picture
means fewer pictures, not slow motion.

## 4. Costs in no S1 measurement

| | Estimate | Evidence | Sure? |
|---|---|---|---|
| (a) cull with up to 3,742 placements | track 4: 722 tested on average (max 1,008; S1 max 928), cull 2.1 ms mean, 2.7 ms worst, against 1.4 to 2.3 ms on S1. Without the group skip it would be 7.6 ms | counters, 150 clocks per placement tested (M), skip works because the file keeps a cell's placements together | high |
| (b) rear-view mirror | second pass of the scene into 320 x 35 rows: 70 to 100 % of the pieces and faces of the main pass, 23 to 36 % of its pixels: **+6.8 to +11.3 ms** (full view), +3.7 to +4.3 ms with a 15,000-unit view | counters of a backward camera at the car, NOTES2 S2-5zh; model | medium (cull cost of a 2nd pass assumed to hide behind the GU backlog) |
| (c) fade by depth (`.DPT`) | 760 to 1,070 faces a frame of 1,800 to 2,700 (night and fog alike) draw with another row than the face before; if each change fetches a 256-byte row (8 lines, 17 clocks each) that is up to +1.4 to +2.0 ms, nothing if hidden. Day frames: none. GU part: 3 adds per face | row rule of NOTES2 S2-5d on the faces' depths; the rows cannot stay in block RAM (64 KB table, 16 M10K free) | low |
| (d) see-through table, damage | **no hardware change**: the table is 64 rows and the image builder fits the 40 to 62 distinct texels the effects use (docs/development.md "Wheel smoke and shadows"). Smoke and shadows, counted on the game's own races: up to 36 faces and 48,000 blended pixels a frame (2 clocks a pixel: 1.3 ms of the rasterizer's time at the most, 0.3 to 0.6 ms on average while a race has puffs), the CPU about 0.2 ms (estimate). Damage = another texture picture, no cost | `comparison.md`; smoke area assumed | medium / low |
| (e) texture working set | S2 files are smaller (MLB 1.05 to 1.95 MB) but touch more distinct lines per frame (6,100 to 10,100 against 3,900 to 6,400) and miss up to twice as often per pixel. Not conflicts: a 16 KB two-way cache changes the misses by -1 to +2 %, a 32 KB direct-mapped one saves 13 to 22 % | cache models on the real texel addresses, checked against the hardware counter | high |

## 5. What would raise the picture rate on a slow track (track 4)

0. **The original's own level of detail (section 6), the largest lever: -3.3 to -5.9 ms on the mean, -4.5 to
   -7.7 ms on the worst frame; the picture stays the one the original draws.** Firmware only, small.
1. **Rear-view mirror: leave it out, or a short view plus a tighter cull** (without LOD). +7 to +11 ms at full
   view, +4 ms at 15,000 units; both still leave track 4 above 20 ms. Firmware only, small. (With the LOD and the
   original's level 1 at least in the mirror: +3 to +5 ms at full view, which fits.)
2. **Far plane x 0.8 (the original's MEDIUM detail, which also takes the smoke and the animated scenery out).**
   -1.1 to -2.0 ms (track 4: 18.6 to 16.6). One constant a track; visible pop-in; not the best DOS quality.
   Firmware only.
3. **Group-level view test in the cull.** 720 placements tested, about 175 in view: the other 550 cost 82,000
   clocks. A box-against-planes test per group of 16 should save 0.5 to 1 ms on track 4 (estimate).
   Firmware only, small.
4. **GU read cache 4 KB to 16 KB.** Cache-miss clocks are 5 to 7 ms of the worst frames; the C-model study of
   `architecture.md` has 27 % fewer misses at 16 KB: -1.3 to -1.9 ms. RTL, 12 of the 16 free M10K, which the wider
   see-through table also wants.
5. **A short queue between geometry unit and clipper.** The GU waits for the units behind it 3.7 to 7.0 ms in
   the worst frames (fitted term); a few polygons of buffering may recover 1 to 3 ms (estimate, not measured).
   RTL, medium.

## 6. Detail levels and LOD at HIGH

Read in `STARTL.EXE` (320 x 200, 256 colours), the baseline. Addresses are the flat ones `tools/dis.sh -f` prints
(`GAME_DIR` = the Screamer 2 folder; the tool keeps one flat image per program name in `build/exe/`: delete it when
switching games). This copy (md5 `3d944cc1...`) differs from the one OpenScreamer's NOTES2 used (`15da1f4e...`): the
code is the same, some addresses are not (NOTES2's `0x7f504`, the far-plane writer, is `0x7f6d4` here).
`STARTH.EXE` (SVGA) cross-checked: the constants and the far-plane code agree (thresholds at `0x7e744` / `0x7e790`,
TRACK_DATA x 0.8 at `0x2ef57`, the five reach immediates at `0x77050`...), only the addresses move; the rest was not compared. Sure: **c** read in the code or data
and every writer of the constants searched; **p** probable; **u** unread.

**What DETAIL changes** (`0x130c54`: 0 LOW, 1 MEDIUM, 2 HIGH; the menu writes it at `0x33a5c`, default 1):

| | LOW | MEDIUM | HIGH | Where | Sure |
|---|---|---|---|---|---|
| Track set | `MAPnL01` | `MAPnH01` | `MAPnH01` | `0x2e69c`: `cmp detail,0` gives `L`, else `H`. The L set has the same drivable run and a 29 to 46 % shorter scenery run on tracks 1 to 6 (track 4: 2,514 against 3,742 placements), 4 to 8 % fewer meshes; track 7 is identical | c |
| Far plane (faces are cut at this depth) | 0.8 x | 0.8 x | TRACK_DATA's first number (-50,000; track 4: -60,000) | `0x2efc7`: `cmp detail,2`; below it `fild [0x1240f8]; fmul 0.8 [0x110ccb]; fistp`; `0x7f6d4` writes it into the face routines' immediates (`0x779c7`...`0x77a54`), `0xc9ad8`, `0xc9adc`, `0x76788`. No table like S1's `0x21d4` | c |
| Placements considered (distance of the placement's position) | to 48,192 (track 4: 56,192) | the same | to **58,192** (track 4: 68,192) | `0x7f6d4`: (far - 0x2000)^2 >> 3 into the five `d^2 >> 3 < K` tests (`0x76f4f`, `0x76fb3`, `0x77039`, `0x7709d` the map's cells, `0x7e772` the moving objects); the file's own K is S1's 52,000. **Far plane + 8192, not 1.2 x** | c |
| Wheel smoke (`asmoke`, `a2smoke`, 8 + 8 puffs a car) | no | no | yes | `0x204c1` (spawn), `0x2f21b` (loads `misc` and the puffs). Built, `firmware/s2fx.c`: the core draws as at HIGH | c |
| Animated scenery rows (`ANIMATIONS`, objects moving along paths) | no | no | yes | `0x3106d`: `[0x124208] = (detail == 2)`, read at `0x2cef0` | c (HIGH only); what a row is: p. Built since: `firmware/s2anim.c`, docs/development.md "Animated scenery" (the rules read in `0x2cee4`, compared with the running original's memory) |
| Block level of detail (below) | the same | the same | the same | no read of the detail in `0x7ee38`...`0x7f330` | c |

Nothing else reads it (the other references are the menu, the attract race `0x3cab4` and the replay copy
`0x32994`). So HIGH = MEDIUM + the full far plane + smoke + animated rows, and LOW = MEDIUM with the L set.

**The LOD rule.** The object header's block table at `+0x18` has 44 entries: block b at level L is entry b + 13 L
(0x34 bytes a level, `0x7f20f`...`0x7f239`), an offset from the object; 0 = no block at that level = **the block is
not drawn**. Entry 41 (-1) and 43 (a sort bias read at `0x7e9a5`, -700 to 6,000; applied since 2026-11-04, `draw_distance.md`) are not blocks. Matrices, middle and sort key stay those of the level 0 block (c).

| | Rule | Constants | Levels change at | Sure |
|---|---|---|---|---|
| Placed meshes (also the ones with several blocks or frames; `0x7ef24` then `0x7f148`) | dist = distance of block 0's middle from the camera, rounded to nearest; key = -(dist >> 1), +0x2000 for a block named `Z...` (none on S2 tracks) | `[0x7e564]` = -3000, `[0x7e568]` = -4500 | **6,000 and 9,000** (S1: 12,000 and 16,000) | c |
| Cars and the other moving objects (`0x7ee38` then `0x7f010`) | key = -dist; the name surcharge (`i` 4000, `x` 1000, not `A`, `a`) only orders the blocks, far first | `[0x7e5b0]` = -2000, `[0x7e5b4]` = -6000 | **2,000 and 6,000** (S1: 4,000 and 7,000) | c |
| Rear-view mirror pass (`[0xae6b4]` = 1 from `0x756b0`) | at least level 1 | | | c |

The constants are image constants. The only code that writes one multiplies and later divides the cars' pair by 3
in a menu screen (`PIC_%03d.raw`, `0x385bc`, `0x38696`), not in the race. Certain from the image and the code;
not watched in a running DOSBox (`tools/dosram.py` reads S1 only). Which routine a piece takes: `0x7ebb8` leaves
`[0xc8]` at -1 (bit 30 set) for a map placement, and sets it to the record's sub-records for a moving object; the
dispatch at `0x7ed64` follows bit 30 (c).

**Data** (`tools/s2/s2_lod_scan.py`; H set, weighted by placements): every mesh of every track has all three levels
for all its blocks (no dropped blocks on tracks).

| S2 track | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| faces at level 0 : 1 : 2 | 100 : 37 : 21 | 100 : 29 : 16 | 100 : 43 : 29 | 100 : 45 : 26 | 100 : 36 : 22 | 100 : 42 : 26 | 100 : 39 : 25 |
| vertices at level 0 : 1 : 2 | 100 : 50 : 35 | 100 : 42 : 29 | 100 : 59 : 46 | 100 : 62 : 41 | 100 : 46 : 34 | 100 : 52 : 38 | 100 : 51 : 38 |

Cars (AUTO1 to 5 of the four teams): level 0 has 12 blocks and 160 to 280 faces, level 1 the same 12 blocks with
143 to 223 faces (-11 to -37 %), level 2 only the body block (`MV_mauto`): 11 to 14 faces, 44 to 48 vertices
(-92 to -96 %); wheels, hubs and arches are dropped.

**Measured** (the counting build of sections 1 to 5, near chase camera, the player and 3 opponents, physics as
above; `tools/s2/perf_patch.py` puts the rule into the temporary copies of `track.c` and `car.c`, the firmware stays
as it is; with the rule off the counters equal the earlier ones: 7,122 frames, no field differs; pieces to far +
8192). Placed blocks take their level from the distance of block 0's middle to the camera, cars per block.

| S2 track | faces looked at, mean | mean ms | worst ms | road within 20 ms | pictures/s mean / worst |
|---|---|---|---|---|---|
| 1 | 2,871 -> 1,101 | 13.7 -> 10.1 | 17.9 -> 12.2 | 100 % -> 100 % | 50.0 / 50.0 -> 50.0 / 50.0 |
| 2 | 3,259 -> 1,101 | 15.1 -> 10.6 | 19.9 -> 13.0 | 100 % -> 100 % | 50.0 / 50.0 -> 50.0 / 50.0 |
| 3 | 2,865 -> 1,295 | 13.9 -> 10.5 | 20.5 -> 13.6 | 99 % -> 100 % | 50.0 / 48.0 -> 50.0 / 50.0 |
| 4 | 4,219 -> 1,569 | 18.6 -> 12.7 | 23.5 -> 15.8 | 73 % -> 100 % | 48.9 / 40.0 -> 50.0 / 50.0 |
| 5 | 3,076 -> 1,208 | 14.4 -> 11.1 | 20.2 -> 15.6 | 99 % -> 100 % | 50.0 / 49.1 -> 50.0 / 50.0 |
| 6 | 2,945 -> 1,239 | 13.8 -> 10.1 | 20.9 -> 13.3 | 99 % -> 100 % | 50.0 / 46.4 -> 50.0 / 50.0 |
| 7 | 3,215 -> 1,269 | 15.6 -> 12.0 | 21.9 -> 17.4 | 96 % -> 100 % | 49.9 / 43.8 -> 50.0 / 50.0 |

What the lower levels remove (means along the road): faces looked at -55 to -66 % (all tracks -61 %), vertices -41
to -54 % (-49 %), geometry-unit mesh-read misses -44 to -54 % (-51 %), but texture misses only -8 to +4 % (-4 %) and
pixels -5 to -8 % (-6 %): the rasterizer side does not shrink, so most worst frames become rasterizer-bound (5 of 7;
RS busy 7.5 to 13.5 ms of 12.2 to 17.4). Of 114 placed blocks drawn on track 1, 8 are at level 0, 6 at level 1, 100 at
level 2 (tracks 2 to 7: 7 to 8 / 4 to 7 / 93 to 159). Of the three opponents' 36 blocks (2,200, 3,900 and 5,600 units
ahead of the player, the camera 1,300 behind him) about 27 are at level 1, 0.6 (the body only) at level 2, 6 dropped and
2 to 4 at level 0; the player's own car stays at level 0. Mean ms with the rule only
for placed meshes: 10.5, 11.0, 11.1, 13.1, 11.4, 10.6, 12.1; only for cars: 12.9, 14.3, 13.1, 17.8, 13.7, 13.0,
14.9, so the scenery gives 2.8 to 5.5 ms and the cars 0.7 to 0.8 ms. Scenery only (no car) with the rule: 8.8, 9.4,
9.4, 11.2, 9.8, 8.9, 10.7 ms. MEDIUM (far x 0.8, pieces to 0.8 far + 8192) with the rule: 9.5, 9.8, 9.9, 11.7,
10.4, 9.4, 11.2 ms mean (12.5 to 16.6 without), worst 11.7 to 16.6 ms.

The rear-view mirror drawn as the original does (level 1 at least): +2.7 to +4.6 ms instead of +6.8 to +11.3. Full
view, 4 cars and the mirror, mean ms and road within 20 ms, without -> with the rule: 1: 21.2 -> 13.2 (33 % -> 100 %), 2: 23.5 ->
14.1 (14 % -> 100 %), 3: 21.2 -> 14.2 (36 % -> 100 %), 4: 29.9 -> 17.3 (0 % -> 93 %), 5: 21.8 -> 14.0 (18 % ->
100 %), 6: 21.1 -> 13.5 (45 % -> 100 %), 7: 22.4 -> 14.7 (29 % -> 99 %); so it fits (same model assumption as
(b): the mirror's cull hides behind the geometry unit's backlog).

**Piece distance.** The first version assumed pieces to 1.2 x the far plane (60,000; track 4: 72,000); the original
uses far plane + 8192 (58,192; 68,192). Without the rule that gives mean 13.7 / 15.1 / 13.9 / 18.6 / 14.4 / 13.8 /
15.6 ms (was 13.8 / 15.3 / 14.0 / 19.0 / 14.6 / 13.9 / 15.8), placements tested -3 to -5 %, track 4's worst frame 24.6
-> 23.5 ms and its road within 20 ms 64 -> 73 %. The forecast tables above are recomputed.

**Does the DOS picture differ from ours without the rule?** DOS draws every block beyond 6,000 units (placed) or
2,000 units (cars) with the level 1 or 2 mesh, at HIGH exactly as at LOW; the rule has no detail setting. Ours
without it draws level 0 everywhere: finer in the distance than DOS, never worse. In six frames of our renderer
(tracks 1 and 4, three road points each, S1 view distances) 9 to 18 % of the pixels differ between the two; in the
two pairs I looked at the differences are small shape changes of distant houses, tree crowns and signs, nothing is
missing. With the rule our picture is the original's (same data, same rule, same far-first order), up to a block
whose distance is within a unit of a threshold (the original rounds in x87, we in float). A **pop-in** happens as
in DOS: a block changes level once when its middle passes 6,000 (level 1) and 9,000 (level 2) units from the
camera, a whole long block at a time; at 320 x 200 an object 500 units wide is 10 pixels at 6,000 and 7 at 9,000, so a
flip is a few pixels. Cars: the body simplifies at 2,000 and shrinks to a plain box at 6,000, wheels and hubs vanish.
In the mirror every block is drawn at level 1 or lower.

**Recommendation: LOD yes, at HIGH, with the original's rule as read** (placed 6,000 / 9,000, cars 2,000 / 6,000, at
least level 1 in the mirror). It gives the DOS picture, saves 3.3 to 5.9 ms on the mean and 4.5 to 7.7 ms on the
worst frame, and takes track 4 from 73 % to all of its road within 20 ms. Not using it only costs time for a finer
distance than DOS. Effort: **firmware only**, about 40 lines: `track.c` `draw()` one block-middle distance and a
block pointer per visible placement (per block for the ones with several), `car.c` `draw_model()` per car block; no
hardware change (the geometry unit takes another block pointer), no change to the track builders (the UNR and 3DX
files carry the levels; `make_track_s2.py` passes them on). Extra CPU: one square root and about 20 float
operations per block, about 150 blocks a frame: 0.1 to 0.3 ms (estimate, not measured), against a CPU path of 7.4
to 9.0 ms mean and 9.5 to 10.8 ms at most (never the bound). Uncertainty: the frames fall to 1,100
to 1,600 faces looked at and 1,700 to 3,200 mesh-read misses, below what the model was fitted on (3,300 to 4,200
faces, 7,000 to 14,000 misses), so its geometry-unit part is extrapolated; but the worst frames are bound by the
rasterizer, whose cost comes from its own counters (+-5 %), and the worst frame (track 7, 17.4 ms) has 2.6 ms to the
limit. Before the picture is called identical, compare one frame with a DOSBox screenshot. Reproduce:
`tools/s2/lod_sweeps.sh` (1.5 minutes, then `tools/s2/lod_report.py`), the data `python -I tools/s2/s2_lod_scan.py
<Screamer 2 folder>`, the code `GAME_DIR=<Screamer 2 folder> tools/dis.sh -f 0x7f148 0x7f25d`.

## 7. The race on the Pocket: where a frame goes, and what was changed (2026-11-04, branch `perf2`, from `main` 3533968)

The Pocket's race on track 4 (the player and three computers, ROOKIE, team 1 car 1; 18,201 frames): mean 14.5 ms, worst 19.6 ms,
none over 20 ms. The study's ride forecast for the same scene was 11.0 / 14.7: the race costs 3.5 to 5 ms more than the model said,
because the model has neither the car physics (the player's step, the three computers') nor the cull of a camera cell with no
visibility list. Labels: **(M)** measured in the simulation (cycles, the whole SoC with the firmware as on the Pocket), **(D)**
derived, **(A)** assumed.

**How it was measured.** The viewer's race on each track in the Verilator simulation (150 frames on the grid, 70 after GO, the
player on the gas: `docs/development.md`, "Driving mode"); the log has the frame timeline of the last frame, the visit line and,
with `tools/fw.sh PROF=1`, cycle counters per stretch of code (`firmware/prof.h`; that build is 10 to 15 % slower, so it tells
proportions only). Every build gives the same numbers every time. The scene is the same for all builds: four cars in view, and on
tracks 1, 4, 5 a camera cell with no visibility list. On the PC: `tools/s2/framestats.sh race` (the game's driver drives all
four cars through a lap, counters per frame, `PIECES=` also per piece), `tools/s2/check_cull_s2.sh` (see below).

### Where a frame went (M, track 4's grid frame, the firmware of `main`; cycles at 74.25 MHz)

| Stretch | cycles | ms |
|---|---|---|
| the player's step (60,000), the scene, the sound, the line | 85,500 | 1.15 |
| backdrop, palette | 10,400 | 0.14 |
| **cull and sort** (843 cells walked, 429 placements tested, 184 in view) | **194,000** | **2.61** |
| pieces handed on (218: 181 static, 37 blocks of the four cars), of it 121,700 waiting for the geometry unit's buffer | 544,500 | 7.33 |
| the computers' three steps (74,300 each + cars touching), after the last piece | 238,000 | 3.2 |
| wait for the rasterizer | 18,000 | 0.24 |
| **frame** | **1,090,300** | **14.7** |
| race text (after the frame's end, before the swap) | 29,200 | 0.39 |

The geometry unit was busy 522,000 and the rasterizer 411,000 cycles. Three things follow. (1) The frame is the CPU's path:
the computers' steps start when the last piece is handed on, and the geometry unit has 28,000 cycles left then, nothing to hide
them behind (the earlier simulation of this race with the steps before the scene, `RACE_OVERLAP` 0, was not slower: 13.8 / 14.6 ms;
the study's "the rasterizer is the bound" did not hold here). (2) Everything
before the first piece (here 289,900 cycles = 3.9 ms) adds one to one to every frame whose hardware is the bound: the geometry
unit starts when the first piece arrives. (3) The model's cull (24,198 + 150 per placement tested + 110 per piece in view) has
no term for the cells: with no visibility list the 843 cells of the map are all looked at (about 106 cycles each), which is
1.2 ms the model did not have.

Cycles by stretch of code (the profile build, so proportions: a car's step comes out at 74,000 there, 77,000 and 70,000 in the real
frames before and after the sine and cosine memo): tyres 47 % (four `tyre` 19 %, angles 8 %, corners 3.5 %, the road under the car 3.4 %,
the rest of it the prologue and a 604-byte copy that picolibc does by bytes), four looks at the ground 10 %, the ground step 16 %, the driver 15 %,
outputs 4.5 %. The library's sine, cosine and arctangent are called 40, 46 and 60 times for the four cars of a frame (picolibc's
sine and cosine compute in double precision, which this CPU does in software); the counters timed them at 17 % of a step,
with their own overhead in it.

### What was changed (all firmware; the track image and `make_track_s2.py` are untouched, `check_track_build_s2.sh` passes 22)

| Change | Measured (track 4's grid frame unless said) | Picture |
|---|---|---|
| **Cull by cells** (`track.c`): a cell carries the height range of its placements and its largest mesh reach; one whose sphere lies wholly outside a plane of the view (every placement in it would be refused by that test) is not walked; without a visibility list the cells of the camera's rows are found by their keys (two binary searches), the others by column; the nearest-placement statistic is gone | cull 194,000 -> 112,100 cycles (-82,000 = -1.1 ms); cells looked at 843 -> 252; placements tested 410 -> 231. On the six other tracks' grids the cull falls 0.05 to 0.76 ms (tracks 1 and 5: 0.6 and 0.8). Over a lap of each track (robot race, `check_cull_s2.sh`): placements tested -40 to -46 % on all seven tracks | **identical**: the sorted list of pieces (a hash) equal in all 5,300 frames of the seven tracks' races and road sweeps, and 35 poses (race robot and ride) equal byte for byte (`tools/s2/check_cull_s2.sh <commit>`) |
| **The hot code of the piece loop together** (`link_prog.ld`): tl_mark, hw_piece, face_eye, block_piece and draw, 7.7 KB one behind the other at the start of the program, checked under 8 KB at link time | 12.4 -> 10.8 ms mean (-1.6 ms) with the same code: the I-cache is 8 KB and direct mapped, and hw_piece, placed a multiple of 8 KB from the draw loop, threw its lines out on every piece. Padding of 32, 1000, 4000 bytes in front of all code: 12.4, 12.4, 12.8 ms; between main.c's code and the rest: 11.4 to 11.5 ms for 64, 160, 400, 2000 bytes | none |
| **Sine and cosine remembered** (`trigmemo.h`, in `s2car.c`): the last answers by the angle's bits; the same floats | `ph` 1.0 -> 0.9, `o` 3.2 -> 2.9 ms (a car step about 77,000 -> 70,000 cycles) | `check_car.sh`: output equal to the digit (only the library-call counts differ) |
| Smaller, exact: the 604-byte copy of the last state by words (picolibc copies by bytes); a car's blocks placed after the level-of-detail test (a car beyond 6,000 units is its body alone); `pal_faded` without the multiplications when not faded | palette 10,400 -> 5,800 cycles; the others a few thousand a frame | identical (35 poses) |
| **The computers' steps in slices** (`race_others_slice`, `RACE_OVERLAP` 2): one car's step, or the cars touching, whenever handing a piece to the geometry unit had to wait 1,200 cycles for room in its buffer (107 pieces), the rest after the last piece | in this frame no push waited that long (4 of 218 over 150 cycles), so nothing moves; with the trigger at 100 cycles the steps ran early, starved the geometry unit and the frame took 12.2 ms instead of 10.8: the trigger is "buffer full", not "waited a little" | the player's run is identical (speed, heading, place after 200 frames equal in the simulation); a computer's car was drawn from the step before or the one just taken (always the one before since 2026-11-05, driving.md) |
| `debug.txt`: the first line (128 bytes now) and the visit line end with the longest frame's parts: ` | a1.3 c1.4 s7.1 o3.4 w0.0 f873 p219 n77` (ms before the scene, in the cull, in the pieces' loop, in the computers' steps, waiting for the rasterizer; faces, pieces, node of the line the player was at) | | |

Tried and left out: the car physics compiled with `-Os` (7 KB less code, 0.7 ms **slower**), the slices triggered by any wait over
300 cycles (+1.4 ms, above), a build with the counters (the instrumented firmware is 10 to 15 % slower and also shifts the
hardware's timing: compare builds without them).

### Track by track (M, the simulation's grid frame of each track, old firmware -> new)

| Track | mean ms | worst ms | last frame ms | time to the first piece ms | cull ms | `ph` ms | `o` ms |
|---|---|---|---|---|---|---|---|
| 1 | 13.3 -> 10.4 | 13.9 -> 11.2 | 13.8 -> 10.8 | 3.2 -> 2.4 | 1.93 -> 1.30 | 1.0 -> 0.9 | 2.7 -> 2.5 |
| 2 | 12.3 -> 10.0 | 12.8 -> 10.8 | 12.6 -> 10.5 | 2.3 -> 2.0 | 0.99 -> 0.89 | 1.1 -> 0.9 | 3.3 -> 2.8 |
| 3 | 12.0 -> 9.7 | 12.9 -> 10.8 | 12.6 -> 10.4 | 2.4 -> 2.1 | 1.06 -> 0.98 | 1.0 -> 0.9 | 2.9 -> 2.5 |
| 4 | 14.3 -> 10.8 | 15.0 -> 11.6 | 14.7 -> 11.5 | 3.9 -> 2.6 | 2.61 -> 1.51 | 1.0 -> 0.9 | 3.2 -> 2.9 |
| 5 | 12.7 -> 9.9 | 13.3 -> 10.6 | 13.1 -> 10.5 | 3.2 -> 2.2 | 1.83 -> 1.07 | 1.0 -> 0.9 | 3.1 -> 2.9 |
| 6 | 11.5 -> 9.6 | 12.3 -> 10.4 | 12.2 -> 10.3 | 2.4 -> 2.1 | 0.93 -> 0.88 | 1.1 -> 0.9 | 3.2 -> 3.0 |
| 7 | 11.8 -> 10.3 | 12.4 -> 11.1 | 12.0 -> 10.8 | 2.0 -> 1.8 | 0.80 -> 0.69 | 1.0 -> 0.9 | 3.0 -> 2.7 |

(220 frames, 150 of them on the grid; the grid frame has four cars in view and is, on track 1, as heavy as the Pocket's worst
frames of the earlier tests: 13.3 against 13.6 to 15.0.)

### Forecast for the Pocket (D, with its limits)

The grid frame is CPU-bound, which is why it gains 1.2 to 3.5 ms. A frame bound by the hardware (the geometry unit or the
rasterizer: a wide view with 2,000 faces and 150,000 pixels) gains only what comes before its first piece: 0.2 to 1.3 ms
(the column "time to the first piece"), since the computers' steps are hidden behind the hardware there (slices, or the backlog).
The Pocket's worst frame on track 4 is of one of these two kinds, and the earlier tests do not say which; the new first line
will (below). So per track, the worst frame of a race:

| Track | worst now (Pocket M, else the model D) | new, if the worst frame is CPU-bound | new, if it is hardware-bound |
|---|---|---|---|
| 1 | 13.6 to 15.0 (M) | 10.6 to 12.0 | 12.8 to 14.2 |
| 2 | 11.3 to 15.1 (D) | 9.2 to 13.0 | 11.0 to 14.8 |
| 3 | 14.3 to 18.1 (D) | 12.1 to 15.9 | 14.0 to 17.8 |
| 4 | **19.6 (M)** | **16.4** | **18.3** |
| 5 | 13.4 to 17.2 (D) | 10.8 to 14.6 | 12.4 to 16.2 |
| 6 | 14.9 to 18.7 (D) | 13.0 to 16.8 | 14.6 to 18.4 |
| 7 | 13.5 to 17.3 (D) | 12.3 to 16.1 | 13.3 to 17.1 |

(D: the model's worst frame of a road sweep with the four cars in view, `tools/s2/perf_race.py ... pack` (13.4 to 14.9; track 2
11.3), to that plus 3.8 ms, the difference the Pocket showed on track 4 and not on track 1, where the model's 14.7 agrees with the
Pocket's 13.6 to 15.0. The Pocket's own numbers for tracks 2 to 7 are not known.) **On track 4 the target of 16 ms is not
reached by this change alone, in either case.** The mean falls from 14.5 to about 11 to 12 ms there.

### Open

- **Firmware, not done.** (1) The geometry unit starts at the first piece, 2.6 ms into the frame; emitting the farthest cells' pieces
  after a first, partial sort (cells carry a key range) would start it about 0.7 ms earlier in hardware-bound frames; the order of
  the pieces must stay the same (a bound per cell, checkable with `check_cull_s2.sh`). (2) The computers' steps by phase for all
  three cars (tyres of the three, then the looks...) instead of car by car: a step runs through some 40 KB of code with an 8 KB
  I-cache, so a car's step pays for most of it again; estimate 0.2 to 0.4 ms. (3) A cheaper step for computers far from the player
  and out of view (half the rate, the game's `DT` is a constant of the physics in 100 places): up to 0.7 ms on average, but it changes
  the cars' laps; it would need a lap-time comparison on the PC first. (4) The 12 trigonometric calls of a drawn car (`draw_model`)
  could share the memo with the physics: 0.1 ms.
- **Hardware (RTL, not built).** (a) A status bit or the fill level of the geometry unit's buffer (a register, a few ALMs): the
  slices could then run exactly when the buffer is deep, not after a long wait: 0.3 to 1 ms in frames between the two kinds.
  (b) A two-way I-cache (VexRiscv `wayCount = 2`, no more block RAM): the 0.9 ms of the layout above is what one conflict costs; the
  physics and the cull have more of them. Estimate 0.3 to 1 ms. (c) A CFU instruction for sine and cosine (the CPU has the
  interface): the library's calls are 8 to 17 % of a car step, at most 0.5 ms a frame for four cars; the numbers would no longer be
  libm's, so `check_car.sh` would have to be re-run against the reference. (d) The geometry unit's read cache 4 KB -> 16 KB
  and a queue between it and the clipper (section 5) lower the hardware's path, which the worst frames follow.

### Worst-case stress: the convoy with shadows and smoke (2026-11-10, D)

The road sweep with the four cars in view (`pack`, section 2) draws no shadows and no smoke, which the race adds (`s2fx.c`). `FX=1` in
`tools/s2/framestats.sh sweep` adds them: a shadow under each car and the player's car (the one the camera chases, the rear of the
convoy) with all 16 puffs of both smokes alive at its rear corners, as in a long brake: 20 more objects a frame (60 against 40 blocks of
objects). The computers' steps are in the model's constants (3 steps and the collisions after the last piece, or hidden), so the
convoy with full physics is this test's model frame. The firmware as it is now (the model's "new" column; the calibration logs of
sections 1 and 7 were gone, the constants of section 1 were used as they are written there, not fitted again):

| Track | mean ms without -> with FX | worst ms without -> with FX | frames over 16 ms with FX |
|---|---|---|---|
| 1 | 9.0 -> 10.9 | 13.4 -> 15.1 | 0 |
| 2 | 8.6 -> 10.6 | 10.5 -> 12.2 | 0 |
| 3 | 9.0 -> 10.8 | 13.5 -> 15.2 | 0 |
| 4 | 10.4 -> 11.6 | 14.3 -> 14.4 | 0 |
| 5 | 9.1 -> 10.6 | 13.1 -> 13.2 | 0 |
| 6 | 8.8 -> 10.9 | 13.4 -> 15.1 | 0 |
| 7 | 9.2 -> 10.2 | 13.1 -> 13.2 | 0 |

The effects cost 1 to 2 ms on the mean and turn the hardware-bound frames into CPU-bound ones (hardware-bound frames on track 4: 367
of 421 before, 106 with FX): the 20 objects are handed on by the CPU. The model counts every object block at the cost of a car's
block (6,300 cycles); a puff or a shadow is one small quad, so this is an upper bound. Track 4 with the far plane at 1.3 times
(the Pocket's test of FAR_SCALE 1.3): worst 16.2 -> 16.3 ms, 4 frames over 16 ms with FX. Adding the 3.8 ms by which the Pocket's
worst frame on track 4 lies above the model (section 7) gives 18 to 20 ms, which is where the Pocket's frames "just over 20 ms" lie.
So the stress does not explain them alone: the unexplained part of that difference is the thing to find. Not run in the SoC
simulation (it needs the cars placed and the smoke forced in the firmware); the model's puff cost is the part to check there first.

### What to read in `debug.txt` after the test

The first line (the last state when the core is quit) ends with the longest frame: ` | a.. c.. s.. o.. w.. f.. p.. n..`.
Expected on track 4 in the race: `avg` 11 to 11.5, `max` 16.4 to 18.3, `late0`, `fps50.0`; `o` of the visit line (` ph0.9/1.1 o2.9/3.4`) 2.9
and 0.9 ms. If the longest frame has `w` (the wait for the rasterizer) over 1.5 ms and `o` near 3, the hardware is its bound: the
computers' steps were hidden (slices) and only the cull and the time to the first piece can still be cut. If `w` is near 0 and
`a + c + s + o` is the frame, the CPU is: `s` (the pieces' loop; with cars it is 5 to 8 ms) is then the next thing to look at.
`n` is where on the line it happened (the player's node; the line of track 4 has 100 nodes). `f` and `p` say how big the scene was:
faces looked at (the model's 2,000 faces are its hardware-bound frames) and pieces handed on. Also check the race text: a computer's
car is drawn one step (20 ms) old, always (`road_race_scene` copies the poses; until 2026-11-05 a car made after its slice was a step
newer than one made before it, which jerked the car beside the player at the start, driving.md). `RACE_OVERLAP` 1 (all computers' steps after the last piece, as the
earlier build) and 0 (before the scene) remain as compile-time switches if the slices misbehave.

### Reproduce

`tools/fw.sh` (zero warnings); `tools/fw.sh PROF=1` (cycle counters, `build/firmware-prof`); `tools/fw.sh CODE_PAD=n` / `CODE_PAD2=n`
(layout tests); `tools/s2/framestats.sh build`, then `race <image> <label> <out.csv> 12000 10 <track>` and `sweep s2 ...`;
`tools/s2/check_cull_s2.sh <reference commit>`; `python -I tools/s2/perf_race.py <old stats dir> <new stats dir> build pack`; `FX=1 tools/s2/framestats.sh sweep s2 <image> <label> <out.csv> -400 4` (the convoy with shadows and the player's smoke; the image must be a current one: an image built before the effects block has no meshes for them and the run is the same as without FX); the
simulations: `tools/s2/sim_race.sh <dir with firmware.bin and program.bin> <image> <track> 220 <name>` (about 9 minutes
each, 6 at a time are fine; `build/gamedata/` as `tools/simcheck.sh` has it); the S2START_ROAD of the image builder does not move the
race's grid.

## Caveats

- The model is fitted on six frames at the S1 grid, all geometry-unit bound. Worst S2 frames have up to 7,600
  faces looked at (calibrated up to 4,200) and 220,000 pixels (133,000): expect +-10 % there. With the LOD
  the frames fall below the calibrated range instead (1,100 to 1,600 faces looked at): section 6.
- `A` (music, physics, preparation) is the S1 grid's 1.7 ms. S2's physics is new and heavier; it was 182,000
  clocks in the S2 simulation frame, and an S1 frame at speed took more than at the grid. Unmodelled; see the
  "physics +1.35 ms" column.
- Car positions and camera (S1's near chase) are assumptions, the view distances are the original's (section 6);
  shadows, smoke, props, sparks, night, fog, damage, the ghost car and the menu are not in the base case.
- Found on the way: the PC game renderer faults with the animation clock running at the road points tried on S2
  tracks 2, 5, 6, 7 (not on 1, 3, 4), probably because moving meshes with 60 to 100 frames are laid out
  differently (not checked); the sweeps draw frame 0. A correctness fix, not a speed one.
- Reproduce: `tools/fw.sh`; the S1 frames `SIM_TRACK=n tools/simcheck.sh tn` (six side by side, 8 minutes);
  the S2 images `tools/s2/make_track_s2.py <game folder> n build/s2/trackn.bin`; `tools/s2/framestats.sh build`
  and `start n`; all sweeps and the model `tools/s2/perf_sweeps.sh` (2 minutes). The S2 simulation frame:
  `S2START_ROAD=130` for track 4's image; the firmware with `MAX_PIECES` 4096, `MAX_OBJ` 1024, `MAX_BLOCKS` 16
  and the class word 0 (the patches of `render_s2.sh`, in a temporary copy of `track.c`); `SIM_QUICK=1
  SIM_IMAGE=<image> SIM_TRACK=4 tools/sim.sh soc ...` (6.5 minutes). Everything lands under `build/`; no game
  data is committed.
