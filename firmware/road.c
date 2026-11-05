/*
 * The road as a loop and the viewer's scene (road.h)
 */
#include "road.h"
#include "car.h"
#include "s2car.h"
#include <math.h>
#include <string.h>

#define ROAD_MAX    2048                /* road points of a track: 921 to 1921 */
#define PI          3.14159265f

static int npts;                        /* points in the loop */
static float px[ROAD_MAX], py[ROAD_MAX], pz[ROAD_MAX], pa[ROAD_MAX];   /* place, height, heading */
static float cum[ROAD_MAX + 1];         /* length of the loop up to point k; cum[npts] the whole */

int road_build(void)
{
    int nd;
    const struct track_dir *d = track_dirs(&nd);
    static int16_t next[ROAD_MAX];
    car_models_reset();                         /* the cars of the scene come from this track's image */
    static uint8_t seen[ROAD_MAX], has[ROAD_MAX];
    npts = 0;
    if(nd < 3 || nd > ROAD_MAX) return 0;
    for(int i = 0; i < nd; i++) {
        float fx = sinf(d[i].angle), fy = -cosf(d[i].angle), bd = 1e30f;
        int j = i + 1, best = -1;
        /* the next in the list if it lies ahead and close, else the nearest point ahead */
        if(j < nd) {
            float dx = (float)(d[j].x - d[i].x), dy = (float)(d[j].y - d[i].y);
            if(dx * fx + dy * fy > 0.0f && hypotf(dx, dy) < 2500.0f) {
                next[i] = (int16_t)j;
                continue;
            }
        }
        for(j = 0; j < nd; j++) {
            float dx = (float)(d[j].x - d[i].x), dy = (float)(d[j].y - d[i].y), dd = dx * dx + dy * dy;
            if(j != i && dx * fx + dy * fy > 0.0f && dd < bd) { bd = dd; best = j; }
        }
        next[i] = (int16_t)best;
    }
    memset(seen, 0, sizeof seen);
    for(int i = 0; i >= 0 && !seen[i] && npts < ROAD_MAX; i = next[i]) {
        seen[i] = 1;
        px[npts] = (float)d[i].x;
        py[npts] = (float)d[i].y;
        pa[npts] = d[i].angle;
        npts++;
    }
    /* the heights: the drivable surface under the point; where there is none, the one before (for the
       first ones, the first that has one) */
    int first = 0, last = -1;
    for(int k = 0; k < npts; k++) {
        float h = 0.0f;
        int grass;
        has[k] = (uint8_t)track_under(px[k], py[k], &h, &grass);
        pz[k] = has[k] ? h : last >= 0 ? pz[last] : 0.0f;
        if(has[k]) last = k;
    }
    while(first < npts && !has[first]) first++;
    for(int k = 0; k < first && first < npts; k++) pz[k] = pz[first];
    cum[0] = 0.0f;
    for(int k = 0; k < npts; k++) {
        int j = k + 1 < npts ? k + 1 : 0;
        cum[k + 1] = cum[k] + hypotf(px[j] - px[k], py[j] - py[k]);
    }
    return npts;
}

float road_length(void)
{
    return cum[npts];
}

float road_floor(void)
{
    float low = npts ? pz[0] : 0.0f;
    for(int k = 1; k < npts; k++)
        if(pz[k] < low) low = pz[k];
    return low;
}

void road_at(float s, float *x, float *y, float *z, float *yaw)
{
    float len = cum[npts];
    if(npts < 2 || len <= 0.0f) {
        *x = *y = *z = *yaw = 0.0f;
        return;
    }
    s = fmodf(s, len);
    if(s < 0.0f) s += len;
    int lo = 0, hi = npts - 1;                  /* the last k with cum[k] <= s */
    while(lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if(cum[mid] <= s) lo = mid; else hi = mid - 1;
    }
    int j = lo + 1 < npts ? lo + 1 : 0;
    float seg = cum[lo + 1] - cum[lo], t = seg > 0.0f ? (s - cum[lo]) / seg : 0.0f;
    *x = px[lo] + (px[j] - px[lo]) * t;
    *y = py[lo] + (py[j] - py[lo]) * t;
    *z = pz[lo] + (pz[j] - pz[lo]) * t;
    float da = pa[j] - pa[lo];
    while(da > PI) da -= 2.0f * PI;
    while(da < -PI) da += 2.0f * PI;
    *yaw = pa[lo] + da * t;
}

/* the cars of the scene: place, heading, model; their blocks are made when the drawing comes to them */
static struct { float x, y, z, yaw; int model; } car[ROAD_CARS];
static struct track_block blocks[ROAD_CARS][CAR_BLOCKS];

static int car_make(int id, const struct track_cam *cam, struct track_object *ob)
{
    /* standing, not braking, whole: picture 0 of every texture (the object's flags 0; s2car.h look) */
    const struct car_look lk = { car[id].x, car[id].y, car[id].z, car[id].yaw, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0 };
    return car_draw(car[id].model, &lk, cam, blocks[id], ob);
}

int road_scene(float s, int ncars, struct track_cam *cam, struct track_object *objs)
{
    const float behind = 1300.0f, above = 300.0f, down = 0.1885f, pitch = -down;
    float x, y, z, yaw;
    int nm, n = 0;
    track_models(&nm);
    road_at(s, &x, &y, &z, &yaw);
    cam->x = x - cosf(pitch) * behind * sinf(yaw);
    cam->y = y + cosf(pitch) * behind * cosf(yaw);
    cam->h = z + above - behind * sinf(pitch);
    cam->yaw = yaw;
    cam->pitch = pitch;
    cam->roll = 0.0f;
    for(int k = 0; k < ncars && k < ROAD_CARS; k++) {
        float cx = x, cy = y, cz = z, cyaw = yaw;
        if(k) {
            float rx, ry;
            road_at(s + 2200.0f + 1700.0f * (float)(k - 1), &rx, &ry, &cz, &cyaw);
            float side = (k & 1) ? 500.0f : -500.0f;
            cx = rx + cosf(cyaw) * side;
            cy = ry + sinf(cyaw) * side;
        }
        car[k].x = cx;
        car[k].y = cy;
        car[k].z = cz;
        car[k].yaw = cyaw;
        car[k].model = nm ? k % nm : 0;
        objs[n] = (struct track_object){ cx, cy, cz, 1200.0f, 0, 0, car_make, k, 0, 0, 0 };
        n++;
    }
    return n;
}

/* the driving mode's car: its blocks are made when the drawing comes to it */
static const struct s2car_state *drive_car;

static int drive_make(int id, const struct track_cam *cam, struct track_object *ob)
{
    static struct track_block blocks[CAR_BLOCKS];
    struct car_look lk;
    (void)id;
    s2car_look(drive_car, &lk);
    return car_draw(0, &lk, cam, blocks, ob);
}

int road_drive_scene(const struct s2car_state *s, struct track_cam *cam, struct track_object *obj)
{
    drive_car = s;
    s2car_camera(s, cam);
    *obj = (struct track_object){ s->pos[0], s->pos[1], s->pos[2], 1200.0f, 0, 0, drive_make, 0, 0, 0, 0 };
    return 1;
}

/* the race's cars: car k is model AUTO(k+1) with the wheels as its physics has them. Each car's pose is taken when the scene is
   set up, not when the drawing comes to the car: the computers' steps run in the gaps of the drawing (viewer.c, race_others_slice),
   and a car whose blocks were made after its step was one step ahead of one made before it, which it was on some frames and not on
   others (by when the geometry unit's buffer happened to fill): the nearest computer's car stood still for a frame and then jumped two steps. */
static struct car_look race_looks[ROAD_CARS];

static int race_make(int id, const struct track_cam *cam, struct track_object *ob)
{
    static struct track_block rblocks[ROAD_CARS][CAR_BLOCKS];
    int nm;
    track_models(&nm);
    return car_draw(nm ? id % nm : 0, &race_looks[id], cam, rblocks[id], ob);
}

int road_race_scene(struct s2car_state *const *cars, int n, struct track_cam *cam, struct track_object *objs)
{
    s2car_camera(cars[0], cam);
    for(int k = 0; k < n && k < ROAD_CARS; k++) {
        const struct s2car_state *s = cars[k];
        s2car_look(s, &race_looks[k]);
        objs[k] = (struct track_object){ s->pos[0], s->pos[1], s->pos[2], 1200.0f, 0, 0, race_make, k, 0, 0, 0 };
    }
    return n < ROAD_CARS ? n : ROAD_CARS;
}
