<img src="core/dist/platforms/_images/screamer2.png" alt="Screamer 2" width="521">

# Screamer 2 for the Analogue Pocket

An [openFPGA](https://www.analogue.co/developer) core that runs *Screamer 2* (Milestone / Virgin
Interactive, 1996, MS-DOS) on the Analogue Pocket.

The game's logic runs as C firmware on a RISC-V soft core (VexRiscv, 74.25 MHz), and the 3D
work is done by dedicated hardware units: geometry, triangle setup, rasterizer, blitter and an
audio mixer. The core reads the **unchanged original game files** from the SD card. No game
data is part of this repository or of its releases.

It is the sister project of [Screamer for the Analogue Pocket](https://github.com/dennisbohn/openfpga-screamer)
and shares its hardware.

## Features

- All seven tracks and the four teams with their cars, three computer opponents, the obstacles on
  the road, sparks, wheel smoke, shadows and the moving scenery (helicopters, trains, birds, ...)
- Arcade, Championship (the four leagues with their award scenes) and Time Attack against the
  ghost of your best run (3 LAPS or FREE RUN)
- Combat Mode: two players on a split screen, in the Dock with two controllers
- The four views from the car, the rear-view mirror, and replays from the trackside cameras
- The game's menus: the options room, the car set-ups with CUSTOMIZE, GAME SETTINGS, the high
  scores and the name entry, saved between sessions; the intro and the attract sequence
- Music from the game's CD image, the sound effects of the race and of the menus
- Up to 50 frames per second at the Pocket's native 320 x 200 picture, perspective-correct textures
- Pocket integration: Core Controls remapping, a docked controller's stick works as the d-pad

As in the original, three tracks, the first league and four cars of each team are open at the
start. Winning leagues of the championship opens the other tracks and leagues and the teams'
fifth cars.

Not supported: network and serial-link play (the menu entries are left out), the weather (every
race is a clear day), the SVGA mode and the DETAIL LEVEL setting (the core always shows the VGA
picture at the highest detail), the link port.

## Installation

1. Unzip the release archive into the root of the SD card.
2. Copy your own game files to `Assets/screamer2/common/` on the SD card. For the GOG version, see
   below.
3. Start *Screamer 2* from the Pocket's core list. You find it in the group *Computer*.

The core has been tested only with the GOG release of the game. Other releases might work if their
files are the same.

### GOG version

The game has about 140 files, more than a core can name one by one, so the core reads them from a
single ZIP archive that you make yourself. Three files go to `Assets/screamer2/common/`:

| File | What it is |
|---|---|
| `SCREAMER2.zip` | One ZIP of the folders `DATA`, `GFX` and `ANIMS` of the game's install folder. |
| `GAME.GOG` | The CD image with the game's music, from the install folder, as it is. |
| `GAME.DAT` | The cue sheet of the CD image, from the install folder, as it is. |

`GAME.GOG` and `GAME.DAT` are optional: without them there is no music.

Make the ZIP with the compression method **Store** (no compression), which the core reads fast.
Deflate, the usual ZIP compression, works too, but loading is slow, and the core says so. Other
methods are not supported.

- **Windows 11 24H2 or later:** select the three folders `DATA`, `GFX` and `ANIMS` together,
  right-click, *Compress to*, *Additional options*; archive format ZIP, compression method Store,
  archive name `SCREAMER2.zip`.
- **Other Windows versions:** open a command prompt in the install folder and run
  `tar --format zip --options zip:compression=store -cf SCREAMER2.zip DATA GFX ANIMS`
- **7-Zip:** select the three folders, *Add to archive...*; archive format zip, compression level
  Store, archive name `SCREAMER2.zip`.

When it starts, the core checks the files. If something is missing or wrong with the ZIP, it shows
the list of the files and says what to do. The same instructions are in
`Assets/screamer2/common/README.txt` of the release.

Saves and the debug log go to `Saves/screamer2/Chainy.Screamer2/`. `debug.txt` helps when something
goes wrong. The Pocket writes it when the core is quit.

## Controls

The defaults can be changed in the Pocket menu under *Core Controls*.

| Action | Button |
|---|---|
| Steer, move in the menus | D-pad |
| Accelerate / OK | B |
| Brake | Y |
| Handbrake / Back | A |
| Next view from the car | X |
| Gear up / down (manual gearbox) | R / L |
| Rear-view mirror / Back | Select |
| Pause menu / OK | Start |

In Combat Mode the first player uses the first controller of the Dock and the second player the
second one.

## Building

Quartus Prime Lite 18.1 and the firmware toolchain run in Docker, so you need Docker, a shell
and Python 3. From the repository root:

```sh
tools/fw.sh          # firmware: build/firmware/firmware.bin, program.bin
tools/build.sh       # bitstream: core/output/bitstream.rbf_r
tools/package.sh     # the SD card layout in build/sdcard/
tools/deploy.sh /i   # package and copy to the SD card at /i, with your game files
```

`tools/fw.sh DEBUG=1` builds the development firmware: a frame time display, statistics in
`debug.txt` and everything open from the start. How the core works and how it was built is in
[docs/](docs/), beginning with [docs/development.md](docs/development.md).

## Credits

- [OpenScreamer](https://github.com/ItsDanik/openscreamer) (GPL-3.0): the reference for the
  game's logic and file formats
- [VexRiscv](https://github.com/SpinalHDL/VexRiscv) (MIT): the CPU
- Analogue's [core template](https://github.com/open-fpga/core-template): `core/src/fpga/apf/`
  and the JSON templates in `core/`
- [font8x8](https://github.com/dhepper/font8x8) (public domain): the firmware's test font

*Screamer 2* belongs to its respective owners. This project is not affiliated with them or
with Analogue.

## License

GPL-3.0, see [LICENSE](LICENSE).
