/*
 * The cockpit's pictures and instruments (docs/screamer2/hud.md "The views from the car"): COCK1_L.PKC .. COCK8_L.PKC and MIRROR_L.PKC of GFX/GAME1.DAT as boxes for the blitter
 * (tb_hud_picture: the pictures are 320 x 200 with see-through runs), the numbers of the dashboards (s2cockpit.h) and what STARTL.EXE's instrument routine 0x46d42 draws of them: in
 * the view INSIDE the mirror's frame (if the mirror is on), the car's cockpit picture (0x518f8) and 0x46755's gear lamp and two needles; in the other views the mirror's frame alone.
 */
#pragma once
#include <stdint.h>
#include "blit.h"
#include "s2cockpit.h"

/* Once, after s2gfx_game_init() while GFX/GAME1.DAT is at hand: the eight cockpits and the mirror's frame made into boxes at SDRAM_BASE + 0xC80000 (0.6 MB). 1 if they are all there
   (the log has the time and the boxes); without them the views from the car have no cockpit. */
int s2dash_init(void);
/* A race begins: the table of the dashboards (CRUSCOTTI of CRUSC.DTR in DATA/GAME.DAT, the container s2car_set_data() was given). 1 if it was read. */
int s2dash_begin(void);
/* The row of model 0 .. 7 (s2ck_model); 0 if there is no table */
const struct s2ck_dash *s2dash_row(int model);
/* 1 if the cockpit picture of model 0 .. 7 and the table are there */
int s2dash_have(int model);
/* The mirror's frame (MIRROR_L.PKC) */
void s2dash_mirror_frame(struct blit_list *l);
/* The cockpit of model: its picture, the gear lamp for `gear` and the speed needle at speed (km/h, the dial's eased figure) and the revs needle at revs (the part of the most revs, eased) */
void s2dash_cockpit(struct blit_list *l, int model, int gear, float speed, float revs);
