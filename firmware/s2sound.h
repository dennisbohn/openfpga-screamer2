/*
 * The race's sound: the music from the CD image and the effects of DATA/SOUND.DAT through the mixer
 * (hw.h AU_*), driven from the viewer's driving mode with a few calls. The rules are the game's
 * (s2snd.h); this file is the voices, the state between frames and the music's start and fade.
 *
 * Voices of the mixer: 0 .. 3 the four cars' engines, 5 the player's tyres (the squeal, or rolling on
 * grass and the like), 6 .. 9 the knocks (a wall, a car, a landing; round robin). The game's sound
 * manager has 64 slots of the same kinds.
 *
 * The music: CD_TRACKS of MISC.DTR names the CD track of each of the seven tracks; it starts with the
 * race (the grid, before the lights), goes on round and round, and fades out over 3 s once the player
 * has finished. Without GAME.GOG / GAME.DAT, or without SOUND.DAT in the ZIP, that part is silent
 * and nothing else is wrong. Everything is silent in the ride (no race).
 */
#pragma once
#include <stdint.h>

struct zip;
struct track_cam;

/* Once, before the first track: SOUND.DAT from the ZIP into the SDRAM (up to 2 MB at 0x11C0_0000), its
   samples listed, and the music's cue sheet read. Whatever is missing is left out. */
void s2snd_init(struct zip *z);
/* A race starts on track 1 .. 7 (race_init worked): the voices stop, the music of the track starts unless it plays
   already, and the sounds begin with the next s2snd_frame() */
void s2snd_race_start(int track);
/* Once a frame of the race, after the physics' steps and with the camera of the picture: the engines'
   pitch and volume, the tyres, the knocks, the music's fade, and the music's ring kept filled
   (music_frame()). Costs 0.1 to 0.2 ms. */
void s2snd_frame(const struct track_cam *cam);
/* The race is left (another track, the ride): every voice and the music stop; a line into the log with the cost
   per frame. Nothing may run on the data slots (a track's load) while music plays, so this comes first. */
void s2snd_stop(void);

/* The menu's sound `id` (MENU.DTR of SOUND.DAT, 0 .. 14: MENU1 .. MENU15, s2snd.h) on a voice of its own, at the sample's rate and the
   effects' master volume. Works in the menus and in the race. Silent without SOUND.DAT. */
void s2sound_menu(int id);
/* The race's effects (engines, tyres) at f of 0x8000 of their volume, f = 0x8000 giving the registers back as the race left them: the
   pause eases them out and restores them at once (the original's, 0xaec90). Knocks are stopped. */
void s2sound_effects(int32_t f);
