# Screamer 2 against the Screamer core: what carries over

Study of 2026-11-03 on branch `compare` (started from the Screamer core at release 1.0.2). Read-only
on the game; no Quartus build, no simulation, nothing run on the Pocket. Reference: OpenScreamer's
`docs/NOTES2.md` ("N2 S2-x" below) and the game files in `C:\GOG Games\Screamer 2`; where a fact
is ours it comes from an experiment (E1 to E5, last section, all in `tools/s2/`).

Verdicts: **identical** (our code or tool works on it as it is), **small change** (a constant, a
name, a field), **new** (nothing of ours reads it), **unknown** (not looked at).

## Summary

1. **The track files are Screamer 1's, and our renderer draws them.** All 143 `.DAT` containers (1,904
   entries, 380 MB unpacked) open with `tools/datfile.py` unchanged (E1). A track image built from
   S2's track files by `tools/make_track.py` (unchanged, through a 100-line adapter) renders on all
   seven tracks with the firmware's own `track.c`, `geom.c`, `setup.c`, `raster.c` (C models of the
   hardware): 8 pictures in this folder (E3). Needed: two constants raised (objects 512, placements
   2560) and one value (the mesh class word) set to 0.
2. **Everything around the track is new**: the car model and its data (`CAR_PARAMS`), the
   opponents' AI, the animations (`.PTH`), the cameras (`.MCM`), the damage, the mirror, the menu
   (3D rooms, `.BKK`), the league. NOTES2: 2 % of Screamer 1's game-logic code reappears. More
   than half of our firmware by lines (about 7,700 of 13,400: car, drive, opp, scenery, champ, menu,
   intro, hud, ghost, save, files, the track build's sections, the glue in main.c) must be rewritten or
   reworked; the renderer, the units, the mixer, CD music and the build/test tools carry over.
3. **Two hard limits, neither in the renderer.** (a) *Data slots*: the minimum set for a race needs 31
   of the 32 slots (we use 26 now); night, reflections, 3D menu rooms and intro scenes do not fit.
   (b) *Block RAM*: 292 of 308 M10K are used; S2 needs a wider see-through table (112 rows made at
   race start; ours has 64) and, for night and fog, a per-face colour map (`DEPTHn.DPT`).

## 0. Which files are "the 320x200 256-colour ones"

The assumption in the task (L/H = resolution) is not right; N2 S2-5d, S2-9 read the program:

| Letter / digit in `GFX/MAPn{L,H}{0,1}{1,2}.DAT` | Meaning |
|---|---|
| `L` / `H` | the DETAIL set, as in Screamer 1: `L` at detail LOW, `H` from MID (the game takes `H` at the default). The resolution is chosen by the program (`STARTL.EXE` 320x200, `STARTH.EXE` 640x400) and by the letter inside `GAME1.DAT`/`GRAF1.DAT` (`GAMEL.SPR` / `GAMEH.SPR`) |
| `0` / `1` (middle) | the look: `condition & 1`, condition 0 DAY, 1 NIGHT, 2 FOG. Day and fog use look 0 (fog adds `DEPTH2.DPT`, only tracks 1 and 5 have it); night is look 1 (darker sky and textures); tracks 5 and 7 have look 0 only |
| `1` / `2` (last) | 1 = 256 colours (`.COL`, `.WLB`, `.SMG`), 2 = 65K colours (`.HIC`, 16-bit `.BCK`, no `.WLB`/`.SMG`; 192 KB `DEPTH`) |

So the equivalent of what we load today (Screamer 1's `MAPnH.DAT`) is **`MAPnH01.DAT`** (day, high
detail, 256 colours), n = 1 to 7; `MAPnH11.DAT` is the night look. 65K colours (`..2`) stays out.
Seen in E1/E2: `MAP1L01` has 594 objects and 1,609 placements, `MAP1H01` 642 and 2,000.

## a) Game files

| Item | Screamer 1 (what we have) | Screamer 2 | Verdict | Evidence |
|---|---|---|---|---|
| Container | `.DAT`: data, 32-byte directory records (name XOR 0x3A), methods 0 and LZW | the same; methods 0, 2, 3 seen (1 not) | identical | E1: 143 files, 1,904 entries, 0 size mismatches; N2 S2-1 |
| Track file | `GFX/MAPnH.DAT` (n 1 to 6; also `MAPnL`): `.MLB .UNR .MMP .BCK .SMG .COL` + 7 cars + props (`AUTOnn.3DX`, `SHADOW`, `SMOKE`, ...); 1.3 to 1.7 MB | `GFX/MAPnH01.DAT` (n 1 to 7): `.MLB .WLB .UNR .MMP .BCK .SMG .COL .DPT` + `MISC.MLB/.COL` + props; 2.16 to 2.63 MB. No cars inside | small change (names, contents) | E1, E2 |
| Track data | `DATA/MAPn.DAT` (also `MAPnI.DAT`, not used by us): `OPP OST CNG SLA ANM CAM RPL .SPL`, `MAPn{A..E}.DTR`, `.MRK`; 17 to 41 KB | `DATA/MAPn.DAT` (n 1 to 7): `MAPnOPP/DIR/OST.SPL`, `MAPnDAT.DTR`, `MAPn.PTH`, `CAMERASn.MCM`; 5 to 20 KB. No `.MRK`, no per-difficulty `.DTR`, no `I` (reverse) files | new content | E1, N2 S2-1 |
| Cars | inside the track file (`AUTO00..06.3DX`, textures in the track's `.MLB`) | `GFX/CARS{team 1..4}{0,1}{1,2}.DAT`: `AUTO1..5` each `.3DX .MLB .WLB .COL`; 1.3 to 1.4 MB unpacked a team; 5 cars a team, 4 teams | new | E1; N2 S2-5e |
| Reflections | in the track's `.MLB` (routine 160) | `GFX/ENVP{track}{0,1}{1,2}.DAT` per track and look, `GFX/ENVM{team}{1,2}.DAT` for the menu: `ENV.MLB` (32 KB) + `ENV.COL` | new | E1; N2 S2-5zq |
| Common data | `DATA/GEN.DAT` (CARS.DTR + 4 `.DTR`), `LOCAL.DAT` (7 `.TXT`), `SOUND.DAT` (92 entries, 1.9 MB), `MUSIC.DAT` | `DATA/GEN.DAT` (`CARS.DTR` + same 4 `.DTR`: 969/672/597/2356 bytes as ours, + `P_n.MCM`, `SCENEn.PTH`), `GAME.DAT` (`CARS.DTR`, `MISC.DTR`, `CRUSC.DTR`), `LOCAL.DAT` (`STRINGS/ATTRACT/GAME.TXT`), `SOUND.DAT` (78 entries, 1.4 MB). No `MUSIC.DAT` | small change | E1 |
| HUD, menus | `GFX/GAME.DAT` (HUD sprites, `IMPLET/IMPRES/CDFRONT .PKC`), `GRAF.DAT` (PCX), `MENU.DAT` (the menu's models and the cars' textures) | `GFX/GAME1.DAT` (`GAME{L,H}.SPR/.FNT`, `COCK1..8_{L,H}.PKC`, `MIRROR`), `GRAF1.DAT` (56 `.BKK`, `MENU{L,H}.FNT/.SPR`; 4.20 MB), `MENU{team}1.DAT` (a room built like a track, 0.8 to 0.9 MB, 4), `LOGO1`, `MAPS1`, `PLATES1`, `ANIMS/SCENEnn1`, `SCCARnn` (4.2 to 4.8 MB each) | new | E1; N2 S2-5zq |
| CD music | `SCREAM.DAT` (cue) + `SCREAM.gog` 383 MB, 7 audio tracks; FM fallback in `MUSIC.DAT` | `GAME.DAT` (cue, 399 bytes, `PREGAP` on track 2) + `GAME.GOG` 569 MB, 8 audio tracks; track per circuit `CD_TRACKS 9 8 6 7 4 3 5`; no MIDI fallback | small change (cue parser: `PREGAP`, other names, other track table); FM music unusable | file listing; N2 S2-1 |
| Saves | our slots `CHOICE.DAT` (1,120 bytes, the game's layout) and `ATTACK.DAT` | the game's `CONFIG/CHOICE.DAT` is 33,244 bytes | new layout (our own mechanism stays) | file listing |
| Other | `TABS/` (not read) | `TABS/VIS.TAB` only (161,024 bytes, not read by us); `REPLAYS/` holds `DUMMY.DAT` (the game writes `temp.rpl` itself) | identical (not needed) | file listing |
| Slot sizes | 4 MB track slot, 4 MB `GRAF.DAT`, 1 MB `GAME.DAT` | track files 2.16 to 2.63 MB fit; `GRAF1.DAT` is 4,203,916 bytes, 9.6 KB over a 4 MiB slot; `GAME1.DAT` 0.45 MB; CD image 569 MB fits 1 GiB | small change | file sizes |
| **Slots needed** | 26 of 32 | minimum for a race, day + fog, HUD, CD music: firmware, log, program (3) + saves (2) + CD cue and image (2) + `DATA/GAME GEN LOCAL SOUND` (4) + `GAME1` (1) + 7 `MAPnH01` + 7 `DATA/MAPn` (14) + 4 `CARSt01` (4) + `GRAF1` (1) = **31**. Not in it: 7 `ENVP`, night (5 `MAPnH11`, 4 `CARSt11`, 5 `ENVP`), 4 `MENUt1`, `LOGO1`, `MAPS1`, `PLATES1`, the `ANIMS` scenes | **new problem** | counted from E1; APF limit 32 |

**Decided (2026-11-03): one ZIP.** The player puts the original `DATA`, `GFX` and `ANIMS` folders into ONE
ZIP in a single data slot (`SCREAMER2.zip`; entries stored, or deflated at the price of a slow load), and the
music image `GAME.GOG` and its cue sheet `GAME.DAT` stay two slots of their own. Slots now: firmware, log, program
(3) + ZIP (1) + cue and image (2) + saves (2) = 8 of 32, and every file of the game is reachable, night, reflections,
menu rooms and scenes included. The ZIP reader is `firmware/zip.c`, the files screen `firmware/files.c`
(`docs/development.md`, "Game data").

## b) Data formats

| Format | Screamer 1 (what we read) | Screamer 2 | Verdict | Evidence |
|---|---|---|---|---|
| `.MMP` map | two-level 256x256 grid, cell header (five base/count pairs; run 4 = the cells visible from this one, `draw_distance.md`), 64-byte piece records (object +0, x +8, height +0xC, y +0x10, heading +0x18), road points (12 B), walls (28 B) | the same; 251 to 843 cells (S1 222 to 573), 1,099 to 3,742 records (S1 1,164 to 2,359), 520 to 826 walls, 921 to 1,921 road points. Record `+0x3C` = road kind id (36 asphalt, 27 grass; 0 in S1) | identical (kind id is new data) | E2, E3 (all 7 tracks render); N2 S2-5c |
| `.UNR` meshes | size, count, offsets; object header 0x330 (3 detail tables of 65 entries at `+0x18`, class word at `+0x32C`); block +0x0C position, +0x10 vertices, +0x14 faces, +0x18 flags, +0x1C verts, +0x20 normals, +0x24 uv, +0x28 16 row pointers, +0x68 name; 36-byte faces | the same blocks and faces (all 11,351 gaps between consecutive faces are 36 bytes; 0 faces with 3..4 corners violated in 12,001; all 16 rows used). Object header **0xC8** (44 entries at `+0x18`, first block at 0xC8; block b at level of detail L is entry b + 13 L, entry 41 is -1, entry 43 a sort bias; `performance.md` section 6): no class word at `+0x32C` (our `track_class_key` reads junk). 345 to 705 objects (S1 223 to 390), 1 block almost everywhere, a few of 3 to 4 with 60 to 100 frames | small change (class key = 0; see Hardware) | E2; E3: using the junk word changes 93 of 64,000 pixels in the start picture, 0 in two others. N2 S2-5 says the "revised 3DX" is only the header |
| `.MLB` materials | 16-byte materials, texture records, pixels; routines 16, 64, 128, 160 (car glass), 192, 224 | the same; routines 16, 64, 128 on tracks, 160 on cars, **208** on track 7 and the shadow/smoke (acts like 224, N2 S2-5p); textures up to 254x254 (S1 256); 1.05 to 1.95 MB (S1 2.4 to 3.0 MB); frames up to 23 on tracks, 128 pictures on cars (damage, brake, wheel blur: N2 S2-5zq) | identical for tracks (208 = 224) | E2, E3, E5 |
| `.WLB` | - | the "8 MB version" of the `.MLB` (smaller, 0.7 to 1.0 MB); the game takes it only on 8 MB machines | new (not needed) | N2 S2-5e |
| `.SMG` see-through | 64 rows x 256 (16 KB) read from the file into the unit | 48 rows (12 KB) with black ramps in the smoke rows; the game does not read it but **makes a 112-row table** at race start from `TRACK_DATA` (`asmoke`, `a2smoke` rgb) and the palette | new (firmware makes it; hardware table widens) | E1; N2 S2-5zd |
| `.COL` | 768 bytes, 6-bit VGA | the same; colours 0 to 119 are equal in every file of the game, tracks own 120 to 255 | identical | E1; N2 S2-5e |
| `.BCK` | PCX 512x256, 8 bit | the same (night look 21 KB instead of 63 KB; track 7 none) | identical | E3 (all 6 with a backdrop parse) |
| `.DPT` | - | 65,536 bytes = 256 rows x 256: row 0 identity, `DEPTH1` darkens to black, `DEPTH2` lightens to fog grey; the game picks a row **per face** from the face's depth and sends every texel through it (128 steps). Used in night (except on one track: N2 says "track 3 (the fourth)", MAP4 if the game counts from 0) and in fog | new | E1; N2 S2-5d |
| `.HIC` / 65K | - | 16-bit pictures in the `..2` containers | new (skipped) | N2 S2-1 |
| `.BKK` | - (PCX) | 24-byte header ("V652", 256, 320, 200, 64,000, 0) + 768-byte palette + raw pixels: 64,792 bytes (L, 320x200) or 256,792 (H, 640x400) | new but trivial | E1 (byte dump) |
| `.3DX` | cars: 12 blocks (names `MV_mauto fauto rauto` + wheels/hubs), flags 0x6a00 body, 0x800 hub, 0xa00; position records | same blocks and flags; header 0xC8; the table holds the levels of detail (entries 13 to 24: level 1 of the 12 blocks, 26: level 2 = the body only; 41 = -1; `performance.md` section 6); 36 to 50 KB a car; max 140 vertices, 94 faces a block | identical for the first set | E5: a car draws through our renderer. N2 S2-5e |
| `.SPL` | count, start, 56-byte nodes; `OPP` has 10 splines (the tenth is the player's) | the same format; `MAPnOPP.SPL` is ONE spline (36 nodes on track 1; it is the cars' aim line and leaves the road in places); `MAPnDIR.SPL` (11 nodes) and `MAPnOST.SPL` (obstacles, 23) new; no `CNG/SLA/ANM/CAM/RPL` | small change | E3; E4 stops here ("camera path": `tb_spl.c` needs 10 splines); N2 S2-5c |
| `.DTR` syntax | `*` comments, f/d/x/s prefixes | the same | identical | file dumps |
| `CARS.DTR` | `CARS_DATA`: 14 numbers a car (max speed, rpm, grip, accel, 7 gear ratios) | `CAR_PARAMS`: named fields a car (MAX_SPEED, MAX_RPM, gears, MAX_STEER, ACCELERAZIONE, grip global and per wheel F/R, TRAZIONE F/R, DRITTIZER, FRENO, SOSPENSIONI, camera position); 4 teams, 5 entries each (the fifth marked "Bonus Car") | new | file dump (GEN and GAME copies) |
| `MAPn?.DTR` | `TRACK_DATA` (bonus time, camera, colour, carnage), `OPPONENTS` rows per difficulty | `MAPnDAT.DTR`: `OSTACLES`, `ANIMATIONS` (17 rows a track), `TRACK_DATA` (far plane -50000, background rgb and palette entry, smoke rgb, tyre pressure), `ROAD KINDS` (grip for slick/rain/snow, smoke); `MISC.DTR`: `SKILLS` (catch-up, 3 levels), `DAMAGES`, `CD_TRACKS` | new | file dumps; N2 S2-2, S2-5n |
| Sound `.DTR` / `.RAW` | `SFXSBASE SPFXBASE SPFXFULL MENU .DTR` + 8-bit `.RAW` | the same four in `GEN.DAT` (same sizes); new `GAME.DTR` (engine per car, own rates 8000 to 11025), `AMBIENTn.DTR` (per track); 8-bit raw, 8000 to 22050 Hz | small change (new tables) | E1; N2 S2-5zf |
| `.PTH` | - (`ANM.SPL`) | count, then per path 100 bytes (name `MV_..`) and three channels of cubic keys (x, y, height): birds, helicopter, airship, cable cars, trains | new | E1 (17 paths on track 1); N2 S2-5zh |
| `.MCM` | - (`CAM.SPL`) | count + 32-byte camera records (`CAMERASn.MCM`: 23 on track 1; `P_n.MCM`: 5 stations of a menu room) | new | E1; N2 S2-5r |
| `.PKC` | run-length (skip / run), 320x200 | the same (checked on `COCK1_L`, `COCK8_L`, `MIRROR_L`, `COCK1_H`: fills exactly 64,000 / 256,000 pixels) | identical | E1 |
| `.SPR` | 63 sprites, 20-byte records | the same format, 115 sprites in `GAMEL.SPR` (header sizes consistent) | identical | E1 |
| `.FNT` | 7 fonts, 12-byte head (height, first, count) | 9 fonts; each head has an extra int and a 16-byte picture name (`font2l.pcx`) | small change | E1 (byte dump) |
| `.TXT` | line lists | line lists (`GAME.TXT` 600 bytes, `STRINGS.TXT`, `ATTRACT.TXT`); other lines | small change (indices) | file dump |
| Replays | ours: our own memory format | the game records every race (0x20 bytes a frame) and writes `temp.rpl` | n/a (ours stays) | N2 S2-5zp |

## c) Firmware modules

Screamer 2's game logic is new (N2 S2-2: 36.6 % of the code bytes match, 2 % of the game-logic part).
The OpenScreamer reference for it is `port/src/s2r_car.h`, 8,000 lines of hand-written, x87-exact
code, against about 2,400 lines of car, drive and opp in our firmware.

| Module | Reads in S1 | S2 data | Verdict | Work |
|---|---|---|---|---|
| `dat.c`, `crc32.c`, `slot.c`, `dlog.c`, `console.c`, `blit.c`, `selftest`, `memtest` | containers, slots | same | identical | none |
| `efx.c` | `MAPn.SMG`, `SHADOW.3DX`, `SMOKE.3DX`; radar row 63 | `ASHADOW/ASMOKE/A2SMOKE.3DX` + `MISC.MLB`; table made from `TRACK_DATA` colours (112 rows) | small change | small (with the hardware table) |
| `track_build.c` | 5 containers, 14 sections | stops at "camera path" on S2 (E4); also missing: `.MRK`, `AUTO00..06.3DX`, `IMPLET_L/IMPRES_L.PKC`, `MAPnB.DTR`, `CARS_DATA` | small change | sections for splines (one), cars (`CAR_PARAMS`), `TRACK_DATA`, no markers; adapter is `tools/s2/make_track_s2.py` |
| `tb_mmp.c` | `.MMP` | same | identical | none |
| `tb_spl.c` | 10 splines | 1 spline, `DIR`, `OST` | small change | indices |
| `tb_gfx.c` | `.COL`, `.BCK` | same | identical | none |
| `tb_hud.c`, `hud.c` | `GAMEL.SPR/.FNT`, `IMPLET_L/IMPRES_L`, `CDFRONTL`, `GAME.TXT` | `GAMEL.SPR` same, `.FNT` head changed; cockpits `COCK1..8_L.PKC`, `MIRROR_L.PKC`, dashboard numbers `CRUSC.DTR`; no `IMPLET/IMPRES`; 115 sprites | small change + new screens | medium |
| `track.c` | image: MLB, UNR, pieces, ground, walls | works with `MAX_OBJ` 1024, `MAX_PIECES` 4096, class key 0 (E3). `MAX_CFACES` 8192: drivable faces 4,697 to 8,079 (track 2 at 98.6 %); grass/kind from the piece record `+0x3C` instead of the block name; fade (`DEPTH`) not there | small change | constants, kinds |
| `car.c`, `drive.c` | `CARS_DATA`, OpenScreamer `drive.c` (engine, gearbox, grip) | new model: per-wheel grip and traction, springs and body lean, road kinds with tyre/weather grip, damage (look = texture picture, power loss), wheelspin smoke (N2 S2-5o, 5u, 5zd, 5zq) | **new** | large |
| `opp.c` | `OPPONENTS` rows, 10 splines | `OPP.SPL` aim line, `DIR.SPL`, `SKILLS` catch-up by distance and level (`0x2da06`), computer cars from teams | new | large |
| `scenery.c` | `ANM/CNG/SLA/OST.SPL`, `MAPnDAT.DTR` | `ANIMATIONS` rows + `.PTH` cubic paths (17 on track 1), obstacles (cones fly and bounce), sounds on paths | new | medium |
| camera views | `CAM.SPL`, `RPL.SPL` | `CAMERASn.MCM` trackside records, mirror (second pass: 160x35 rows, camera 3, N2 S2-5zh) | new | medium |
| `ghost.c` | recorded keys in `ATTACK.DAT` | S2 time attack: 2 cars, the ghost is the controls record | small change (follows the physics) | small |
| `champ.c` | stages 0 to 52, `CHAMP.TXT` | leagues with CUSTOMIZE set-ups, weather by chance, points, LEAGUE REPORT | new | medium |
| `menu.c`, `intro.c` | `MENU.DAT`, PCX, `LOGOL.SPR`, `ATTR.TXT`, attract race | 3D rooms (`MENUt1.DAT`), team select with 3D badges, name entry with 3D tyres, `.BKK`, plates, maps, logo scenes, attract race | **new** | large (a plain list menu first: small) |
| `sound.c`, `mixer.c`, `speech.c` | `SFXSBASE/SPFXBASE/SPFXFULL/MENU.DTR`, `.RAW`, commentary | same four tables; engine per car with rates, ambient and animation sounds heard from a distance (64 slots in the game, we have 32 voices); **no commentary** | small change; `speech.c` drops | small |
| `music.c` | cue + `.gog` | cue with `PREGAP`, other names/track table | small change | small |
| `fm.c` | `MUSIC.DAT` (HMP songs, BNK) | not in S2 | drops | none |
| `save.c`, `files.c`, `main.c` | slots 3 to 25, `CHOICE.DAT` layout | other file list, other layout, 31 slots | rewrite of tables | small |

## d) Hardware assumptions

Measured against `core/src/fpga/core` (geom.v, setup.v, raster_tex.v, ...) and the firmware limits; E2/E3.

| Item | Limit | Screamer 1 | Screamer 2 | Verdict |
|---|---|---|---|---|
| Face format: 36 bytes, 3 or 4 corners, colour +2, material +4, corners +8, normal +0x14 | geom.v / geom.c | yes | the same on 12,001 faces (track 1) and 3 more tracks scanned | identical |
| Row pointers per block | 16 (row 0..15 read at block +0x28) | 16 used | 16 used | identical (the "16 against 8" of N2 S2-2 is the old port, not us) |
| Vertices per block | 512 (geom.v `nvert`, `GU_MAX_VERT`) | 230 | 283 (track 3); cars 140 | identical |
| Faces per row | 1024 (`GU_MAX_FACES`) | 168 | 151 | identical |
| Materials | 16-bit signed offset from the MLB base (+-32 KB, 4,096 records) | 3,488 | 6,480 (track 4: 392 materials) | identical; merging cars' libraries stays well inside |
| Material routines | 16, 64, 128, 160, 192, 224 | all | tracks 16/64/128; 208 (= 224); 160 on cars | small change (208 as 224) |
| Texture size | 256 x 256 (9-bit) | 254 x 256 | 254 x 254 | identical |
| Texture frames | 16-bit `n` | up to 61 | 23 (tracks), 128 (cars) | identical |
| Objects per track (firmware `MAX_OBJ`) | 512 | 223 to 390 | **345 to 705** (5 of 7 tracks over) | small change (constant, a few hundred KB of arrays) |
| Placements (firmware `MAX_PIECES`) | 2,560 | 1,164 to 2,359 | 1,099 to **3,742** (track 4 over) | small change (constant); the CPU culling cost grows with it: unmeasured |
| Drivable pieces, faces (`MAX_GROUND` 4,096, `MAX_CFACES` 8,192) | | 487 / 3,299 | 1,055 / **8,079** | small change (raise) |
| Cells of the map | not limited | 222 to 573 | 251 to 843 | identical |
| Track image | 28 CRC blocks = 7 MB, window `0x1210_0000` to `0x1280_0000` | 4.2 to 4.8 MB | 3.0 to 4.9 MB (MLB + UNR + map, no `.WLB`) | identical |
| Cars' libraries | one MLB base register; materials within +-32 KB | cars inside the track MLB | 4 teams x (MLB 0.6 MB + 3DX 0.2 MB) + `MISC` + `ENV` as separate files: merged after the track MLB with rebased offsets (the PC port's `mlb_add`/`mesh_rebase`); 8.3 MB with all four teams loaded against the 7 MB window | small change (a merge step; move the window) |
| See-through table (routine 224/208) | 64 rows x 256 = 16 KB = 16 M10K, texel index 6 bits | 64 rows (row 63 = radar) | made at race start, 112 rows (smoke 0..31 and 48..79, shadow 32..41, brighter and white rows above): needs a 7-bit index; 80 rows would take about +8 M10K, 128 rows +16 | **hardware change** (16 M10K are left: 292 of 308 used) |
| Reflection (routine 160) | wrap at 256, face size scale | car glass uses the track's picture | the picture is the first material of `ENV.MLB` (128 x 128, 1 to 3 frames); wraps at 128 (`& 0x7f7f`, `STARTL.EXE` 0xcd771). The port's notes say the face's size counts 16 x (cars 8 x); the face routine's code (0xcd118: `shl eax,3`, the same as Screamer 1's 0xdb310) takes the reflection's u, v as whole texels like ours, and only the clippers (0xcc6e0, 0xcca6c, 0xcce98) multiply by the material's size and shift by 5 | **no hardware change** (E10): each frame stored tiled 2 x 2 as 256 x 256 after the cars' libraries, the windows' materials point to it; 64 KB a frame |
| Fade by depth (`.DPT`) | - | none | per face: row = |z - near| x 16384 / range & 0x3f80, every texel through a 256-byte row; night (all but one track) and fog | **new hardware** (a 256-byte row buffer per face plus the geometry unit picking the row; small in M10K, a big step in design) or accept day only |
| Mesh class word | track.c sort key | at `+0x32C` | does not exist (header 0xC8); the correct S2 rule is not in NOTES2 | small change (0; 93 of 64,000 pixels differ against using the junk) |
| 65K colours | - | - | `..2` files, `.HIC` | skipped |
| Mirror | - | - | a second drawing pass into 160 x 35 rows (blitter squeezes every other column) | new (software; CPU time unmeasured) |
| Damage | - | - | by texture picture (frames of the car's material via the phase), power loss: software only (N2 S2-5zq) | new, no hardware |
| SDRAM map | `docs/soc.md` | slot sizes as listed | `GRAF1.DAT` over its 4 MiB slot; more slots; car libraries | small change |
| Frame content | 50 fps | start grid with 9 opponents: 257 pieces, ~2,500 faces | 22 to 173 pieces, 547 to 2,111 faces a frame from the road (no cars) | within S1's range; cars add more |

## Per track (Screamer 2 `MAPnH01`, E2/E3; Screamer 1 `MAPnH` for comparison)

Faces and pieces: range over the four cameras on the road (track 1: eight), what the firmware's
`track_draw_cam` hands on and draws, no cars.

| Track | Objects | Cells | Placements (drivable) | Image | Pieces / frame | Faces / frame |
|---|---|---|---|---|---|---|
| S2 1 | 642 | 457 | 2,000 (948) | 4.09 MB | 35 to 149 | 547 to 1,653 |
| S2 2 | 705 | 388 | 1,576 (633) | 4.61 MB | 47 to 142 | 684 to 1,121 |
| S2 3 | 618 | 510 | 2,346 (834) | 4.50 MB | 89 to 166 | 1,198 to 1,650 |
| S2 4 | 618 | 843 | 3,742 (1,055) | 4.79 MB | 114 to 173 | 1,209 to 1,994 |
| S2 5 | 559 | 558 | 2,116 (961) | 4.28 MB | 75 to 118 | 1,105 to 1,508 |
| S2 6 | 705 | 655 | 2,467 (1,047) | 4.91 MB | 82 to 122 | 1,344 to 1,679 |
| S2 7 | 345 | 251 | 1,099 (550) | 3.00 MB | 22 to 97 | 676 to 2,111 |
| S1 1 to 6 | 223 / 390 / 373 / 261 / 360 / 375 | 222 / 385 / 365 / 366 / 573 / 418 | 1,164 (163) / 1,408 / 1,590 / 1,764 / 2,359 / 2,073 | 4.17 (track 1), 4.78 (4) MB | 257 on the grid | 2,497 to 2,587 on the grid |

## e) Work estimate

Calibration: the Screamer core took 10 days from the template to release (2026-10-25 to 11-03, 289
commits). Sizes: small = under a day, medium = 1 to 2 days, large = 3 days or more at that pace.

| Area | Size | Note |
|---|---|---|
| Files, slots, `files.c`, saves | small | but the slot count is a decision |
| Track build on the Pocket (S2 sections, constants, merge of libraries) | small | adapter exists as a PC tool |
| Hardware: 7-bit see-through table, routine 208 (the reflection's wrap is done in the image, E10) | small to medium | Quartus cycles; block RAM 95 % |
| Fade by depth (night, fog) | medium to large | new unit logic; may stay out at first |
| Car, physics, wheels, body, road kinds, damage | large | the heart; 8,000-line reference |
| Opponents, skills, cones, animations (`.PTH`), cameras (`.MCM`), mirror | large | together |
| HUD, cockpits, dashboards | medium | |
| Sound (new tables), CD cue | small | |
| Menus (3D rooms, `.BKK`, league), intro, attract race | large | a plain menu first: small |
| Total for a drivable race with a plain menu | about 8 to 10 days | |
| Total for the game as the original | about 16 to 25 days (2 x Screamer 1) | uncertain: physics and menu |

**First, in this order:** (1) decide the slot economy (user); (2) run the renderer on the Pocket with the
S2 track image (small firmware change, no new hardware): the first S2 picture on the Pocket; (3) compare
one picture against DOSBox's RAM (`tools/dosram.py` style) to confirm sort order without the class word
(not done: the pictures here are only compared by eye); (4) the S2 `CAR_PARAMS` parser and a car that
rolls on a track; (5) hardware items while the physics is written.

## Experiments (all in `tools/s2/`, game data only under `build/`)

| Id | What | Result |
|---|---|---|
| E1 | `s2_unpack.py <Screamer 2 folder>`: every `.DAT` under GFX, DATA, ANIMS, CONFIG with `datfile.py` as it is | 143 files, 1,904 entries, 380,456,051 bytes, 0 failures; `CHOICE.DAT` is a save, not a container |
| E2 | `s2_scan.py`, `s2_scan3dx.py`, `s2_collide.py`: UNR, MLB, MMP, `.3DX` against our limits | the tables above |
| E3 | `make_track_s2.py` runs `tools/make_track.py` unchanged (its `main()` call cut off at load) with these ADAPTERS: file names, the one spline repeated ten times, no markers, one stand-in car row, AUTO1..5 of `CARS101.DAT` as models, empty race screen, no opponents, background colour from `TRACK_DATA`'s "background 256" (milestone 2: no spline at all, the cars merged with their libraries, see E6). `render_s2.sh` + `render_s2.c` build the firmware's `track.c geom.c setup.c raster.c blit.c` natively in Docker and draw from the road's points | all 7 tracks build (3.0 to 4.9 MB) and draw. Unpatched, `track_init` refuses track 1 (642 objects > 512); the script's patches raise `MAX_OBJ`, `MAX_PIECES`, `MAX_BLOCKS` (cars) and set the class word 0 (`S2PATCH=0` shows the refusal). The first try with the opponents' line as the camera path showed bare green ground: the line leaves the road (N2 S2-5c); the camera now follows the road points (MMP array 2) with the height from `track_under` |
| E4 | `check_track_build_s2.sh`: the firmware's own builder on S2 files | first run (milestone 1 tree): stopped at "camera path" (S2 has one spline). Since milestone 2 the builder is `track_build_s2()` (E6) and the script compares it with `make_track_s2.py` byte for byte: **passes for all 7 tracks** (4.6, 5.1, 5.0, 5.2, 4.8, 5.4, 3.5 MB; ASan and UBSan clean) |
| E5 | `S2CAR=CARS101:AUTO3 make_track_s2.py`: one S2 car as the only mesh, its own MLB and COL | drew through `track.c`: `s2_car.png` (windows white: no environment map). Superseded by E6, where the cars are merged into every track image and drawn with their own materials (sponsor logos, paint); the windows showed the cars' placeholder picture and, read at 256 columns, the paint behind it (until E10) |
| E6 | `make_track_s2.py` and `track_build_s2()` (firmware/track_build.c), milestone 2: the image with the cars AUTO1..5 of team 1 merged | the cars' material records (113 of them) go into a section in front of the MLB (a face's material is a signed 16-bit offset, so they must lie within 32 KB of its start), their faces (all three levels of detail) point there, their `.MLB` files follow the track's with the offsets inside them moved; no camera path, markers, race screen, opponents; the far plane in the header (0x70); version 7. Image 3.5 to 5.4 MB, in the 7 MB window as it is (no move needed) |
| E7 | the original's level of detail in `track.c`/`car.c` against the counting build of the study (3,561 road frames: 7 tracks, about 160 road points, 0 / 1 / 4 cars) | pieces, faces, triangles, vertices, objects, nearest piece and 19 more counters equal in every frame; only the triangles split into flat and textured and the rasterizer's walked triangles differ, because the cars now have their own materials (a car body is a flat colour now). PC pictures of all seven tracks with and without the level of detail: the same scene to the eye (`tools/s2/render_s2.sh`, `NOLOD=1`) |
| E8 | the viewer in the Verilator simulation (track 1, 8 frames at the start of the road, 4 cars; the finished image from `make_track_s2.py`, whose bytes the builder reproduces) and on the Pocket | simulation: no trap, frames **6.1 ms mean, 6.3 ms worst** (the cost model of performance.md for these frames: 7.7 ms with the game's physics and preparation of 1.7 ms, so 6.0 ms without; +2 %), 81 pieces and 483 faces (the counting build: 78 to 88 and 481 to 504), the hardware's picture equals the C models' (the PC render of the same camera) in every pixel except the overlay's box, image CRC check 1.4 s. The Pocket's numbers are the test of the forecast: `docs/development.md`, "Viewer" |
| E9 | the viewer's driving mode (Start; `firmware/s2car.h` with the kinematic stand-in `s2car_stub.c` until the real physics is merged): Verilator simulation on scripted buttons, track 1, and the PC renderer (`render_s2.sh ... d<k>`) | no trap, 160 frames at 50.3 a second, 5.5 ms mean and 6.4 ms worst for the chase camera over one car (332 faces, 60 pieces; the 4-car ride took 6.1 ms), the stand-in's step about 0.1 ms; the hardware's last frame equals the PC render of the same step below the overlay's rows. The S2 car model (`AUTO01`, 12 blocks) has its four wheels as separate blocks (hubs `flx blx brx frx` flagged 0x800, tyres `fli fri rli rri`), so `car.c` turns the hubs and steers the front ones; from the chase camera (1,300 units behind) frames with the wheels' steering at +-0.3 rad and a turned hub were pixel-identical, so the wheels are not visible from there. In the world `yaw` grows to the left (the view turns left, the first bend of track 1 raises it): steering right must lower it. The load without the image's CRC check: 654 ms in the simulation instead of 2,091 ms |
| E10 | the cars' windows (routine 160): `make_track_s2.py` / `track_build_s2()` store the first material of `GFX/ENVP{n}01.DAT`'s `ENV.MLB` (128 x 128, 1 to 3 frames: tracks 1, 2, 5 one, tracks 4, 6, 7 two, track 3 three) tiled 2 x 2 as 256 x 256 after the cars' libraries, under a texture record of its own, and the records of the cars' routine-160 materials point to it; `tools/s2/check_env_s2.py` | no hardware change, image version 7 (no new section): +64 KB a frame (+64.3 KB on tracks 1, 2, 5, +128.3 KB on 4, 6, 7, +192.3 KB on 3; the largest image 5.5 MB of 7), 6 to 20 KB more read from the ZIP. The firmware's builder and the reference stay byte-identical (7 of 7), every texel the reflection can address (u, v -512 to 511) equals the original's wrap-128 read. Verilator simulation of the viewer (track 1, 8 frames, 4 cars) with the old and the new image: 6.6 ms mean, 6.7 ms worst, 483 faces in both (frame time unchanged), the hardware's picture equals the PC render in every pixel below the overlay, and 541 pixels differ between the two images: the player's and the other cars' windows, grey streaks of the sky picture instead of the cars' paint. Not measured against the running original (no Screamer 2 DOSBox tooling here): the texel mapping is read from `STARTL.EXE` (0xcd118, 0xcd771, the routine table at 0x76158) and the picture's pixels from the game's file |

The pictures here (`s2_*.png`, 320 x 200, 2.8 to 43 KB, eight of them; made with the first adapters, so the cars in them are not merged) are pictures of the game, not
data to ship; the user decides whether they stay in the repository. The fade (night, fog) is not drawn:
night look 1 renders with its own darker textures only.
