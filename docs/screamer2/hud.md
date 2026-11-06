# Screamer 2's race screen and the views from the car

Built 2026-11-04 as `firmware/s2hud.c` / `s2hud.h` on the 2D toolkit (`s2gfx.c`, docs/screamer2/menus.md section 6). What the original draws was read from
`STARTL.EXE` (`tools/dis.sh -f`, flat addresses below; OpenScreamer's `docs/NOTES2.md` S2-5q, S2-5zc has the same screen read by hand, and
agrees) and from the game's files (`GFX/GAME1.DAT`: `GAMEL.SPR`, `GAMEL.FNT`; `DATA/LOCAL.DAT`: `GAME.TXT`; `DATA/MAPn.DAT`: `MAPnDIR.SPL`).
Sections 1 to 4 were not compared with the original running: positions, fonts, colours and rules come from its code. Section 5 (the views from the car) was.

## 1. What the original draws

The race screen is `0x45a4e` (called by the picture routine `0x2b873` after the scene), the instrument `0x46d42`. The game's views are numbered 1 EXTERNAL
(trackside), 2 INCAR, 3 BACK (the chase camera), 4 NEAR, 6 INSIDE (`GAME.TXT`); only 1 and 6 are told apart in the code read here (below); the views from
the car are section 5. In the chase camera it draws, at 320 x 200 and in this order (fonts are the numbers of `GAMEL.FNT`: 0 `font2l` 18 px, 1 `fnum2l` digits 27 px,
2 `fgearl` digits 10 px, 3 `font4l` 7 px, 4 `fnum3l` digits 18 px, 8 `font3_2l` 12 px; a colour is a ramp of the palette, `colour * 16 + shade`; "space"
is the engine's text spacing, `0xc67f0`):

| Element | Where | Made of | When (address) |
|---|---|---|---|
| the 1ST mark | over the car that leads, 10 left and 12 up of the point 500 above it as the scene projects it | sprite `0x26` (15 x 11) | arcade (mode bit 1), the car that leads is not the player's, 0 < depth <= 20,000 (`0x4962c`) |
| the dial | (238, 132) | sprite `0x1a` (72 x 62, with "Mph" and "GEAR") | every view but 1 (trackside: none) and 6 (the dashboard, `COCKn_L.PKC`), also after the finish (`0x46d42`) |
| speed | (267, 177) | `%03d`, font 2, colour 5, space 1: the car's `+0x500` x 1.513 (km/h) eased, / 1.6 when `GAME.TXT`'s unit word begins with M (here MPH) | every frame |
| gear | (299, 177) | `%d`, font 2, colour 4, space 2 (`+0xc7c`: 0 neutral) | |
| rev needle | pivot (271, 168), 30 long | three lines to one tip, colour `0x5b`; angle = revs / most revs x pi - 2 radians, eased (`0x4923d`) | |
| the bend's arrow | centred, y 50 | sprite `0x1b + n` (27 to 32: left, right, ...) | for 1 s when the car is within 8,000 units of the next node of `MAPnDIR.SPL` (56-byte nodes: x +4, y +8, arrow +0xc); the next node is then the node after, round the list (`0x456c8`) |
| the laurel | (270, 4) | sprite `0x72` (44 x 42) | the player is first |
| POSITION | right edge at x 312, y 2 | `GAME.TXT` "POSITION", font 3, colour 3, space -1 | with more than one car |
| place | (286, 11) | `%d`, font 1, colour 1, space 2 | |
| checkpoint gap | centred, y 35 | `+mm:ss:hh`, control byte 1, font 8, colour 3, space -1 | 4 s after the player gets further along the line than ever before, at each fifth of a lap, unless he leads: the second at which the car that led got to that checkpoint against his own time (`0x1ffcb`, `0x16789`) |
| LAP | (8, 2) | "LAP", font 3, colour 3, space -1 | until the player has finished (`0x45cfb`) |
| lap | (8, 11) | `%d:` (the colon of the figures' font is the stroke), font 1, colour 1, space 2: the lap (`+0x4ac` + 1, at most the laps) | |
| laps | (width of the lap + 5, 20) | `%d`, font 4, colour 1, space 2 | |
| THIS LAP, TOTAL | (8, 56), (8, 72) with their clocks at (8, 64), (8, 80) | labels font 3 colour 3 space -1; clocks `mm:ss:hh` after the control byte 1, font 3, colour 1, space -1 (car `+0xc3c`, `+0xc38`) | |
| BEST LAP, BEST TOTAL | (270, 56), (270, 72), their times at (270, 64), (270, 80) | the track's records from the save (`--:--:--` for none); a lap that beats the player's best and the record puts it in at once (`0x201ee`) | |
| BEST LAP! | centred, y 15 | font 0, colour 1, space 2 | blinks (on while `int(2 t)` is odd) for 4 s after that lap |
| the bar | box (108, 4, 104 x 7) darkened, sprite `0x25` at (108, 2), marks y 3 | a mark (sprite `4 +` team, 8 x 8) for each other car at 156 - 50 d, d = (the player's distance along the line less the car's) / 200,000 within +-1, the player's (sprite 9) at 156 | with more than one car (`0x458c7`) |
| WRONG WAY | centred, y 15 | font 0, colour 1, space 2 | while the car's `+0x1f0` is -1 (turned 90 degrees from the road for a second), blinking: on in the second second of each two (`0x463ee`) |
| the start lights | centred, y 60 | sprite `0x42 + int(clock)`: 3, 2, 1, GO (`GAMEL.SPR` 66 to 69) | the race clock under 4 s (the cars go at 3) |

The numbers are `"%d"` and `"%02d:%02d:%02d"` (`0x37e1e`: minutes, seconds, hundredths, each cut off; 3599.999 is the empty record), the dial's eased
numbers `v = (v - target) x 0.75^n + target` for n pictures of 1/50 s (table row `0x32`, `0x315a4`). After the finish nothing is drawn but the dial, the
1ST mark and WRONG WAY; the end screen is `0x47040` (not here: docs/screamer2/menus.md). Not drawn in the chase camera: the radar (Screamer 1's); the cockpit and the mirror's frame
come with their views (section 5).

## 2. What was built

| File | |
|---|---|
| `firmware/s2hud.c`, `s2hud.h` | `s2hud_init(z)` (the set and the texts), `s2hud_begin(track)` (a race starts: the records from `s2_best_get`, `MAPnDIR.SPL`, every timer), `s2hud_draw(cam, view, steps, top)` (one frame: update the timers and easing from the car records, build one blitter list of about 90 commands, 46 of them copied from a list made when `top` or a record changes, hand it to the blitter) |
| `firmware/s2gfx.c` | `s2gfx_game_init` and `s2gfx_game_sprite / _text` (and `_centred`): `GAMEL.SPR` and `GAMEL.FNT` from `GFX/GAME1.DAT`, in 0.25 MB at `0x10C0_0000` (docs/soc.md); the menu's calls are unchanged |
| `firmware/race.c` | `race_line_nodes()` (the checkpoints are the fifths of a lap) |
| `firmware/viewer.c` | `s2hud_init` once, `s2hud_begin` in `race_start`, `s2hud_draw` right after the last piece of the scene, the old console text only where there is no set; the finish text and the timing overlay as before |

The screen's state is not in the race: it reads the four cars' records (`race_car(i)->rec`, the game's layout: places `+0xc30`, progress `+0x4b8`, `+0x4bc`, times
`+0xc38`, `+0xc3c`, best lap `+0xc40`, speed `+0x500`, revs `+0xc78`, most revs `+0x60`, gear `+0xc7c`, wrong way `+0x1f0`, laps `+0x4ac`, flags `+0x4c`)
and `race_get_hud()`. The checkpoints and BEST LAP! are noticed from the change of those words between frames. Formatting is done by hand (no library calls).

**Cost** (Verilator simulation, track 4, 220 frames from the grid, `tools/s2/sim_race.sh`, 2026-11-05). The list is handed to the blitter right after the last piece of the scene,
as Screamer 1's race screen was: the blitter starts it when the 3D units are done and draws it (about 22,000 clocks) while the CPU runs the computers' steps, so only the CPU's part
counts: the update 2,500 cycles, the list 18,200 and the hand-over of its 90 commands 4,200 (counters of a build made for this, not kept), about 25,000 cycles = 0.33 ms. Before the commands
that do not change were kept (46 of the 90) the list took 22,400.

The frame statistic (`T4 drv ... avg / max`) ends when the rasterizer is done: it did not count the old text (drawn after it) and counts this screen's CPU part. **11.2 / 12.2 ms**
mean / worst, against 10.7 / 11.5 for main (eb6dbe8, built and simulated the same way: the numbers of the baseline given). What comes after that end (the timing overlay's two lines, and in
the old build the speed, lap, place and times as console text) took 29,200 cycles (0.39 ms) in the last frame of the baseline and 13,100 now: the whole frame costs 13,000 cycles
(0.18 ms) more in that frame, and like for like (the statistic plus that stretch) the mean went from 11.1 to 11.4 ms (+0.29 ms; the worst frames, 11.9 to 12.4). The first version, which made
the list after the rasterizer had finished and waited for the blitter before the overlay, left 55,800 cycles after the end (+0.36 ms): the early hand-over is what hides the blitter.
The code lies 112 to 312 bytes further on than main's (62 objects moved, `tools/s2/map_shift.py`); the I-cache is direct mapped, and moving the code after `main.o` by 64, 128 and 256 bytes
(`CODE_PAD2`) gives 11.3 / 12.2, 11.3 / 12.2 and 11.1 / 12.0 ms: a tenth of a millisecond either way, which no measurement here separates from the screen's own cost (none of it was tuned).
The program's stack rose from 4,800 to 5,116 bytes.

**The timing overlay.** The viewer's two lines (top left, `con_text`, up to 22 characters = 176 pixels wide, 16 rows) stay as they are, switched by X in the
race and B in the ride; the overlay is drawn after the screen. While it is on, the elements that would sit under it move down 16 rows: the lap block (LAP, the lap, the
laps), the bar, BEST LAP! and WRONG WAY, the checkpoint gap and the bend's arrow (the chain, so that none of them overlaps another); everything else
(POSITION and the place, the times, the dial, the lights, the 1ST mark) stays where the original has it. With the overlay off nothing moves. `tools/hud_screens.sh`
checks that no pixel of the overlay's box is drawn over when `top` is 16.

**The text routine.** The 2D toolkit's text (`s2gfx_text`) did not follow the engine's: STARTL.EXE's `0xc67f0` (draw) and `0xc6c88` (width) put `space`
between two characters only (no trailing space in a width), and after the control byte 1 a character sits in a cell of (the font header's fourth number - 2)
pixels, the first character's cell without the space, centred by an arithmetic shift of (cell - width). The old rule put every glyph of a fixed-pitch text (the clocks)
one pixel off and made every width one space too wide (centred menu texts sat half a space too far left). Now it is the engine's; `tools/ui_reference.py` renders by
the machine code's rule.

## 3. What differs from the original

- **The bar's darkened box** is a black box. The original darkens what is under it through a table (`0x51868`, a pixel-by-pixel loop on the CPU); the blitter has no
  such mode, the radar's way (a face through the see-through table) needs the SMG table, which the Screamer 2 viewer does not load yet, and per-pixel CPU work is out. When the
  race's see-through table exists (docs/screamer2/comparison.md), one more row (the darkening) and a quad as `efx_radar_piece()` did make it exact.
- The records of BEST LAP and BEST TOTAL are `s2_best_get()`'s (s2load.h): none until the save's best times are filled in (`--:--:--`); then a first lap on a track always
  beats the record and blinks BEST LAP!, as the original does for an empty list. BEST TOTAL is not updated at the finish (the screen has no texts then).
- The marker 1ST and the bend's arrows are placed by the viewer's camera (the original's projection is the same lens: 128 behind the camera, focal 128).
- The speed in miles is computed in single precision (the original's x87 division by 1.6 in double): a number can differ by one at a boundary.
- Only the arcade race with one player: no time attack (`mode & 4`: the ghost; no POSITION), free run (`mode & 8`: five split times instead of THIS LAP / TOTAL), split screen (`mode & 0x40`).
- The views from the car are section 5. Since menus row 10 view 1 (the replay's, and the end of every race: `s2hud_view()`) shows no
  dial, none of the texts and no WRONG WAY (`0x46fea`, `0x45d17`, `0x463fc`; the start lights, the leader's mark and the band stay), and the dial is that of the car the camera follows; the replay's band
  (`0x4658c`: the bottom 20 rows darkened, "TEAM - VIEW" at (10, 180) in font 0, colour 1, spacing 2) is `s2hud_band()`; `tools/hud_screens.sh` has 11 frames of them.

## 4. Checked

`tools/hud_screens.sh` (about 10 s; `tools/hud_reference.py`, `tools/host/hud_screen.c`): the firmware's own code over a made-up race (the car records `race.c` keeps), the
blitter's C model, over a picture of the repository's track 4 render; 62 frames (the four lights, the dial settling over frames, a checkpoint's gap and its 4 s,
speeds from 0 to 104 with their padding, the laurel, the times, a record beaten and BEST LAP! blinking through its 4 s, WRONG WAY through its phases, a bend's arrow and its
second, the next node's arrow, the 1ST mark with a straight and a turned camera, the finish, `top` 16) each equal in every pixel to the picture
`hud_reference.py` makes from the game's files and the rules above (its own readers of `GAMEL.SPR` / `.FNT`, the engine's text layout, Bresenham, and a model of the timers in single
precision); also on a ZIP whose `GAME1.DAT` is deflated. What is the same by construction: that the reference and the firmware were written from the same reading of the program;
what an independent look would add is the original running.

## 5. The views from the car (2026-11-06)

Read from `STARTL.EXE` (`tools/dis.sh -f`), held against the running original (`tools/s2/dosbox_cockpit.py`: the camera record and the player's car in every frame of a race with the
game's own keys, and 12 raw screenshots, twice: England with the first team's second car (dashboard model 1) and track 4 with the third team's bonus car (model 6); `tools/s2/check_cockpit_s2.sh`). Code: `firmware/s2cockpit.c` (the cameras and the dashboards' numbers, no hardware), `s2dash.c` (the cockpit
pictures as blitter boxes and the instruments), `s2cam.c` (the view and the car followed), `s2hud.c`, `viewer.c`, and `track.c`'s `track_draw_mirror`.

**What the original does.**

| | |
|---|---|
| the views | the camera record (`[0x1192f4]`, 0xaec bytes: `+0xae4` the view, `+0xadc` the car followed, `+0x48 +0x4c +0x50` x, height, z as whole numbers, `+0x54 +0x58 +0x5c` pitch, heading, roll, `+0x1c` the scene's rows) is made by `0x12dff` once a picture, `0x2b873`: 1 EXTERNAL (the trackside cameras, `0x13223`), 2 INCAR `0x13bab`, 3 BACK (the chase camera, `0x13f38`), 4 NEAR `0x14200`, 6 INSIDE `0x13cbd`; 5 is the mirror's own record (`[0x1192fc]`, `0x13e1a`) |
| keys (`0x132b4`) | F1 INCAR, F2 INSIDE, F3 NEAR, F4 BACK (F5 .. F8 the same for the second player); F9 (F10) EXTERNAL **in a replay only** (the mode word's bit 0x100); F11 the next car (`0x2d7c0`, replay only); M the mirror (`0x2d7c0`: scan code 0x32, once a press, not in a replay or the attract race). Not read in the attract race, once the player's car has done its laps (the camera record's flags are then 0xb, `0x201ee`: the trackside cameras follow his car) |
| the view a race starts in | the options' word `[0x130d68]` (`0x2e205`: 2, 3, 4 or 6, anything else 2; a new `CHOICE.DAT` has 2 = INCAR, `0x32dee`), which the race writes back where the player finishes and where he leaves it (`0x20469`, `0x2b64a`: not view 1, not in a replay). The mirror is off at the start of every race (`0x2e277`) |
| INCAR | the car's place and angles, 400 above its body: x, z the car's, height = the place's + the springs' mean (record `+0x4f4`) + the body block's (`+0x218`) + 400, heading = the car's, pitch = the car's + the body's lean (`+0xc18`), roll = the car's + half the lean's (`+0xc20`); 200 rows |
| INSIDE | the same 300 above the body; the rows are the car's dashboard row's first number halved (`CRUSC.DTR`, 127 to 143), the eye line 0.65 of them, so the scene stops above the dashboard |
| NEAR | at the car's POSIZIONE TELECAMERA (`CARS.DTR`: along -10 or 0, up 520 to 550): the two offsets (0, 0, along) and (0, up, 0) each turned by roll, by minus pitch and by heading (`0x105cd`), added to the body's place; the angles are the eased ones: pitch = half the car's pitch + lean - 0.3, roll = half the car's roll + lean, heading = the car's (+ the heading lean, always 0), by `0x1016b` at rows 0x3c, 0x3c and 0x50 of the easing table, snapped when the last picture was another view; 200 rows |
| BACK | the chase camera (`s2car.c`, checked earlier); the original snaps it when the view is entered, `s2car.c` keeps its own eased state (a stand-in, below) |
| what a view hides | INCAR, INSIDE and the mirror's pass take the car's object and its shadow's off the draw list (`0x2b6b4`, record `+0xbf0`, `+0xbf8`); its wheel smoke stays; every other car is drawn as ever |
| the mirror | `[0x11f0c4]` on and the view not 1: after the scene and the leader's mark `0x2b873` makes the third camera record: the place as INCAR, 400 above the body, heading + pi, pitch and roll the car's turned over (minus the car's, minus the lean), 35 rows, and `0x756b0` draws the scene from it into a work buffer, every block at level of detail 1 or lower (`[0xae6b4]`, read at `0x7f0fc`, `0x7f214`), then copies every other pixel of each row (2k, k = 0 .. 159) to x 240 - k of the screen's rows 14 .. 48: the glass is 160 x 35 at x 81 .. 240, the picture turned over left to right. `MIRROR_L.PKC` (a frame whose opening is exactly that box) is drawn by the instrument routine `0x46d42` over it |
| the instrument (`0x46d42`) | in every view but 1: the mirror's frame if the mirror is on; in INSIDE then the car's cockpit picture (`COCK<n>_L.PKC`, n = 1 .. 4 for a team's cars, 5 .. 8 the bonus cars of the teams; `0x518f8`, see-through where 0) and `0x46755`: the gear lamp (`GAMEL.SPR` `0x46 + 5 model + gear`, five a model) at the table's place halved, the speed needle at nought - (nought - most) speed / max (speed the dial's eased km/h) and the revs needle at the part of the most revs, each three lines of colour 12 to a tip len / 2 away (from the pivot and from a pixel either side at a right angle); no dial, speed or gear figures; in the other views the dial as before |

**Buttons on the Pocket.** The Pocket has no F keys; the manual ("Bleifuss 2", the German title) gives F1 "Vollbildschirm" (only the track: INCAR), F2 "Am Steuer" (with the dashboard: INSIDE), F3 "Wagendach" (the car's nose: NEAR),
F4 "Blick von hinten" (BACK), F9 and F11 in the replay only, and M for the mirror. The d-pad steers and nothing changes mode on it or on any steering input. In the race (owner's wish, 2026-11-06): **X** the next view
from the car in the order of the keys (INCAR, INSIDE, NEAR, BACK, INCAR, ...) as Screamer 1's X changes its camera, **Select** the mirror (M), **L and R together** the timing overlay; no combination with Select is left
(Select + X, Select + Y, Select + Start and Select + left / right of the first version are gone: the pause menu has RESTART and a menu chooses the track); in the replay (where Select ends it) **X** or **B** the next view
from the car, L the trackside cameras, R the chase camera, d-pad up the next car as before. Why a stray touch cannot do any harm: a view or the mirror is one press of a button that does nothing else in a race (the d-pad,
the gas, the brake, the handbrake and the gears are untouched), and neither changes the physics. The view is kept in the save (`S2S_VIEW`, kept as the original keeps it in its options) and is the next race's; **a new save and every save
made before this start in INCAR** (the original's default, the inside view without the dashboard: the owner confirmed it on a fresh install of the original, 2026-11-06; he had first remembered the
chase camera, which a player who once chose it keeps in the original too), one press of X away from BACK; the mirror is off at every race's start.
The in-game help (the viewer's text screen) has the line.

**What was built.**

| | |
|---|---|
| `s2cockpit.c` | the cameras of 2, 4, 5 and 6, the rows, the dashboards' numbers (`CRUSCOTTI` read by `s2_dtr_words`), the needles' lines and the lamp's place as `0x46755` works them in single precision |
| `s2dash.c` | at boot (`s2hud_init`) the eight cockpits and the mirror's frame from `GFX/GAME1.DAT` as blitter boxes (`tb_hud_picture`: each picture is one box of 24,000 to 28,000 bytes, 0 see-through) at `SDRAM_BASE + 0xC80000` (0.6 MB), the table at each race; the cockpit's draw is one box, the lamp's sprite and six lines |
| `s2cam.c` | views 2, 4 and 6 and `s2cam_next_view()`, the mirror's camera `s2cam_mirror()`; the camera is made once a picture from the followed car's record |
| `viewer.c` | the keys, the view from the save, the scene's rows (the clip's bottom: INSIDE's 127 to 143, the rest below it black under the cockpit), the car and its shadow off the list, `draw_mirror()` and the glass on the race screen |
| `track.c` `track_draw_mirror` | the pass: the camera's right-hand side reversed (the geometry unit's test of a face is the eye's side of its plane, no reflection changes it, the rasterizer takes either way round), the backdrop's panorama turned (a 128 KB copy made when first needed), the lens's middle three pixels to the right (so that the picture turned over about its middle, x to 319 - x, is the game's; measured), level of detail 1 at least |
| `s2fx.c` | `s2fx_hide_shadow`: the followed car's shadow is left out |

**Mirror: what it adds, and what is cut.** The original cuts the mirror's pass by the 35 rows (the eye line 22: the frustum is a slit, so the walk and the cull take what is in it) and the level of detail
of at least 1 (every block of the pass is drawn at level 1 or lower: cars lose wheels and hubs); both are in. Counted on the PC, `tools/s2/mirrorstats.sh` (the firmware's own `track_draw_mirror` and the C models of the
three units, perf counters, the race on every track with the game's driver driving all four cars, a frame every 10 steps from step 200, 280 frames a track; the shadows and the puffs are in neither pass):

| Track | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| main pass: pieces / faces / pixels drawn | 132 / 843 / 94,500 | 113 / 673 / 92,400 | 133 / 934 / 84,900 | 157 / 1,037 / 107,500 | 112 / 784 / 97,400 | 122 / 944 / 100,300 | 85 / 770 / 98,400 |
| the mirror adds: pieces / faces / pixels | 106 / 455 / 21,400 | 79 / 280 / 22,300 | 99 / 555 / 18,800 | 126 / 683 / 26,400 | 73 / 392 / 23,600 | 85 / 535 / 22,500 | 56 / 347 / 18,700 |
| share of the main pass: pieces / faces / pixels | 80 / 54 / 23 % | 70 / 42 / 24 % | 75 / 59 / 22 % | 81 / 66 / 25 % | 66 / 50 / 24 % | 69 / 57 / 22 % | 66 / 45 / 19 % |
| CPU of the pass by the cost model of `perf_race.py` (cull 25,000 + 126 a cell + 150 a placement tested + 110 a piece in view; 920 cycles a static piece, 6,300 a car's block) | 2.5 ms | 2.0 | 2.7 | 2.9 | 2.0 | 2.3 | 1.5 |

The CPU adds, outside the model, the glass's read-back (35 rows x 40 words of 2 loads and a store, about 35,000 cycles = 0.5 ms, estimated from the code) and the shadows' and puffs' objects made again (about 0.1 ms):
**+2 to +3.5 ms on a frame, with the mirror on, against the 6.7 ms the T4 race has free** (11.8 mean, 13.3 worst of 20); the mirror is off at every race's start and costs a test of one flag when it is off. The 128 KB panorama
turned is made once the first time the mirror is on (about 10 ms, one late frame). **Not timed on the Pocket or in the simulation** (`tools/s2/sim_race.sh` is the main session's); the counts are the PC's.

**Compared with the running original** (`python -I tools/s2/dosbox_cockpit.py 1` and `... 4 26 2 4`: 1,300 frames of a race and 12 raw screenshots each, the first recording England with the first team's second car, the second track 4 with the
third team's bonus car; the DOSBox this script starts, under the lock folder):
- the camera records, England / track 4: INCAR 274 / 276 frames equal to the unit (0.00 in x, z, height, heading, pitch, roll), INSIDE 273 / 272 frames equal and its rows equal to the table's (133 / 133), the mirror's own record 257 / 257 frames equal
  with its 35 rows, NEAR 387 / 385 frames within 1.2 / 1.3 units and 0.0016 / 0.0014 rad over the whole recording (the easing, 0.0003 rad in a step: the original's easing table is built for the frame rate it measures, base^(50 / pictures a second):
  DOSBox's step is 1.1 times 20 ms, so the exponent is 1.1, fitted from the recording and set in the test; the Pocket's 50 pictures a second give 1, which is what `s2ck_ease_power` is); the body block's height (record `+0x218`) is -1 and 0 for the two
  cars and the mesh's block `MV_mauto` has it (the other 18 cars' by the same rule, not recorded);
- the screenshots: the INSIDE cockpit picture, the gear lamp and both needles as the rules give them: the needles pixel for pixel (England 58, 68 and 65 of 65 pixels at speed 0, 164 and 159 km/h; track 4 60, 67 and 63), the lamps' 130 of 130 and 99 of 99, the
  pictures 99.85 to 99.93 % (the rest is the race screen's own elements over it); the mirror's frame in the shots with the mirror on 98.9 to 99.1 % (its top row is under the race screen's bar of the cars); the frame's opening is exactly 160 x 35 at (81, 14), the box the glass fills;
- the keys do what the code says: F1 .. F4 switch the view the next picture, M puts the mirror on and off, the mirror was off at the start;
- the firmware's own drawing of the 2D parts equals the same reading: the nine pictures' boxes (the pixels they draw: 22,648 to 27,084 a cockpit, 1,257 the frame), the 8 table rows and 40 lamp places, 336 needle positions (320 exact, 16 a pixel
  off at a rounding edge, none worse) in `tools/s2/check_cockpit_s2.sh`, and the glass's squeeze (`s2ck_glass`, the words of the framebuffer's top rows to the 160 x 35 strip) equal to the game's own loop (`0x756df`) ported line by line, over 5,600 pixels; each with a negative control that fails.
- the mirror's pass is the ordinary pass turned over about the picture's middle on all seven tracks (42 pictures, six cameras a track, 82 to 99 % of the pixels equal, the rest textures sampled on the other side of an edge and the panorama's seam; the backdrops alone are equal pixel for pixel where the window does not run past the end of a panorama row; `tools/s2/check_mirror_s2.sh`, with its negative controls).

**Stand-ins and left out.** The panorama's seam: the ordinary backdrop is one copy from the panorama with the row's stride, so a window that runs past the end of a row (about a third of the headings) shows the next row's start there; the turned panorama has its own seam at the other end of the window, so the sky differs there from a true turning over of the ordinary picture (a row's height of sky texture). The chase camera is not snapped on entering BACK (`s2car.c` keeps its eased state all along: the same picture to within the easing lag); the car's place is a float where the game's camera uses whole numbers (the owner's decision);
the mirror's pass renders into the back buffer's top rows and the CPU reads the glass back (the original has a work buffer; the rasterizer here only draws into the two screen buffers); the original's halving of the projection in the mirror's
visibility test (`0x7e9d2`) is not copied (the unit's cull is by the mirror's own 35 rows); the mirror shows the cars, their shadows and wheel smoke, the animated scenery and the obstacles (all objects, as the original); the low-memory rule (`[0x11f14c]`: INSIDE
of another car becomes INCAR) and the second player are not built; the view's saved word is the save's `S2S_VIEW` (no layout change); the dashboard of a replay's followed car is that car's (as the original, `[0x11f0c8]`). Not measured against the original: NEAR and INCAR pictures of the 3D scene
beyond the camera records (the scene's pixels are the renderer's, as in every view); the cockpit's cost on the Pocket.

## 6. Open

- The original's pictures of the whole race screen to compare pixel for pixel exist now for the views from the car's 2D parts only (above); the rest of section 1's screen was read from the code.
- The darkened box, the records from the save, the other kinds of game (above).
