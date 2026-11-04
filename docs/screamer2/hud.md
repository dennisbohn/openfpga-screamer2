# Screamer 2's race screen (chase camera)

Built 2026-11-04 as `firmware/s2hud.c` / `s2hud.h` on the 2D toolkit (`s2gfx.c`, docs/screamer2/menus.md section 6). What the original draws was read from
`STARTL.EXE` (`tools/dis.sh -f`, flat addresses below; OpenScreamer's `docs/NOTES2.md` S2-5q, S2-5zc has the same screen read by hand, and
agrees) and from the game's files (`GFX/GAME1.DAT`: `GAMEL.SPR`, `GAMEL.FNT`; `DATA/LOCAL.DAT`: `GAME.TXT`; `DATA/MAPn.DAT`: `MAPnDIR.SPL`).
Nothing was compared with the original running (no Screamer 2 DOSBox pictures exist here): positions, fonts, colours and rules come from its code.

## 1. What the original draws

The race screen is `0x45a4e` (called by the picture routine `0x2b873` after the scene), the instrument `0x46d42`. The game's views are numbered 1 EXTERNAL
(trackside), 2 INCAR, 3 BACK (the chase camera a race starts in), 4 NEAR, 6 INSIDE (`GAME.TXT`); only 1 and 6 are told apart in the code read here (below); the others get what
follows, a cockpit's picture (`COCKn_L.PKC`) being drawn by the scene routine, not read. In the chase camera it draws, at 320 x 200 and in this order (fonts are the numbers of `GAMEL.FNT`: 0 `font2l` 18 px, 1 `fnum2l` digits 27 px,
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
1ST mark and WRONG WAY; the end screen is `0x47040` (not here: docs/screamer2/menus.md). Not drawn in the chase camera: the radar (Screamer 1's), the
cockpits, the mirror.

## 2. What was built

| File | |
|---|---|
| `firmware/s2hud.c`, `s2hud.h` | `s2hud_init(z)` (the set and the texts), `s2hud_begin(track)` (a race starts: the records from `s2_best_get`, `MAPnDIR.SPL`, every timer), `s2hud_draw(cam, view, steps, top)` (one frame: update the timers and easing from the car records, build one blitter list of about 100 commands, hand it to the blitter) |
| `firmware/s2gfx.c` | `s2gfx_game_init` and `s2gfx_game_sprite / _text` (and `_centred`): `GAMEL.SPR` and `GAMEL.FNT` from `GFX/GAME1.DAT`, in 0.25 MB at `0x10C0_0000` (docs/soc.md); the menu's calls are unchanged |
| `firmware/race.c` | `race_line_nodes()` (the checkpoints are the fifths of a lap) |
| `firmware/viewer.c` | `s2hud_init` once, `s2hud_begin` in `race_start`, `s2hud_draw` after the rasterizer has finished, the old console text only where there is no set; the finish text and the timing overlay as before |

The screen's state is not in the race: it reads the four cars' records (`race_car(i)->rec`, the game's layout: places `+0xc30`, progress `+0x4b8`, `+0x4bc`, times
`+0xc38`, `+0xc3c`, best lap `+0xc40`, speed `+0x500`, revs `+0xc78`, most revs `+0x60`, gear `+0xc7c`, wrong way `+0x1f0`, laps `+0x4ac`, flags `+0x4c`)
and `race_get_hud()`. The checkpoints and BEST LAP! are noticed from the change of those words between frames. Formatting is done by hand (no library calls).

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
- Views other than the chase camera are not drawn (the viewer has none): view 1 shows none of it, view 6 the dashboard (`0x46755`: the needles of the two instruments from
  `CRUSC.DTR`'s section `COCKn` L, the gear lamp of `GAMEL.SPR` 70 to 109, over `COCKn_L.PKC` copied by `0x518f8`), and the replay's band (`0x4658c`).

## 4. Checked

`tools/hud_screens.sh` (about 10 s; `tools/hud_reference.py`, `tools/host/hud_screen.c`): the firmware's own code over a made-up race (the car records `race.c` keeps), the
blitter's C model, over a picture of the repository's track 4 render; 62 frames (the four lights, the dial settling over frames, a checkpoint's gap and its 4 s,
speeds from 0 to 104 with their padding, the laurel, the times, a record beaten and BEST LAP! blinking through its 4 s, WRONG WAY through its phases, a bend's arrow and its
second, the next node's arrow, the 1ST mark with a straight and a turned camera, the finish, `top` 16) each equal in every pixel to the picture
`hud_reference.py` makes from the game's files and the rules above (its own readers of `GAMEL.SPR` / `.FNT`, the engine's text layout, Bresenham, and a model of the timers in single
precision); also on a ZIP whose `GAME1.DAT` is deflated. What is the same by construction: that the reference and the firmware were written from the same reading of the program;
what an independent look would add is the original running.

## 5. Open

- The cockpit views (2 and 6), the dashboards and the rear-view mirror (`MIRROR_L.PKC`, a second pass of the scene into 160 x 35 rows) are not drawn: the viewer has only the chase camera.
- The original's pictures of the race screen, to compare pixel for pixel (DOSBox screenshots of `STARTL.EXE`), do not exist here.
- The darkened box, the records from the save, the other kinds of game (above).
