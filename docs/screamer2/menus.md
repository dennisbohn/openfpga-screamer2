# Screamer 2's menus: the original's screens, what they are made of, what the core can do

Study and plan (2026-11-04); built so far: the start menu (`firmware/s2menu.c`, docs/development.md "Start menu") and rows 1 to 3 of section 7, the 2D
toolkit with the LOADING screen and the pause menu, the save, the end of the race with the best times, and the GAME SETTINGS and HI SCORES pages
(section 6). Sources:
OpenScreamer's `docs/NOTES2.md` (S2-5zg, S2-5zk to S2-5zq, S2-5r, S2-5s: the menu read from the decompiled program and
compared with DOSBox), `docs/screamer2/comparison.md` (the menu rows), the game's files (sizes below measured with `tools/dat_sizes.py -v`,
formats checked on the real files), the program `STARTL.EXE` (`tools/dis.sh -f`, `GAME_DIR` = the Screamer 2 folder), and Screamer 1's
firmware menus in this tree (`menu.c`, `champ.c`, `intro.c`, `hud.c`, `save.c`; still compiled, not reached by the Screamer 2 viewer).
"Checked" below means read from the files or the program in this study; everything else is NOTES2's reading.

## 1. In short

- The original's menu is one loop, `while (page) page();` (`STARTL.EXE` 0x3855b to 0x3867c, checked: the page is an address in
  `0x132494`, every page sets the next and returns, 0 starts a race), over about twenty pages. They fall into four kinds: **2D pages**
  (a 320 x 200 `.BKK` picture, text and sprites), **pages with a few 3D objects** (team badges, letter tyres, track maps, plates,
  cars) over such a picture, **3D rooms** (the options room, the arcade page's scene: a scene built like a track, drawn by the track's
  engine) and **race overlays** (loading, pause, end of race).
- Nothing in it needs new hardware. The pictures are 8-bit 320 x 200 with their own palette: exactly the framebuffer,
  blitter copies. The 3D objects are meshes with a material library, which the geometry unit draws as the cars' and Screamer 1's menu
  models (`menu.c` did it for 6 maps, 14 cars and 26 letter cubes). The rooms are track images (`MAP1.MMP .UNR .MLB .BCK .COL`: the
  entries `track_build_s2()` already reads, under the name `MENUt1.DAT`), 2.4 to 3 MB of materials, about a hundred and fifty placed pieces at most (the `.MMP` is 10 KB of 64-byte records, a track's 100 to 400 KB): one room fits the
  track window and a frame of it should cost less than a race frame (not measured).
- **No data slots are needed**: since the game's files come from one ZIP (`firmware/zip.c`), the menu's twenty-odd containers are entries of the
  ZIP read on demand (the comparison table's slot count predates the ZIP). `ANIMS/` must be in the ZIP for the arcade page's scene (it
  is optional today). What does need a change is the **save**: the original's `CHOICE.DAT` is 33,244 bytes (best times, set-ups,
  leagues), the core's slot is 1,120 (`core/data.json` `size_maximum`: a JSON edit, not RTL).
- **Not feasible or not worth it**: the multiplayer pages (network, serial link, combat mode, two players on a split screen), the CD check
  ("PLEASE INSERT SCREAMER 2 DISK"), the 640 x 400 and 65K-colour variants. The pages' camera moves and fades are cheap.
- Proposed order (section 7): the 2D page toolkit with the loading screen and the pause menu (about 2 days, first), the end-of-race
  screen and game settings, the arcade pages (track pictures, transmission, the 3D car page), the team and name pages, then the
  championship, time attack with its ghost and the best-times table, the options room, the attract sequence, replay and car set-ups.
  Rows 1 to 6 (the complete arcade and the championship, 2D and small 3D) are about 13 days, the whole list about 29 (rough, plus or minus half:
  Screamer 1's code is the template for most rows; comparison.md's 16 to 25 days for the whole game counted the menus as one "large" row).

## 2. The flow

```
power-up -> CHOICE.DAT read -> menu loop at the ATTRACT page (0x3ca20)
ATTRACT (0x3c9ac): Virgin logo, AND, Milestone, PRESENTS, then an AI race on a track of 0..5 in turn (4 cars, trackside cameras
        changed every 10 s) under the SCREAMER 2 logo and the credits (ATTRACT.TXT), 87 s; any ENTER / ESC ends it.
        30 s idle on TEAM SELECTION starts it again.
   -> TEAM SELECTION (0x3ff37): four 3D badges turning (LOGO1), left/right, ENTER; ESC leaves the game
   -> ENTER YOUR NAME (same page, 0x1324c4 = 1): a wheel of letter tyres (ALETTER, 64 objects), the camera walks round, 3 letters
   -> OPTIONS ROOM (0x38d8d): the team's room (MENUt1.DAT), the camera moves between five stations (P_t.MCM), one per choice:
        ARCADE | CHAMPIONSHIP | TIME ATTACK | MULTIPLAYER | GAME SETTINGS
   ARCADE (0x3b674: ARCADE_L.BKK + the team's cars driving at the camera in a scene, ANIMS)
        -> TRACK page (0x4189f: TKG pictures, three shown, the chosen one lit and framed)
        -> CAR page (0x40a67: the team's cars on a carousel, the plate with name, top speed, drive; BKG_2)
        -> TRANSMISSION page (AUTOMATIC / MANUAL; BKG_6)
        -> LOADING (LOAD_n picture, BEST TIME, BEST LAP of the track) -> RACE -> END SCREEN -> back to the ARCADE page
   CHAMPIONSHIP: LEAGUE SELECTION (0x431b3, four leagues of 4, 5, 6, 6 races, NOT AVAILABLE until won) -> WELCOME TO LEAGUE n
        -> TRACK page with TERRAIN / DIFFICULTY and the 3D map (MAPS1) -> CAR page with SET-UP (Standard, Custom 1..4) and RACE / CUSTOMIZE
        -> (CUSTOMIZE 0x3e161: a turning 3D car and eight items) -> LOADING -> RACE -> END SCREEN with points -> LEAGUE REPORT
        (0x43914) -> next race; LEAVING LEAGUE? ARE YOU SURE? YES / NO
   TIME ATTACK: TRACK SELECTION (3D map) -> CAR page with 3 LAPS / FREE RUN -> RACE with the ghost car -> END SCREEN (TOTAL, LAP TIME,
        RACE AGAIN / EXIT, best times)
   MULTIPLAYER: MULTIPLAYER MENU, COMBAT MODE, NETWORK GAME, SERIAL LINK, split screen (mode 0x40)
   GAME SETTINGS (0x4228a): TRANSMISSION, DETAIL LEVEL, NUMBER OF LAPS (1/3/5/10), ARCADE ENEMIES SKILL, MULTIPLAYER ENEMIES, TIME ATTACK
        GHOST CAR, DAMAGES, MUSIC, VIEW HIGH SCORES -> HI SCORES TABLE (0x42719: total / lap, arcade / championship / time attack,
        1/3/5/10 laps, track, 15 rows, VIEW REPLAY)
in the race:  ESC = pause: RESUME | RESTART (not in the championship) | CD PLAYER | EXIT, over the darkened race (0x49f8e)
at its end:   4 s CONGRATULATIONS / place / total time, then REPLAY | RACE AGAIN | EXIT and the track's 15 best times (0x4720c, by the kind of game
              0x47636 arcade, 0x48041 championship, 0x48b4c time attack)
exit:         EXIT SCREAMER 2? YES / NO
```

The kind of game is the mode word `0x130d10` (1 arcade, 2 championship, 4 time attack, 8 free run, 0x40 two players, 0x80 attract, 0x100
replay), set by the room's choice in `0x339e7`; `0x339e7` also builds the race's record (cars, laps, level, set-ups), the page after a
race comes from it. Keys: the pages take only ENTER, ESC and the arrows (`0x38996`); with a controller accelerate is ENTER, brake ESC
(Screamer 1's menu on the Pocket: B or Start = ENTER, A or Select = ESC; the pages to come follow that, the start menu takes A as well as Start to go).

## 3. The screens and what each is made of

Class: **2D** = picture + text + sprites (blitter, console); **3D-o** = a few meshes through the geometry unit over a picture; **3D-r** =
a scene built like a track (the track's engine). Sizes are stored in the ZIP / unpacked (the containers are LZW inside).

| Screen | Made of (files) | Class | Notes |
|---|---|---|---|
| Boot logos, ATTRACT credits | `MENUL.SPR` sprites 0x6e (Virgin), 0x6f (Milestone), 0x71 (SCREAMER 2), `ATTRACT.TXT` (852 B), an AI race | 2D + race | the race is the existing one with every car on the game's driver and the trackside cameras (`CAMERASn.MCM`, kinds 0 to 4); Screamer 1's `intro.c` is the model |
| TEAM SELECTION | `LOGO1.DAT` (166 KB / 595 KB): `A1LOG-00 .. A4LOG-00.3DX` (4 badges, 6 to 15 KB), `LOGO.MLB` 560 KB, `LOGO.COL`; `logoL.BKK` (the S2 watermark) | 3D-o | four meshes turning (left/right turns the chosen one to the front) |
| ENTER YOUR NAME | `ALETTER.3DX` (2.6 KB, one letter tyre; the page loads 64 objects of it, a frame of `LOGO.MLB` each), `MENUL.FNT` | 3D-o | S1's `letters_step` / `letter_blocks` do the same with cubes; a plain letter row is the cheap stand-in |
| OPTIONS ROOM | `MENU<t>1.DAT` (806 to 925 KB / 2.75 to 3.3 MB): `MAP1.MLB` 2.4 to 3 MB, `MAP1.UNR` 0.3 MB, `MAP1.MMP` 10 KB (at most 150 piece records), `MAP1.BCK` 60 KB (backdrop), `MAP1.COL`, `ASHADOW.3DX`; `P_t.MCM` (252 B, five camera stations, also in `DATA/GEN`) | 3D-r | the entries are a track's (`MAPn.*`), the game loads it "as a track"; materials are routines 16 and 208 (shadows, the see-through table the race already has); one room per team, 4 |
| ARCADE page | `ARCADE_L.BKK` (S2 ARCADE title over a blue grid, 6 KB), `ANIMS/SCENEnn1.DAT` (4.2 MB / 8.1 MB: four scenes `MAP1..4.*` of 1.6 MB materials each, `SPLINES.PTH`), `ANIMS/SCCAR<t>1.DAT` (209 KB / 730 KB: the team's five cars), `DATA/GEN` `SCENE0..3.PTH` (the four cars' paths), `ENVM<t>1.DAT` (4 KB) | 2D + 3D-r | a scene with four cars driving at the camera; one scene at a time is about 2 MB. The cheap stand-in: the picture alone |
| TRACK page (arcade) | `TKG_1..6L.BKK` (the track pictures, 34 to 40 KB each), `BKG_1L.BKK` (bg), `MENUL.SPR` sprites around 0x27 to 0x2e (the track names, NOTES2: sprite 0x27 + track; 125 to 185 x 27 to 110), arrows | 2D | the chosen picture is lit and framed. Track 7 (THE TUNNEL) is the last to open in the original (after league 4 with all four teams); it has a LOAD and a BKG picture but no `TKG_7`, so the page needs a stand-in for it |
| CAR page (arcade) | `CARS<t>01.DAT` (the team's cars `AUTO1..5` .MLB + .3DX), `ENVM<t>1.DAT` (windows), `PLATES1.DAT` (138 KB / 452 KB: `A1..A7PLATE.3DX` 5 KB each, `PLATES.MLB` 416 KB), `BKG_2L.BKK` (blue grid), `CARS.DTR` (max speed, grip) | 3D-o | the five cars round a carousel (S1's `car_blocks`, 4000 apart, camera glides), the plate with the name (the 8 names of `STRINGS.TXT`: RADIANCE, HORIZON, NEBULA, SPARK, BLACK CLAW, AFRODYTE, HORNET, THUNDER; which plate and name belongs to which of the 20 cars is not read), MAX SPEED / bars as `MENUL.SPR` bars |
| TRANSMISSION page | `BKG_6L.BKK`, text AUTOMATIC / MANUAL, `MENUL.FNT` | 2D | probably page `0x41fcf` (NOTES2 names it with `bkg_6`; the program loads `bkg_6` at 0x421aa and 0x3bfb9) |
| LOADING | `LOAD_1..7L.BKK` (28 to 41 KB), the track's banner (`MENUL.SPR` 0x13 + n - 1, 144 x 50), `LOADING...`, BEST TIME, BEST LAP (`STRINGS.TXT` lines 30 to 32), the track's record `--:--:-- MIL` | 2D | built, `firmware/s2load.c` (section 6); read from the page's code `STARTL.EXE` 0x34ede: banner centred at y 4, BEST TIME label (font 2) y 72 and its record (font 1) y 84, BEST LAP y 112 and 124, LOADING... (font 1) y 170, all centred, colour 1, spacing 2; the two records only for the kinds of game that have them (arcade, time attack, league); shown while the track builds (3 to 5 s) |
| TRACK SELECTION (time attack, league) | `MAPS1.DAT` (523 KB / 865 KB): `AMAP1..7-VS.3DX` (29 to 109 KB; the largest has four blocks, 377 vertices at most, 1,886 faces, GU_MAX_VERT is 512), `MAPS.MLB` 406 KB; `LEAG_0L.BKK`, TERRAIN / DIFFICULTY lines (`STRINGS.TXT`) | 3D-o | a turning 3D map of the track, see-through look from the material table; S1's `map_blocks` did this with six maps |
| LEAGUE SELECTION, WELCOME, LEAGUE REPORT | `LEAG_1..4L.BKK` (cups, 33 to 36 KB), `BKG_3L` (two black boxes in orange frames, loaded at 0x433b8 inside the league pages), `STRINGS.TXT` lines, big font, the four badges (3D in the original) | 2D (+3D-o) | the report lists place, badge, name (MIL / CPU), points; the badges can be the race screen's small team marks (what the PC port did) |
| CUSTOMIZE | the car (3D-o), `STRINGS.TXT` (HANDLING, BRAKES, TIRE TYPE, TIRE PRESSURE, FRONT / REAR SUSPENSIONS, SAVE, EXIT), the six numbers a set-up holds | 3D-o + 2D | the numbers go into the car's start record (`0x14e20`); whether `s2car.c`'s record takes them was not checked |
| GAME SETTINGS | `BKG_7L.BKK`, `STRINGS.TXT` (9 rows, 22 characters, values `:AUTO :MANUAL :LOW :MEDIUM :HIGH :1 :3 :5 :10 :ROOKIE :NORMAL :PRO :OFF :ON ...`) | 2D | built, `firmware/s2pages.c` (section 6); the start menu's rows are a subset of this page |
| HI SCORES TABLE | `HISCOREL.BKK` (40 KB), `STRINGS.TXT`, the lists from `CHOICE.DAT`, `MENUL.SPR`'s cars' side views | 2D | built, `firmware/s2pages.c`; clip box y 119 to 180, five rows visible of 15, VIEW REPLAY lit on rows that have a replay |
| Pause | `GAME.TXT` lines 34 to 37 (RESUME, RESTART, CD PLAYER, EXIT), the darkened race, `GAMEL.SPR` sprite 10 for the CD player | 2D | built (RESUME, RESTART, EXIT), `firmware/s2pause.c`; `STARTL.EXE` 0x49f8e: the items in font 0 (`GAMEL.FNT`'s, the same art as `MENUL.FNT`'s second font) colour 1 spaced 2, centred at y = 22 i - 11 n + 102, over the picture darkened once (the table the original builds for the palette: for each colour the nearest entry to 0.67 of it), a box (x 80, y 94 - 11 n, 160 x 22 n + 12) darkened again over them, the chosen item bright and blinking over it; the effects ease out, the CD plays on |
| End of the race | `GAME.TXT` (1ST .. 4TH, POSITION, CONGRATULATIONS !, TIME OVER, REPLAY, RACE AGAIN, EXIT, CONTINUE, BEST TOTAL TIMES, THIS LAP, TOTAL, BEST LAP!), `GAMEL.SPR`'s track banners, the best-times list with the cars' pictures | 2D over the race | built, `firmware/s2end.c` (section 6); the race screen's fonts (`GAMEL.FNT`) are the menu's fonts 1, 2 and 3 |
| Replay | the player's controls per 20 ms step | race | S1's `rec[]` / `RACE_REPLAY` (docs/architecture.md) |
| Credits | `ATTRACT.TXT` | 2D | in the attract sequence |

Not feasible here: MULTIPLAYER (network, serial link, combat mode, split screen), CD check, the 640 x 400 set (`*H.BKK`, 2.7 MB of it in
`GRAF1.DAT`), the 65K-colour containers (`*2`).

## 4. The formats (checked on the files)

- **`.BKK`** (the pictures of `GFX/GRAF1.DAT`, 28 of the 320 x 200 `L` set, stored 6 to 41 KB each, 881 KB in all): 24-byte header (`"V652"`, 256 colours,
  width, height, 64,000, 0), the **palette as 768 bytes of 6-bit values (0 to 63)**, then 64,000 raw pixels, 64,792 bytes in all. The palette
  needs `x4` (or `x255/63`) into `VID_PAL`; the pixels are the framebuffer, so a blitter copy (BLIT_COPY | BLIT_OPAQUE) draws it. The `H` set is the
  same at 640 x 400 (256,792 bytes): not used.
  Where the program loads them (flat addresses in `STARTL.EXE`, checked): `load_%c%c` 0x34f19 (the loading screen), `leag_%c%c` 0x3959c and
  0x43fe0, `bkg_4` 0x39989, `bkg_5` 0x3a915, `arcade_%c` 0x3b712, `bkg_6` 0x3bfb9 and 0x421aa, `tkg_%c%c` 0x3ee6b, `leag_0` 0x3f7d6,
  `logo%c` 0x3ffea (TEAM SELECTION), `bkg_2` 0x40c69 (the car page), `bkg_1` 0x41bcd (the track page), `bkg_7` 0x422bb, `hiscore%c` 0x4275f, `bkg_3` 0x433b8.
  Looked at (contact sheet): `LOGOL` and `BKG_1/4/6/7L` are the S2 watermark in other tints, `BKG_2/5L` blue grids, `ARCADE_L` the title,
  `TKG_nL` a track's picture with a column of name plates, `LOAD_nL` a landscape per track, `LEAG_nL` cups and trophies, `HISCOREL` the table frame.
- **`.SPR`, `.FNT`**: `MENUL.SPR` is Screamer 1's format (`firmware/tb_hud.c` header): 111 sprites (count, size, count x x y w h offset, the pixels; 446 KB
  unpacked, 145 KB stored; checked: the sizes add up). `MENUL.FNT` is not: Screamer 2's has a byte with the number of fonts (6, not 7) and per font
  a header of 32 bytes (height, first character, count, a fourth number, the picture's name), then per font count x (width, offset), an `i32` size
  and the pixels, two to a byte, high half first, rows of (width + 1) / 2 bytes (70 KB, 27 KB stored; checked: the sizes add up). Its fonts, by the number the
  original's text routine takes: 0 the titles (29 pixels), 1 and 4 the 18-pixel ones (`font2l`, `font2_2l`), 2 and 3 the 12-pixel (`font3l`, `font3_2l`), 5
  the small digits (7 pixels); characters 32 to 167. Font 1 is glyph for glyph `GAMEL.FNT`'s font 0 (the race screen's, `GFX/GAME1.DAT`).
  A font's colour is a ramp of 15 palette entries (colour * 16 + shade, shades 1 to 15): colour 0 grey and orange, 1 white, 2 gold, in every
  `.BKK` palette and every track's `MAPn.COL` alike. The sprites carry the logos, arrows, bars, the track banners (0x13 to 0x19, 144 x 50) and the
  cars' side views. The blitter draws sprites (BLIT_COPY, 0 see-through) and fonts (BLIT_FONT, shades).
- **Room, scene, map, badge, plate, letter**: the containers' entries are `.MLB` (materials and textures, as a track's), `.UNR` + `.MMP` + `.BCK`
  + `.COL` (a track's, for rooms and scenes), `.3DX` (a mesh with blocks, as the cars and props), `.MCM` (camera stations), `.PTH` (object paths,
  the `SPLINES.PTH` of a scene and `DATA/GEN`'s `SCENE0..3.PTH`). The firmware reads all of them already (`tb_*.c`, `s2car.c`, `track.c`) but the `.PTH` paths and the `.MCM` camera records (the race's trackside cameras
  are the same records, `CAMERASn.MCM`).
- **Text**: `DATA/LOCAL.DAT`'s `STRINGS.TXT` (2.6 KB, `*` lines are comments, "max chars n" tell the widths), `GAME.TXT`, `ATTRACT.TXT`; read by `s2menu_init()`
  for the start menu.

## 5. The core's path, screen by screen

- **Blitter + console (2D)**: copy a picture (0.9 ms: 64,000 pixels at one a clock), sprites, `BLIT_FONT` text, fills, lines. Palette fades are a per-frame palette
  write (`pal_faded()`), the darkened still is the radar's table (`efx_darken`). Everything Screamer 1's menu pages, loading screen, pause and end of race do.
- **Geometry unit (3D-o)**: a piece = a mesh + a matrix; a race frame hands it 200 or more pieces, the maps, plates, badges and cars of a menu page
  are 1 to 30. The menu's models use their own material file (`GU_MLB`), as Screamer 1's `menu.c` set it. A ring of 26 letter tyres is far below a race frame's pieces. The heaviest page is the track map of time attack and league (California's is
  four blocks, 2,599 vertices and about 1,900 faces by NOTES2, more than a 4-car race frame's 900, but flat colours and half of them back faces: measure it, a
  level of detail or half the frame rate is the fallback). Cars come from `s2teams.c` (the grid's four, or all five of a team, 1 MB).
- **Track engine (3D-r)**: `track_build_s2()` with the room's entries (`MAP1.*` of `MENUt1.DAT` instead of `GFX/MAPnH01.DAT` + `DATA/MAPn.DAT`: no road, no walls, no
  line, no scenery) and a camera from `P_t.MCM`; no physics. A scene of the arcade page also needs the object paths (`SPLINES.PTH`) and the cars' placement along
  them (`road.c` places cars by a road point; paths give position and heading per time). The frame cost is the number of faces in view: a room (150 pieces at most) is
  below a track's (400 to 1,100 faces); to be measured when built.
- **Memory**: a room or scene image is built in the track window (`0x1210_0000`, 7 MB; nothing else lives there between races), the 2D pictures (64 KB
  each) and `MENUL` (0.5 MB) in the SDRAM below the cars' blobs, the way `menu.c` placed them in Screamer 1 (`MENU_MEM`, 0.5 MB). Reading: a stored ZIP is read by
  range (`zip_read_range`: the container's directory and the entries a page needs, like `s2_cars_load()`), a deflated one whole (`GRAF1.DAT` 4.2 MB, `SCENEnn1.DAT`
  4.2 MB). At the Pocket's 1.2 MB/s and 3 MB/s for the LZW: a 2D page 35 KB read and 65 KB unpacked, about 50 ms; a room 0.8 MB read, 2.9 MB unpack, about 1.7 s; a scene's one `MAPn`
  (about 2 MB) and the team's cars about 2 s. Pages that must change at once (the 5 stations of the room) stay loaded.
- **Slots**: none needed for the menu (ZIP). Free: 24 of 32 (firmware, log, program, ZIP, music 2, CHOICE.DAT, ATTACK.DAT are 8).
- **Saves** (done, section 7.1): the original's `CHOICE.DAT` (33,244 bytes: name, volumes, view, settings, leagues and unlocks, set-ups 0x188 per team, best times: lists of 15 rows of time,
  name, team, model, replay number, per kind, track and length of race) did not fit the 1,120 bytes of the Screamer 1 layout the core's slot had (and the start menu borrowed
  fields of). `firmware/s2save.c` keeps a Screamer 2 layout of our own in the same slot, 19,348 bytes, with `core/data.json`'s `size_maximum` raised to 36,864
  (the Pocket writes back what the firmware keeps).
  The ghost (the original records 0x20 bytes a frame, 7,000 frames, per track) is re-simulated from the controls, which the physics' fixed 20 ms step allows: 4 bytes a step,
  3 laps about 60 KB a track, 7 tracks 0.4 MB in `ATTACK.DAT` (`ghost.c` of Screamer 1 is the model; its slot is 360 KB: raise it likewise).

## 6. What exists

**The start menu.** `s2menu.c` is the arcade flow in one list: track, team, car, laps, level, gearbox, race / watch, with the original's texts and the choices kept.
What it leaves out of the original's arcade: the pictures and 3D of the TRACK and CAR pages, the game's own names for the 20 cars,
the options room, every other kind of game. Its choices are kept in the save (section 7.1): the laps, level and gearbox rows are the GAME SETTINGS rows of the same name.
Two rows at its end, GAME SETTINGS and HI SCORES, open the pages of row 3 (`s2menu_run()` runs them and takes the settings back from the save).

**The 2D toolkit (row 1, built 2026-11-04).** Four small modules over the blitter, all in `firmware/`, tested on the PC (`tools/menu_screens.sh`):

| Module | What it does |
|---|---|
| `s2gfx.c` | `.BKK` pictures (`s2gfx_bkk`: 24-byte header, palette x4, 64,000 pixels, read from `GFX/GRAF1.DAT` of the ZIP by range, about 40 KB and 50 ms for a picture), `MENUL.SPR` sprites and `MENUL.FNT` fonts as the blitter's image tables, text with the original's spacing (`s2gfx_text`, 0x50f45), sprites (also centred, 0x333d9), the `LOCAL.DAT` lines, the original's darkening table and a darkened still of the screen; one frame = `s2gfx_begin()` (blitter list for the back buffer), the page's commands, `s2gfx_end(pal, fade)` (palette faded, swap) |
| `s2page.c` | the page runner: `struct s2_page` (picture, palette, fade in / out frames, and a frame, key and draw function) and `s2page_run()`, the loop of the original's pages (keys with repeat as the start menu has them, fade, draw, swap); the Pocket's buttons are Screamer 1's: B or Start = ENTER, A or Select = ESC (`S2PAGE_ENTER`, `S2PAGE_ESC`) |
| `s2load.c` | the LOADING screen; `s2_best_get` gives it the track's best total time and best lap, the first rows of the save's arcade lists (`--:--:-- MIL` for a track nobody has driven); `s2_time_text` is the original's time text (0x37e1e) |
| `s2pause.c` | the pause menu |
| `s2end.c` | the end of the race (below) |
| `s2pages.c` | the GAME SETTINGS and HI SCORES pages (below) |

*Added with row 3*: `s2gfx_clip()` (the original's two clip boxes, 0x51189 for text and 0x50f3c for sprites: rows only; the pages and the end screen narrow them for their lists),
`s2gfx_text_boxed()` (0x332f5: centred in a box), `s2gfx_dark_fb()` (the original's box darkening of the screen, 0x51868: in words of four pixels, so x and width lose their low two bits, a box at
x 30 is 28 to 288), `s2gfx_dark_after()` (the same applied once the blitter has drawn the page: for the page's text under the box), `s2gfx_begin_on()` / `s2gfx_flush()` (a list for a frame another
loop draws: the race), and the race screen's track banners. The end screen draws `GAMEL.SPR` 0x27 + track - 1, which is the loading screen's `MENUL.SPR` 0x13 + track - 1 but for the second: the
race screen's banner reads COLUMBIA, the menu's COLOMBIA (224 pixels differ). `s2gfx_init()` therefore unpacks `GFX/GAME1.DAT`'s `GAMEL.SPR` (197,668 bytes, 60 KB read, 0.1 s on the Pocket) to `0x10B0_0000`
and puts the seven banners into the image table behind the fonts (`s2gfx_banner(track)` gives the number; without GAME1.DAT in the ZIP it falls back to the loading screen's). The cars' side views
(`S2G_SPR_CAR` = `MENUL.SPR` 0x55 + 5 team + car) and the race screen's fonts 0, 7 and 8 (`GAMEL.FNT`) are, glyph for glyph and pixel for pixel, `MENUL.SPR`'s and `MENUL.FNT`'s fonts 1, 2 and 3
(checked), so nothing else is loaded.

*Memory* (SDRAM, docs/soc.md): the toolkit lives at `0x10A0_0000`, Screamer 1's menu region, 1 MB with 0.93 MB used (and `GAMEL.SPR` at +0x100000): two picture buffers (64,000 bytes each: the page's picture,
the pause's still), 160 KB for an entry's stored bytes, 72 KB for one unpacked, then `MENUL.SPR` as the file is (446 KB, the sprites' pixels are read from it) and the fonts'
glyphs one byte a pixel (121 KB); the tables of 937 images are 11 KB of `.bss`. A deflated ZIP cannot be read by range: its `GRAF1.DAT` is inflated whole once, at
`0x1100_0000` (4.2 MB; the files screen's read test region, free by then), tested on a deflated ZIP made on the fly. The track window, the cars, `SOUND.DAT` and the music ring are not touched.

*Sounds.* The menu's click sounds are not in `GAME.DTR`'s 33: `MENU.DTR` is a second table of `DATA/SOUND.DAT` (the container the race's sound is read from, already
in memory at `0x11C0_0000`), 15 samples `MENU1.RAW` .. `MENU15.RAW` with ids 0 to 14 (11,025 Hz, two at 22,050; the original loads them with its menu pages, 0xafc70).
`s2snd_parse_menu()` reads the table with the race's rules, `s2sound_menu(id)` plays one on voice 10. Which sample a page plays is the page's own business; read from the
code: the TRANSMISSION page 0 (MENU1) for a cursor move and 2 (MENU3) for ENTER, the GAME SETTINGS page 10 (MENU11) for a cursor move, 12 (MENU13) when ENTER steps a value and 13 (MENU14)
for VIEW HIGH SCORES (ESC is silent); the HI SCORES page and the end of the race play none (`S2PAGE_SND_*`; an earlier reading of this page's sounds was of another page's code). The original's pause is silent (no `MENU.DTR` in a race); ours clicks (`PAUSE_CLICKS` in `s2pause.c`).

**The LOADING screen.** `viewer_run()` puts it up before `load_track()` (the track image build, the cars, 3 to 5 s) and keeps it through the race's set-up (`race_init`, the music's first
fill); `visit()` then fades it out in 0.4 s. The display is driven by the hardware from the front framebuffer and its palette, so nothing has to be done while the CPU is busy: the
picture is blitted into the back buffer, the palette written and the buffers swapped once, and it stays (nothing in the build writes the framebuffers). It covers the whole load: the
picture itself takes about 60 ms to put up (35 ms in the simulation; the log has `ui: loading screen up after N ms`), the load line of the log (`T1 load N ms`, 4.5 s for track 4 on the Pocket, plus `race init` 0.16 s) says how long it then stays. Without the
picture (no `GRAF1.DAT`, damaged) the old text screen is shown. The simulation has none.

**The pause menu.** Start in the race opens it (not before the first frame, not once the player has finished): RESUME, RESTART (the race again from the grid, the track stays
loaded) and EXIT (the start menu), over the darkened race. Up / down choose, B or Start take the item, A or Select resume; Start twice resumes. The race clock does not run (the
race loop stops; the first frame after it takes one 20 ms step), the effects ease out over a second and come back whole, the music plays on. Select + B back to the menu stays only
where the toolkit is not up. Start no longer switches to the ride; the ride is the start menu's WATCH.

**The end of the race** (`firmware/s2end.c`, `STARTL.EXE` 0x4720c, the arcade's 0x47636). When the player's car has done its laps it is the driver's and slows to a stop; the race goes on behind the
screen, and the viewer calls `s2end_frame()` once a frame after the rasterizer is done (the CPU darkens part of what the race drew and one blitter list draws the rest, about as much as the pause's
list; nothing is added to a frame of the race before the finish). The clocks run by the video frame counter (50 a second); all positions are the original's, read from its code:

| Time since the finish | What is drawn (font 0 of the race screen = the menu's font 1, colour 1, spacing 2, centred; the others the menu's fonts 2 and 3) |
|---|---|
| 0 to 4 s | `CONGRATULATIONS !` at y 70 if first, `1ST POSITION` (the place's word and `POSITION`) at y 100, the total time (`MM:SS:CC`) at y 130; the keys do nothing |
| from 4 s | the track's banner (sprite 0x27 + track - 1 of `GAMEL.SPR`) at y 4; the band y 180 to 200 and the box x 28 to 288, y 78 to 142 darkened once through the table of the race's palette (the pause's table); `RACE AGAIN` and `EXIT` in halves of the band at y 184 (the lit one in font 3, colour 3, blinking with the original's clock bit 0x20, lit again when it moves); the list of the 15 best times of the race's length: rows `N. NAM      MM:SS:CC` (fixed pitch) at x 50, 12 apart, the car's side view at x 134, the clip rows 81 to 140 (sprites 80 to 140), the player's row in font 3 colour 3 |
| 4.5 s on | the list moves up four rows a second until the player's row is the third in view (row - 2, at most the 11th at the top) |

The best times are put in when the choices come up (the original's call at that moment, 0x353cb): the total time into the list of the race's length (arcade), the best lap into the lap list, the player's
name (`s2save_name()`), team and car, no replay; a time slower than the fifteenth goes nowhere (no row is lit, the list stays at the top). Only the human car is entered, and only if it finished
(a race left by the pause or from the menu enters nothing). The original's REPLAY is left out (it appears in thirds with RACE AGAIN and EXIT once there is a replay; without one the two stand in halves,
as here). The keys: d-pad left / right choose, B or Start take (the original's ENTER; only a new press counts, so the gas held across the line does nothing), A and Select do nothing (as the original's ESC).
RACE AGAIN is the race from the grid again (like the pause's RESTART), EXIT the start menu. The music fades out over 3 s from the finish as before. The log has `end: T4 place 1 01:25:50 rank 7` for the first
three races of a run (the rank is the row, 0 if none). The end screen is only up when the toolkit is (`ui_ready` of the viewer); without it the old FINISH text and A / B are used.

**GAME SETTINGS** (`firmware/s2pages.c`, `s2_page_settings()`, 0x4228a; `BKG_7L.BKK`). The title at y 4 (font 0), the nine rows from y 48 in steps of 14: label at x 10, value at x 200 (font 2, colour 1; the
values are the texts `:AUTO` ... of `STRINGS.TXT`), the value of the row under the cursor in font 3 colour 3 while the original's clock bit 0x20 is clear (the colon drawn again over it in font 2), VIEW HIGH SCORES
lit as a whole. The original's keys: up / down move (no wrap, a click of id 10), ENTER steps the row's value round (id 12; VIEW HIGH SCORES: id 13 and the HI SCORES page), ESC leaves (the screen fades to black
over 0.5 s; it comes in whole). Here B or Start are ENTER, A or Select ESC, and left / right also step the value back and on (the original ignores them). The value goes into the save at once
(`s2save_set`); the start menu reads the save again when the page returns. Which of the eight act: TRANSMISSION, NUMBER OF LAPS and ARCADE ENEMIES SKILL (the start menu's rows, `apply_cfg()` in `viewer.c`),
DAMAGES (`s2car_damages`: off, a knock from a wall marks nothing and hurts nothing, as the game's 0x19aaa; **off is the save's default, as in the game, so the damage marks of earlier builds need DAMAGES ON**) and
MUSIC (`music_on`: off, no song in the race). **DETAIL LEVEL, MULTIPLAYER ENEMIES and TIME ATTACK GHOST CAR only keep their value** (the core draws the H sets of the tracks and has no ghost or multiplayer yet);
the original's MUSIC row also starts and stops the menu's CD track, which this core does not play.

**HI SCORES TABLE** (`s2_page_hiscores()`, 0x42719; `HISCOREL.BKK`). The title at y 4; four boxes of 160 pixels (text centred in them, font 2 colour 1; the chosen one in font 3 colour 3 while the clock bit 0x20 is set):
TOTAL TIME / LAP TIME and the kind (ARCADE, CHAMPIONSHIP, TIME ATTACK) at y 44, the length (1 LAP .. 10 LAPS) and the track at y 65, VIEW REPLAY centred at y 85; below, the list of 15 rows at y 120 + 12 n: the rows as at the end
of the race, the chosen row in font 3 colour 3, clip rows 121 to 180 (sprites 119 to 180), so five rows show; the list starts at the chosen row - 2 (the top for the first three, the 11th for the last three). The keys: up / down
the row, left / right the box (lit again when it moves), ENTER (B or Start) steps the value of the chosen box round (the list's type, the kind, the length (only for arcade and championship totals: time attack has only the 3-lap list),
the track in the order they open: England, Egypt, California, Finland, Switzerland, Columbia, The Tunnel; the unlocked ones), ESC (A or Select) leaves back to GAME SETTINGS or the start menu. The page is silent. VIEW REPLAY is
dark (its box 0x21, 0x51, 0x102 x 0x12 through the darkening table three times, over the text) unless the chosen row has a replay number: the save keeps them (`s2save_set_replay`), nothing writes one yet and ENTER
on it does nothing. The four choices are kept while the power is on only (the original keeps them in its settings block); the first visit shows the arcade totals of the laps and the track the start menu is set to.

What the start menu still leaves out of the original: see the list above; the other pages follow in rows 4 to 6.

## 7. Build order and rough costs (days of work, one developer; the PC-side tests of each step as for the start menu)

| # | Step | Needs | Cost |
|---|---|---|---|
| 1 | **done 2026-11-04** (section 6). **2D toolkit**: `.BKK` loader (header, palette x4, copy), `MENUL.SPR` / `.FNT` loader into the blitter's image tables (Screamer 1's `menu_init` and `tb_hud.c` as templates), a page runner (keys with repeat, fade in/out, the menu click sounds from `SOUND.DAT`'s `MENU.DTR`); the **LOADING screen** (`LOAD_nL` + LOADING + BEST TIME / BEST LAP) and the **pause menu** (RESUME, RESTART, EXIT over the darkened race, replacing Select + B) | zip range reads of `GRAF1.DAT`; the existing blitter | 2 |
| 2 | **The save** (done, 7.1): a Screamer 2 layout for `CHOICE.DAT` (settings, volumes, unlocks, set-ups, best times; the start menu's fields moved into it, the old layout is migrated once), `data.json` size 36,864 | `core/data.json` | 1 |
| 3 | **done 2026-11-04** (section 6). **End of race**: the 4 s CONGRATULATIONS / place / time screen, then RACE AGAIN / EXIT (REPLAY later) and the track's best times (a table of 15 in the new save); GAME SETTINGS and HI SCORES pages on the same toolkit | 2 | 2 |
| 4 | **Arcade pages**: TRACK page (`TKG`, banners, lit frame), TRANSMISSION page, the arcade title, the page chain with ESC going back; the start menu becomes the stand-in for the pages that do not exist yet | 1 | 1.5 |
| 5 | **3D-o pages**: the CAR page (carousel of the team's cars with the plate: the 20 cars' names to read), TEAM SELECTION (four badges), ENTER YOUR NAME (letter tyres), the track maps for time attack and league | geometry unit pieces with a page MLB; `s2teams.c`; `LOGO1`, `PLATES1`, `MAPS1` read by range | 4 |
| 6 | **Championship**: leagues, points (10 / 6 / 3 / 0), unlocks, LEAGUE SELECTION / WELCOME / REPORT, the level per league, weather per race (our core has day only: skip), LEAVING LEAGUE | 2, 4, 5 | 3 |
| 7 | **Time attack**: the ghost (recorded controls, second car re-simulated), FREE RUN with the five splits, best times | 2, 3; `race.c` second player-car | 3 to 4 |
| 8 | **Options room**: the track builder for `MENUt1.DAT`, the camera from `P_t.MCM`, five stations, the room's items; the stand-in until then is a 2D page with the five choices | `track_build_s2` for a room; measured frame cost | 3 |
| 9 | **Attract sequence and credits**: logos (sprites), the AI race with trackside cameras (`CAMERASn.MCM` kinds 0 to 4), the credits scroll from `ATTRACT.TXT`, 30 s idle | race with four AI cars (exists), cameras | 2 |
| 10 | **Replay** of the finished race (record the controls, run the race again) | S1's scheme | 2 |
| 11 | **The arcade scene** (`ANIMS`): the track engine with the scene's `MAPn` and four cars on `SCENE0..3.PTH` | the path reader, 8 | 3 |
| 12 | **CUSTOMIZE** and the set-ups' effect on the car (tyres, pressure, springs, brakes, handling) | `s2car.c` fields | 2 |

Total about 29 days if every row is done (13.5 for rows 1 to 6: a complete arcade and championship with 2D and small-3D pages, none of the room, the scene or the ghost; rows 9 and 10 add 4). The order follows what the player needs while testing: the loading screen and a pause menu first, then the end of the race, then the
choice pages.

### 7.1 The save (row 2)

`firmware/s2save.c` / `s2save.h`, test `tools/s2/check_save_s2.sh`. **Where and when it is written**: the save is the nonvolatile data slot 23
(`CHOICE.DAT`, `core/data.json` `size_maximum` 36,864) and lives in the SDRAM at `0x13F0_0000`, 64 KB of window up to `ATTACK.DAT`. The firmware never writes
the file: the Pocket reads the slot out of the SDRAM through the bridge when the core is quit or switched off (docs/soc.md, "Saved games"). So there is no dirty flag and no
"write back": every call that changes the save seals the changed part (its CRC-32) at once, and what the SDRAM holds is always what the next start reads. At boot `s2save_load()`
(called by `save_load()`, early in `prog_main`) reads the slot and puts its size, 19,348 bytes, into the APF's slot table (`slot_set_size`), because the APF keeps a size only for a file it
found and writes the slot back with it: an existing shorter file (the 1,120 bytes of the old layout) is read by its own size and is replaced by the full-size one at the next quit.
`size_maximum` is only the limit for what the Pocket may load: 36,864 leaves room for later versions of the layout; more than that (the window ends at 64 KB) needs `data.json` raised again, and
`data.json` is read when the core is loaded, so a change of it needs a full deploy.

**The layout** (little-endian, all offsets in bytes; the numbers in brackets are the defaults, which are the original's first start where it has one):

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | marker `S2SV` |
| 4 | 2 | version (1) |
| 6 | 2 | size of the layout (19,348) |
| 8 | 4 | CRC-32 of the main part (bytes 16 .. 447) |
| 12 | 4 | CRC-32 of the best times (bytes 448 .. 19,347) |
| 16 | 4 | the player's name: three printable characters and 0 (`MIL`) |
| 20 | 8 | the GAME SETTINGS rows, each the index of its value in the page's list: TRANSMISSION (0 AUTOMATIC), DETAIL LEVEL (2 HIGH), NUMBER OF LAPS (1: 3 laps), ARCADE ENEMIES SKILL (1 NORMAL), MULTIPLAYER ENEMIES (0 OFF), TIME ATTACK GHOST CAR (0 ON), DAMAGES (0 OFF), MUSIC (0 ON). The start menu's laps, level and gearbox rows are the rows 2, 3 and 0 |
| 28 | 6 | volumes: effects, CD, FM music; three `u16`, 0 .. 0x7FFF, 0x6000 is the nominal level of `snd_master()`, `music_master()`, `fm_master()` (0x6000 each) |
| 34 | 1 | the view a race starts in (the game's view number 1 .. 7; 2, the first from the car) |
| 35 | 3 | the start menu's last choices: track 1 .. 7, team 1 .. 4, car 1 .. 5 as the files number them (1, 1, 1) |
| 38 | 1 | the highest league open, 0 .. 3 (0) |
| 39 | 1 | tracks opened beyond the first three, 0 .. 4 (0) |
| 40 | 8 | reserved, 0; a later field takes one of these bytes if its 0 is a valid value (or clamps to one): no new version |
| 48 | 400 | per team (4 x 100): the fifth (bonus) car won (1 byte, 0), the set-ups (4 cars x Custom 1 .. 4 x 6 numbers, `int8` -1 .. 4, each set `2 2 0 2 2 2`: HANDLING, TIRE PRESSURE, TIRE TYPE, FRONT SUSPENSIONS, REAR SUSPENSIONS, BRAKES), 3 bytes unused |
| 448 | 18,900 | the best times: 105 lists of 15 rows of 12 bytes |

A **row**: `float time` in seconds (+0; 3599.999 for a row nobody has driven, as the game's file has it), `char name[4]` (+4, three characters and 0), `u8 team` 1 .. 4 (+8),
`u8 car` 1 .. 5 (+9), `u16 replay` (+10; 0 for none). An empty row is `3599.999`, `MIL`, team (row & 3) + 1, car 1, 0, as the game starts them. A **list** is the quickest 15 times of
one kind of game, track and thing timed; list number `(kind * 7 + track - 1) * 5 + list`: kind 0 league (CHAMPIONSHIP), 1 arcade, 2 time attack (the game's numbering), track 1 .. 7,
list 0 .. 3 the total time of a race of 1, 3, 5 or 10 laps (the NUMBER OF LAPS values), list 4 the best single lap. Time attack has only the 3-lap totals: a total list asked for with
kind 2 is list 1.

**Loading** (`s2save_load()`; one log line, `save: CHOICE.DAT <how> (<bytes> bytes)`): at most 19,348 bytes of the file are read, whatever its size is (a longer file is read to the layout's end;
a size that is not a multiple of 4 loses its last bytes). Then: no file or under 4 bytes, a failed read, a file that is not ours (marker missing and not 1,120 bytes), or another version or
size field give the defaults for everything. A file of the right marker and version is checked **part by part**: the main part must be whole, match its CRC-32 and hold only values in range (and a
printable name); the times must be whole, match theirs and hold only times 0 .. 3599.999, printable names and teams and cars in range. A part that fails is replaced by its defaults and the other is
kept (a short file keeps the main part if it reaches byte 448). A file of exactly 1,120 bytes without the marker is the **old layout**, Screamer 1's block whose unused fields held the start menu's choices
(`track`, `intro` = team, `car`, `laps`, `difficulty`, and `failed` = `0x5332xxxx | gearbox`): with the marker `0x5332` in the high half of `failed` the six choices are taken over (clamped), everything else is the
defaults; without it the defaults. The next quit writes the new layout, so this happens once. Every getter answers within its range, before `s2save_load()` too (whatever the SDRAM holds is clamped).

**The interface** (`s2save.h`; every setter clamps, seals and ignores a wrong number):

| Call | |
|---|---|
| `s2save_get(key)`, `s2save_set(key, v)`, `s2save_count(key)` | the numbers `S2S_TRANS .. S2S_MUSIC` (the GAME SETTINGS rows in the page's order), `S2S_VIEW`, `S2S_VOL_FX / CD / FM`, `S2S_TRACK / TEAM / CAR`, `S2S_LEAGUE_OPEN`, `S2S_TRACKS_OPEN`; `count` is how many values the row steps through |
| `s2save_name()`, `s2save_set_name(n)` | the name (three characters; shorter is padded with spaces, other characters become `?`) |
| `s2save_track_at(pos)`, `s2save_track_open(t)`, `s2save_league_open(l)`, `s2save_car5_open(team)`, `s2save_set_car5(team, won)` | the unlocks as data: the tracks open in the original's order (England, Egypt, California, Finland, Switzerland, Columbia, The Tunnel): three at the start, one more for each league won, the last when league 4 is won with all four teams. **`s2save_unlock_all`** (build default `S2SAVE_UNLOCK_ALL`, 1; `tools/fw.sh EXTRA_CFLAGS=-DS2SAVE_UNLOCK_ALL=0 TAG=x`) opens everything whatever the save says; it is a plain variable, not saved. No page asks these yet: the start menu still shows all seven tracks and all five cars |
| `s2save_setup(team, car, slot)`, `s2save_set_setup(team, car, slot, six)` | a car's set-up (team 1 .. 4, car 1 .. 4, Custom 1 .. 4 as slot 0 .. 3) |
| `s2save_row(kind, track, list, row)`, `s2save_row_empty(r)` | a row of a list, read-only, good until the next `s2save_add_time` |
| `s2save_add_time(kind, track, list, time, name, team, car, replay)` | the rank (0 .. 14) the time took, or -1 (slower than the fifteenth, not a time, wrong number). A time goes ahead of the first it is not slower than: an equal time goes ahead of the old one (`STARTL.EXE` 0x354cf: `fcomp`, `ja`; the rows below move down, the fifteenth falls out). The caller adds the total time to list 0 .. 3 and the best lap to list 4, as the original does for every finished human car (not for a race left or out of time). `name` 0 is the player's name |
| `s2save_set_replay(kind, track, list, row, n)` | the replay number of a row, when the replay of that run is kept |
| `s2save_load()`, `s2save_reset()` | boot; all defaults (the tests) |

The start menu (`s2menu_load()` / `s2menu_store()`) uses `S2S_TRACK / TEAM / CAR / LAPS / SKILL / TRANS`. Screamer 1's choice block (`champ.h`) is plain RAM now, only its unreached code reads it; `main.c`'s
`volumes()` takes the three volumes from the save.

**The original's file, for the steps to come** (decoded from `CONFIG/CHOICE.DAT` of the game: 0x2077 words, word *i* stored as `(v + i * 0x1050c) ^ i`; OpenScreamer's `docs/NOTES2.md` S2-5zk to S2-5zn and
`port/src/s2_hooks.c` name the fields; the offsets below are in the file, checked on a real one, 105 lists of 300 bytes end exactly at 0x81dc):
+0x00 the player's team 0 .. 3; +0x08 and +0x0c the names of players 1 and 2; +0x24 the league in hand and +0x30 races done (not kept across a sitting by the game; not kept here); +0x28 extra tracks open,
+0x2c next league open; +0x3c .. +0x58 the eight GAME SETTINGS rows; +0x5c .. +0x68 the HI SCORES page's own four choices (not kept here); +0x6c the attract race's track counter (not kept); +0x70 CD, +0x74 MIDI, +0x78 effects volume
(0x7FFF, 0x7FFF, 0x5FFF); +0x7c the view; +0x88 the standings of the league in progress; +0xb0 four team records of 0x188 bytes (chosen car, fifth car won, 4 cars x 4 set-ups x 6 words, set-up start `2 2 0 2 2 2`);
+0x6d0 the 105 lists, each 300 bytes in columns (15 times, 15 names, 15 replay numbers, 15 teams 0 .. 3, 15 car models 0 .. 4): first [kind][track][laps option] for the totals, then [kind][track] for the best laps.
The BEST TIME / BEST LAP of the LOADING screen are the first rows of the total list of the chosen length and of the lap list (time attack: the 3-lap list).
The routine that puts a finished race into the lists, `STARTL.EXE` 0x353cb (`tools/dis.sh -f 0x353cb 0x358c9`, read for this step): for every car that finished (time attack: only the player's) the total time goes into
the total list (strides 0x20d0 a kind, 0x4b0 a track, 0x12c a length; time attack always +0x12c) and the best lap into the lap list (0x12f32c + 0x834 a kind + 0x12c a track); the
place is the first row the new time is not slower than (`fcomp`, `ja`), the rows below move down one, the new row gets replay 0, the car's team, model and name; then the original names a replay for it
(not in time attack): the smallest number from 1 not used in that list's replay numbers, `REP<length><level><track><letter>`.

**What differs from the original**: our lists are rows of 12 bytes (not columns of words), teams and cars are counted from 1, the file has a CRC-32 per part in place of the original's word-by-word scrambling, the volumes use
the firmware's scale (0x6000 nominal) so that nothing sounds different, and the HI SCORES page's choices, the attract counter and the league in progress are not kept. The original's file is not read (no conversion of
the game's files; a `CHOICE.DAT` of the game put into the slot is a foreign file and gives the defaults). **Not checked on the Pocket**: the write-back of the longer file (the first quit after the new firmware turns the owner's 1,120-byte file
into 19,348 bytes), and the migration with the owner's real file.

## 8. Open

- Which BKK page backs which screen (the table in section 4 gives the program's addresses; `BKG_3L`, `BKG_5L`, `BKG_7L` are not placed).
- The 20 cars' names: `STRINGS.TXT` has eight (RADIANCE .. THUNDER) and the files have 4 teams x 5 cars; the mapping and the plates' use (7 plates) are not read.
- Which of the four scenes of `SCENEnn1.DAT` a team's arcade page uses (`STARTL.EXE` loads `anims\scene%02d%c.dat` and `sccar%c%c.dat` at 0x1169a, 0x117b4, 0x11544;
  the selector is a variable of the page).
- The room's face count and the frame cost of a room, a scene and the heaviest map (measure when step 5 or 8 starts; the budget is the race's 20 ms).
- The menu sounds: the 15 samples are read (section 6); which one each page plays is known for two pages only (the code of the others calls
  the sound manager with ids 0 to 14 at 60 places, `STARTL.EXE` 0x307e3 .. 0x436fd). The menu's music (CD track 2 while in the menu) is not played.
- The loading screen's, the pause screen's, the end screen's and the two pages' pixels were not compared with the original running (their positions, fonts, spacing and colours are read from its code, and the
  tests compare the firmware with an independent drawing of the same reading: `tools/menu_screens.sh`); the race screen's own text is still the console's, not `GAMEL.FNT`'s.
- The pages' fade in (a cut, as read: nothing in the pages' code fades them in, the way out fades to black in 0.5 s), the end screen's sound fade of the effects (0xaee60 in its code) and the CD / menu music are not done.
- The end screen's cost on the Pocket is not measured (the simulation has no menus: the darkening of 23,000 pixels, one list and the 74 ms of the darkening table at the finish).
