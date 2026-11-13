/*
 * The menu of Screamer 2 (docs/screamer2/menus.md has the plan for the rest of the original's screens): the options room (s2room.h), the page the original
 * shows after the files screen (and after TEAM SELECTION and ENTER YOUR NAME on a new save), and whenever the race is left with Select + B. Its five stations
 * lead to the original's pages:
 *
 *   ARCADE          the arcade pages (s2arcade.h): title, TRACK, CAR (the 3D carousel) and TRANSMISSION, and then the race; ESC goes back
 *   CHAMPIONSHIP    the championship (s2leaguepages.h, s2league.h): LEAGUE SELECTION, WELCOME TO LEAGUE n, the league's races with the LEAGUE REPORT after each;
 *                   the viewer runs it (S2M_CHAMP)
 *   TIME ATTACK     the original's TRACK SELECTION with the turning 3D maps (s2trackpage.h), the CAR page with 3 LAPS / FREE RUN, then the race against the
 *                   ghost of the track's best run, or the free run (s2ghost.h); the viewer runs it (S2M_ATTACK)
 *   MULTIPLAYER     the MULTIPLAYER MENU (s2pages.h): COMBAT MODE, the two players' race on a split screen (S2M_COMBAT)
 *   GAME SETTINGS   the page of the laps, the level, the gearbox, ... with VIEW HIGH SCORES (s2pages.h)
 *
 * ESC in the room is TEAM SELECTION and ENTER YOUR NAME (s2pages3d.h), which come back to the room. The choices (track, team, car, laps, level, gearbox) are kept in
 * the save (s2save.h, CHOICE.DAT, data slot 23): the Pocket writes it back when the core exits, so they survive a power-up. Everything is open from the start
 * (all seven tracks, all four teams and their five cars). If the room cannot be built (the log says why) the arcade pages stand in for it.
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
    int free_run;                       /* time attack's choice on its CAR page: 0 3 LAPS, 1 FREE RUN (not kept: the original asks it every time) */
    int setup;                          /* the CAR page of the championship and of time attack: 0 Standard, 1 .. 4 Custom n (a car of the first four: s2save_setup). Kept while the core runs, not in the
                                           save, as the original's variable 0x13237c is while the program does: the page opens on it. The arcade has none. */
};
extern const uint8_t s2_menu_laps[S2M_LAPS];

/* what the menu ends with (s2menu_run() carries out the other stations itself): a race on the choices, or the championship or time attack, which the viewer runs, or the attract
   sequence (s2attract.h): the room, or TEAM SELECTION, was left alone for 30 s; the viewer runs it and calls s2menu_run() again, which shows the page the sequence interrupted */
enum { S2M_NONE, S2M_RACE, S2M_CHAMP, S2M_ATTACK, S2M_ATTRACT, S2M_COMBAT };

/* The names from STRINGS.TXT of DATA/LOCAL.DAT in the ZIP (up to 8 KB read, 3 KB kept); whatever is not found keeps the built-in English text,
   which is the same. Once, before the first menu. Returns how many of the 3 texts (the tracks, the teams, the levels) were read from the file. */
int s2menu_init(struct zip *z);
const char *s2menu_track_name(int track);       /* track 1 .. 7 */
const char *s2menu_team_name(int team);         /* team 1 .. 4 */
const char *s2menu_skill_name(int skill);       /* 0 .. 2 */

/* Track 1, team 1 car 1, 3 laps, NORMAL, automatic: the first start */
void s2menu_defaults(struct s2_menu_cfg *c);
/* The last choices from the save (s2save.h: the defaults if there are none; values out of range are clamped) / into it. */
void s2menu_load(struct s2_menu_cfg *c);
void s2menu_store(const struct s2_menu_cfg *c);

/* The grid of a race for the player's team and car: slot 0 the player's, slot k team (team + k) mod 4 and car (car + k) mod 4 counted from 0 (the original's, 0x33b0e, the
   same in every kind of game; the default grid for team 1 car 1 is team n, car n; the bonus car, 4, gives the computers 1 2 3). */
void s2menu_grid(const struct s2_menu_cfg *c, struct s2_pick grid[4]);

/* The one short line a start leaves in the debug log: "menu: race T4 CONDOR 1 3L NORMAL AUTO" */
void s2menu_log(const struct s2_menu_cfg *c);

/* The menu's room (s2room.h, s2room_menu() at the start): the tests put a stand-in. It returns an S2ROOM_* result: the choice of the station, ESC, or a failure when
   the toolkit is not up or the room cannot be built (the arcade pages are shown instead). */
extern int (*s2menu_room)(const struct s2_menu_cfg *c);

/* The menu until a race, the championship or time attack is chosen; the choices are stored. Needs s2menu_init(). */
int s2menu_run(struct s2_menu_cfg *c);
