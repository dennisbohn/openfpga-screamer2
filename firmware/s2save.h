/*
 * The save of Screamer 2: CHOICE.DAT (nonvolatile data slot 23, core/data.json), in a layout of our own (docs/screamer2/menus.md
 * "The save"). The file lives in the SDRAM at SAVE_CHOICE_ADDR; the Pocket reads it from there through the bridge when the core is
 * quit or switched off, so there is nothing to write back and nothing to mark: every call below that changes the save also seals it
 * (checksum), and the SDRAM is always what the next start reads.
 *
 *   header    16 bytes   marker "S2SV", version, size, CRC-32 of the main part, CRC-32 of the best times
 *   main     432 bytes   name, the GAME SETTINGS rows, volumes, view, the start menu's last choices, unlocks, per team: bonus car, set-ups
 *   times  18,900 bytes  best times: 3 kinds x 7 tracks x 5 lists x 15 rows of 12 bytes
 *
 * s2save_load() at boot reads the slot and starts from the defaults for anything that is missing, short, foreign or damaged (each
 * part on its own: a bad main part keeps the times); it also reads the old layout (Screamer 1's block with the start menu's
 * choices in its unused fields) once. Before it has run, the getters answer within their ranges all the same (whatever the SDRAM holds is clamped).
 */
#pragma once
#include <stdint.h>

#define S2SAVE_VERSION      1
#define S2SAVE_MAIN_OFF     16
#define S2SAVE_MAIN_SIZE    432
#define S2SAVE_TIMES_OFF    448
#define S2SAVE_TIMES_SIZE   18900
#define S2SAVE_SIZE         19348           /* core/data.json: size_maximum 36,864 leaves room for later versions */

/* The old layout, read once and replaced (s2save_load()): the core's first builds kept the start menu's choices in the unused fields of
   Screamer 1's settings file, 0x460 bytes as that game reads CONFIG\CHOICE.DAT (little-endian). Of it the track, the team (intro), the
   car, the laps, the difficulty and the marker with the gearbox (failed) are read; the rest is that game's and means nothing here. */
#define CHOICE_SIZE 0x460
struct choice_rec { float time[5]; char name[5][4]; int32_t flag[5], mode[5]; };
struct choice_dat {
    int32_t car, track;                     /* +0x000 */
    char name[4];                           /* +0x008 */
    int32_t detail, difficulty, laps;       /* +0x00c */
    int32_t stage, prev, total;             /* +0x018 */
    float time[6];                          /* +0x024 */
    int32_t pos[6], pts[6];                 /* +0x03c */
    struct choice_rec lap[6], race[6];      /* +0x06c, +0x24c */
    int32_t bullet, view, intro, speech, music;     /* +0x42c */
    int32_t failed;                         /* +0x440 */
    int32_t volume[3], unused1, level, level_shown, difficulties;   /* +0x444 .. +0x45c */
};

/* Everything that may be open from the start for testing: 1 = every track, league and bonus car is open, whatever the save says.
   The firmware's Makefile sets it: 0 in a release, the original's gating (three tracks, league 1 and four cars a team at first;
   the championship opens the rest), and 1 in a debug build (tools/fw.sh DEBUG=1; UNLOCK_ALL=0 or 1 on the command line says
   otherwise). The 1 below is what the PC tests compile with. The variable is the runtime switch and is not saved */
#ifndef S2SAVE_UNLOCK_ALL
#define S2SAVE_UNLOCK_ALL   1
#endif
extern int s2save_unlock_all;

/* ------------------------------------------------------------------------------------------------ settings */

/* The numbers: s2save_get / s2save_set. The first eight are the GAME SETTINGS page's rows in its order, kept as the index of the
   value in the page's list (STRINGS.TXT): TRANSMISSION AUTOMATIC / MANUAL; DETAIL LEVEL LOW / MEDIUM / HIGH; NUMBER OF LAPS 1 / 3 / 5 / 10;
   ARCADE ENEMIES SKILL ROOKIE / NORMAL / PRO; MULTIPLAYER ENEMIES OFF / ON; TIME ATTACK GHOST CAR ON / OFF; DAMAGES OFF / ON; MUSIC ON / OFF.
   The start menu's laps, skill and gearbox rows are the same three settings. */
enum {
    S2S_TRANS, S2S_DETAIL, S2S_LAPS, S2S_SKILL, S2S_MP_ENEMIES, S2S_GHOST, S2S_DAMAGE, S2S_MUSIC,
    S2S_VIEW,                               /* the camera a race starts in (the game's view number 1 .. 7; 2 is the first from the car) */
    S2S_VOL_FX, S2S_VOL_CD, S2S_VOL_FM,     /* 0 .. 0x7FFF; 0x6000 is the nominal level (the effects', music_master()'s; the third is the game's MIDI volume, kept and not used) */
    S2S_TRACK, S2S_TEAM, S2S_CAR,           /* the start menu's last choices: track 1 .. 7, team 1 .. 4, car 1 .. 5 as the files number them */
    S2S_LEAGUE_OPEN,                        /* 0 .. 3: the highest league open (league 1 is open at the start) */
    S2S_TRACKS_OPEN,                        /* 0 .. 4: tracks opened beyond the first three (the seventh needs 4) */
    S2S_PROFILE,                            /* 0, 1: the player has been through TEAM SELECTION and ENTER YOUR NAME (a new save has not; it is a byte of the main part's reserved bytes: no new version) */
    S2S_KEYS
};

int s2save_get(int key);                    /* the value, within the key's range */
void s2save_set(int key, int value);        /* clamped to the range, sealed */
int s2save_count(int key);                  /* how many values the key has (the row of the page steps round through them) */

/* The player's name: three printable characters and a 0 ("MIL" at first) */
const char *s2save_name(void);
void s2save_set_name(const char *name);

/* ------------------------------------------------------------------------------------------------ unlocks */

/* The seven tracks open in the original's order (England, Egypt, California, Finland, Switzerland, Columbia, The Tunnel): the first
   three at the start, one more for each league won, the last when league 4 is won with all four teams. s2save_track_at(0 .. 6)
   is the track (1 .. 7) in that place. */
int s2save_track_at(int pos);
int s2save_track_open(int track);           /* track 1 .. 7 */
int s2save_league_open(int league);         /* league 0 .. 3 */
int s2save_car5_open(int team);             /* the team's fifth (bonus) car, team 1 .. 4: won with the team in league 4 */
void s2save_set_car5(int team, int won);

/* The six numbers of a car's set-up (HANDLING, TIRE PRESSURE, TIRE TYPE, FRONT, REAR SUSPENSION, BRAKES in the file's order: each
   -1 .. 4; the game's own start is 2 2 0 2 2 2): team 1 .. 4, one of its first four cars 1 .. 4, Custom 1 .. 4 as slot 0 .. 3.
   0 for a wrong number. */
const int8_t *s2save_setup(int team, int car, int slot);
int s2save_set_setup(int team, int car, int slot, const int8_t six[6]);

/* ------------------------------------------------------------------------------------------------ best times */

/* The game keeps the best times by kind of game, track and what is timed: the total of a race of 1, 3, 5 or 10 laps (lists 0 .. 3, the
   number of the NUMBER OF LAPS value) or the best single lap (list S2S_LAP), fifteen rows each, the quickest first. Time attack has
   only the 3-lap totals: a total list asked for with S2S_KIND_ATTACK is that one. */
enum { S2S_KIND_LEAGUE, S2S_KIND_ARCADE, S2S_KIND_ATTACK, S2S_KINDS };
#define S2S_TRACKS  7                       /* track 1 .. 7 */
#define S2S_LAP     4
#define S2S_LISTS   5
#define S2S_ROWS    15
#define S2S_NO_TIME 3599.999f               /* the time of a row nobody has driven, as in the game's file */

struct s2save_row {
    float time;                             /* seconds; S2S_NO_TIME if the row is empty (its name MIL, team (row & 3) + 1, car 1) */
    char name[4];                           /* three characters and 0 */
    uint8_t team, car;                      /* team 1 .. 4 and car 1 .. 5 of the driver */
    uint16_t replay;                        /* the number of a saved replay of this run, 0 for none */
};

/* Row 0 .. 14 of a list; 0 for a wrong number. The pointer stays good until the next s2save_add_time(). */
const struct s2save_row *s2save_row(int kind, int track, int list, int row);
static inline int s2save_row_empty(const struct s2save_row *r) { return r->time >= S2S_NO_TIME; }

/* A time into a list ahead of the first it is not slower than (an equal time goes ahead of the old one, as the game does), the rows
   below move down, the fifteenth falls out. name: three characters (0: the player's name); team 1 .. 4, car 1 .. 5. Returns the row
   it took, or -1 if it is not among the fifteen (or not a time: 0 or less, or S2S_NO_TIME and over). */
int s2save_add_time(int kind, int track, int list, float time, const char *name, int team, int car, int replay);
void s2save_set_replay(int kind, int track, int list, int row, int replay);

/* ------------------------------------------------------------------------------------------------ the file */

/* At boot, early (save_load(), save.c): reads the slot into the SDRAM, makes it a valid save, tells the APF the full size (it keeps a
   size only for a file it found, and writes the slot back with it), and leaves one line in the debug log. */
void s2save_load(void);
/* All defaults, sealed (the first start; the tests) */
void s2save_reset(void);
