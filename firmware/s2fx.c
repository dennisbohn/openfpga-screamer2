/*
 * Wheel smoke and shadows of the race (s2fx.h)
 */
#include "s2fx.h"
#include "car.h"
#include <math.h>

#define STEP        0.02f               /* a step of the race: the game's raw frame time, which its spawn timer adds */
#define PUFF_AGE    (2162.0f / 65536.0f)    /* the puff's age (0 .. 1) grows by 2162 of 65536 a frame: the game's step x 1.5 (0x2073b), its step 1.1 x the raw time, 0.022 (docs/screamer2/driving.md); 30 frames, as measured */
#define PUFF_RADIUS 420.0f              /* culling radii of the objects (the quads' own are 409 and 871) */
#define SHADOW_RADIUS 900.0f
#define SHADOW_KEY  -200                /* "class 4 sorts 200 further back": the shadow is drawn before what stands round it */

struct puff {
    uint8_t on, carried;
    float age;                          /* 0 .. 1 */
    float p[3];                         /* the game's x, up, z: in the world, or the car's own frame when carried (the camera's car) */
};

static struct {
    float timer;                        /* the car's record +0x224 */
    struct puff puff[2][S2FX_PUFFS];    /* [0] 'asmoke' (record +0x228), [1] 'a2smoke' (+0x368) */
} car[S2FX_CARS];

static struct s2car_fx before[S2FX_CARS];  /* each car as it stood when the last call ended: where the next step begins */
static int have_before;

static struct track_block shadow_block[S2FX_CARS], puff_block[S2FX_CARS][2][S2FX_PUFFS];

void s2fx_reset(void)
{
    have_before = 0;
    for(int i = 0; i < S2FX_CARS; i++) {
        car[i].timer = 0.0f;
        for(int k = 0; k < 2; k++)
            for(int j = 0; j < S2FX_PUFFS; j++) car[i].puff[k][j].on = 0;
    }
}

int s2fx_puffs(void)
{
    int n = 0;
    for(int i = 0; i < S2FX_CARS; i++)
        for(int k = 0; k < 2; k++)
            for(int j = 0; j < S2FX_PUFFS; j++) n += car[i].puff[k][j].on;
    return n;
}

int s2fx_puff_state(int i, int k, int j, float p[3], float *age, int *carried)
{
    const struct puff *q = &car[i].puff[k][j];
    for(int m = 0; m < 3; m++) p[m] = q->p[m];
    *age = q->age;
    *carried = q->carried;
    return q->on;
}

void s2fx_puff_load(int i, int k, int j, int on, float age, const float p[3], int carried)
{
    struct puff *q = &car[i].puff[k][j];
    q->on = (uint8_t)on;
    q->age = age;
    q->carried = (uint8_t)carried;
    for(int m = 0; m < 3; m++) q->p[m] = p[m];
}

float s2fx_timer(int i, int set, float t)
{
    if(set) car[i].timer = t;
    return car[i].timer;
}

/* 0x204ae: every 0.08 s (the timer adds the step while it is under 0.05) a puff at each rear corner: `forced` (the caller found the tyres sliding) or the road kind's
   flag 4 and the car over speed 20; the kind's flags & 3 say which sort, the first free record takes it */
static void spawn(int i, const struct s2car_fx *f, int forced, uint32_t T, int followed)
{
    if(car[i].timer < 0.05f) {
        car[i].timer += STEP;
        return;
    }
    car[i].timer = 0.0f;
    for(int w = 0; w < 2; w++) {
        if(!forced && !((f->rear[w].flags & 4) && f->speed > 20.0f)) continue;
        const int sort = f->rear[w].flags & 3;
        if(sort != 1 && sort != 2) continue;
        struct puff *q = car[i].puff[sort - 1];
        int j = 0;
        while(j < S2FX_PUFFS && q[j].on) j++;
        if(j == S2FX_PUFFS) continue;
        const int d = f->rear[w].dispersion, spread = (int)((uint32_t)(d + d) & T) - d;
        const int ox = spread * (f->x & 7), oz = spread * (f->z & 7);
        const int *at = i == followed ? f->rear[w].local : f->rear[w].world;
        q[j] = (struct puff){ 1, (uint8_t)(i == followed), 0.0f, { (float)(at[0] + ox), (float)at[1], (float)(at[2] + oz) } };
    }
}

void s2fx_update(struct s2car_state *const *cars, int n, int steps, uint32_t clock, int followed)
{
    if(!track_fx(&(struct track_fx){ 0 })) return;
    for(int s = 0; s < steps; s++)
        for(int i = 0; i < n && i < S2FX_CARS; i++) {
            struct s2car_fx now;
            s2car_fx(cars[i], &now);
            /* the spawn is the tyres' (0x1b36f, the step's first work): it finds the slip the step has just worked out (the call is under `slip < 0.93`), and the rest, the
               corners and the places, the road kinds, the speed and the whole-number place, as the step before left them (the corners come last in a step) */
            const struct s2car_fx *at = s == 0 && have_before ? &before[i] : &now;
            if(have_before && now.on_ground) spawn(i, at, now.slip < 0.93f, clock, followed);
            for(int k = 0; k < 2; k++)
                for(int j = 0; j < S2FX_PUFFS; j++) {
                    struct puff *q = &car[i].puff[k][j];
                    if(q->on && (q->age += PUFF_AGE) >= 1.0f) q->on = 0;
                }
            if(s == steps - 1) before[i] = now;
        }
    have_before = 1;
}

/* a carried puff's place (0x2073b; checked against the game's records in tools/s2/check_smoke_s2.py): its spot in the car's frame turned about the vertical by minus
   the heading (0x10388), its height dropped (the game zeroes it before the next turn), then about the side axis by the pitch (0x1030d), each cut to a whole number
   (the game's rounding is set to chop), and the car's place added; out: the game's x, up, z */
static void carried(const struct puff *q, const struct s2car_fx *f, float out[3])
{
    const float c = cosf(-f->yaw), s = sinf(-f->yaw), cp = cosf(f->pitch), sp = sinf(f->pitch);
    const float x = (float)(int32_t)(q->p[2] * s + q->p[0] * c), z = (float)(int32_t)(q->p[2] * c - q->p[0] * s);
    out[0] = (float)f->x + x;
    out[1] = (float)f->y + (float)(int32_t)(-z * sp);
    out[2] = (float)f->z + (float)(int32_t)(z * cp);
}

int s2fx_objects(const struct track_cam *cam, struct s2car_state *const *cars, int n, struct track_object *objs, int max)
{
    struct track_fx fx;
    if(!track_fx(&fx)) return 0;
    int count = 0;
    float f[3], r[3], u[3];
    track_cam_axes(cam, f, r, u);
    for(int i = 0; i < n && i < S2FX_CARS; i++) {
        struct s2car_fx a;
        s2car_fx(cars[i], &a);
        if(fx.shadow && count < max) {              /* 0x1f4a7: 30 units further from the camera than the car along the ground, 2 under the ground under it */
            const float dx = cam->x - (float)a.x, dz = cam->y - (float)a.z, d = sqrtf(dx * dx + dz * dz);
            const float x = d > 0.0f ? (float)(int32_t)((float)a.x - dx / d * 30.0f) : (float)a.x;
            const float z = d > 0.0f ? (float)(int32_t)((float)a.z - dz / d * 30.0f) : (float)a.z;
            struct car_look lk;
            s2car_look(cars[i], &lk);
            car_flat_block(fx.shadow, x, z, (float)(a.ground - 2), lk.yaw, lk.pitch, lk.roll, &shadow_block[i]);
            objs[count++] = (struct track_object){ x, z, (float)(a.ground - 2), SHADOW_RADIUS, 1, &shadow_block[i], 0, i, SHADOW_KEY, 0, 0 };
        }
        for(int k = 0; k < 2; k++) {
            const uint8_t *mesh = k ? fx.smoke2 : fx.smoke;
            for(int j = 0; j < S2FX_PUFFS && mesh && count < max; j++) {
                const struct puff *q = &car[i].puff[k][j];
                if(!q->on) continue;
                float p[3] = { q->p[0], q->p[1], q->p[2] };
                if(q->carried) carried(q, &a, p);
                struct track_block *t = &puff_block[i][k][j];
                for(int m = 0; m < 3; m++) {        /* facing the screen: the camera's right, up and forward are the block's x, y and z */
                    t->m[3 * m] = r[m];
                    t->m[3 * m + 1] = u[m];
                    t->m[3 * m + 2] = f[m];
                }
                t->o[0] = p[0];
                t->o[1] = p[2];
                t->o[2] = p[1];
                t->file = mesh;
                t->blk = mesh + *(const int32_t *)(mesh + 0x18);
                t->phase = (uint32_t)(q->age * 65536.0f) >> 2;
                objs[count++] = (struct track_object){ p[0], p[2], p[1], PUFF_RADIUS, 1, t, 0, j, 0, 0, 0 };
            }
        }
    }
    return count;
}
