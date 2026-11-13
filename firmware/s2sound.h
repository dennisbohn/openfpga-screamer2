/*
 * The race's sound: the music from the CD image and the effects of DATA/SOUND.DAT through the mixer
 * (hw.h AU_*), driven from the viewer's driving mode with a few calls. The rules are the game's
 * (s2snd.h); this file is the voices, the state between frames and the music's start and fade.
 *
 * Voices of the mixer: 0 .. 3 the four cars' engines, 5 the player's tyres (the squeal, or rolling on
 * grass and the like), 4 the second player's in combat mode, 6 .. 9 the knocks (a wall, a car, a
 * landing; round robin). The game's sound manager has 64 slots of the same kinds.
 *
 * The music: CD_TRACKS of MISC.DTR names the CD track of each of the seven tracks; it starts with the
 * race (the grid, before the lights) and goes on round and round until the race is left: the original's
 * CD never fades (no volume or stop call is made by the race or its end screens, STARTL.EXE 0x4d990 /
 * 0x4d908), only the effects ease out on the end screens (s2sound_effects, s2end.h). Without
 * GAME.GOG / GAME.DAT, or without SOUND.DAT in the ZIP, that part is silent and nothing else is wrong.
 * Everything is silent in the ride (no race). The menus' song (CD track 2) is s2menumusic.h's.
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
/* The track's song (CD_TRACKS of MISC.DTR) from its start, unless it plays already: what s2snd_race_start() does last. The attract race sets s2snd_music_later before s2snd_race_start()
   so that its song begins when its picture comes out of white (STARTL.EXE 0x38207: the song is let go at the third stage of the screen, 0x4d928) and calls this then. */
extern int s2snd_music_later;
void s2snd_music(int track);
/* Once a frame of the race, after the physics' steps and with the camera of the picture: the engines'
   pitch and volume, the tyres, the knocks, and the music's ring kept filled (music_frame()). Costs 0.1 to 0.2
   ms. cam2 is 0 but in combat mode, where it is the second view's camera, and the sound is the original's of that
   mode (STARTL.EXE, the mode word's bit 0x40): the first car's engine is heard from the first camera and the second's
   from the second (0x1fe18: each player hears his own engine and not the other's car), both cars' tyres are heard
   (0x1b77e), the scenery from the first camera; the knocks (a wall, an obstacle, a landing) are the first car's alone,
   as the original's are the followed car's (0x11f0c8, car 0 in the running game's combat race), a knock of the two
   cars being both players'. */
void s2snd_frame(const struct track_cam *cam, const struct track_cam *cam2);
/* The race is left (another track, the ride): every voice and the music stop; a line into the log with the cost
   per frame. Nothing may run on the data slots (a track's load) while music plays, so this comes first. */
void s2snd_stop(void);
/* A race is on: s2snd_race_start() was called and s2snd_stop() not yet (the pause menu and the end screen are in it; the menus' song is not played then) */
int s2snd_racing(void);

/* The menu's sound `id` (MENU.DTR of SOUND.DAT, 0 .. 14: MENU1 .. MENU15, s2snd.h) on a voice of its own, at the sample's rate and the
   effects' master volume. Works in the menus and in the race. Silent without SOUND.DAT. */
void s2sound_menu(int id);
/* The race's effects (engines, tyres, the scenery's sounds, the knocks that start) at f of 0x8000 of their volume from now on, f = 0x8000 giving the
   registers back as the race left them. The original's sound manager has one master volume that 0xaee60 eases to a target by a step a frame: the pause
   eases it to 0 at 32767 a second of 0x7fff and puts it back at once, the end screens ease it to 0 at 16383 a second once the choices are up (s2end.h). The
   knocks playing when the level first goes down are stopped. */
void s2sound_effects(int32_t f);
