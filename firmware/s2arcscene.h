/*
 * The ARCADE title's cars (docs/screamer2/menus.md, row 11; STARTL.EXE 0x3b6e8)
 *
 * Over the title's picture (ARCADE_L.BKK, the blue grid) four of the team's cars drive past a camera that stands still. There is no scene behind them: the
 * page loads no map (its load list is the menu's font, the sprites, the texts, the picture, the team's room, the five cars and the screen; the ANIMS scenes
 * are the championship's desk scene, 0x3bc38), it draws the picture and then the cars, then its prompt.
 *
 * The paths are DATA/GEN.DAT's SCENE0.PTH .. SCENE3.PTH (the game's path files, as the tracks' MAPn.PTH: s2anim.h): four takes, each with the paths of the four cars
 * (MV_car0 .. MV_car3), the camera's place (Camera01) and the point it looks at (Camera01.target). A take runs 0 to 1 of its paths at 0.25 a second of the frame
 * time (0.02: the page does not measure it), then the next take starts: car j of take k at its path's start, the clock back to 0, the takes round and round
 * (0, 1, 2, 3, 0, ..) until a key ends the page. The cars are the team's AUTO1 .. AUTO4 (the first four of the five the page loads), which s2showroom.h loads and
 * draws as it does for the CAR page; the picture, the prompt and the fade are the page's (s2arcade.c).
 *
 * The cars' place is the path's value cut to whole units (x, height, depth as the engine's record has it) and its heading the path's way as the tracks' animations
 * have it (s2anim_paths_angles), no pitch, no roll. The camera stands at the first values of the take's camera path, looks at the first values of its target path
 * (the same for all four takes: (-2013, 1084, -3934) at (-2013, -79, 1751), a0 1982 and a1 1023), and the lens is the menu's own (s2scene.h).
 */
#pragma once
#include <stdint.h>
#include "blit.h"
#include "zip.h"

#define S2ARCSCENE_TAKES    4
#define S2ARCSCENE_CARS     4
#define S2ARCSCENE_TAKE_S   4.0f        /* a take's length in seconds at the frame time 0.02: 201 frames (the clock passes 1.0 on the 201st step) */

/* The paths of DATA/GEN.DAT and the team's cars (s2showroom_load: n = 4 or 5 of them, the page of the CAR choice then finds them in memory); 1 if all of it is there. The paths are read once. */
int s2arcscene_load(struct zip *z, int team, int n);

/* One frame (the page's, before its keys): tick 0 starts the first take, the others go 0.02 s on; the cars to their places on their paths and the camera for the take */
void s2arcscene_frame(uint32_t tick);

/* The cars into the back buffer over the picture in the list l (s2showroom_draw_cam); afterwards l is empty again for the page's text */
void s2arcscene_draw(struct blit_list *l);

/* The state, for the tests: the take (0 .. 3) and its clock (0 .. 1), car i's place (x, y up, z) and heading, the camera's place and angles (a0 pitch, a1 heading) */
int s2arcscene_take(void);
float s2arcscene_clock(void);
void s2arcscene_car(int i, int32_t pos[3], int *a1);
struct track_cam;
struct track_cam s2arcscene_camera(int32_t pos[3], int *a0, int *a1);
