# Screamer 2's menus: the original's screens, what they are made of, what the core can do

Study and plan (2026-11-04); built so far: the start menu (`firmware/s2menu.c`, docs/development.md "Start menu"), rows 1 to 4 of section 7, the 2D
toolkit with the LOADING screen and the pause menu, the save, the arcade pages, the end of the race with the best times, the GAME SETTINGS and HI SCORES pages
(section 6), and from row 5 (2026-11-05) the 3D-o pages TEAM SELECTION, ENTER YOUR NAME and the CAR page's carousel (section 6; the track maps of time attack and league are not built). Sources:
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
| TEAM SELECTION | `LOGO1.DAT` (166 KB / 595 KB): `A1LOG-00 .. A4LOG-00.3DX` (4 badges, 6 to 15 KB), `LOGO.MLB` 560 KB, `LOGO.COL` (not used: the page's palette is `logoL.BKK`'s, checked against the running original); `logoL.BKK` (the S2 watermark) | 3D-o | built (row 5, `firmware/s2pages3d.c`, section 6): four meshes turning (left/right turns the chosen one to the front) |
| ENTER YOUR NAME | `ALETTER.3DX` (2.6 KB, one letter tyre; the page loads 64 objects of it, a frame of `LOGO.MLB` each: 26 are used), `MENUL.FNT` | 3D-o | built (row 5): the same page as TEAM SELECTION in its second state; the ring of 26 tyres in 3D as the original has it, not a stand-in |
| OPTIONS ROOM | `MENU<t>1.DAT` (806 to 925 KB / 2.75 to 3.3 MB): `MAP1.MLB` 2.4 to 3 MB, `MAP1.UNR` 0.3 MB, `MAP1.MMP` 10 KB (at most 150 piece records), `MAP1.BCK` 60 KB (backdrop), `MAP1.COL`, `ASHADOW.3DX`; `P_t.MCM` (252 B, five camera stations, also in `DATA/GEN`) | 3D-r | the entries are a track's (`MAPn.*`), the game loads it "as a track"; materials are routines 16 and 208 (shadows, the see-through table the race already has); one room per team, 4 |
| ARCADE page (0x3b6e8) | `ARCADE_L.BKK` (S2 ARCADE title over a blue grid, 6 KB), the prompt `ACCELERATE TO CONTINUE` (`STRINGS.TXT` line 15) blinking at y 184, `ANIMS/SCENEnn1.DAT` (4.2 MB / 8.1 MB: four scenes `MAP1..4.*` of 1.6 MB materials each, `SPLINES.PTH`), `ANIMS/SCCAR<t>1.DAT` (209 KB / 730 KB: the team's five cars), `DATA/GEN` `SCENE0..3.PTH` (the four cars' paths), `ENVM<t>1.DAT` (4 KB) | 2D + 3D-r | built (the picture and the prompt, `firmware/s2arcade.c`); a scene with four cars driving at the camera, not built (row 11); one scene at a time is about 2 MB |
| TRACK page (0x41a69) | `BKG_1L.BKK` and `MENUL.SPR` only: sprite 105 `TRACK selection`, 0 the lit frame (104 x 126), 1 / 2 the arrows, 5 .. 11 the seven tracks' pictures with their name banners lit and 12 .. 18 dim (92 x 116; in the original's order England, Egypt, California, Finland, Switzerland, Columbia, The Tunnel). **Not `TKG_nL.BKK`**: those (`tkg_%c%c`, 0x3ee6b) are the background of the 3D map pages of time attack and league (page 0x3ee28), see below | 2D | built, section 6. The seventh place (THE TUNNEL) opens when all four extra tracks are won, as the save has it (`s2save_track_open`) |
| CAR page (0x40c31, state 0) | `CARS<t>01.DAT` (the team's cars `AUTO1..5` .MLB + .3DX), `ENVM<t>1.DAT` (windows), `PLATES1.DAT` (138 KB / 452 KB: `A1..A7PLATE.3DX` 5 KB each, `PLATES.MLB` 416 KB), `BKG_2L.BKK` (blue grid), `CARS.DTR` (max speed, grip), `MENUL.SPR`: 34 `CAR selection`, 35 .. 38 the team's badge, the car's name plate 26 .. 29 (RADIANCE, HORIZON, NEBULA, SPARK: cars 1 to 4 of every team) or 30 .. 33 (BLACK CLAW, AFRODYTE, HORNET, THUNDER: the team's fifth), 106 .. 110 MAXSPEED, the drive and MPH | 3D-o | the five cars round a carousel, the plate with the name, MAX SPEED as `MENUL.SPR` pieces and text; built (row 5, `firmware/s2showroom.c` and `s2arcade.c`, section 6): the 3D carousel is the original's (a ring of radius 3000, not S1's row of cars 4000 apart); the **names are the plate sprites' art** (`MENUL.SPR` 26 to 33), not `PLATES1.DAT`'s meshes (seven plates `A1 .. A7PLATE`, 184 x 232 textures: not loaded by this page, `STARTL.EXE`'s load list of 0x40c31 has no plates) |
| TRANSMISSION page (0x40c31, state 1) | the same page as the CAR page (one routine with a state: ENTER on the car goes to state 1, ESC back), `BKG_2L.BKK` and `MENUL.SPR` 39 `select TRANSMISSION`, 40 / 41 AUTOMATIC dim / lit, 42 / 43 MANUAL dim / lit (141 x 27), the badge | 2D | built, section 6. (`bkg_6` at 0x421aa is the CD-check message page, at 0x3bf7a a page of the main menu's side: not this one) |
| LOADING | `LOAD_1..7L.BKK` (28 to 41 KB), the track's banner (`MENUL.SPR` 0x13 + n - 1, 144 x 50), `LOADING...`, BEST TIME, BEST LAP (`STRINGS.TXT` lines 30 to 32), the track's record `--:--:-- MIL` | 2D | built, `firmware/s2load.c` (section 6); read from the page's code `STARTL.EXE` 0x34ede: banner centred at y 4, BEST TIME label (font 2) y 72 and its record (font 1) y 84, BEST LAP y 112 and 124, LOADING... (font 1) y 170, all centred, colour 1, spacing 2; the two records only for the kinds of game that have them (arcade, time attack, league); shown while the track builds (3 to 5 s) |
| TRACK SELECTION (time attack, league) | `MAPS1.DAT` (523 KB / 865 KB): `AMAP1..7-VS.3DX` (29 to 109 KB; the largest has four blocks, 377 vertices at most, 1,886 faces, GU_MAX_VERT is 512), `MAPS.MLB` 406 KB; `TKG_1..6L.BKK` (the page 0x3ee28's background: a landscape with the names' plates), `LEAG_0L.BKK`, TERRAIN / DIFFICULTY lines (`STRINGS.TXT`) | 3D-o | a turning 3D map of the track, see-through look from the material table; S1's `map_blocks` did this with six maps |
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
  0x43fe0, `bkg_4` 0x39989, `bkg_5` 0x3a915, `arcade_%c` 0x3b712, `bkg_6` 0x3bfb9 (a page of the main menu's side, 0x3bf7a) and 0x421aa (the CD message), `tkg_%c%c` 0x3ee6b (the 3D map page 0x3ee28), `leag_0` 0x3f7d6,
  `logo%c` 0x3ffea (TEAM SELECTION), `bkg_2` 0x40c69 (the car and the transmission page, one routine), `bkg_1` 0x41bcd (the track page), `bkg_7` 0x422bb, `hiscore%c` 0x4275f, `bkg_3` 0x433b8.
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

**The start menu.** `s2menu.c` is a list in the console's style that stands in for the original's options room: ARCADE (the arcade pages below), NUMBER OF LAPS and
ARCADE ENEMIES SKILL (GAME SETTINGS rows, which the GAME SETTINGS page below shows too), START RACE (the race at once with the choices as they are; left / right and L / R change the track,
T1 .. T7 in the files' numbering, for testing) and WATCH (the ride), and GAME SETTINGS and HI SCORES (the two pages of row 3, below). Its choices are kept in the save (section 7.1): the track, team and car of the arcade pages, the
gearbox of the TRANSMISSION page and the laps and level rows are the GAME SETTINGS rows of the same name. What it leaves out of the original: the
options room, every other kind of game, the track maps (the TEAM SELECTION row and the first-start pages of row 5 are in the paragraph below).

**The arcade pages (row 4, built 2026-11-04).** `firmware/s2arcade.c`, test `tools/menu_screens.sh`. ARCADE on the start menu runs the original's chain, ENTER (B or
Start) forward, ESC (A or Select) back, each page as `STARTL.EXE` has it (the routine's addresses and every number are in the file's header comment; read with
`tools/dis.sh -f`): **title** (0x3b6e8: `ARCADE_L.BKK`, the prompt ACCELERATE TO CONTINUE blinking, one second and a quarter), **TRACK** (0x41a69: `BKG_1L.BKK` and sprites,
three of the open tracks' pictures with their name banners at a time, the chosen one lit and framed, the window scrolling and the frame gliding with the original's easing,
arrows where there are more; LEFT / RIGHT move without wrapping, L / R three on or back round, which is ours, so that any track is two presses away), **CAR**
(0x40c31, state 0: the picture of the blue grid, the title sprite, the team's badge, the car's name plate and, since row 5, the 3D carousel with MAXSPEED, the speed and the drive; the original is one routine with a
state, and so is ours: LEFT / RIGHT turn the ring, ENTER goes on to state 1 without a fade, ESC goes back; UP / DOWN do nothing, the team is chosen on its own page) and **TRANSMISSION** (state 1 of the same routine: AUTOMATIC and
MANUAL as two sprites, the chosen one blinking lit, the cars glide away). ENTER on the TRANSMISSION state fades the picture out over 25 frames (0.5 s) and the next page is cut in; ESC is a cut.
The sounds are each page's own, read from its code (`MENU.DTR` ids 0 to 14): title ENTER 1, TRACK moves 0 and ENTER 2, CAR moves 4, TRANSMISSION moves 0 and ENTER 3. ESC on the TRACK
page goes to the options room, as the original's does (here the start menu), not back to the title; ESC on the CAR page goes to TRACK and on the TRANSMISSION state to the CAR state. After
the TRANSMISSION state the race loads (the LOADING screen). **Checked against the running original** (STARTL.EXE in its DOSBox, `tools/s2/dosbox_arcade.py`, Ctrl+F5 raw frames, 47 screenshots with
all seven tracks open and 27 on a fresh save with three): the title's prompt band (on and off), the TRACK page whole at every one of the seven places (the frame
hidden, and shown at the first place and in the glides; the dim and lit pictures, the arrows, the scroll), the CAR page's title, badge and name plate, and the TRANSMISSION page's title, badge and buttons (both choices, lit and dim) are
**pixel for pixel equal** to the model of `tools/ui_arcade_ref.py`, and so to the firmware (`tools/menu_screens.sh` compares 105 frames of the firmware's code with the model
pixel for pixel); the 8 screenshots taken in the middle of a move are frames the original was half way through drawing (it draws on the screen it shows: the top rows are one tick of
the easing, the lower the next, every row equals a row of the model's frames), and the glide's steps are the 0.75 of the distance a frame the code says (a frame at 88 and the
top rows at 82 of a glide to 107: 0.75^6 and 0.75^5 of the 105 pixels). This also showed that the original centres a text by a width without the gap after the last character
(0xc6c88), which `s2gfx_text_centred` now does too (the LOADING and pause texts moved one pixel right).
The track page's three states of the original's other kinds of game (the 3D map of time attack
and league) are row 5's last item, not built.

**The 3D-o pages (row 5, built 2026-11-05).** Three pages with 3D objects over a 320 x 200 picture, on a small toolkit of their own (`firmware/s2scene.c`: the containers' entries read from the ZIP, the geometry unit set up with the page's material file, the
objects' blocks handed on as pieces by `track.c`'s `track_block_piece()`, the race's lens: focal length 128, the eye 128 behind the camera, the centre of the view at column 159, row 100), `firmware/s2pages3d.c` (TEAM SELECTION and ENTER YOUR NAME)
and `firmware/s2showroom.c` (the CAR page's cars; the page itself is `s2arcade.c`). Read from `STARTL.EXE` (`tools/dis.sh -f`; the numbers are in the files' header comments): the pages' state, keys, sounds, clocks, texts and the objects' rules.
- **TEAM SELECTION** (0x3ffab .. 0x40a64): `logoL.BKK` (the page's palette is its own, not `LOGO.COL`'s: the screenshots' palettes equal it), the title (font 0) at y 4, the team's name (font 1) at y 175; the four badges of `LOGO1.DAT` start at the origin and glide to their slots (0, 300, 3500) in front, (-3000, 0, 1000),
  (0, 2000, 1000), (3000, 0, 1000), turned round once for each team number (the chosen team's badge is in front), the chosen one turning 20 of 2048 a frame, the others 5; the camera at (0, 500, 4800). The glide is the engine's whole-number easing, cut towards zero each frame, which
  stops 9 units short of the slot on the side it came from (the screenshots show it). LEFT / RIGHT choose (sound 4, a fresh press), ENTER (B or Start) goes on (sound 6), ESC (A or Select) asks EXIT SCREAMER 2? (STRINGS.TXT): LEFT / RIGHT YES or NO (YES blinks in font 3 colour 3), ENTER on YES leaves
  (here: back to the start menu; the original leaves the program), on NO or ESC again the page goes on; the original's 30 s idle (back to the attract) and typing a letter on the keyboard are not built.
- **ENTER YOUR NAME** (the same page, state 1): the badges go to (slot x, slot y + 500, slot z - 500) (the chosen one up and back) and (slot x, slot y - 500, slot z - 25000) (the others), 26 tyres of `ALETTER.3DX` (the glyph is a frame of the material: phase (k x 0x9d8 + 0x226) >> 2) fly in on a ring of radius 3500 round the origin
  from radius 8500 at 2 a second, the chosen letter at the front tilted -0.314 radians about x, the others 2.827 (at 4 pi a second), every tyre's heading and place turning with the ring (a turn takes 1/8 s). LEFT / RIGHT turn the ring a letter (sound 9; held, a letter every 6 frames) and the letter becomes the name's letter at the cursor,
  UP / DOWN move the cursor along the three letters (the letter in hand is copied to the new place), ENTER keeps the name (sound 1), flashes the picture to white over 13 frames and leaves, ESC goes back to the team (the tyres fly in again next time). The name is `s2save_name()`; the team goes into the choices and `S2S_TEAM`, the save's new `S2S_PROFILE` byte is set.
- **The flow**: a save without a profile (`s2save.h` `S2S_PROFILE`, a byte of the main part's reserved bytes, 0 on a new save: no new version of the layout) shows TEAM SELECTION and ENTER YOUR NAME once, before the start menu, on the first start (the original shows them at every start). A new row of the start menu, TEAM SELECTION (last row), runs the pages again; the CAR page has no team choice any more.
- **The CAR page** (0x40c31): the team's cars `AUTO1 .. AUTO4` (`AUTO5` with the bonus car, as `s2save_car5_open`) from `GFX/CARS<t>01.DAT`, each with its own material library, on a ring: car k at angle -2 pi k / n, radius 3000 (car 0: 2800), turned round as many times as the chosen car's number so that the chosen one is at (0, 0, 2800); the camera fixed at (0, 600, 4000)
  pitched 22.5 degrees down; the chosen car turns 10 of 2048 a frame, the others 2; the cars glide 15 % of the way a frame; LEFT / RIGHT are taken while the last car is within 150 of its place. The second state (TRANSMISSION): the chosen car glides to (0, y + 200, z + 200) (5 % a frame), the others to (0, y - 1200, z - 4000). The plate: sprite 26 + car (30 + team for the fifth: the names RADIANCE, HORIZON, NEBULA, SPARK, BLACK CLAW, APHRODITE, HORNET, THUNDER
  are the sprites' art), sprite 106 "maxspeed:" at (166, 170), the speed (font 5, colour 5, fixed pitch, three digits: `MAX_SPEED` x 1.12 (the car made with the standard set-up, 0x14e20) x 1.188 / 1.6 for miles, from `DATA/GAME.DAT`'s `CARS.DTR`: HORIZON's 144.5 shows 120, as the running original does) at (213, 169), the unit (sprite 110) at (231, 171) and the drive (sprites 107 front, 108 rear, 109 four-wheel by the shares of the left wheels) at (251, 163).
  The cars' windows keep the placeholder picture of their own library (the original loads `ENVM<t>1.DAT` for the reflection: not done).
- **Memory** (SDRAM, docs/soc.md): `0x1180_0000`, 4 MB below the sound effects: a stage of 256 KB (an entry's stored bytes), 1 MB for a deflated ZIP's container inflated whole, then the page's models from `+0x140000`: TEAM SELECTION / ENTER YOUR NAME 0.65 MB (`LOGO.MLB` 560 KB, four badges, the tyre), the CAR page's five cars 1.1 MB (0x38000 each). Nothing of the race is touched (the track image, the cars of the grid, the toolkit).
  Reading (estimates from the Pocket's rates, not measured on it; the log has `pages3d: LOGO1.DAT in N ms` and `showroom: 5 cars of team 1 in N ms`): `LOGO1.DAT` 165 KB read and 595 KB unpacked about 0.35 s, the five cars about 0.6 s (a stored ZIP: by range, 53 to 76 KB a car). Per frame: the picture by the blitter (0.9 ms), 4 or 27 pieces (the badges and tyres are 56 to 192 vertices), a car 12 blocks;
  nothing of it runs in the race.
- **Checked** (`tools/menu_screens.sh`, `tools/host/pages3d_screen.c`; `tools/pages3d.sh` with the DOSBox screenshots): see below. The keys, sounds, results and the save are tested with scripted buttons: ESC / NO / YES, the team choice and its wrap, the name (the ring, the cursor, ESC back, a held key), the first-start flow and the TEAM row of the menu, the CAR page's frames.
  Against an independent reference (a second reading of the rules in plain C, with a piece builder of its own in double precision: the camera's frame from the angles, the matrices, the face row) the page's pictures differ in **0 of 43,520 pixels** (TEAM SELECTION after 150 frames), **3 of 44,160** (ENTER YOUR NAME after 280 frames) and **0 of 32,000** in each of the CAR page's three states (the ring at rest, after RIGHT, the second state; rows 50 to 150).
  Against the **running original** (`python -I tools/s2/dosbox_arcade.py --pages3d`: 70 Ctrl+F5 raw frames of the three pages on a private `CHOICE.DAT`; `tools/pages3d.sh`): the pages' palette is the picture's (256 of 256 entries). For each screenshot the matcher draws the firmware's page in the state that matches it (the objects where the page puts them when the glide has stopped, the turning objects' angles searched over all 2048, since a screenshot cannot tell them)
  and counts the pixels that differ. The **outline** of the objects agrees: of the pixels the objects cover in the screenshots 1.6 % differ in TEAM SELECTION (15 frames), 0.5 % in ENTER YOUR NAME (21 frames; the ring, the chosen letter M or N, the badge) and 3.9 % on the CAR page (5 frames; the cars' turning has the matcher guess only the chosen car's angle). The **inside** is the same picture, not pixel for pixel: 58 %, 48 % and 49 % of the object pixels are equal, and
  86 %, 83 % and 76 % are equal or have the screenshot's colour in a pixel's reach (the textures are minified three to five times: a sub-pixel shift picks other texels; the original builds its matrices from its own 16-bit tables, ours are floats). Frames caught in a move (the glide in flight: 10 of 25 team frames, 3 of 24 name frames, 16 of 21 car frames) are not counted.
  The **2D parts** of the settled frames, compared in the boxes they fill (`tools/pages3d_2d.py`): the car page's title, badge, plate, the "maxspeed:" label, the speed's digits, the unit and the drive sprite, the name page's title and the team page's team name are **pixel for pixel equal** (3 to 12 screenshots each; the comparison found the first reading of the speed wrong: 107 for HORIZON where the original shows 120, which is MAX_SPEED x 1.12),
  the team page's title differs in 30 pixels over 12 screenshots (a badge reaches into its band), the name's text and cursor in 4 pixels a screenshot at the tyres' lowest rows. **Not compared with the running original**: the flash to white, the sounds (read from the code), the ESC prompt, the held key's pace in the original, the CAR page's TRANSMISSION state and the cars' windows.

**The 2D toolkit (row 1, built 2026-11-04).** Four small modules over the blitter, all in `firmware/`, tested on the PC (`tools/menu_screens.sh`):

| Module | What it does |
|---|---|
| `s2gfx.c` | `.BKK` pictures (`s2gfx_bkk`: 24-byte header, palette x4, 64,000 pixels, read from `GFX/GRAF1.DAT` of the ZIP by range, about 40 KB and 50 ms for a picture), `MENUL.SPR` sprites and `MENUL.FNT` fonts as the blitter's image tables, text with the original's spacing (`s2gfx_text`: the engine's 0xc67f0 and 0xc6c88, docs/screamer2/hud.md section 2), sprites (also centred, 0x333d9), the `LOCAL.DAT` lines, the original's darkening table and a darkened still of the screen; one frame = `s2gfx_begin()` (blitter list for the back buffer), the page's commands, `s2gfx_end(pal, fade)` (palette faded, swap) |
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
code: the TRACK page 0 (MENU1) for a cursor move and 2 (MENU3) for ENTER (the first study put these on the TRANSMISSION page, which plays 0 and 3), the GAME SETTINGS page 10 (MENU11) for a
cursor move, 12 (MENU13) when ENTER steps a value and 13 (MENU14) for VIEW HIGH SCORES (ESC is silent); the HI SCORES page and the end of the race play none (`S2PAGE_SND_*`; the first reading of
GAME SETTINGS' sounds was of another page's code; the arcade pages' ids are in `s2arcade.c`). The original's pause is silent (no `MENU.DTR` in a race); ours clicks (`PAUSE_CLICKS` in `s2pause.c`).

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
lit as a whole. The original's keys: up / down move (no wrap, a click of id 10), ENTER steps the row's value round (id 12; VIEW HIGH SCORES: id 13 and the HI SCORES page), ESC leaves with a cut (it sets the page's
done flag, 0x1324a0, at once; VIEW HIGH SCORES sets only the fade, 0x132498, and the page ends when the fade to black, 0.5 s, is done; the pages come in whole). Here B or Start are ENTER, A or Select ESC, and left / right also step the value back and on (the original ignores them). The value goes into the save at once
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

What the start menu still leaves out of the original: see the list above; the 3D pages follow in row 5.

## 7. Build order and rough costs (days of work, one developer; the PC-side tests of each step as for the start menu)

| # | Step | Needs | Cost |
|---|---|---|---|
| 1 | **done 2026-11-04** (section 6). **2D toolkit**: `.BKK` loader (header, palette x4, copy), `MENUL.SPR` / `.FNT` loader into the blitter's image tables (Screamer 1's `menu_init` and `tb_hud.c` as templates), a page runner (keys with repeat, fade in/out, the menu click sounds from `SOUND.DAT`'s `MENU.DTR`); the **LOADING screen** (`LOAD_nL` + LOADING + BEST TIME / BEST LAP) and the **pause menu** (RESUME, RESTART, EXIT over the darkened race, replacing Select + B) | zip range reads of `GRAF1.DAT`; the existing blitter | 2 |
| 2 | **The save** (done, 7.1): a Screamer 2 layout for `CHOICE.DAT` (settings, volumes, unlocks, set-ups, best times; the start menu's fields moved into it, the old layout is migrated once), `data.json` size 36,864 | `core/data.json` | 1 |
| 3 | **done 2026-11-04** (section 6). **End of race**: the 4 s CONGRATULATIONS / place / time screen, then RACE AGAIN / EXIT (REPLAY later) and the track's best times (a table of 15 in the new save); GAME SETTINGS and HI SCORES pages on the same toolkit | 2 | 2 |
| 4 | **done 2026-11-04** (section 6). **Arcade pages**: the title, the TRACK page (sprites: pictures with banners, lit frame, arrows), the TRANSMISSION page, a 2D stand-in for the CAR page, the page chain with ESC going back; the start menu is the stand-in for the options room | 1 | 1.5 |
| 5 | **done 2026-11-05 except the maps** (section 6): the CAR page (carousel of the team's cars with the plate; the names are `MENUL.SPR`'s sprites), TEAM SELECTION (four badges), ENTER YOUR NAME (letter tyres). **Not built**: the track maps for time attack and league (`MAPS1.DAT`, page 0x3ee28), which belong to rows 6 and 7 | geometry unit pieces with a page MLB; `LOGO1`, `CARS<t>01` read by range; `MAPS1` for the maps | 3 (4 with the maps) |
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

- Which BKK page backs which screen (the table in section 4 gives the program's addresses; `BKG_3L`, `BKG_5L`, `BKG_7L` are not placed; `BKG_1L` the TRACK page, `BKG_2L` the CAR and
  TRANSMISSION pages, `ARCADE_L` the title, `TKG_nL` the 3D map pages of time attack and league).
- The 20 cars' names (read from the CAR page's name plate, 0x41653 .. 0x416ab): the four cars of every team are RADIANCE, HORIZON, NEBULA, SPARK, and the fifth is BLACK CLAW (CONDOR), APHRODITE
  (ANGELS), HORNET (WASP) or THUNDER (ZEUS), the name sprites 26 .. 33 of `MENUL.SPR` (the earlier note's AFRODYTE was a slip; the plate sprites are the original's own data, no stand-in names were ever drawn). `PLATES1.DAT`'s seven plates are not on the CAR page.
- Row 5 on the Pocket (not seen on it): the frame rate of the three 3D pages, the load times (estimates above), the white flash, the cars' windows (placeholder reflection). TEAM SELECTION's 30 s idle and the typed letters, the options room's ESC back to TEAM SELECTION and the attract are not built.
- The arcade pages were compared with the running original (section 6); what was not: the page's fade out (0.5 s) and the sounds (read from the code), the 3D parts, and the TRACK page on
  a save with fewer extra tracks than 0 or 4 (the code's rule for the window is the same for any number). The original's keys have no repeat
  (a held button gives one press, 0x389ae); the toolkit's runner repeats the d-pad after 0.5 s, which the start menu always did. The LOADING and pause pages were not compared with the original running
  (only their texts' centring was corrected by the ARCADE page's comparison).
- Which of the four scenes of `SCENEnn1.DAT` a team's arcade page uses (`STARTL.EXE` loads `anims\scene%02d%c.dat` and `sccar%c%c.dat` at 0x1169a, 0x117b4, 0x11544;
  the selector is a variable of the page).
- The room's face count and the frame cost of a room, a scene and the heaviest map (measure when step 5 or 8 starts; the budget is the race's 20 ms).
- The menu sounds: the 15 samples are read (section 6); which one each page plays is known for the arcade title, TRACK, CAR, TRANSMISSION and GAME SETTINGS pages (the code of the others calls
  the sound manager with ids 0 to 14 at 60 places, `STARTL.EXE` 0x307e3 .. 0x436fd). The menu's music (CD track 2 while in the menu) is not played.
- The loading screen's, the pause screen's, the end screen's and the two pages' pixels were not compared with the original running (their positions, fonts,
  spacing and colours are read from its code, and the tests compare the firmware with an independent drawing of the same reading: `tools/menu_screens.sh`). The race
  screen is built (`firmware/s2hud.c`, docs/screamer2/hud.md): `GAMEL.SPR` / `.FNT` through `s2gfx_game_*`; the toolkit's text now follows the engine's routine (a width has no
  trailing space, fixed-pitch cells), which moved centred texts by up to half a space.
- The pages' fade in (a cut, as read: nothing in the pages' code fades them in; VIEW HIGH SCORES fades out in 0.5 s, ESC is a cut), the end screen's sound fade of the effects (0xaee60 in its code) and the CD / menu music are not done.
- The end screen's cost on the Pocket is not measured; in the simulation (docs/development.md "End of the race and the two pages") the first frame takes 70 ms (the darkening table) and then 3.3 ms a frame, and
  the race on track 4 before the finish took 10.9 ms mean and 11.8 ms worst against 10.7 / 11.5 before row 3 (cache layout).
