/*
 * The race's views for the replay and the end (s2cam.h)
 */
#include "s2cam.h"
#include "s2car.h"
#include "track.h"
#include "dat.h"
#include <math.h>
#include <string.h>

#define MAX_ROWS    80                  /* the camera record has room for 80 (0x14d00) */
#define PI_F        3.14159274f
#define HALF_PI_F   1.57079637f
#define TWO_PI_F    6.28318548f
#define SPEED_WORD  (0x500 / 4)         /* the car's speed in its record: the figure of the drifting camera goes by it */

struct row {
    int32_t x, h, z;
    float pitch, heading, roll;
    int32_t kind;
    float figure;
};

_Static_assert(sizeof(struct row) == 0x20, "a camera's row");

static struct {
    int n;
    struct row row[MAX_ROWS];
    int view, follow;
    int shot, last_shot;                /* the camera in use (-1: none chosen yet) and the one the last frame was drawn from (camera record +0x24, +0x28) */
    int32_t x, h, z;                    /* +0x48 .. : the camera's place, whole numbers */
    float pitch, yaw, roll;             /* +0x54 .. */
    float turn, rise;                   /* +0x3c, +0x38: the circling camera's two turns, 0 .. 1 */
} C;

static uint8_t buf[0x800 + 8];

void s2cam_begin(int track, int view)
{
    memset(&C, 0, sizeof C);
    C.shot = C.last_shot = -1;
    C.view = S2CAM_BACK;
    uint32_t msize = 0;
    const uint8_t *map = s2car_map_dat(&msize);
    if(map && track >= 1 && track <= 7) {
        char name[16] = "CAMERAS0.MCM";
        name[7] = (char)('0' + track);
        struct dat_entry e;
        if(dat_find(map, msize, name, &e) && e.size >= 4 && e.size <= sizeof buf - 8) {
            const uint32_t size = dat_extract(map, msize, &e, buf, sizeof buf - 8);
            int32_t n;
            if(size >= 4) {
                memcpy(&n, buf, 4);
                if(n > 0 && n <= MAX_ROWS && size >= 4u + 0x20u * (uint32_t)n) {
                    memcpy(C.row, buf + 4, 0x20u * (uint32_t)n);
                    C.n = n;
                }
            }
        }
    }
    s2cam_view(view);
}

int s2cam_rows(void)
{
    return C.n;
}

int s2cam_shot(void)
{
    return C.shot;
}

void s2cam_view(int view)
{
    if((view == S2CAM_TRACKSIDE && C.n) || view == S2CAM_BACK) {
        C.view = view;
        C.shot = -1;                    /* 0x12d31 and the key: no camera chosen yet */
    }
}

int s2cam_get_view(void)
{
    return C.view;
}

void s2cam_follow(int car, int ncars)
{
    if(car >= 0 && car < ncars && car != C.follow) {
        C.follow = car;
        C.shot = -1;                    /* 0x12d00: the car to follow, and no camera chosen for it yet */
    }
}

int s2cam_get_follow(void)
{
    return C.follow;
}

void s2cam_next_car(int ncars)
{
    if(ncars > 0) s2cam_follow((C.follow + 1) % ncars, ncars);
}

/* ------------------------------------------------------------------------------------------------ view 1 */

static float rec_float(const struct s2car_state *c, int word)
{
    float f;
    memcpy(&f, &c->rec[word], 4);
    return f;
}

/* The car's place as the camera sees it: whole numbers (the game cuts the car's place every frame) */
static void place_of(const struct s2car_state *c, int32_t p[3])
{
    p[0] = (int32_t)c->pos[0];
    p[1] = (int32_t)c->pos[2];
    p[2] = (int32_t)c->pos[1];
}

/* The camera at `at` looks at the car at p (0x33905): the heading from the vector from the car to the camera (0x10d9e: its angle less a quarter turn), the pitch from the
   angle between that vector and the ground (up if the camera is lower than the car, down if higher). Single precision: the Pocket's CPU has no double. */
static void look_at(int32_t ax, int32_t ay, int32_t az, const int32_t p[3], float *pitch, float *yaw)
{
    const float dx = (float)(ax - p[0]), dy = (float)(ay - p[1]), dz = (float)(az - p[2]);
    const float flat = sqrtf(dx * dx + dz * dz);
    *yaw = atan2f(dz, dx) - HALF_PI_F;
    *pitch = atan2f(fabsf(dy), flat) * (dy < 0.0f ? 1.0f : -1.0f);
}

/* A point turned about the vertical axis (0x10388): x' = x cos a + z sin a, z' = z cos a - x sin a */
static void turned(float x, float z, float a, float out[2])
{
    out[0] = z * sinf(a) + x * cosf(a);
    out[1] = z * cosf(a) - x * sinf(a);
}

/* Which row (0x14acf): the one nearest to the car on the ground, in 64ths and not at no distance at all, taken when it is the next one or the one before the one in use (round the end
   too), or when none is yet. The distance is a float here (the game's is a double): only a row that is nearer by less than a millionth could be taken instead of another. */
static void choose(const int32_t p[3])
{
    const int now = C.shot, next = now + 1, before = now - 1;
    int best = -1, pick = now;
    for(int i = 0; i < C.n; i++) {
        const float dx = (float)((C.row[i].x >> 6) - (p[0] >> 6)), dz = (float)((C.row[i].z >> 6) - (p[2] >> 6));
        const float d = sqrtf(dx * dx + dz * dz);
        if(d != 0.0f && ((float)best > d || best < 0)) {
            best = (int32_t)d;
            pick = i;
        }
    }
    if((pick == next || now == -1 || (next == C.n && pick == 0) || pick == before || (before < 0 && C.n - 1 == pick)) && pick != now) C.shot = pick;
}

static void trackside(const struct s2car_state *car, float dt)
{
    int32_t p[3];
    place_of(car, p);
    choose(p);
    if(C.shot < 0) return;
    const struct row *r = &C.row[C.shot];
    const float figure = r->figure;
    switch(r->kind) {
    case 0:
        C.x = r->x; C.h = r->h; C.z = r->z;
        C.pitch = r->pitch; C.yaw = r->heading; C.roll = r->roll;
        return;
    case 1:
        if(C.shot != C.last_shot) {
            C.x = r->x; C.h = r->h; C.z = r->z;
        } else if(figure != 0.0f) {
            const float k = figure * rec_float(car, SPEED_WORD) * 111.0f, cp = cosf(C.pitch);
            C.x = (int32_t)(cp * sinf(C.yaw) * k * dt + (float)C.x);
            C.h = (int32_t)(sinf(C.pitch) * k * dt + (float)C.h);
            C.z = (int32_t)(-cp * cosf(C.yaw) * k * dt + (float)C.z);
        }
        look_at(C.x, C.h, C.z, p, &C.pitch, &C.yaw);
        C.roll = r->roll;
        return;
    case 2: {
        const float share = 0.8f - figure * 0.75f;
        C.x = (int32_t)((float)(r->x - p[0]) * share + (float)p[0]);
        C.h = (int32_t)((float)(r->h - p[1]) * share + (float)p[1]);
        C.z = (int32_t)((float)(r->z - p[2]) * share + (float)p[2]);
        look_at(C.x, C.h, C.z, p, &C.pitch, &C.yaw);
        C.roll = r->roll;
        return;
    }
    case 3: {
        float t[2];
        turned(1100.0f, 0.0f, C.turn * TWO_PI_F, t);
        C.x = p[0] + (int32_t)t[0];
        C.h = p[1] + (int32_t)(sinf(C.rise * 12.566371f) * 220.0f + 440.0f);
        C.z = p[2] + (int32_t)t[1];
        for(int k = 0; k < 2; k++) {
            float *v = k ? &C.rise : &C.turn;
            *v += (figure * 0.5f + 0.1f) * dt;
            if(*v > 1.0f) *v -= 1.0f;
        }
        look_at(C.x, C.h, C.z, p, &C.pitch, &C.yaw);
        C.roll = 0.0f;
        return;
    }
    case 4: {
        float t[2];
        turned((float)(int32_t)(figure * 500.0f + 1320.0f), 0.0f, r->heading + PI_F, t);
        C.x = p[0] + (int32_t)t[0];
        C.h = p[1] + (int32_t)(figure * 200.0f + 550.0f);
        C.z = p[2] + (int32_t)t[1];
        look_at(C.x, C.h, C.z, p, &C.pitch, &C.yaw);
        C.roll = 0.0f;
        return;
    }
    default:
        return;
    }
}

void s2cam_frame(const struct s2car_state *const *cars, int n, float dt, struct track_cam *cam)
{
    if(C.follow >= n) C.follow = 0;
    const struct s2car_state *car = cars[C.follow];
    if(C.view == S2CAM_TRACKSIDE && C.n) {
        trackside(car, dt);
        if(C.shot >= 0) {
            *cam = (struct track_cam){ (float)C.x, (float)C.z, (float)C.h, C.yaw, C.pitch, C.roll };
            C.last_shot = C.shot;
        } else {                        /* no camera chosen (the car stands where no row is nearer than none) */
            s2car_camera(car, cam);
        }
    } else {
        s2car_camera(car, cam);
    }
}
