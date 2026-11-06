/*
 * The views from the car (docs/screamer2/hud.md "The views from the car"): the cameras of the original's views 2 INCAR, 4 NEAR, 5 (the rear-view mirror's) and 6 INSIDE,
 * and the numbers of the dashboards. Read from STARTL.EXE (tools/dis.sh -f): the picture routine 0x2b873 takes the camera record's view and calls one routine each, 0x13bab
 * (view 2), 0x13cbd (6), 0x14200 (4), 0x13e1a (5; 0x13f38 is the chase camera, view 3, s2car.c, and 0x13223 the trackside cameras, view 1, s2cam.c); the dashboards are the section
 * CRUSCOTTI of CRUSC.DTR (DATA/GAME.DAT), 0x58 bytes a car model, drawn by 0x46755. No hardware here: the host tests link it.
 *
 * The car's words are its record's (s2car.h keeps the game's layout): the place +0x4d8 (x), +0x4dc (height, a whole number), +0x4e0 (z), the angles +0x4e4 pitch (+ nose up),
 * +0x4e8 heading, +0x4ec roll, the body's own lean on its springs +0xc18 pitch, +0xc1c heading (never set after the start: 0), +0xc20 roll, the springs' mean +0x4f4 (the object's +0x1c)
 * and the camera position of CARS.DTR +0xac (along), +0xb0 (up).
 */
#pragma once
#include <stdint.h>

struct s2car_state;
struct track_cam;
struct s2_pick;

/* GAME.TXT's numbers of the views (EXTERNAL INCAR BACK NEAR, then 5 the mirror's own, 6 INSIDE) */
enum { S2CK_EXTERNAL = 1, S2CK_INCAR = 2, S2CK_BACK = 3, S2CK_NEAR = 4, S2CK_MIRROR = 5, S2CK_INSIDE = 6 };

/* the scene's rows (the camera record's +0x1c; the eye line is 0.65 of them, track_view): 200 in every view but the mirror's (35) and INSIDE's (the dashboard's table) */
#define S2CK_ROWS           200
#define S2CK_MIRROR_ROWS    35
/* the mirror: the scene's pass of 320 x 35 is squeezed to the pixels 2k (k = 0 .. 159) of each row, laid right to left from x 240, so the glass is 160 wide from x 81, rows 14 .. 48 (0x756b0) */
#define S2CK_MIRROR_W       160
#define S2CK_MIRROR_X       81
#define S2CK_MIRROR_Y       14

/* a view has the car's body in the picture or not: INCAR, INSIDE and the mirror's pass take the car and its shadow off the draw list (0x2b6b4) */
int s2ck_hides_car(int view);

/* The camera record's state across frames (+0x54 pitch, +0x58 heading, +0x5c roll of NEAR, which eases them; +0x2c the view the last frame was drawn in: a view that is not the
   same snaps) */
struct s2ck_state { float pitch, yaw, roll; int last_view; };
void s2ck_reset(struct s2ck_state *k);

/* The camera of `view` (2, 4, 5 or 6) from the car c in *cam, one call a picture. body: the car model's block height (0x218: 0 if unknown, s2ck_body_height()). NEAR eases k's angles and
   remembers the view in k->last_view (every view does). */
void s2ck_camera(const struct s2car_state *c, int view, int body, struct s2ck_state *k, struct track_cam *cam);

/* The rows of the scene in a view: dash is the car's dashboard (s2ck_dash_parse; INSIDE takes its table's first number halved, 0 for none: 200) */
struct s2ck_dash;
int s2ck_rows(int view, const struct s2ck_dash *dash);

/* The model of the car's dashboard (COCKn_L.PKC is n = model + 1; the table row is the model): 0 .. 3 for a team's four cars, 4 .. 7 for the bonus car of team 1 .. 4 */
int s2ck_model(const struct s2_pick *p);

/* The height the camera adds to the body's (record +0x218): block "MV_mauto" of the car's model mesh (the track image's model slot, track_model_mesh), its position record's third
   number as the game keeps it */
int s2ck_body_height(int slot);

/* ----------------------------------------------------------------------------------------------- the dashboards (CRUSC.DTR) */

#define S2CK_DASHES 8
/* a needle: its pivot (in the 640-wide picture's units: the low-res program halves them), length and the angles at nought and at the most (radians, anticlockwise from the right) */
struct s2ck_needle { int32_t x, y, len; float a0, a1; };
struct s2ck_dash {
    int32_t rows;                               /* the scene's rows, in the same units */
    struct s2ck_needle speed;
    int32_t speed_max;                          /* the speed (km/h) the speed needle's end is at */
    struct s2ck_needle revs;
    int32_t lamp[5][2];                         /* the gear lamp's place by gear (neutral, 1 .. 4) */
};
/* CRUSCOTTI of the text of CRUSC.DTR: 1 if the 8 rows (4 cars, 4 bonus cars) were all there */
int s2ck_dash_parse(const uint8_t *text, uint32_t size, struct s2ck_dash out[S2CK_DASHES]);

/* A needle at `part` (0 at nought, 1 at the most) as the three lines the game draws to its tip (0x46755): [0] from the pivot, [1] and [2] from the points a pixel to either side of it.
   Whole numbers in the 320 x 200 picture. */
struct s2ck_line { int x0, y0, x1, y1; };
void s2ck_needle(const struct s2ck_needle *n, float part, struct s2ck_line out[3]);
/* the gear lamp's place (the 320-wide picture's) and sprite (GAMEL.SPR 0x46 + 5 model + gear) */
void s2ck_lamp(const struct s2ck_dash *d, int model, int gear, int *x, int *y, int *sprite);
