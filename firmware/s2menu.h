/*
 * The start menu of Screamer 2 (docs/screamer2/menus.md has the plan for the rest of the original's screens): a list in the console's
 * text style, shown after the files screen and whenever the race or the ride is left with Select + B. It stands in for the original's
 * options room, whose choices are pages of their own as they get built:
 *
 *   ARCADE               the original's arcade pages (s2arcade.h): title, TRACK, CAR (the 3D carousel) and TRANSMISSION, and then the race;
 *                        ESC goes back
 *   CHAMPIONSHIP         the championship (s2leaguepages.h, s2league.h): LEAGUE SELECTION, WELCOME TO LEAGUE n, the league's races with the LEAGUE
 *                        REPORT after each; the viewer runs it (S2M_CHAMP)
 *   TIME ATTACK          the original's TRACK SELECTION with the turning 3D maps (s2trackpage.h), the CAR page with 3 LAPS / FREE RUN, then the race
 *                        against the ghost of the track's best run, or the free run (s2ghost.h); the viewer runs it (S2M_ATTACK)
 *   NUMBER OF LAPS       the original's options, 1 / 3 / 5 / 10 (a GAME SETTINGS row; the championship races take it too)
 *   ARCADE ENEMIES SKILL ROOKIE / NORMAL / PRO (a GAME SETTINGS row)
 *   START RACE           the race with the choices as they are, at once (left / right: the track, T1 .. T7 in the files' numbering)
 *   WATCH                the viewer's ride (for timing)
 *   TEAM SELECTION       the original's TEAM SELECTION and ENTER YOUR NAME pages (s2pages3d.h): the team and the name. A new save (no profile yet:
 *                        s2save.h S2S_PROFILE) shows them once, before the menu, as the original shows them at every start.
 *
 * Since row 8 this list is the developer's menu behind the options room (s2room.h): the room is the menu, X in it opens the list, Select in the list goes back. What the list
 * carries that the original has on other pages, NUMBER OF LAPS and ARCADE ENEMIES SKILL (GAME SETTINGS), HI SCORES (GAME SETTINGS, VIEW HIGH SCORES) and TEAM SELECTION (ESC in the room),
 * is also reached there; START RACE (the race on any track at once, L / R: the track) and WATCH (the ride) are the core's own.
 *
 * The choices are kept in the nonvolatile CHOICE.DAT (s2save.h, data slot 23): the Pocket writes it back when the core
 * exits, so they survive a power-up. Everything is open from the start (all seven tracks, all four teams and their five cars).
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
};
extern const uint8_t s2_menu_laps[S2M_LAPS];

/* what the menu ends with (S2M_ARCADE: a key press on the ARCADE row, which s2menu_run() answers by running the arcade pages; S2M_SETTINGS and
   S2M_SCORES and S2M_TEAM: the GAME SETTINGS, HI SCORES and TEAM SELECTION rows, which s2menu_run() answers by running the page; S2M_CHAMP: the
   CHAMPIONSHIP row, which s2menu_run() returns to the viewer, whose races the league needs) */
enum { S2M_NONE, S2M_RACE, S2M_WATCH, S2M_ARCADE, S2M_SETTINGS, S2M_SCORES, S2M_TEAM, S2M_CHAMP, S2M_ATTACK, S2M_ROOM };

/* The rows, top to bottom */
enum { S2M_ROW_ARCADE, S2M_ROW_CHAMP, S2M_ROW_ATTACK, S2M_ROW_LAPS, S2M_ROW_SKILL, S2M_ROW_RACE, S2M_ROW_WATCH, S2M_ROW_SETTINGS, S2M_ROW_SCORES, S2M_ROW_TEAM, S2M_ROWS };

/* The names and labels from STRINGS.TXT of DATA/LOCAL.DAT in the ZIP (up to 8 KB read, 3 KB kept); whatever is not found keeps the
   built-in English text, which is the same. Once, before the first menu. Returns how many of the 8 texts (the tracks, the teams, the
   levels, five labels) were read from the file. */
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
   returns S2M_RACE, S2M_WATCH or S2M_ARCADE when the menu ends or goes on to the arcade pages. d-pad up / down: row; left / right:
   the row's value (a ring; on START RACE the track); L / R: the track three on either way from any row (any track is two moves
   away); A, B or Start: the row's action (the race on any row but ARCADE and WATCH); X: the ride. */
int s2menu_key(struct s2_menu_cfg *c, int *cursor, uint32_t press);

/* The list into fb (palette of the back buffer too) */
void s2menu_draw(volatile uint8_t *fb, const struct s2_menu_cfg *c, int cursor);

/* The one short line a start leaves in the debug log: "menu: race T4 CONDOR 1 3L NORMAL AUTO" */
void s2menu_log(const struct s2_menu_cfg *c, int go);

/* The row the cursor is on (S2M_ROW_*), kept from one visit of the menu to the next; the tests set it */
extern int s2menu_row;

/* The options room (s2room.h), which the viewer sets when it can show one (the toolkit is up): s2menu_run() shows it first, as the original's menu does after TEAM SELECTION and ENTER
   YOUR NAME. It returns an S2ROOM_* result (the choice of the station, ESC, X); without it (0, as in the PC tests of the list) the list below is the menu. The list stays as the
   developer's menu: X in the room opens it, Select in it goes back to the room, and so does a YES to EXIT SCREAMER 2? on TEAM SELECTION (where the original leaves the program). */
extern int (*s2menu_room)(const struct s2_menu_cfg *c);

/* The menu until a race or the ride is chosen; the choices are stored. Needs s2menu_init(). */
int s2menu_run(struct s2_menu_cfg *c);
