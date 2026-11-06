/*
 * The views from the car (s2cockpit.h)
 */
#include "s2cockpit.h"
#include "s2car.h"
#include "s2params.h"
#include "s2teams.h"
#include "track.h"
#include <math.h>
#include <string.h>

#define PI_F        3.14159274f
#define HALF_PI_F   1.57079637f
#define TWO_PI_F    6.28318548f

/* the car record's words (s2car.h keeps the game's layout) */
union word { int32_t i; uint32_t u; float f; };
#define RF(c, o) (((const union word *)(c)->rec)[(o) >> 2].f)
#define RI(c, o) (((const union word *)(c)->rec)[(o) >> 2].i)

float s2ck_ease_power = 1.0f;

/* 0x1016b: an angle eased the short way round towards its target by the row of the game's easing table (0x1240c0: the game builds it for the frame rate it measures, 0x315a0: a row's factor
   0.5 + 0.005 row to the power of 50 / the pictures a second; 1 at the Pocket's 50), kept in 0 .. 2 pi. s2car.c's is the same and static. */
static void ease_angle(float *v, float target, int row)
{
    float factor = 0.5f + (float)row * 0.005f, a = *v;
    if(s2ck_ease_power != 1.0f) factor = powf(factor, s2ck_ease_power);
    if(a != target) {
        if(a < target) *v = target - a >= PI_F ? -((target - a) + -TWO_PI_F) * factor + target : (a - target) * factor + target;
        else *v = a - target >= PI_F ? -((target - a) + TWO_PI_F) * factor + target : (a - target) * factor + target;
    }
    if(*v < 0.0f) *v += TWO_PI_F;
    else if(*v >= TWO_PI_F) *v += -TWO_PI_F;
}

int s2ck_hides_car(int view)
{
    return view == S2CK_INCAR || view == S2CK_INSIDE || view == S2CK_MIRROR;
}

void s2ck_reset(struct s2ck_state *k)
{
    memset(k, 0, sizeof *k);
    k->last_view = -1;                          /* (the record's +0x2c is set to something no view is: the first frame snaps) */
}

/* a point (x, y, z) turned by roll about z, by pitch about x, by turn about the up axis: 0x105cd's relative, which 0x14200 runs twice. s2car.c's turn3 is the same and static. */
static void turn3(float v[3], float roll, float pitch, float turn)
{
    float s = sinf(roll), co = cosf(roll);
    float x = v[0] * co - v[1] * s, y = v[1] * co + v[0] * s;
    v[0] = x;
    v[1] = y;
    s = sinf(pitch);
    co = cosf(pitch);
    y = v[1] * co + v[2] * s;
    float z = v[2] * co - v[1] * s;
    v[1] = y;
    v[2] = z;
    s = sinf(turn);
    co = cosf(turn);
    x = v[0] * co - v[2] * s;
    z = v[2] * co + v[0] * s;
    v[0] = x;
    v[2] = z;
}

void s2ck_camera(const struct s2car_state *c, int view, int body, struct s2ck_state *k, struct track_cam *cam)
{
    const float pitch = RF(c, 0x4e4), yaw = RF(c, 0x4e8), roll = RF(c, 0x4ec);
    const float lean_p = RF(c, 0xc18), lean_y = RF(c, 0xc1c), lean_r = RF(c, 0xc20);
    const float x = RF(c, 0x4d8), z = RF(c, 0x4e0);
    const float h = (float)(RI(c, 0x4dc) + RI(c, 0x4f4) + body);     /* the place's height, the springs' mean (the object's +0x1c) and the body's block */
    const int snap = k->last_view != view;
    switch(view) {
    case S2CK_INCAR:                            /* 0x13bab: the car's place and angles, 400 above its body; the lean's roll at half */
    case S2CK_INSIDE:                           /* 0x13cbd: the same, 300 above */
        *cam = (struct track_cam){ x, z, h + (view == S2CK_INCAR ? 400.0f : 300.0f), yaw, pitch + lean_p, roll + lean_r * 0.5f };
        break;
    case S2CK_MIRROR:                           /* 0x13e1a: backwards, the tilt and lean turned over, 400 above the body */
        *cam = (struct track_cam){ x, z, h + 400.0f, yaw + PI_F, -pitch - lean_p, -roll - lean_r * 0.5f };
        break;
    case S2CK_NEAR:                             /* 0x14200: the car's POSIZIONE TELECAMERA (along, up), turned by the eased angles: half the car's pitch less 0.3, half its roll, its heading */
        if(snap) {
            k->pitch = (pitch + lean_p) * 0.5f - 0.3f;
            k->roll = (roll + lean_r) * 0.5f;
            k->yaw = yaw + lean_y;
        } else {
            ease_angle(&k->pitch, s2car_ang_diff(pitch, -lean_p) * 0.5f - 0.3f, 0x3c);
            ease_angle(&k->roll, s2car_ang_diff(roll, -lean_r) * 0.5f, 0x3c);
            ease_angle(&k->yaw, yaw + lean_y, 0x50);
        }
        {
            float along[3] = { 0.0f, 0.0f, (float)RI(c, 0xac) }, up[3] = { 0.0f, (float)RI(c, 0xb0), 0.0f };
            turn3(along, k->roll, -k->pitch, k->yaw);
            turn3(up, k->roll, -k->pitch, k->yaw);
            *cam = (struct track_cam){ x + along[0] + up[0], z + along[2] + up[2], h + along[1] + up[1], k->yaw, k->pitch, k->roll };
        }
        break;
    default:
        break;
    }
    if(view != S2CK_MIRROR) k->last_view = view;           /* (the mirror's camera is a record of its own in the game) */
}

void s2ck_glass(const volatile uint32_t *fb, uint8_t *strip)
{
    uint32_t *dst = (uint32_t *)strip;
    for(int y = 0; y < S2CK_MIRROR_ROWS; y++) {
        const volatile uint32_t *row = fb + y * (320 / 4);
        for(int s = 0; s < S2CK_MIRROR_W / 4; s++) {
            const uint32_t a = row[79 - 2 * s], b = row[78 - 2 * s];
            *dst++ = (a >> 16 & 0xFF) | (a & 0xFF) << 8 | (b >> 16 & 0xFF) << 16 | (b & 0xFF) << 24;
        }
    }
}

int s2ck_rows(int view, const struct s2ck_dash *dash)
{
    if(view == S2CK_MIRROR) return S2CK_MIRROR_ROWS;
    if(view == S2CK_INSIDE && dash && dash->rows > 0) return dash->rows / 2;      /* (the table's units are the 640-wide picture's) */
    return S2CK_ROWS;
}

int s2ck_model(const struct s2_pick *p)
{
    return p->car >= 5 ? p->team - 1 + 4 : p->car - 1;
}

int s2ck_body_height(int slot)
{
    const uint8_t *mesh = track_model_mesh(slot);
    if(!mesh) return 0;
    const int32_t n = *(const int32_t *)(mesh + 0x14);
    for(int32_t b = 0; b < n && b < 64; b++) {
        const uint8_t *blk = mesh + *(const int32_t *)(mesh + 0x18 + 4 * b);
        if(strncmp((const char *)blk + 0x68, "MV_mauto", 9)) continue;
        const int32_t so = *(const int32_t *)(blk + 0x0C);
        return so > 0 ? *(const int32_t *)(mesh + so + 0x14) : 0;
    }
    return 0;
}

/* ----------------------------------------------------------------------------------------------- the dashboards */

int s2ck_dash_parse(const uint8_t *text, uint32_t size, struct s2ck_dash out[S2CK_DASHES])
{
    _Static_assert(sizeof(struct s2ck_dash) == 0x58, "a CRUSCOTTI row is 0x58 bytes");
    uint32_t w[S2CK_DASHES * 22];
    if(s2_dtr_words(text, size, "CRUSCOTTI", w, S2CK_DASHES * 22) != S2CK_DASHES * 22) return 0;
    memcpy(out, w, sizeof w);
    return 1;
}

/* a whole number halved as the low-res program does (sar after the sign: towards zero) */
static int half(int32_t v)
{
    return (v - (v >> 31)) >> 1;
}

void s2ck_needle(const struct s2ck_needle *n, float part, struct s2ck_line out[3])
{
    const int px = half(n->x), py = half(n->y);
    float a = n->a0 - (n->a0 - n->a1) * part;                   /* nought less the part of the way to the most */
    if(fabsf(a) < 0.001f) a = 0.0f;
    const int tx = (int)((float)n->len * cosf(a) * 0.5f + (float)px), ty = (int)((float)py - (float)n->len * sinf(a) * 0.5f);
    a += HALF_PI_F;                                             /* a pixel to either side, at a right angle to the needle */
    const int sx = (int)(cosf(a) + (float)px), sy = (int)((float)py - sinf(a));
    a += -PI_F;
    const int ux = (int)(cosf(a) + (float)px), uy = (int)((float)py - sinf(a));
    out[0] = (struct s2ck_line){ px, py, tx, ty };
    out[1] = (struct s2ck_line){ sx, sy, tx, ty };
    out[2] = (struct s2ck_line){ ux, uy, tx, ty };
}

void s2ck_lamp(const struct s2ck_dash *d, int model, int gear, int *x, int *y, int *sprite)
{
    if(gear < 0) gear = 0;
    if(gear > 4) gear = 4;
    *x = half(d->lamp[gear][0]);
    *y = half(d->lamp[gear][1]);
    *sprite = 0x46 + 5 * model + gear;
}
