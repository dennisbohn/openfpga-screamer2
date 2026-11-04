/*
 * The race's sound, driven from the viewer's driving mode with a few calls. So far the music: the CD
 * image's audio tracks through the mixer's ring (music.c).
 *
 * CD_TRACKS of MISC.DTR names the CD track of each of the seven tracks; it starts with the race (the
 * grid, before the lights), goes on round and round, and fades out over 3 s once the player has
 * finished. Without GAME.GOG / GAME.DAT nothing plays and nothing is wrong. Everything is silent in the
 * ride (no race).
 */
#pragma once

struct zip;
struct track_cam;

/* Once, before the first track: the music's cue sheet read (music_init) */
void s2snd_init(struct zip *z);
/* A race starts on track 1 .. 7 (race_init worked): the music of the track starts unless it plays already */
void s2snd_race_start(int track);
/* Once a frame of the race, after the physics' steps: the music's fade, and its ring kept filled (music_frame()) */
void s2snd_frame(const struct track_cam *cam);
/* The race is left (another track, the ride): the music stops. Nothing may run on the data slots (a track's load)
   while music plays, so this comes first. */
void s2snd_stop(void);
