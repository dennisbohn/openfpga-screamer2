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
  gaps: white car windows, no shadows, smoke, props, fade, night, mirror, menus, sound, music.
- **Driving mode:** Start in the viewer puts the player's car on the start line and lets you drive it with
  Screamer 1's buttons (B gas, Y brake, d-pad steering, L / R gears), from the physics' chase camera, with speed
  and gear on the screen. The car physics sits behind `firmware/s2car.h`; until the real physics
  (`firmware/s2car.c`) is merged a kinematic stand-in (`firmware/s2car_stub.c`) drives the car: it is not
  the game's car. Docs: docs/development.md, "Driving mode".
