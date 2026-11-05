/*
 * The arcade pages of Screamer 2's menu (docs/screamer2/menus.md), on the toolkit (s2gfx.h, s2page.h): the ARCADE title, the TRACK
 * page and the CAR page with its second state, the TRANSMISSION page (the CAR page's cars are 3D: s2showroom.h), run as a chain: ENTER
 * (B or Start) goes on, ESC (A or Select) goes back, as the original's pages do (STARTL.EXE 0x3b6e8, 0x41a69, 0x40c31; what each draws,
 * plays and does is in s2arcade.c).
 *
 *   ARCADE title -> TRACK -> CAR (-> its TRANSMISSION state) -> (the race)
 *
 * They change the start menu's choices (s2menu.h): the track, the team and car, and the gearbox.
 */
#pragma once
#include <stdint.h>
#include "s2menu.h"

/* the pages: S2ARC_TRANS is the CAR page started in its second state (the choice of the gearbox), for the tests; the chain goes through it inside S2ARC_CAR */
enum { S2ARC_TITLE, S2ARC_TRACK, S2ARC_CAR, S2ARC_TRANS, S2ARC_ATTACK, S2ARC_PAGES };

/* what a page ends with: ENTER goes on, ESC goes back; 0 if its picture could not be read */
enum { S2ARC_BACK = -1, S2ARC_FAIL = 0, S2ARC_NEXT = 1 };

/* One page, until ENTER or ESC. Needs s2gfx_init() and the menu sounds; changes c. */
int s2arcade_page(int page, struct s2_menu_cfg *c);

/* Time attack's CAR page (S2ARC_ATTACK): the arcade page's cars in state 0, then in place of the TRANSMISSION choice time attack's own, 3 LAPS or FREE RUN (c->free_run; the
   original's car page, 0x3cc2c, is another scene with a set-up list between the two: row 12). ENTER on the choice is S2ARC_NEXT, ESC in the first state S2ARC_BACK. */

/* The chain from the title page: 1 (S2ARC_NEXT) when the TRANSMISSION state was left with ENTER (the race is to start), S2ARC_BACK when
   the player left it with ESC (from the title or from the TRACK page: the original leaves to its options room, which is the start
   menu here), 0 if the first page could not be shown. */
int s2arcade_run(struct s2_menu_cfg *c);

/* 3 LAPS / FREE RUN of that page (state 1; the page calls it; defined with time attack's other pages in s2trackpage.c so that the test against the running original reaches it without the
   cars): the chosen one lit in the phase `lit_phase` of the blink */
struct blit_list;
void s2tp_modes_draw(struct blit_list *l, int free_run, int lit_phase);

/* The tracks the TRACK page offers: how many places of the original's order (s2save_track_at) are open, 3 to 7 */
int s2arcade_tracks_open(void);
