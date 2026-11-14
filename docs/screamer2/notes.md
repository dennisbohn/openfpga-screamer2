# Screamer 2 for the Analogue Pocket: the project's notes

(This was the repository's README while the core was built, up to release 1.0.0; README.md is the player's page now. The timing overlay,
`debug.txt`'s statistics and "everything is open from the start" are the debug build's since then: docs/development.md, "Release and debug builds".)

Started from the Screamer core (analogue-pocket/screamer, release 1.0.2, commit 3b12f21): the
same RISC-V soft core with its 3D units, the SDRAM, the audio and the build tools.

Plan: measure what Screamer 2 needs against what the hardware does (docs/screamer2/), then show
a Screamer 2 track on the Pocket with the Screamer renderer, then cars, physics and menus.
Reference: OpenScreamer's docs/NOTES2.md and the game files in `C:\GOG Games\Screamer 2`.

## Decisions

- **Identity:** core `Chainy.Screamer2`, platform `screamer2`, version 1.1.1 (1.0.0 was the first release, 2026-11-14), game
  files in `Assets/screamer2/common/`, saves in `Saves/screamer2/Chainy.Screamer2/`. The platform
  image is the game's own since release 1.0.0.
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
  gaps: the cars' windows show the original's environment picture (sliding with the car), no fade, night, the original's menus (most are built, see below; docs/screamer2/menus.md has what is left); the ride is silent (the race has sound, next bullet).
- **The menu:** after a loading screen and the intro (the attract sequence: logos, a race of four computer cars under trackside cameras, the credits; any button leads on) and, on a new save, TEAM SELECTION and ENTER YOUR NAME, the original's options room: the team's room as a 3D scene, the camera gliding between five stations
  with left / right (ARCADE, CHAMPIONSHIP, TIME ATTACK, MULTIPLAYER, GAME SETTINGS), B or Start take the choice, A or Select go back to TEAM SELECTION (`firmware/s2room.c`; docs/development.md "The options room").
  The stations lead to ARCADE (the original's arcade pages: the title with the team's four cars driving past, TRACK with the tracks' pictures, CAR, TRANSMISSION; B or Start go on, A or Select go back), CHAMPIONSHIP (the four leagues: LEAGUE SELECTION, WELCOME with the league's track plates flying in,
  the races with the LEAGUE REPORT after each, what winning opens and the desk scene, a 3D room with your car, after a league is won (league 4 shows the team's new fifth car as well); before each race the CAR page with its set-ups Standard / Custom 1 .. 4 and CUSTOMIZE, which change the car as in the original, both in the team's room with the camera going round the car or flying to each item: docs/development.md "The championship", "CUSTOMIZE and the set-ups", "The garage"), TIME ATTACK (TRACK SELECTION with the turning 3D map, the same CAR page, 3 LAPS or FREE RUN with its five splits, the race
  against the ghost of the track's best run, re-simulated from recorded controls; docs/development.md "Time attack"), MULTIPLAYER (COMBAT MODE: two players on a split screen, the second on the Dock's second controller) and GAME SETTINGS
  (laps, level, gearbox, ...; VIEW HIGH SCORES). Everything is open from the start. Start in the race opens the pause menu (RESUME, RESTART, EXIT); the end screen of an arcade or championship race offers REPLAY (the race run again from the player's recorded
  controls, from the trackside cameras with the original's band; docs/development.md "Replay"). The choices are kept across power-ups. 30 s with no button in the room (or on TEAM SELECTION, as the original) starts its attract sequence: the Virgin and Milestone logos, then a race of four computer cars under the trackside cameras out of white with the credits of `ATTRACT.TXT` over it, a different track each time (`firmware/s2attract.c`; docs/development.md "The attract sequence"); any button ends it.
  Docs: docs/development.md, "The menu".
- **Driving mode (a race):** a race from the menu puts you and three computer cars on the game's start grid, holds them for
  the start lights (3, 2, 1, GO) and races three laps: you drive with Screamer 1's buttons (B gas, Y brake, d-pad steering,
  L / R gears), from the original's views (X changes the view as in Screamer 1: INCAR, INSIDE with the cockpit, NEAR, BACK the chase camera; Select the rear-view mirror; docs/screamer2/hud.md section 5), with speed, gear, lap, position and times on the screen. The computer cars
  each wear their own team's model and paint (by default teams 2 to 4's cars 2 to 4, the player's is team 1's car 1; the menu changes the player's and the computers get the other teams; the image holds exactly those four, `race_grid`, docs/development.md "Teams") and are driven by the game's own driver along its line (`firmware/race.c`); laps and places are counted on that line. The car is the game's own model in single precision (`firmware/s2car.c`, ported from
  OpenScreamer's routines; the car's numbers read from the game's files, `firmware/s2params.c`), checked against traces of
  the original's routines (docs/screamer2/driving.md): equal on level ground, in the first wall hit and in a jump, the
  car's place kept as a float (the original's whole-number places hold a car back at 50 steps a second). The stand-in
  `firmware/s2car_stub.c` is only built when `s2car.c` is missing (the race needs the real one). Docs: docs/development.md, "Driving mode".
- **Sound (the race):** the music is the CD track the original plays for the track (`CD_TRACKS` of `MISC.DTR`; the optional
  `GAME.GOG` + `GAME.DAT`, without them no music and no error), started with the race and playing on until you leave it (the original's CD is never faded); the end screen eases the effects out as the original does; the effects come
  from `DATA/SOUND.DAT` in the ZIP: every car's engine (pitch from its revs, volume and stereo place from the camera's distance),
  your tyres (squeal, rolling on rough ground), wall and car knocks and a landing, all by the original's own rules
  (`firmware/s2snd.c`, tested natively with `tools/s2/check_sound.sh`; `firmware/s2sound.c` plays them). Open: the volume balance (not
  heard yet), the car-against-car knock is the firmware's closing
  speed, no menu sounds in the start menu. The original has no sound for the start lights or the gears. **The menus have the original's song** (CD track 2, MUSIC ON): it begins with the first page after the intro and after every race, goes on through the pages, begins again at the desk scene and when GAME SETTINGS' MUSIC
  is turned on, and stops with MUSIC OFF or when a race begins. The CD plays through loads, the core cannot: a long load (the options room's build, the desk scene) holds the song and it goes on from where it stood once the page is up; short reads do not chop it (not heard yet). Docs: docs/development.md, "Sound" and "The menus' music".
- **Animated scenery (the race):** the tracks' moving things run along their paths as the original runs them at DETAIL HIGH (`firmware/s2anim.c`): the helicopters of
  Columbia, Egypt and Switzerland (Egypt's comes 7 s into the racing), England's and Columbia's flocks of birds, California's airship, cable cars and bulldozers, Finland's trains;
  and the sounds that stand in places (crowds, market, mill, traffic, works, wolves, cows, ...), heard from the camera. Their materials are the track's `MISC.MLB`, which the
  track image now holds. The helicopters' rotors are see-through (the game's routine 208) and are drawn through the wheel smoke's table (docs/development.md, "Wheel smoke and shadows"). Checked against the running original's memory (`tools/s2/check_anim_s2.sh`, `tools/s2/dosbox_anim.py`). Docs: docs/development.md, "Animated scenery".
- **Obstacles and sparks (the race):** the cones, fences, chests and crates that stand on England's, California's, Egypt's and Switzerland's roads (`firmware/s2props.c`) are knocked by the cars, fly, bounce and tumble as the original has it (it does nothing to the car that knocks one), and two cars touching or a car's knock against a wall throw sparks. Checked against the running original's memory frame by frame (`tools/s2/check_props_s2.sh`). Docs: docs/development.md, "Obstacles and sparks".
