/*
 * Screamer 2's sound: the rules the race's sound follows, without any hardware (the driver is s2sound.c).
 * So far the music's: which CD track plays for which track.
 */
#pragma once
#include <stdint.h>

/* CD_TRACKS of DATA/GAME.DAT's MISC.DTR: the CD track of each of the 7 tracks, in the game's order (the race's set-up,
   0x2eb98 .. 0x2ebdc in STARTL.EXE, plays CD_TRACKS[track - 1]); 1 if it read all seven */
int s2snd_cd_tracks(const uint8_t *game_dat, uint32_t size, int out[7]);
