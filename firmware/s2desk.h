/*
 * The championship's desk scene (docs/screamer2/menus.md, "The desk scene"): the 3D room the original shows after a league is won (STARTL.EXE 0x3bc38; the LEAGUE REPORT's
 * last ENTER leads to it, 0x44144) and before LEAGUE SELECTION comes back. Its file is ANIMS/SCENE<nn>1.DAT, nn = 1 .. 4: a team's room per map (MAP1 .. MAP4: the player's
 * team picks one) and SPLINES.PTH, the paths of the camera, the point it looks at and the player's car, which SCCAR<team>1.DAT's AUTO<car> is. The room is built like the
 * options room's (s2room.h), the car stands in it (or, in the fourth scene, drives in), the camera flies along its path, the scene lasts as long as SPLINES.TXT says (6, 8, 8
 * and 12 seconds), then the picture fades out over 25 frames. ENTER ends it early; ESC does nothing. There is no text on the page.
 *
 * The league in hand picks the scene (0x3bcb2): leagues 1, 2, 3 and 4 show scenes 1, 2, 3 and 3; after league 4 a second visit shows scene 4 with the team's fifth car, which the
 * win has opened, and which is the player's chosen car from then on (0x3bf06 .. 0x3bf51).
 */
#pragma once
#include <stdint.h>
#include "s2room.h"
#include "zip.h"

#define S2DESK_SCENES   4

/* The scene (1 .. 4) the page shows for the league in hand 0 .. 4 (0 .. 3 are the leagues, 4 is the second visit after league 4) */
int s2desk_scene(int league);

/* The scene `scene` for team 1 .. 4 and the player's car 1 .. 5, from the ZIP (ANIMS/SCENE<nn>1.DAT and SCCAR<team>1.DAT, read by range: only the team's map, 1.1 to 1.3 MB of the
   4.2 to 4.8 MB), built in the memory m (the track window: whatever was built there is gone, as the room's is). The black screen stands while it is built (about three seconds on
   the Pocket, the log has `desk: ...`). 1 when the scene ran to its end, 0 when it could not be built or the toolkit is not up (nothing is shown). */
int s2desk_run(struct zip *z, const struct s2room_mem *m, int scene, int team, int car);

/* The viewer's call after the player has won league `league` (0 .. 3) with team `team` and car `car` (1 .. 5): the desk scene; after league 4 (3) the second visit, if the car is
   not the fifth. Returns the car the player has from now on: the fifth after league 4, else `car`. The memory is the viewer's track window (trackmem.h), the ZIP files_zip(). */
int s2desk_won(int league, int team, int car);

/* ------------------------------------------------------------------------------------------------ the scene's rules (for the tests)
   The state of the page at frame n (0 on): the camera, the car's place and heading (the records the original keeps: 0x131ea4 and the object's, 0x11d6f), the animation clock and
   the scene's time t (0 .. 1). s2desk_open() reads the paths of a file (SPLINES.PTH and SPLINES.TXT as they are in the container, size bytes each) for team's car; 0 if they are not usable. */
struct s2desk_state {
    int32_t cam[3], aim[3];                 /* the camera's place and the point it looks at (engine x, y up, z), whole numbers */
    int a0, a1;                             /* the camera's pitch and heading in 2048ths (0x33905 and x 1024 / pi, & 0x7ff) */
    int32_t car[3];                         /* the car's place */
    int car_a1;                             /* its heading */
    float t, anim, wheels;                  /* the scene's time, the textures' clock (0 .. 1) and the wheels' phase (radians) */
};
int s2desk_open(const uint8_t *pth, uint32_t pth_size, const uint8_t *txt, uint32_t txt_size, int scene);
float s2desk_seconds(void);                 /* how long the scene lasts (SPLINES.TXT's first number) */
void s2desk_step(int frame, struct s2desk_state *st);   /* frame 0, 1, 2 ...: calls must come in order */
