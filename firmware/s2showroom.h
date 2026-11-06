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
/* The same with a box of the picture and the cars darkened through the table (s2gfx_dark_table) before the page draws its text over it (the CAR page's list of set-ups) */
void s2showroom_draw_dark(struct blit_list *l, const uint8_t table[256], int x, int y, int w, int h);
/* Car k alone (0 .. n - 1 of s2showroom_load()) at (x, y, z) turned a1 (of 2048) as the page's camera sees it (the CAR CUSTOMIZATION page's), the picture before it as above */
void s2showroom_draw_one(struct blit_list *l, int k, int32_t x, int32_t y, int32_t z, int a1);

/* The cars at places of the page's own (the ARCADE title's cars drive along their paths, s2arcscene.h): car i (0 .. n - 1) to (x, y up, z) and the heading a1 (2048ths; the places are the engine's, as s2showroom_init
   keeps them), and the cars drawn as s2showroom_draw() does but from the camera cam */
struct track_cam;
void s2showroom_place(int i, const int32_t pos[3], int a1);
void s2showroom_draw_cam(struct blit_list *l, const struct track_cam *cam);

/* What the plate shows (CARS.DTR through DATA/GAME.DAT): the car's top speed in km/h as the file has it and its drive: 0 four-wheel, 1 front,
   2 rear (from the shares of the two left wheels, 0x4176c ..). 0 if the file could not be read. */
struct s2show_plate { float max_speed; int drive; };
int s2showroom_plate(struct zip *z, int team, int car, struct s2show_plate *out);

/* The cars as objects of the track engine, for a room they stand in (s2garage.h): cars first .. first + count - 1 of the ring at the places s2showroom_place() gave them (the engine's
   drawing makes their blocks when they come up, the windows' reflections go by the camera), at most max; how many. Each has its own material library: s2showroom_piece is the
   track_hw_piece that switches the unit's material file to the car's for a car's pieces and back to the track's for the rest (the unit reads the file while it works: it waits for it to be idle
   at each change); s2showroom_scene_begin() before every picture says the unit has the track's (s2scene_begin sets it). s2showroom_fresh(): 1 once after the cars were loaded. */
struct track_object;
struct gu_piece;
int s2showroom_objects(int first, int count, struct track_object *objs, int max);
void s2showroom_piece(const struct gu_piece *g);
void s2showroom_scene_begin(void);
int s2showroom_fresh(void);
