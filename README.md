# Screamer 2 for the Analogue Pocket (work in progress)

Started from the Screamer core (analogue-pocket/screamer, release 1.0.2, commit 3b12f21): the
same RISC-V soft core with its 3D units, the SDRAM, the audio and the build tools.

Plan: measure what Screamer 2 needs against what the hardware does (docs/screamer2/), then show
a Screamer 2 track on the Pocket with the Screamer renderer, then cars, physics and menus.
Reference: OpenScreamer's docs/NOTES2.md and the game files in `C:\GOG Games\Screamer 2`.

## Decisions

- **Identity:** core `Chainy.Screamer2`, platform `screamer2`, version 0.1.0 (no release yet), game
  files in `Assets/screamer2/common/`, saves in `Saves/screamer2/Chainy.Screamer2/`. The platform
  image is still Screamer's until there is one for this game.
- **Game files:** one ZIP of the game's `DATA`, `GFX` and `ANIMS` folders in a single data slot
  (`SCREAMER2.zip`), plus the music image `GAME.GOG` and its cue sheet `GAME.DAT` in slots of their own
  (the 32-slot limit, see docs/screamer2/comparison.md). Entries stored ("Store", fast) or deflated
  (slow to load, the player is told before); other compression is refused in plain words. Built:
  `firmware/zip.c`, the files screen `firmware/files.c`, tested by `tools/check_zip.sh`.
- **Quality:** the VGA version (320x200, 256 colours) at DETAIL HIGH with the original's level of
  detail (docs/screamer2/performance.md, section 6). No quality options in the menu, as in Screamer.
- **Track viewer (milestone 2):** the firmware builds a Screamer 2 track image from the ZIP (`track_build_s2()`,
  checked byte for byte against `tools/s2/make_track_s2.py` for all seven tracks) and shows it from the game's
  chase camera over a car driving the road at 150 km/h, with four cars (player + 3), through the hardware
  path with the original's level of detail at DETAIL HIGH (placed meshes 6,000 / 9,000 units, cars 2,000 /
  6,000; far plane from TRACK_DATA). A timing overlay (B) and `debug.txt` give the frame times to hold against the
  forecast of docs/screamer2/performance.md. Buttons and what to look for: docs/development.md, "Viewer". Known
  gaps: the cars' windows show the original's environment picture but it does not slide, no shadows, smoke, props, fade, night, mirror, menus; the ride is silent (the race has sound, next bullet).
- **Driving mode (a race):** Start in the viewer puts you and three computer cars on the game's start grid, holds them for
  the start lights (3, 2, 1, GO) and races three laps: you drive with Screamer 1's buttons (B gas, Y brake, d-pad steering,
  L / R gears), from the physics' chase camera, with speed, gear, lap, position and times on the screen. The computer cars
  each wear their own team's model and paint (teams 2 to 4's cars 2 to 4; the player's is team 1's car 1; the image holds exactly those four, `race_grid`, docs/development.md "Teams") and are driven by the game's own driver along its line (`firmware/race.c`); laps and places are counted on that line. The car is the game's own model in single precision (`firmware/s2car.c`, ported from
  OpenScreamer's routines; the car's numbers read from the game's files, `firmware/s2params.c`), checked against traces of
  the original's routines (docs/screamer2/driving.md): equal on level ground, in the first wall hit and in a jump, the
  car's place kept as a float (the original's whole-number places hold a car back at 50 steps a second). The stand-in
  `firmware/s2car_stub.c` is only built when `s2car.c` is missing (the race needs the real one). Docs: docs/development.md, "Driving mode".
- **Sound (the race):** the music is the CD track the original plays for the track (`CD_TRACKS` of `MISC.DTR`; the optional
  `GAME.GOG` + `GAME.DAT`, without them no music and no error), started with the race and faded out when you finish; the effects come
  from `DATA/SOUND.DAT` in the ZIP: every car's engine (pitch from its revs, volume and stereo place from the camera's distance),
  your tyres (squeal, rolling on rough ground), wall and car knocks and a landing, all by the original's own rules
  (`firmware/s2snd.c`, tested natively with `tools/s2/check_sound.sh`; `firmware/s2sound.c` plays them). Open: the volume balance (not
  heard yet), the scenery's own sounds (birds, mill, crowd), the cones' knock, the car-against-car knock is the firmware's closing
  speed, no pause or menu sounds. The original has no sound for the start lights or the gears. Docs: docs/development.md, "Sound".
