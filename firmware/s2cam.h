/*
 * The race's views that are not the player's chase camera: the trackside cameras of the end of a race and of the replay, and the chase camera of any car
 * (docs/screamer2/menus.md, "Replay (row 10)"). Read from STARTL.EXE (tools/dis.sh -f): the camera record of the picture routine 0x12dff, the choice of a trackside
 * camera 0x14acf, its five kinds 0x13514, 0x135b6, 0x13768, 0x138e8, 0x13a8e, the look-at 0x33905; the track's cameras are CAMERASn.MCM of DATA/MAPn.DAT: a
 * count and, for each, 0x20 bytes: x, height, z (whole numbers), the three angles pitch, heading, roll (floats), the kind 0 .. 4 and a figure for it (a float).
 * The game's views are numbered 1 EXTERNAL (the trackside cameras), 2 INCAR, 3 BACK (the chase camera), 4 NEAR, 6 INSIDE (GAME.TXT); only 1 and 3 are here: the
 * viewer has no view from the car (docs/screamer2/hud.md).
 *
 * The cameras of view 1 (each looks at the car it follows, unless it is of the first kind):
 *   kind 0  stands where its row says, with the row's angles
 *   kind 1  stands at its row's place when it is newly chosen, then drifts the way it looks at the figure x the car's speed x 111 units a second
 *   kind 2  between the car and its row's place: 0.8 - 0.75 x the figure of the way from the car to the row
 *   kind 3  circles the car at 1100 units, rising and falling 440 +- 220, the two turns going on by (0.5 x the figure + 0.1) a second
 *   kind 4  at (1320 + 500 s, 550 + 200 s) from the car, s the figure, turned by the row's heading + pi
 * The camera takes the row nearest to the car (the ground distance, in 64ths) when that is the next row or the one before the one it has, else it keeps its own
 * (0x14acf), so it cuts from one camera to the next along the track. No hardware: tested on the PC (tools/s2/check_replay_s2.sh).
 */
#pragma once
#include <stdint.h>

struct s2car_state;
struct track_cam;

enum { S2CAM_TRACKSIDE = 1, S2CAM_BACK = 3 };

/* A race begins: the track's cameras (CAMERAS<track>.MCM of the DATA/MAPn.DAT the car physics has loaded, s2car_map_dat()), in `view`, following car 0. Without
   cameras (no such file) view 3 is the only one. */
void s2cam_begin(int track, int view);
int s2cam_rows(void);
int s2cam_shot(void);                           /* the trackside camera in use (a row of the track's list), -1 before the first frame */

/* The view (S2CAM_TRACKSIDE, S2CAM_BACK: others are ignored; view 1 needs cameras; a view asked for is a new choice of camera) and the car followed (0 .. n - 1: the viewer
   sets s2car_state.cam_on on it, so that its chase camera is worked out) */
void s2cam_view(int view);
int s2cam_get_view(void);
void s2cam_follow(int car, int ncars);
int s2cam_get_follow(void);
void s2cam_next_car(int ncars);                 /* F11 */

/* The camera of this frame in *cam: `dt` seconds have passed since the last (the drifting and the circling cameras go by it). The chase camera view 3 is the followed car's
   own (s2car_state.cam, kept by its steps); *cam is not touched in view 3 when that car has none worked out. */
void s2cam_frame(const struct s2car_state *const *cars, int n, float dt, struct track_cam *cam);
