/*
 * The CAR page's carousel (docs/screamer2/menus.md, row 5; STARTL.EXE 0x40c31, the page that is the CAR and the TRANSMISSION page in two states): the
 * team's four or five cars (GFX/CARS<team>01.DAT, AUTO1 .. AUTO5 each with its own material library) stand on a ring of radius 3000 in front of the camera
 * and turn; LEFT / RIGHT turn the ring a place (the cars glide, 15 % of the way a frame), the chosen car is in front. In the TRANSMISSION state the other
 * cars glide away down and back and the chosen one forward and up. The page (s2arcade.c) draws the picture, this the cars, then its sprites.
 *
 * SDRAM: the toolkit's 3D region (s2scene.h): car k at S3D_MODELS + k * 0x38000.
 */
#pragma once
#include <stdint.h>
#include "blit.h"
#include "s2menu.h"
#include "zip.h"

#define S2SHOW_MAX   5

/* The team's cars 1 .. n (n 4 or 5) read from the ZIP; 1 if all are there. A second call for the same team and count reads nothing. */
int s2showroom_load(struct zip *z, int team, int n);

/* The ring for n cars with car `chosen` (0 .. n - 1) in front; the cars stand on their places, facing forward (a1 = 0) */
void s2showroom_init(int n, int chosen);

/* One frame (the original's order: before the keys): the cars turn (the chosen one 10 of 2048 a frame, the others 2) and glide to their places;
   state 0 the CAR page, 1 the TRANSMISSION page. Returns 1 if the last car is still more than 150 from its place (the page takes no
   LEFT / RIGHT then). */
int s2showroom_step(int state);

/* The chosen car moves a place (dir -1: the one before it, +1: the next; round); the ring turns */
void s2showroom_move(int dir);
int s2showroom_chosen(void);

/* The cars into the back buffer: the picture is in the list l, which is handed to the blitter first; afterwards l is empty again for the page's sprites */
void s2showroom_draw(struct blit_list *l);

/* What the plate shows (CARS.DTR through DATA/GAME.DAT): the car's top speed in km/h as the file has it and its drive: 0 four-wheel, 1 front,
   2 rear (from the shares of the two left wheels, 0x4176c ..). 0 if the file could not be read. */
struct s2show_plate { float max_speed; int drive; };
int s2showroom_plate(struct zip *z, int team, int car, struct s2show_plate *out);
