/*
 * The championship's pages of Screamer 2's menu (docs/screamer2/menus.md, row 6), on the 2D toolkit (s2gfx.h, s2page.h): LEAGUE SELECTION, WELCOME TO LEAGUE n
 * and the LEAGUE REPORT with the league's end (the win or the lost), each as STARTL.EXE has it (0x4337d, 0x39569, 0x43ade; the numbers are in the header comment
 * of s2leaguepages.c). The rules are s2league.h; the viewer runs the races between the pages.
 *
 * What the original has and this does not: the WELCOME page is a 3D scene (a flight past the league's track plates over LEAG_nL's picture) and the pages
 * between the report and the next race, the TRACK page with the 3D map, TERRAIN and DIFFICULTY and the CAR page with its set-ups, are rows 7 and 12; the
 * pictures of LEAG_nL and BKG_3L, the cups, the team marks and every text are the original's. The "LEAVING LEAGUE? ARE YOU SURE?" question (which the original
 * asks on its TRACK page when ESC is pressed between two races) is asked on the report here, where the player stands between two races too.
 */
#pragma once
#include "s2league.h"

/* What a page ends with: ENTER goes on (after the page's fade out), ESC goes back (at once); 0 if the page's picture could not be read */
enum { S2LP_BACK = -1, S2LP_FAIL = 0, S2LP_GO = 1 };

/* LEAGUE SELECTION: the four leagues two at a time, LEFT / RIGHT choose (*league is the one chosen: 0 .. 3; the page starts at the one it is given),
   ENTER (B or Start) takes an open league (S2LP_GO) and does nothing on a league that is NOT AVAILABLE, ESC (A or Select) leaves (S2LP_BACK). The leagues
   open are the save's (s2save_league_open). Needs s2gfx_init() and the menu sounds. */
int s2lp_select(int *league);

/* WELCOME TO LEAGUE n: the league's picture under a darkened band with the two lines; ENTER (S2LP_GO) goes on to the first race, ESC (S2LP_BACK) back to the selection */
int s2lp_welcome(int league);

/* The LEAGUE REPORT after a race (L has the race's points): the standings, RACES COMPLETED and RACES TO GO; ENTER goes on. After the last race the next ENTER
   shows the end: CONGRATULATIONS !!! YOU'VE WON LEAGUE n (the unlocks are made, s2l_unlock), or BETTER LUCK NEXT TIME !, and the one after it ends the page.
   Results: S2LP_NEXT the next race, S2LP_DONE the league is over (back to LEAGUE SELECTION), S2LP_LEAVE the player left the league (ESC between two races, then YES
   to LEAVING LEAGUE? ARE YOU SURE ?: nothing is kept but what was won before). */
enum { S2LP_NEXT = 2, S2LP_DONE = 3, S2LP_LEAVE = 4 };
int s2lp_report(const struct s2_league *L);
