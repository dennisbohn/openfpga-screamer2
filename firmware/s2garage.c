/*
 * The team's room as the scene of the CAR page and the CAR CUSTOMIZATION page (s2garage.h)
 *
 * Read from STARTL.EXE (tools/dis.sh -f) and compared with the running original (tools/s2/dosbox_garage.py: the camera record, the pages' words and the cars' and shadows' object records
 * every frame, raw screenshots; tools/garage.sh): the CAR page 0x3cc2c, the camera 0x3d749 .. 0x3d964, the shadows 0x3d017 .. 0x3d131; the CUSTOMIZE page 0x3e1d5, the camera 0x3e854 .. 0x3e94e.
 */
#include "s2garage.h"
#include "s2room.h"
#include "s2showroom.h"
#include "s2scene.h"
#include "s2fx.h"
#include "s2gfx.h"
#include "car.h"
#include "dlog.h"
#include <math.h>

#define ROW_CAR         0x5a                    /* the easing rows (the table's fraction 0.5 + 0.005 row kept each frame) */
#define ROW_CUST_PLACE  0x5a
#define ROW_CUST_ANGLES 0x5d
#define RADIUS          900                     /* the camera's distance from the chosen car along x before the sway turns it */
#define HEIGHT_FIXED    450                     /* its height in the set-up and the choice */
#define HEIGHT_MID      360.0                   /* in the cars' state: 360 + 180 sin(4 pi phase), the first frame's 360 */
#define HEIGHT_SWING    180.0
#define STEP            0.02f                   /* the page's frame time (0x133350) */
#define TURN_STEP       0.05                    /* the sway's and the swing's share of a turn a second */
#define SHADOW_AWAY     30.0                    /* the shadow's offset from the car along the ground away from the camera, and under it */
#define CAR_RADIUS      1200.0f                 /* the culling radius of a car (car.c) */
#define CUST_HEADING    512                     /* the CUSTOMIZE page's car: its heading +0x18 */
#define CUST_LAST       7                       /* the station of SAVE and EXIT */

static struct s2garage_cam cam;                 /* sway: the original's [0x1324a4], which no page resets: the program's life */
static int n_cars, chosen, first_frame;
static struct track_block shadow_block[S2SHOW_MAX];

const struct s2garage_cam *s2garage_camera(void)
{
    return &cam;
}

void s2garage_set(int c, const struct s2garage_cam *k)
{
    chosen = c;
    cam = *k;
}

int s2garage_open(int team, int n)
{
    if (!s2room_open(team)) return 0;
    if (s2room_record_count(1) < n || s2room_record_count(2) <= CUST_LAST) {
        dlog_str("garage: DATA/GEN.DAT's P_t.MCM has no places for the CAR pages\n");
        return 0;
    }
    if (!s2showroom_load(s2gfx_zip(), team, n)) {
        dlog_str("garage: the team's cars could not be read: no CAR page scene\n");
        return 0;
    }
    n_cars = n;
    return 1;
}

/* the record's angles as radians again (the engine's page keeps them as whole numbers of 2048ths, the float is made from them every frame: 0x3d8b7), eased towards the look, cut to the
   record's whole numbers */
static void ease_angles(float look_pitch, float look_head, int row)
{
    float p = (float)((double)((float)cam.a0 * 0.0009765625f) * (double)3.1415927f), h = (float)((double)((float)cam.a1 * 0.0009765625f) * (double)3.1415927f);
    s2scene_ease_angle(&p, look_pitch, row);
    s2scene_ease_angle(&h, look_head, row);
    cam.a0 = s2scene_angle(p);
    cam.a1 = s2scene_angle(h);
}

/* ------------------------------------------------------------------------------------------------ the CAR page */

void s2garage_car_begin(int c)
{
    chosen = c;
    cam.phase = 0.0f;                                           /* ([ebp-0x10] = 0, set before the loads) */
    first_frame = 1;
    const int32_t *a = s2room_record(1, c, 1);
    s2showroom_init(n_cars, c);
    for (int i = 0; i < n_cars; i++) {
        int32_t at[3];
        for (int k = 0; k < 3; k++) at[k] = s2room_record(1, i, 1)[k];
        s2showroom_place(i, at, 0);                             /* (0x3cd0d: the aim, the angles 0) */
    }
    cam.pos[0] = a[0] + RADIUS;                                 /* 0x3cc98: x + 900, y + 360 */
    cam.pos[1] = a[1] + (int32_t)HEIGHT_MID;
    cam.pos[2] = a[2];
    float pitch, head;
    s2scene_look_at(cam.pos, a, &pitch, &head);
    cam.a0 = s2scene_angle(pitch);
    cam.a1 = s2scene_angle(head);
}

void s2garage_car_choose(int c)
{
    chosen = c;
    cam.phase = 0.125f;                                         /* 0x3d4e1, 0x3d5cc: 0x3e000000 */
}

/* one step of the camera (0x3d749 ..): the target a vector of 900 and a height from the chosen car's aim, turned by the sway; the place eased to it, the angles to the look from it at the aim,
   the sway and the swing on */
void s2garage_car_step(int state)
{
    const int32_t *a = s2room_record(1, chosen, 1);
    const int height = state ? HEIGHT_FIXED : (int)((double)sinf((float)((double)cam.phase * (double)12.566371f)) * HEIGHT_SWING + HEIGHT_MID);
    const float angle = (float)((double)cam.sway * (double)6.2831855f);            /* (0x10388: the vector turned about the vertical, sin and cos kept as floats, the sums cut) */
    const float s = sinf(angle), c = cosf(angle);
    const int32_t target[3] = { a[0] + (int32_t)((double)RADIUS * (double)c), a[1] + height, a[2] + (int32_t)(0.0 * (double)c - (double)RADIUS * (double)s) };
    s2room_ease_place(cam.pos, target, ROW_CAR);
    float pitch, head;
    s2scene_look_at(target, a, &pitch, &head);                  /* (from the target, not from the place the camera is at) */
    cam.sway = (float)((double)STEP * TURN_STEP + (double)cam.sway);
    if (cam.sway > 1.0f) cam.sway += -1.0f;
    cam.phase = (float)((double)STEP * TURN_STEP + (double)cam.phase);
    if (cam.phase > 1.0f) cam.phase += -1.0f;
    ease_angles(pitch, head, ROW_CAR);
}

/* the shadow of car i (0x3d017): 30 further from the camera than the car along the ground (cut), 30 under its place, as the camera stood at the end of the last frame */
int s2garage_shadow(int i, int32_t out[3])
{
    const int32_t *a = s2room_record(1, i, 1);
    const int dx = cam.pos[0] - a[0], dz = cam.pos[2] - a[2];
    const float d = (float)sqrt((double)(dx * dx + dz * dz));
    const float nx = (float)((double)dx / (double)d), nz = (float)((double)dz / (double)d);
    out[0] = (int32_t)((double)a[0] - (double)nx * SHADOW_AWAY);
    out[1] = a[1] - (int32_t)SHADOW_AWAY;
    out[2] = (int32_t)((double)a[2] - (double)nz * SHADOW_AWAY);
    return d > 0.0f;
}

/* the room's picture with the objects: the shadows (the room's own mesh, its materials the room's), the cars (their own libraries) */
static void draw_scene(struct blit_list *l, const struct track_cam *camera, struct track_object *objs, int n, const uint8_t *dark, int bx, int by, int bw, int bh)
{
    const struct s2room_view view = { camera, objs, n, s2showroom_piece, first_frame || s2showroom_fresh(), dark, bx, by, bw, bh };
    first_frame = 0;
    track_reflection_rate(0.002f);                              /* how fast the windows' picture slides: the carousel's value (s2showroom.c) */
    s2showroom_scene_begin();
    s2room_scene(l, &view);
}

void s2garage_car_draw(struct blit_list *l, int state, const uint8_t *dark, int bx, int by, int bw, int bh)
{
    struct track_object objs[2 * S2SHOW_MAX];
    int n = 0;
    const uint8_t *mesh = s2room_shadow();
    for (int i = 0; i < n_cars && mesh; i++) {
        int32_t at[3];
        if (!s2garage_shadow(i, at)) continue;
        const float x = (float)at[0], z = (float)at[2], h = (float)at[1];
        car_flat_block(mesh, x, z, h, 0.0f, 0.0f, 0.0f, &shadow_block[i]);
        objs[n++] = (struct track_object){ x, z, h, S2FX_SHADOW_RADIUS, 1, &shadow_block[i], 0, i, S2FX_SHADOW_KEY, 0, 0 };
    }
    n += s2showroom_objects(0, n_cars, objs + n, 2 * S2SHOW_MAX - n);
    s2garage_car_step(state);
    const struct track_cam camera = s2scene_camera_of(cam.pos, cam.a0, cam.a1);
    draw_scene(l, &camera, objs, n, dark, bx, by, bw, bh);
}

/* ------------------------------------------------------------------------------------------------ the CAR CUSTOMIZATION page */

void s2garage_cust_begin(int car)
{
    chosen = car;
    first_frame = 1;
    s2showroom_init(n_cars, car);
    const int32_t *p = s2room_record(2, CUST_LAST, 0), *a = s2room_record(2, 0, 1);
    s2showroom_place(car, a, CUST_HEADING);                     /* (0x3e33d: aim 0, the angles 0 512 0) */
    for (int k = 0; k < 3; k++) cam.pos[k] = p[k];              /* 0x3e26c: the last station's place */
    float pitch, head;
    s2scene_look_at(cam.pos, a, &pitch, &head);
    cam.a0 = s2scene_angle(pitch);
    cam.a1 = s2scene_angle(head);
}

/* the camera for the item under the cursor (0x3e854): station k = item + 1, at most 7 */
void s2garage_cust_step(int item)
{
    const int k = item + 1 > CUST_LAST ? CUST_LAST : item + 1;
    const int32_t *p = s2room_record(2, k, 0), *a = s2room_record(2, k, 1);
    s2room_ease_place(cam.pos, p, ROW_CUST_PLACE);
    float pitch, head;
    s2scene_look_at(cam.pos, a, &pitch, &head);                 /* (from the place the camera has now) */
    ease_angles(pitch, head, ROW_CUST_ANGLES);
}

void s2garage_cust_draw(struct blit_list *l, int item)
{
    struct track_object objs[1];
    const int n = s2showroom_objects(chosen, 1, objs, 1);
    s2garage_cust_step(item);
    const struct track_cam camera = s2scene_camera_of(cam.pos, cam.a0, cam.a1);
    draw_scene(l, &camera, objs, n, 0, 0, 0, 0, 0);
}
