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
- **Game files:** one uncompressed ZIP ("Store") of the game's `DATA`, `GFX` and `ANIMS` folders in
  a single data slot, plus the music image `GAME.GOG` in its own slot (the 32-slot limit, see
  docs/screamer2/comparison.md). Not built yet.
- **Quality:** the VGA version (320x200, 256 colours) at DETAIL HIGH with the original's level of
  detail (docs/screamer2/performance.md, section 6). No quality options in the menu, as in Screamer.
