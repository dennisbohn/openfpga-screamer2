/*
 * The arcade pages of Screamer 2's menu (docs/screamer2/menus.md), 2D, on the toolkit (s2gfx.h, s2page.h): the ARCADE title, the TRACK
 * page, the CAR page (a stand-in until the 3D page of row 5) and the TRANSMISSION page, run as a chain: ENTER (B or Start) goes on, ESC
 * (A or Select) goes back, as the original's pages do (STARTL.EXE 0x3b6e8, 0x41a69, 0x40c31; what each draws, plays and does is in
 * s2arcade.c).
 *
 *   ARCADE title -> TRACK -> CAR -> TRANSMISSION -> (the race)
 *
 * They change the start menu's choices (s2menu.h): the track, the team and car, and the gearbox.
 */
#pragma once
#include <stdint.h>
#include "s2menu.h"

/* the pages */
enum { S2ARC_TITLE, S2ARC_TRACK, S2ARC_CAR, S2ARC_TRANS, S2ARC_PAGES };

/* what a page ends with: ENTER goes on, ESC goes back; 0 if its picture could not be read */
enum { S2ARC_BACK = -1, S2ARC_FAIL = 0, S2ARC_NEXT = 1 };

/* One page, until ENTER or ESC. Needs s2gfx_init() and the menu sounds; changes c. */
int s2arcade_page(int page, struct s2_menu_cfg *c);

/* The chain from the title page: 1 (S2ARC_NEXT) when the TRANSMISSION page was left with ENTER (the race is to start), S2ARC_BACK when
   the player left it with ESC (from the title or from the TRACK page: the original leaves to its options room, which is the start
   menu here), 0 if the first page could not be shown. */
int s2arcade_run(struct s2_menu_cfg *c);

/* The tracks the TRACK page offers: how many places of the original's order (s2save_track_at) are open, 3 to 7 */
int s2arcade_tracks_open(void);
