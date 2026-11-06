/*
 * The CAR CUSTOMIZATION page of Screamer 2 (docs/screamer2/menus.md row 12; STARTL.EXE 0x3e1d5, the page the CAR page of the championship and of time attack leaves for with
 * CUSTOMIZE): the six numbers of a car's Custom set-up (s2car.h, docs/screamer2/driving.md "The set-up") as eight items, HANDLING, BRAKES, TIRE TYPE, TIRE PRESSURE, FRONT and REAR
 * SUSPENSIONS, SAVE and EXIT, on the 2D toolkit (s2gfx.h, s2page.h) over the chosen car turning in 3D (s2showroom.h).
 *
 * Keys (the original's; B or Start is ENTER, A or Select ESC): the d-pad moves the cursor over the items (two columns, then SAVE and EXIT below), ENTER on one of the six opens
 * it (its picture and scale slide up from the bottom, 1/8 s), LEFT / RIGHT change the value (0 .. 4, the tyres A, B, C), ENTER keeps it, ESC drops the change; ESC with nothing open
 * puts the cursor on EXIT; ENTER on SAVE writes the six numbers into the save (s2save.h) and leaves, on EXIT leaves without.
 */
#pragma once
#include "s2menu.h"

/* Custom set-up number `slot` (0 .. 3 for Custom 1 .. 4) of the car c->team, c->car (1 .. 4), until SAVE or EXIT. 1 when the page was left, 0 if its picture or the cars could not be
   read (the page was not shown). Needs s2gfx_init() and the menu sounds. */
int s2custom_page(struct s2_menu_cfg *c, int slot);
