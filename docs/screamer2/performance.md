# Screamer 2 on our hardware: how many pictures a second?

Study of 2026-11-03 on branch `perf` (from `main`, 122da04). A cost model of one frame, fitted on measured
Screamer 1 (S1) frames and applied to the road of all seven S2 tracks. Nothing was run on the Pocket;
one S2 frame was run in the simulation as a check. Labels: **(M)** measured, **(D)** derived from
measured numbers, **(A)** assumed.

## Answer

Near chase camera, the player's car and 3 opponents in view (A), physics as on the S1 grid (M):

| S2 track | mean ms | worst ms | road within 20 ms | pictures/s mean / worst | with 9 opponents: mean ms, within 20 ms |
|---|---|---|---|---|---|
| 1 | 13.8 | 18.1 | 100 % | 50.0 / 50.0 | 16.3, 91 % |
| 2 | 15.3 | 20.4 | 99 % | 50.0 / 48.3 | 17.9, 76 % |
| 3 | 14.0 | 21.2 | 99 % | 50.0 / 45.8 | 16.5, 89 % |
| 4 | **19.0** | **24.6** | **64 %** | 48.4 / **37.5** | 21.5, 29 % |
| 5 | 14.6 | 20.6 | 99 % | 50.0 / 47.6 | 17.1, 88 % |
| 6 | 13.9 | 21.0 | 98 % | 50.0 / 46.2 | 16.3, 92 % |
| 7 | 15.8 | 22.5 | 95 % | 49.8 / 41.7 | 18.1, 68 % |

In words: tracks 1, 2, 3, 5, 6 hold 50 pictures/s nearly everywhere and dip to 46 to 48 at their
heaviest spots (track 1 never); track 7 dips to about 42; **track 4 is the problem**: about two thirds of
the road at 50, the rest at 40 to 48, the worst spot about 37. Crowded places cost more: the starting grid
(10 cars) is 20.7 to 21.8 ms on tracks 4, 6, 7 (43 to 47 pictures/s), 19.9 on track 2. The geometry unit is
the bottleneck in every worst frame. Uncertainty: the renderer part is good to about 5 % (below); the
unknown S2 physics can add 1 to 2 ms (track 4 would then be within 20 ms on only 41 % of its road). **The
rear-view mirror, as the original draws it, adds 7 to 12 ms and does not fit** (lever 1).

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
  60,000), placements out to 1.2 times that (OpenScreamer's rule); S1 keeps 43,808 and 52,000.

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
| S2 1 | 493 / 1,409 / 2,919 | 115 | 534 (704) | 93,353 | 0.152 | 7,459 |
| S2 2 | 545 / 1,572 / 3,296 | 140 | 660 (1,040) | 104,694 | 0.155 | 8,188 |
| S2 3 | 545 / 1,501 / 3,461 | 126 | 609 (912) | 85,396 | 0.184 | 7,569 |
| S2 4 | 826 / 2,265 / 3,399 | 179 | 761 (1,072) | 114,125 | 0.164 | 10,084 |
| S2 5 | 648 / 1,463 / 3,403 | 107 | 509 (768) | 124,450 | 0.119 | 6,068 |
| S2 6 | 678 / 1,500 / 2,828 | 117 | 537 (800) | 99,480 | 0.129 | 6,935 |
| S2 7 | 482 / 1,620 / 3,786 | 118 | 498 (768) | 102,776 | 0.241 | 6,660 |

S2 draws on average 17 % more faces than S1 (track 4: +64 %) and up to twice the texture misses per pixel.
Along the road (player + 3 opponents) S1 comes out at 10.9 to 12.5 ms mean, S2 at 13.8 to 19.0. A car costs
about 0.7 ms (the player's, +150 faces looked at) and 0.6 ms (each opponent, +230).

## 3. Forecast: more cases (D)

Mean ms along the road. Columns: cars in view (0 = scenery only, the study's measure; 1 = the player's; 4 = and
3 opponents; 10 = and 9), then variants of the 4-car case.

| Track | 0 | 1 | 4 | 10 | physics +1.35 ms | mirror (full view) | mirror, 15,000 view | S1 view distances | far plane x 0.8 | grid, 10 cars |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 11.2 | 11.9 | 13.8 | 16.3 | 15.2 | 21.3 | 17.7 | 13.0 | 12.5 | 17.9 |
| 2 | 12.6 | 13.3 | 15.3 | 17.9 | 16.7 | 23.9 | 19.2 | 13.9 | 13.2 | 19.9 |
| 3 | 11.5 | 12.1 | 14.0 | 16.5 | 15.3 | 21.4 | 17.8 | 13.1 | 12.6 | 16.8 |
| 4 | 16.1 | 16.9 | 19.0 | 21.5 | 20.3 | 30.6 | 23.3 | 15.8 | 16.7 | 21.8 |
| 5 | 11.8 | 12.6 | 14.6 | 17.1 | 15.9 | 22.1 | 18.4 | 13.5 | 13.0 | 17.6 |
| 6 | 11.3 | 12.0 | 13.9 | 16.3 | 15.3 | 21.3 | 17.7 | 13.0 | 12.4 | 20.7 |
| 7 | 13.3 | 14.0 | 15.8 | 18.1 | 17.1 | 22.6 | 19.5 | 14.5 | 13.8 | 20.7 |

Worst frame (4 cars), ms: before the cull 1.7, cull 1.6 to 2.4, GU cache misses 4.7 to 7.0, GU faces 5.7
to 7.7, GU waiting for the units behind it 3.7 to 7.1, tail 0.6; the RS is busy 7.6 to 14.8 ms of the frame (track 7:
42,977 misses). The CPU path is never the longest (75 to 83 % of the frame on S1).

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
| (a) cull with up to 3,742 placements | track 4: 761 tested on average (max 1,072; S1 max 928), cull 2.1 ms mean, 2.9 ms worst, against 1.4 to 2.3 ms on S1. Without the group skip it would be 7.6 ms | counters, 150 clocks per placement tested (M), skip works because the file keeps a cell's placements together | high |
| (b) rear-view mirror | second pass of the scene into 320 x 35 rows: 70 to 100 % of the pieces and faces of the main pass, 23 to 36 % of its pixels: **+6.8 to +11.6 ms** (full view), +3.7 to +4.3 ms with a 15,000-unit view | counters of a backward camera at the car, NOTES2 S2-5zh; model | medium (cull cost of a 2nd pass assumed to hide behind the GU backlog) |
| (c) fade by depth (`.DPT`) | 760 to 1,070 faces a frame of 1,800 to 2,700 (night and fog alike) draw with another row than the face before; if each change fetches a 256-byte row (8 lines, 17 clocks each) that is up to +1.4 to +2.0 ms, nothing if hidden. Day frames: none. GU part: 3 adds per face | row rule of NOTES2 S2-5d on the faces' depths; the rows cannot stay in block RAM (64 KB table, 16 M10K free) | low |
| (d) 112-row see-through table, damage | no clocks; +8 to +16 M10K (16 free) competes with lever 4. Smoke and shadows: 2 clocks a pixel, S1 shadows are in the calibration; wheelspin smoke at most about +1 ms while it lasts. Damage = another texture picture, no cost | `comparison.md`; smoke area assumed | medium / low |
| (e) texture working set | S2 files are smaller (MLB 1.05 to 1.95 MB) but touch more distinct lines per frame (6,100 to 10,100 against 3,900 to 6,400) and miss up to twice as often per pixel. Not conflicts: a 16 KB two-way cache changes the misses by -1 to +2 %, a 32 KB direct-mapped one saves 13 to 22 % | cache models on the real texel addresses, checked against the hardware counter | high |

## 5. What would raise the picture rate on a slow track (track 4)

1. **Rear-view mirror: leave it out, or a short view plus a tighter cull.** +7 to +12 ms at full view, +4 ms at
   15,000 units; both still leave track 4 above 20 ms. Firmware only, small.
2. **Far plane x 0.8 (the original's MID detail).** -1.3 to -2.3 ms (track 4: 19.0 to 16.7). One constant a
   track; visible pop-in. Firmware only.
3. **Group-level view test in the cull.** 800 placements tested, about 200 in view: the other 600 cost 90,000
   clocks. A box-against-planes test per group of 16 should save 0.5 to 1 ms on track 4 (estimate).
   Firmware only, small.
4. **GU read cache 4 KB to 16 KB.** Cache-miss clocks are 5 to 7 ms of the worst frames; the C-model study of
   `architecture.md` has 27 % fewer misses at 16 KB: -1.3 to -1.9 ms. RTL, 12 of the 16 free M10K, which the wider
   see-through table also wants.
5. **A short queue between geometry unit and clipper.** The GU waits for the units behind it 3.7 to 7.1 ms in
   the worst frames (fitted term); a few polygons of buffering may recover 1 to 3 ms (estimate, not measured).
   RTL, medium.

## Caveats

- The model is fitted on six frames at the S1 grid, all geometry-unit bound. Worst S2 frames have up to 7,600
  faces looked at (calibrated up to 4,200) and 220,000 pixels (133,000): expect +-10 % there.
- `A` (music, physics, preparation) is the S1 grid's 1.7 ms. S2's physics is new and heavier; it was 182,000
  clocks in the S2 simulation frame, and an S1 frame at speed took more than at the grid. Unmodelled; see the
  "physics +1.35 ms" column.
- Car positions, camera (S1's near chase) and view distances are assumptions; shadows, smoke, props, sparks,
  night, fog, damage, the ghost car and the menu are not in the base case.
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
