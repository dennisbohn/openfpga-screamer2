/*
 * The team's room as the scene of the CAR page and the CAR CUSTOMIZATION page (docs/screamer2/menus.md "The garage"; STARTL.EXE 0x3cc2c and 0x3e1d5, the pages the championship and
 * time attack show, the arcade's own CAR page (0x40c31) is a picture with the carousel and has no room)
 *
 * Both pages draw the room of the options room (GFX/MENU<t>1.DAT built as a track, s2room.h; the original loads it once and its load lists 0x1132b0 and 0x1132e0 keep it, here it is the image
 * of the track window and a page finds it there) with the engine's list of the team's cars and their shadows (the objects of the track engine, sorted in among the room's pieces: s2showroom.h
 * has the cars, the unit's material file is switched to a car's for its pieces). The places are DATA/GEN.DAT's P_<t>.MCM's (s2room.h): the CAR page's five cars stand at the aims of record 1,
 * the CUSTOMIZE page's car at aim 0 of record 2 turned a quarter (heading 512).
 *
 * CAR page (0x3cc2c, the camera 0x3d749 ..): the camera goes round the chosen car: its place eases (0x3364d, row 0x5a: 0.95 of the way each frame, cut towards zero) to the car's aim plus
 * a vector of 900 along x and a height turned about the vertical by the sway [0x1324a4] x 2 pi (the sway grows 0.001 a frame, 20 s a turn, and stays where it is for the program's life); the
 * height is 450 in the set-up and the choice, in the cars' state 360 + 180 sin(4 pi p) with p (the swing's phase, 0 at the page's start, 0.125 where the car is changed, + 0.001 a frame, 20 s
 * a swing); the angles are the look from the target place at the car's aim (0x33905) eased the short way round (0x1016b, row 0x5a) from the record's own, which are kept as whole numbers of
 * 2048ths and made radians again each frame. The first frame's place is the aim + (900, 360, 0), the angles the look from it. The shadows (ASHADOW.3DX, loaded with the room) stand
 * 30 further from the camera than the car along the ground, 30 under its place, as the camera stood when the last frame ended; the cars have the heading 0.
 * CAR CUSTOMIZATION page (0x3e1d5, the camera 0x3e854 ..): one car (the chosen one, at aim 0 of record 2, heading 512) in the room; the camera starts at place 7 of the record and goes
 * to the station of the item under the cursor, k = item + 1 (at most 7: SAVE and EXIT share the last): the place eases to place k (row 0x5a), the angles are the look from the place
 * now at aim k (0x33905) eased from the record's angles at row 0x5d.
 */
#pragma once
#include <stdint.h>
#include "blit.h"

/* The room of the team and its n cars (4 or 5) read and built where they are not yet in memory (the log has `room:` and `showroom:` lines). 1, 0 if the room or a car could not be had */
int s2garage_open(int team, int n);

/* The CAR page's scene: the cars on their five places (chosen 0 .. n - 1 the one the camera goes round), the camera at its start; and one frame: the cars' state (0) or not (the set-up and the
   choice: the camera's height is 450), the camera one step on, the room and the cars into the back buffer of the list l, a box of it darkened (dark 0: none) before the page's text goes over it.
   The list is empty afterwards. */
void s2garage_car_begin(int chosen);
void s2garage_car_choose(int chosen);                   /* LEFT / RIGHT: another car (the swing of the camera's height starts at 0.125) */
void s2garage_car_draw(struct blit_list *l, int state, const uint8_t *dark, int bx, int by, int bw, int bh);

/* The CUSTOMIZE page's scene: car `car` (0 .. 3) in the room, the camera at its start; one frame for the item under the cursor (0 .. 7) */
void s2garage_cust_begin(int car);
void s2garage_cust_draw(struct blit_list *l, int item);

/* For the tests: one step of the camera alone (what the drawing does first), the camera as the record of the original's has it (the angles a0 pitch and a1 heading in 2048ths, the
   place), and the shadow i's place as the game's record has it (x, y up, z) as the next step would make it from the camera now */
void s2garage_car_step(int state);
void s2garage_cust_step(int item);
struct s2garage_cam { int32_t pos[3]; int a0, a1; float sway, phase; };
const struct s2garage_cam *s2garage_camera(void);
void s2garage_set(int chosen, const struct s2garage_cam *c);   /* the camera as it was in a frame of a recording, and the car it goes round (the tests step from there) */
int s2garage_shadow(int i, int32_t out[3]);
