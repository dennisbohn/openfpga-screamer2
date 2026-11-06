# Firmware

The programs for the core's RISC-V CPU, in two parts:

- `firmware.bin`, the boot loader in the boot RAM (`boot.c`): self-tests, memory test, checks
  and starts the program. At most 32 KB; the build fails above that.
- `program.bin`, the game, in the SDRAM from `0x1300_0000` (`main.c`), running through the
  CPU's caches. Its header holds the size, a CRC-32 and the entry point.

The memory map, the registers and the boot sequence are in [`docs/soc.md`](../docs/soc.md),
the memory system and its test in [`docs/memory.md`](../docs/memory.md).

At start the firmware clears the debug log, writes the checksum of its own image into it and
tests the CPU (integer multiply, divide and shifts, floating point, the CFU). A short memory
test of the SDRAM follows; only a failure leaves its results on the screen. Then `program.bin`
reads the game's ZIP from its data slot (`zip.c`) and shows the list of the game's files (`files.c`):
green where they are, red where a needed one is missing, grey for the optional ones, and in plain words
what is wrong with the ZIP. When nothing is missing it says "ready" and reads the first race's small
files whole as a test, logging the time of each (`zip: <name> method <m> <bytes> bytes in <ms> ms`).
When the files are all there the menu (the options room, `s2room.c`, run by `s2menu.c`) follows and then the viewer and the race (`viewer.c`); the Screamer 1 game code
is still in the tree and not reached. A crash (trap) ends up in the log with `mcause`, `mepc` and `mtval`.

## Building

```sh
tools/fw.sh
```

GCC 14 and picolibc from Debian trixie run in Docker (`firmware/docker/Dockerfile`, base image
and package versions pinned); the script builds the image on first use. The result is in
`build/firmware/`: `firmware.bin`, `program.bin`, and for each the `.elf`, `.lst`
(disassembly) and `.map`. Both binaries are copied to `build/assets/screamer2/Chainy.Screamer2/`;
`tools/package.sh` and `tools/deploy.sh` take them from there to the SD card.

## Files

| Files | Content |
|---|---|
| `start.S`, `link.ld`, `boot.c` | boot loader: jump at `0x0`, trap entry at `0x20`, everything in the boot RAM below 24 KB |
| `start_prog.S`, `link_prog.ld`, `prog_crc.py` | program: header, start, its own trap entry; the build writes the CRC-32 into the header |
| `main.c` | program flow: loading, menus, race loop, pause, replay, restart |
| `hw.h` | addresses and registers from `docs/soc.md`, `rdcycle`, `fixmul` (CFU), cache instructions |
| `slot.c`, `dat.c`, `crc32.c` | data slots (deferred load), the game's `.DAT` containers, CRC-32 |
| `zip.c` | the game's single ZIP in a data slot: the central directory (up to 512 files, names matched on the end of the path), stored entries read through word-aligned slot reads, deflated entries inflated into the destination buffer (own inflate, 64 KB input chunks, CRC-32 checked). 110 KB of .bss; tested by `tools/check_zip.sh` |
| `setup.c`, `geom.c`, `raster.c`, `blit.c` | C models of the hardware units and their drivers: setup, geometry, rasterizer, blitter. Each model is bit-exact with its Verilog. |
| `mixer.c` | C model of the audio mixer (`audio.v`) |
| `track.c`, `track_build.c`, `tb_gfx.c`, `tb_hud.c`, `tb_mmp.c`, `tb_spl.c` | the track: drawing it, and building its image from the game's own files |
| `s2teams.c` | the cars of the race, each its own team's (`race_grid`): reads the two entries of each car from the team container in the ZIP and unpacks them for the track image |
| `efx.c` | Screamer 1's effects through its lookup table (glass, shadows, smoke); Screamer 2's are `smg.c` and `s2fx.c` |
| `smg.c` | the see-through table in the hardware's 64 rows (docs/development.md "Wheel smoke and shadows"): the image builder gives each texel the see-through textures use a row, copies the textures with their texels as rows and makes the table; tested against `tools/s2/smg_ref.py` by `tools/s2/check_smg_s2.sh` and the image check |
| `s2fx.c` | wheel smoke and shadows of the race: the puffs of the four cars (the game's rule of `0x204ae`), the shadows (`0x1f4a7`), as objects for `track_draw_cam()`; checked against recordings of the running game (`tools/s2/check_fx_s2.sh`) |
| `drive.c`, `car.c` | the player's car: engine, gearbox, brakes, steering, walls, crashes, cameras |
| `opp.c`, `scenery.c`, `ghost.c` | opponents, props and moving scenery, the time-attack ghost |
| `champ.c`, `save.c` | the championship, saved data (nonvolatile data slots) |
| `menu.c`, `hud.c`, `intro.c` | menus, the race screen, the intro |
| `sound.c`, `speech.c`, `music.c` | sound effects, the race commentary, the music: streamed from the CD image, or the MIDI songs on the FM chip |
| `s2menu.c` | Screamer 2's menu: its run on the options room (`s2menu_room`, `s2room_menu()` by default; the arcade pages stand in if the room cannot be built), the names from `DATA/LOCAL.DAT`, the choices kept in the save (`s2save.c`); the grid, the kept choices and the run on a stand-in room are tested on the PC (`tools/menu_screens.sh`) |
| `s2room.c` | the options room (docs/screamer2/menus.md section 6, row 8): the team's room (`MENU<t>1.DAT`) built like a track by `track_build_s2()` without cars and drawn by the track's engine, the five stations of `P_<t>.MCM`, the camera's glides (the original's easing, checked frame by frame against the running game), the band, texts, arrows and the fade out; `tools/menu_screens.sh` (`tools/ui_room_ref.py`), `tools/room.sh` against the running original |
| `s2save.c` | Screamer 2's save (nonvolatile data slot `CHOICE.DAT`): its own layout with a CRC-32 per part, the settings, volumes, unlocks, set-ups and the 105 best-times lists, loaded at boot with defaults for a missing, short, foreign or damaged file and a one-time migration of the old layout; `tools/s2/check_save_s2.sh` |
| `s2anim.c` | the tracks' animated scenery (docs/development.md "Animated scenery"): `ANIMATIONS` rows of `MAPnDAT.DTR` run along the cubic paths of `MAPn.PTH` by the race clock as `STARTL.EXE` `0x2cee4` does, their `.3DX` meshes (materials: the image's copy of `MISC.MLB`) as scene objects after the cars, and the sound rows for `s2sound.c`; its path reader and evaluation also serve other path files (`s2anim_paths_*`); `tools/s2/check_anim_s2.sh` (against `tools/s2/anim_ref.py` and the running original) |
| `s2snd.c`, `s2sound.c` | Screamer 2's race sound: `s2snd.c` the rules without hardware (the samples of `DATA/SOUND.DAT`, `CD_TRACKS`, an engine's rate, volume and pan, the tyres, the knocks; `tools/s2/check_sound.sh`), `s2sound.c` the voices, the music's start and fade, called from `viewer.c` |
| `s2gfx.c`, `s2page.c`, `s2load.c`, `s2pause.c` | the 2D toolkit of Screamer 2's menu pages: `.BKK` pictures, sprites and fonts of `GRAF1.DAT`, text, the page runner (keys, fades), the LOADING screen and the race's pause menu (docs/screamer2/menus.md section 6; `tools/menu_screens.sh`) |
| `s2arcade.c` | the original's arcade pages on the toolkit: ARCADE title, TRACK page (sprites of the tracks with their banners, the frame, the easing), CAR and TRANSMISSION page (one page in two states, the 3D carousel of `s2showroom.c` in it), and the chain of them (ENTER on, ESC back); positions, easing, blinking, fades and sounds read from `STARTL.EXE`, checked against the running original (`tools/s2/dosbox_arcade.py`) and a model (`tools/menu_screens.sh`) |
| `s2scene.c`, `s2pages3d.c`, `s2showroom.c` | the 3D menu pages (docs/screamer2/menus.md section 6, row 5): the toolkit (entries of the ZIP's containers read into the SDRAM at `0x1180_0000`, the geometry unit set up with the page's material file, blocks handed on as pieces), TEAM SELECTION and ENTER YOUR NAME (four badges, a ring of 26 letter tyres), the CAR page's carousel of the team's cars |
| `s2arcscene.c` | the ARCADE title's four cars (docs/development.md "The arcade title's cars"): the paths of `DATA/GEN.DAT`'s `SCENE0..3.PTH` (read by `s2anim.c`'s path reader into a set of their own), four takes of 201 frames, a still camera, the cars drawn by `s2showroom.c`; `tools/scene.sh` (against the running original) |
| `s2league.c`, `s2leaguepages.c` | the championship (docs/screamer2/menus.md section 6, row 6): the rules without hardware (the four leagues, the grid, the places, 10 / 6 / 3 / 0 points, the standings, what winning opens; `tools/s2/check_league_s2.sh`) and the pages LEAGUE SELECTION, WELCOME TO LEAGUE n and LEAGUE REPORT with the question LEAVING LEAGUE? on the toolkit; the viewer runs the races between them |
| `s2ghost.c`, `s2trackmap.c`, `s2trackpage.c` | time attack (docs/screamer2/menus.md section 6, row 7): the ghost (one byte of the player's controls a step, the best run of each track as a block of `ATTACK.DAT`, the second car re-simulated; `tools/s2/check_ghost_s2.sh`), the seven track maps of `MAPS1.DAT` (a block over the geometry unit's limits is cut by the geometry driver for every piece alike: `geom.c`, `tools/s2/check_cut_s2.sh`) and the TRACK pages with the turning 3D map (time attack's TRACK SELECTION and the championship's TRACK page; `tools/trackmap.sh` against the running original); the race kinds `RACE_ATTACK` / `RACE_FREE` (the free run's five splits) are in `race.c` |
| `s2cam.c` | the replay's cameras (docs/screamer2/menus.md section 6, row 10; no hardware): the track's trackside cameras (`CAMERAS<n>.MCM`, the five kinds, the choice of the nearest) and the car followed; the recording and the replay of a race are in `race.c` (`race_replay`) on `s2ghost.c`'s recorder; `tools/s2/check_replay_s2.sh` |
| `s2end.c`, `s2pages.c` | the end of the race (CONGRATULATIONS, REPLAY / RACE AGAIN / EXIT, the best times; the player's times into the save) and the GAME SETTINGS, HI SCORES and MULTIPLAYER MENU pages, on the toolkit (docs/screamer2/menus.md section 6) |
| `fm.c` | the FM music: HMI's song player and OPL2 driver, queueing the chip's register writes with their times |
| `files.c` | the files screen: the ZIP's state, every file green/red/grey, the notice for a compressed ZIP, the read test |
| `console.c`, `screen.c`, `dlog.c`, `selftest.c`, `memtest.c`, `trap.c` | text drawing, the test band, the debug log, self-tests, the memory test, the trap handler |
| `font8x8_basic.h` | font from [dhepper/font8x8](https://github.com/dhepper/font8x8) (public domain), unchanged |

## Getting the debug log from the Pocket

The Pocket saves the log area when you quit the core from its menu or switch it off, also
after a CPU crash. Connect the Pocket by USB as a drive (or insert the SD card) and open
`/Saves/screamer2/Chainy.Screamer2/debug.txt`, or run `tools/pocketlog.py` for a short table. The
old `debug.txt` is loaded into the area at start but the core ignores it: after the core has
started, the content is from this run (a file of only zeros means the CPU never ran). When you
only reload the firmware from the menu, that is a reset, and the previous run's log stays until
the new firmware clears it; the checksum in the second line shows which firmware wrote it.

The first line is rewritten after every frame: if its counter stands still, the firmware hung
there. The checksum must be the one `tools/fw.sh` printed, otherwise a different file reached
the boot RAM. A failed test names the check, the actual and the expected value. A crash looks
like this:

```
TRAP mcause=00000005 load access fault
     mepc=00000178 mtval=f0000000 ra=00000174 sp=00003f60
```

`mepc` is the address of the instruction that trapped; look it up in
`build/firmware/firmware.lst` (for the same firmware version).

A failed memory test lists each error with address, expected value, actual value and two
re-reads (at most eight lines per memory), for example
`  bytes @10000080 want 5aa5c35e got 5aa55e3c 5aa55e3c 5aa55e3c`, and ends the test line with
"FAIL, errors n". The phase map shows one `#` per 168 ps step where the SDRAM works and `.`
where it does not; the firmware picks the middle of the longest run.
