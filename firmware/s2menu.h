/*
 * The start menu of Screamer 2 (docs/screamer2/menus.md has the plan for the rest of the original's screens): a list in the
 * console's text style, shown after the files screen and whenever the race or the ride is left with Select + B. Everything is
 * open from the start (all seven tracks, all four teams and their five cars, no championship gating).
 *
 *   TRACK                the seven tracks (names from DATA/LOCAL.DAT's STRINGS.TXT)
 *   TEAM, CAR            the player's team (CONDOR, ANGELS, WASP, ZEUS) and car 1 .. 5: slot 0 of race_grid (race.h); the three
 *                        computers get the three other teams
 *   NUMBER OF LAPS       the original's options, 1 / 3 / 5 / 10
 *   ARCADE ENEMIES SKILL ROOKIE / NORMAL / PRO
 *   TRANSMISSION         automatic / manual
 *   START RACE, WATCH    the race, or the viewer's ride (for timing)
 *
 * The choices are kept in the nonvolatile CHOICE.DAT (s2save.h, data slot 23): the Pocket writes it back when the core
 * exits, so they survive a power-up.
 */
#pragma once
#include <stdint.h>
#include "s2teams.h"

struct zip;

#define S2M_TRACKS  7
#define S2M_TEAMS   4
#define S2M_CARS    5
#define S2M_LAPS    4                   /* the options: s2_menu_laps */
#define S2M_SKILLS  3

struct s2_menu_cfg {
    int track;                          /* 1 .. 7 */
    int team, car;                      /* the player's: team 1 .. 4, car 1 .. 5 as the files number them */
    int laps;                           /* index into s2_menu_laps: 1, 3, 5, 10 */
    int skill;                          /* 0 ROOKIE, 1 NORMAL, 2 PRO (race_level = 2 - skill) */
    int manual;                         /* 1: the manual gearbox */
};
extern const uint8_t s2_menu_laps[S2M_LAPS];

/* what the menu ends with */
enum { S2M_NONE, S2M_RACE, S2M_WATCH };

/* The rows, top to bottom */
enum { S2M_ROW_TRACK, S2M_ROW_TEAM, S2M_ROW_CAR, S2M_ROW_LAPS, S2M_ROW_SKILL, S2M_ROW_GEAR, S2M_ROW_RACE, S2M_ROW_WATCH, S2M_ROWS };

/* The names and labels from STRINGS.TXT of DATA/LOCAL.DAT in the ZIP (up to 8 KB read, 3 KB kept); whatever is not found keeps the
   built-in English text, which is the same. Once, before the first menu. Returns how many of the 12 texts (the tracks, the teams, the
   levels, nine labels) were read from the file. */
int s2menu_init(struct zip *z);
const char *s2menu_track_name(int track);       /* track 1 .. 7 */
const char *s2menu_team_name(int team);         /* team 1 .. 4 */
const char *s2menu_skill_name(int skill);       /* 0 .. 2 */

/* Track 1, team 1 car 1, 3 laps, NORMAL, automatic: the first start */
void s2menu_defaults(struct s2_menu_cfg *c);
/* The last choices from the save (s2save.h: the defaults if there are none; values out of range are clamped) / into it. */
void s2menu_load(struct s2_menu_cfg *c);
void s2menu_store(const struct s2_menu_cfg *c);

/* The grid of a race for the player's team and car: slot 0 the player's, the others the three other teams (in order, each with the
   car of its own number: the default grid for team 1 car 1). */
void s2menu_grid(const struct s2_menu_cfg *c, struct s2_pick grid[4]);

/* One press of buttons (KEY_* bits of hw.h, the ones that went down in this frame) on the list: moves *cursor, changes c, and
   returns S2M_RACE or S2M_WATCH when the menu ends. d-pad up / down: row; left / right: the row's value (a ring); L / R: the
   track three on either way from any row (any track is two moves away); A or Start: the race (the ride on the WATCH row); X: the ride. */
int s2menu_key(struct s2_menu_cfg *c, int *cursor, uint32_t press);

/* The list into fb (palette of the back buffer too) */
void s2menu_draw(volatile uint8_t *fb, const struct s2_menu_cfg *c, int cursor);

/* The one short line a start leaves in the debug log: "menu: race T4 CONDOR 1 3L NORMAL AUTO" */
void s2menu_log(const struct s2_menu_cfg *c, int go);

/* The menu until a race or the ride is chosen; the choices are stored. Needs s2menu_init(). */
int s2menu_run(struct s2_menu_cfg *c);
