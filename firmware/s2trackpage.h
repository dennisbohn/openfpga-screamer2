/*
 * The two pages of the original that show a track as a turning 3D map (docs/screamer2/menus.md, row 7), on the toolkit of the 3D pages (s2scene.h, s2trackmap.h):
 *
 *   TRACK SELECTION of time attack (STARTL.EXE 0x3f79e, `leag_0L.BKK` behind): the map of the chosen track turns in front of the picture of a landscape and a column
 *       of name plates; LEFT / RIGHT choose among the open tracks (the original's order: England, Egypt, California, Finland, Switzerland, Columbia, The Tunnel; no
 *       wrapping): the old map flies off, the new one comes in, a quarter of a second; on the right the track's banner, TERRAIN and DIFFICULTY (STRINGS.TXT);
 *       arrows where there are more tracks; ENTER fades out and goes on to the CAR page, ESC is back to the options room (a cut).
 *   TRACK of a league's race (0x3ee28, `tkg_nL.BKK`): the same map and texts for the race the league comes to, the league's name on top, ACCELERATE TO CONTINUE
 *       blinking; ENTER goes on (fade), ESC before the first race goes back to LEAGUE SELECTION and later asks LEAVING LEAGUE? ARE YOU SURE? YES / NO (YES leaves
 *       for LEAGUE SELECTION; the question sits in the left half of the screen, as the original puts it).
 * Everything else of both is in the header of s2trackpage.c.
 */
#pragma once
#include "s2menu.h"
#include "track.h"

/* What a page ends with: ENTER goes on (after the fade), ESC goes back (at once); S2TP_LEAVE: the league's question was answered YES; 0 if a picture or the maps could not be read */
enum { S2TP_BACK = -1, S2TP_FAIL = 0, S2TP_GO = 1, S2TP_LEAVE = 2 };

/* Time attack's TRACK SELECTION: c->track (1 .. 7, as the files number them) is the track the page starts on (the last choice; one that is not open: the first) and the one
   chosen when it ends. The tracks open are the save's (s2arcade_tracks_open: 3 to 7). */
int s2tp_attack(struct s2_menu_cfg *c);

/* The camera of both pages (for the tests): the original's look-at of 0x33905 as angles of 2048ths */
struct track_cam s2tp_camera(void);

/* The championship's TRACK page of race number `race` (0 .. : the races done) of league `league` (0 .. 3) on `track` (1 .. 7 as the files number them) */
int s2tp_league(int league, int race, int track);
