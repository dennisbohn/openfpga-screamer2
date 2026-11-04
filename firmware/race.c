/*
 * The race of the driving mode (race.h)
 *
 * The cars are s2car.c's, all four. What is added here is the game's: the line the computers follow and the road
 * is measured on (MAPnOPP.SPL: nodes of 0x38 bytes, each with two handles; the curve through them is a quadratic
 * B-spline per half node), where a car is along it and how many laps it has done (0x153c6, 0x201ee), the places and
 * the computers' pace (0x2da06), and the computers' driver (0x17726). The names in the comments are the game's
 * addresses; the record words (RF / RI) are the game's car record, which s2car.c keeps word for word.
 * Single precision (the game's x87 sums are not followed to the last bit: the check against the game's own cars is
 * that they drive the line, not a trace).
 */
#include "race.h"
#include "s2params.h"
#include "dat.h"
#include <math.h>
#include <string.h>

#define PI_F        3.14159274f
#define TWO_PI_F    6.28318548f
#define HALF_PI_F   1.57079637f
#define DT          0.02f
#define LEVEL       1               /* the arcade race's level: 0 pro, 1 normal, 2 rookie (the SKILLS row) */
#define LINE_MAX    64              /* nodes of a line: 36 to 48 */
#define OFF_STEPS   150             /* a car with no ground under it for 3 s is put back on the line */
#define STILL_STEPS 300             /* a computer's car that has not moved for 6 s too */

union word { int32_t i; uint32_t u; float f; };
#define F(c, o) (((union word *)(c)->rec)[(o) >> 2].f)
#define I(c, o) (((union word *)(c)->rec)[(o) >> 2].i)

/* ------------------------------------------------------------------------------------------------ the line */

struct node {
    int32_t x, z;                   /* the point (the grid starts at node 0) */
    int32_t share;                  /* +0xc: the speed share (percent under the top speed the car lifts at; 0: flat out) */
    int32_t next;
    int32_t h1[2], h2[2];           /* the handles */
    int32_t dist;                   /* +0x34: the way from node 0 to the end of this node, in units (0x309b0) */
};

static struct { int n, track; struct node nd[LINE_MAX]; } L;

/* a place on the line (the game's 0x78-byte state at car +0xc4) */
struct pstate {
    int node, half;
    float share;                    /* 0 .. 1 in the half node */
    int32_t ix, iz;                 /* the last point, cut to whole numbers (+0x10, +0x18) */
    float h[3][2];                  /* the handles of this half node */
    float px, pz, ph;               /* the point (+0x60, +0x64) and its height (+0x68) */
    float way;                      /* the heading there (+0x34), set by line_step() */
};

static float ang_diff(float a, float b)                 /* 0x14dc0 */
{
    float d = a - b;
    while(d > PI_F) d += -TWO_PI_F;
    while(d < -PI_F) d += TWO_PI_F;
    return d;
}

/* 0x111d6: a float eased towards a target by the row i of the easing table */
static void ease(float *v, float target, int i) { *v = (*v - target) * (0.5f + (float)i * 0.005f) + target; }

static float ground_angle(float x, float z)
{
    float a = z != 0.0f ? atanf(fabsf(x / z)) : HALF_PI_F;
    if(z > 0.0f) a = PI_F - a;
    if(x < 0.0f) a = TWO_PI_F - a;
    return a;
}

/* the handles of the half node and the curve's point at the share (0x1099a, 0x4a831) */
static void bspline(const float h[3][2], float t, float *x, float *z)
{
    float a = ((1.0f - t) * (1.0f - t)) * 0.5f, c = (t * t) * 0.5f, b = 1.0f - (a + c);
    *x = (h[1][0] * b + h[0][0] * a) + h[2][0] * c;
    *z = (h[0][1] * a + h[1][1] * b) + h[2][1] * c;
}

static void load_handles(struct pstate *s)
{
    const struct node *a = &L.nd[s->node], *b = &L.nd[a->next];
    const int32_t *p[3] = { s->half ? a->h2 : a->h1, s->half ? b->h1 : a->h2, s->half ? b->h2 : b->h1 };
    for(int k = 0; k < 3; k++) { s->h[k][0] = (float)p[k][0]; s->h[k][1] = (float)p[k][1]; }
}

static void point(struct pstate *s)
{
    load_handles(s);
    bspline(s->h, s->share, &s->px, &s->pz);
    const struct node *a = &L.nd[s->node], *b = &L.nd[a->next];
    s->ph = (float)(b->share - a->share) * ((float)s->half + s->share) * 0.5f + (float)a->share;
}

/* how fast the curve goes at the share (0x108f5) */
static float slope_len(const struct pstate *s)
{
    const float (*h)[2] = s->h, t = s->share;
    float dx = ((h[0][0] - h[1][0] * 2.0f) + h[2][0]) * t + h[1][0] - h[0][0];
    float dz = ((h[0][1] - h[1][1] * 2.0f) + h[2][1]) * t + h[1][1] - h[0][1];
    float len = sqrtf(dx * dx + dz * dz);
    return len == 0.0f ? 1.0f : len;
}

/* the place on the line moves on by `by` units (0x2d59c): the share by what it makes of the curve's length there; at the
   end of a half node on to the next, with what is left of the distance less the way from the last point to the new */
static void line_step(struct pstate *s, float by, int with_way)
{
    for(int guard = 0; guard < 64; guard++) {
        float len = slope_len(s);
        s->share = by / len + s->share;
        if(!(s->share >= 1.0f)) break;
        s->share = 0.0f;
        if(s->half == 0) s->half = 1;
        else { s->half = 0; s->node = L.nd[s->node].next; }
        point(s);
        float dx = s->px - (float)s->ix, dz = s->pz - (float)s->iz;
        by = by - sqrtf(dz * dz + dx * dx);
        s->ix = (int32_t)s->px;
        s->iz = (int32_t)s->pz;
    }
    point(s);
    s->ix = (int32_t)s->px;
    s->iz = (int32_t)s->pz;
    if(with_way) {                                      /* the way at the point: its second point 0.002 on */
        float x2, z2;
        bspline(s->h, s->share + 0.002f, &x2, &z2);
        s->way = ground_angle(x2 - s->px, z2 - s->pz);
    }
}

/* the point of the line t nodes from its start, in the 3-vector (x, height, z) (0x152f7) */
static void point_at(struct pstate *tmp, float out[3], float t)
{
    const int n = L.n;
    int node = (int)((float)n + t);
    float share = (t - (float)(node - n)) * 2.0f;
    tmp->node = node % n;
    if(share < 1.0f) tmp->half = 0;
    else { share += -1.0f; tmp->half = 1; }
    tmp->share = share;
    point(tmp);
    out[0] = tmp->px; out[1] = tmp->ph; out[2] = tmp->pz;
}

/* how far along the line from c to b the point a falls (0x1766c), as a share of its length; 0 for a line of no length */
static float share_along(const float a[3], const float b[3], const float c[3])
{
    float dx = b[0] - c[0], dz = b[2] - c[2], len2 = dz * dz + dx * dx;
    if(len2 == 0.0f) return 0.0f;
    return (dx * (b[0] - a[0]) + dz * (b[2] - a[2])) / len2;
}

/* The line measured (0x309b0): stepped along 100 units at a time, each node takes the distance covered when the
   place leaves it */
static void measure(void)
{
    struct pstate s;
    memset(&s, 0, sizeof s);
    point(&s);
    float dist = 0.0f;
    int prev, guard = 0;
    do {
        prev = s.node;
        line_step(&s, 100.0f, 0);
        dist += 100.0f;
        if(prev != s.node) L.nd[prev].dist = (int32_t)dist;
    } while((prev == 0 || s.node != 0) && ++guard < 40000);
}

static uint8_t scratch[0x1000];

static int load_line(int track)
{
    if(L.track == track && L.n) return 1;
    L.n = 0;
    uint32_t msize;
    const uint8_t *map = s2car_map_dat(&msize);
    if(!map) return 0;
    char name[16] = "MAP0OPP.SPL";
    name[3] = (char)('0' + track);
    struct dat_entry e;
    if(!dat_find(map, msize, name, &e)) return 0;
    uint32_t size = dat_extract(map, msize, &e, scratch, sizeof scratch);
    int32_t count;
    if(size < 8 + 56) return 0;
    memcpy(&count, scratch, 4);
    if(count < 3 || count > LINE_MAX || size < 8u + 56u * (uint32_t)count) return 0;
    for(int i = 0; i < count; i++) {
        int32_t w[14];
        memcpy(w, scratch + 8 + 56 * i, 56);
        if(w[4] < 0 || w[4] >= count) return 0;
        L.nd[i] = (struct node){ w[1], w[2], w[3], w[4], { w[5], w[6] }, { w[8], w[9] }, 0 };
    }
    L.n = count;
    L.track = track;
    measure();
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the race */

static struct {
    int track, n, running;
    float clock;                                        /* the race's clock (0x13335c) */
    uint32_t steps;
    float floor_z;
    float skills[3][7];                                 /* MISC.DTR's SKILLS: the pace taken off a computer's car */
    int skills_ok;
    struct s2car_state car[RACE_CARS];
    struct pstate st[RACE_CARS];                        /* each car's place on the line (car + 0xc4) */
    float near[RACE_CARS][2];                           /* the nearest point of the line, whole numbers */
    uint32_t off[RACE_CARS], still[RACE_CARS];
    int team[RACE_CARS], model[RACE_CARS];
} R;

struct s2car_state *race_car(int i) { return &R.car[i]; }
int race_cars(void) { return R.n; }
uint32_t race_steps(void) { return R.steps; }
int race_line_length(void) { return L.n ? L.nd[L.n - 1].dist : 0; }
static uint32_t respawns;
uint32_t race_respawns(void) { return respawns; }

static void load_skills(void)
{
    uint32_t size;
    const uint8_t *game = s2car_game_dat(&size);
    struct dat_entry e;
    uint32_t w[21];
    R.skills_ok = 0;
    if(!game || !dat_find(game, size, "MISC.DTR", &e)) return;
    uint32_t n = dat_extract(game, size, &e, scratch, sizeof scratch);
    if(!n || s2_dtr_words(scratch, n, "SKILLS", w, 21) != 21) return;
    memcpy(R.skills, w, sizeof w);
    R.skills_ok = 1;
}

/* A lap done (0x201ee): a lap not done before counts; the car's best lap; after the last the car has finished (it is
   the driver's, and stops when its time is up), given a fifth of a second for each car behind it */
static void lap_done(int i, int by)
{
    (void)i;                                            /* (the laps are not counted yet) */
    (void)by;
}

/* Where the car is along the road (0x153c6): the line's four points half a node apart round where the car was last
   (a node count with the laps in it, +0x4b8), the car's share along each stretch and its distance from it; the middle
   point should be the nearest, else the four step half a node to the nearer side. Round the end of the line a lap is
   done (or undone). Gives the node, the road point there (st.ix, st.iz), the distance in nodes (+0x4b8) and in units
   (+0x4bc), and how far from the middle of the road the car is, signed by the side (+0x4c0). */
static void locate(int i)
{
    struct s2car_state *c = &R.car[i];
    struct pstate *st = &R.st[i], tmp;
    const int nodes = L.n;
    const float P[3] = { F(c, 0x4d8), (float)I(c, 0x4dc), F(c, 0x4e0) };
    float Q[4][3], sh[5] = { 0 }, dist[3] = { 0 };
    int lap = 0;
    float u = (float)(int)((F(c, 0x4b8) - (float)(I(c, 0x4a8) * nodes)) / 0.5f) * 0.5f;
    point_at(&tmp, Q[0], u + -0.5f);
    point_at(&tmp, Q[1], u);
    point_at(&tmp, Q[2], u + 0.5f);
    point_at(&tmp, Q[3], (u + 0.5f) + 0.5f);
#define DIST(j, s) do { \
        float om_ = 1.0f - sh[s], d_[3]; \
        for(int k_ = 0; k_ < 3; k_++) d_[k_] = (P[k_] - Q[j][k_] * om_) - Q[(j) + 1][k_] * sh[s]; \
        dist[j] = sqrtf(d_[1] * d_[1] + d_[0] * d_[0] + d_[2] * d_[2]); } while(0)
    for(int j = 0; j < 3; j++) {
        float v = share_along(P, Q[j], Q[j + 1]);
        if(0.0f >= v) sh[3] = 0.0f;
        else { sh[4] = !(1.0f > v) ? 1.0f : v; sh[3] = sh[4]; }
        sh[j] = sh[3];
    }
    for(int j = 0; j < 3; j++) DIST(j, j);
    for(int guard = 0; guard < 2 * nodes + 8; guard++) {
        float dir = !(dist[0] >= dist[1]) ? -1.0f : dist[2] >= dist[1] ? 0.0f : 1.0f;
        if(dir == 0.0f) break;
        u = dir * 0.5f + u;
        if(dir == 1.0f) {
            for(int j = 0; j < 3; j++) { memcpy(Q[j], Q[j + 1], sizeof Q[0]); sh[j] = sh[j + 1]; }
            point_at(&tmp, Q[3], (u + 0.5f) + 0.5f);
            DIST(2, 2);
        } else {
            for(int j = 3; j > 0; j--) { memcpy(Q[j], Q[j - 1], sizeof Q[0]); sh[j] = sh[j - 1]; }
            point_at(&tmp, Q[0], u + -0.5f);
            DIST(0, 0);
        }
        if(0.0f > u) { lap = (int)dir; u = (float)nodes + u; }
        else if(!((float)nodes > u)) { lap = (int)dir; u = u - (float)nodes; }
    }
#undef DIST
    if(sh[1] > 0.99f) sh[1] = 0.99f;
    u = sh[1] * 0.5f + u;
    int node = (int)u;
    float frac = u - (float)node;
    st->share = frac * 2.0f;
    if(st->share < 1.0f) st->half = 0;
    else { st->share += -1.0f; st->half = 1; }
    st->node = (node + nodes) % nodes;
    point(st);
    st->ix = (int32_t)st->px;
    st->iz = (int32_t)st->pz;
    R.near[i][0] = (float)st->ix;
    R.near[i][1] = (float)st->iz;
    F(c, 0x4b8) = (float)(nodes * (I(c, 0x4a8) + lap)) + ((float)st->node + frac);
    float x = P[0] - (float)st->ix, z = P[2] - (float)st->iz;
    float side = ang_diff(ground_angle(x, z), F(c, 0x4c4)) < 0.0f ? -1.0f : 1.0f;
    F(c, 0x4c0) = sqrtf(z * z + x * x) * side;
    const struct node *nd = L.nd;
    if(st->node > 0) F(c, 0x4bc) = (float)nd[st->node - 1].dist * (1.0f - frac) + (float)nd[st->node].dist * frac;
    else F(c, 0x4bc) = (float)nd[0].dist * frac;
    F(c, 0x4bc) = (float)(nd[nodes - 1].dist * (I(c, 0x4a8) + lap)) + F(c, 0x4bc);
    if(lap) lap_done(i, lap);
}

/* A computer car's driving (0x17726): the throttle (+0x1ec), the front wheels' angle (+0x660) and the handbrake
   (+0x1e8). It aims at the line's point `pace` units ahead of where the road is under it (the first six seconds a long
   way, 22,100; after that 170 x its speed held to 10 .. 130) from a point a step ahead on its way; it lifts when the
   line's speed share says so, avoids a slower car on the way to it by the side that turns it the least, lifts behind
   one, and steers with a gain that depends on which axle drives. */
static void drive(int self)
{
    struct s2car_state *c = &R.car[self];
    struct pstate *st = &R.st[self];
    const float grip = 3.0f - s2car_ground_grip(c);
    const float speed = F(c, 0x500), top = F(c, 0x5c);
    I(c, 0x4c) &= ~0x30;
    if(speed < 30.0f && I(c, 0x58) != 0) { F(c, 0x1ec) = 1.0f; I(c, 0x660) = 0; }
    float pace;
    if(R.clock < 6.0f) pace = 22100.0f;
    else pace = (speed <= 10.0f ? 10.0f : speed < 130.0f ? speed : 130.0f) * 170.0f;
    float lift = (100.0f - (float)L.nd[st->node].share) * 0.01f;       /* (the arcade race: the share is not eased) */
    lift = speed / top >= lift ? 0.0f : 1.0f;
    line_step(st, pace, 1);
    if(!R.running) {                                    /* on the grid: the engine revved now and then, in neutral */
        int k = (int)(((float)I(c, 0) * 0.4f + 3.0f) * R.clock + (float)I(c, 0) * 0.4f);
        F(c, 0x1ec) = (k & 3) ? 0.0f : 1.0f;
        ease(&F(c, 0x1e8), 0.0f, 0x28);
        return;
    }
    const float off = fabsf(ang_diff(F(c, 0x4c4), st->way));
    const float ahead = (speed * (DT * 1.5f)) * 111.0f;
    const float A[3] = { sinf(F(c, 0x508)) * ahead + F(c, 0x4d8), 0.0f, F(c, 0x4e0) - cosf(F(c, 0x508)) * ahead };
    const float B[3] = { (float)st->ix, 0.0f, (float)st->iz };
    const float aim = ang_diff(F(c, 0x4e8), F(c, 0x44) * (F(c, 0x90) > 0.8f ? -1.0f : -2.0f) * DT);
    const float ex = B[0] - A[0], ey = B[1] - A[1], ez = B[2] - A[2];
    const float head = ground_angle(ex, ez);
    int avoided = 0, lifted = 0;
    if(!(off >= 0.9f) && R.clock > 6.0f) {
        float best = 2.0f, reach = 0.0f, D[3] = { 0 }, Q[3] = { 0 };
        int who = 0;
        for(int j = 0; j < R.n; j++) {
            struct s2car_state *o = &R.car[j];
            if(j == self || F(o, 0x500) >= speed) continue;
            const float p[3] = { F(o, 0x4d8), 0.0f, F(o, 0x4e0) };
            const float s = share_along(p, A, B);
            if(!(s >= 0.0f) || s > 1.0f || s >= best) continue;
            const float om = 1.0f - s, r = (float)(I(o, 0x4c8) + I(c, 0x4c8)) * 1.2f;
            float d[3];
            for(int k = 0; k < 3; k++) d[k] = (A[k] * om + B[k] * s) - p[k];
            if(sqrtf(d[1] * d[1] + d[0] * d[0] + d[2] * d[2]) >= r) continue;
            who = j; best = s; reach = r;
            for(int k = 0; k < 3; k++) { D[k] = d[k]; Q[k] = p[k]; }
        }
        if(best < 2.0f) {
            if(R.car[who].ai) I(&R.car[who], 0x4c) |= 0x10;       /* it is told it is being passed */
            I(c, 0x4c) |= 0x20;
            const float lim = (float)(2 * I(c, 0x4d0));
            const float len = sqrtf(ey * ey + ex * ex + ez * ez);
            if(0.0f > best || !(len * best >= lim)) {   /* nearer than twice its length along: taken as at that length */
                best = lim / len;
                const float om = 1.0f - best;
                for(int k = 0; k < 3; k++) Q[k] = (A[k] * om + B[k] * best) - D[k];
            }
            const float dl = sqrtf(D[1] * D[1] + D[0] * D[0] + D[2] * D[2]);
            float n[3] = { 0.0f, 0.0f, 0.0f }, Lv[3], Rv[3];
            if(dl != 0.0f) { const float inv = 1.0f / dl; for(int k = 0; k < 3; k++) n[k] = D[k] * inv; }
            for(int k = 0; k < 3; k++) {
                const float w = n[k] * reach;
                Lv[k] = (Q[k] - w) - A[k];
                Rv[k] = (Q[k] + w) - A[k];
            }
            const float left = ground_angle(Lv[0], Lv[2]), right = ground_angle(Rv[0], Rv[2]);
            if(fabsf(ang_diff(right, head)) > fabsf(ang_diff(left, head))) F(c, 0x660) = ang_diff(left, aim);
            else F(c, 0x660) = ang_diff(right, aim);
            if(fabsf(F(c, 0x660)) > 0.785398185f) {     /* more than an eighth of a turn: at the line instead */
                F(c, 0x660) = ang_diff(head, aim);
                if(best > 0.1f) lifted = 1;
            }
            avoided = 1;
        } else F(c, 0x660) = ang_diff(head, aim);
    } else {
        F(c, 0x660) = ang_diff(head, aim);
        for(int j = 0; j < R.n; j++) {                  /* a car in front within its length x grip and alongside: lift */
            struct s2car_state *o = &R.car[j];
            if(j == self) continue;
            const float along = F(o, 0x4bc) - F(c, 0x4bc);
            if(!((float)I(c, 0x4d0) * grip > along) || 0.0f >= along) continue;
            if(!((float)I(c, 0x4c8) * (grip * 3.0f) > fabsf(F(o, 0x4c0) - F(c, 0x4c0)))) continue;
            if(speed > top * 0.4f) lifted = 1;
        }
    }
    if(!avoided) {
        float by;
        if(F(c, 0x94) > 0.8f) by = 0.0f >= F(c, 0x660) * F(c, 0x4d4) ? 3.0f : 5.0f;
        else by = F(c, 0x90) > 0.8f ? 2.0f : 3.0f;
        F(c, 0x660) = F(c, 0x660) * by;
    }
    if(lifted) I(c, 0x1ec) = 0;
    else {
        const float t = (speed / top) * (F(c, 0x48) * 0.3f);
        F(c, 0x1ec) = 1.0f - (off < 1.0f ? off : 1.0f) * t;
    }
    if(!(F(c, 0x6c) >= fabsf(F(c, 0x660)))) {           /* past the lock: held to it, the throttle cut by the pace */
        F(c, 0x660) = (0.0f > F(c, 0x660) ? -1.0f : 1.0f) * F(c, 0x6c);
        const float q = F(c, 0x48) * 1.5f * speed / top;
        F(c, 0x1ec) = (1.0f - (1.0f >= q ? q : 1.0f)) * F(c, 0x1ec);
    }
    if(F(c, 0x1ec) > 0.7f) F(c, 0x1ec) = 1.0f;
    F(c, 0x1ec) = F(c, 0x1ec) * lift;
    float hand = F(c, 0x1ec) > 0.5f ? 0.0f : 1.0f - F(c, 0x1ec);
    if((I(c, 0x510) & 0x7fffffff) == 0) { I(c, 0x1e4) = 0; hand = 0.0f; }       /* in the air */
    ease(&F(c, 0x1e8), hand, 0x28);
}

/* what s2car_step_driven() calls between the physics and the steering: where the car is along the road, then the driver */
static void driver(struct s2car_state *s, void *ctx)
{
    (void)s;
    const int i = (int)(intptr_t)ctx;
    locate(i);
    drive(i);
}

/* Places and the computers' pace (0x2da06). A car still racing is placed by how many cars are no further along (or
   out); the computers' pace (+0x48, what the engine takes off their speed) is set in the first second of every eight, from
   0 before the sixth second, then from the level's row at 3 + 3 x (how far ahead of the player the car is) / 200,000 */
static void places_and_pace(void)
{
    const int n = R.n;
    for(int i = 0; i < n; i++) {
        struct s2car_state *a = &R.car[i];
        if(I(a, 0x4c) & 2) continue;
        I(a, 0xc30) = 0;
        for(int j = 0; j < n; j++)
            if(!(F(a, 0x4b8) > F(&R.car[j], 0x4b8)) || (I(&R.car[j], 0x4c) & 2)) I(a, 0xc30) += 1;
        for(int j = 0; j < i; j++)
            if(I(a, 0xc30) == I(&R.car[j], 0xc30)) I(a, 0xc30) += 1;
    }
    if(!R.skills_ok || (int)R.clock % 8 != 0) return;
    const float mean = F(&R.car[0], 0x4bc);             /* the player's: the only car with controls */
    for(int i = 1; i < n; i++) {
        struct s2car_state *a = &R.car[i];
        if(R.clock < 6.0f) { F(a, 0x48) = 0.0f; continue; }
        float x = (F(a, 0x4bc) - mean) * 3.0f / 200000.0f + 3.0f;
        x = !(x > 0.01f) ? 0.01f : x >= 5.99f ? 5.99f : x;
        const int k = (int)x;
        const float f = x - (float)k, v = (1.0f - f) * R.skills[LEVEL][k] + R.skills[LEVEL][k + 1] * f;
        F(a, 0x48) = !(0.0f > v) ? v : 0.0f;
    }
}

/* ------------------------------------------------------------------------------------------------ cars touching */

/* Two cars that overlap are pushed apart and swap the speed they close at along the line of least overlap. The game's
   solver (0x245a3) sweeps the cars' outlines (the wheel corners 1.4 times as wide) through the step for the first
   touch and runs the rest of the step again; this is the same outline tested at the end of the step. No damage, sparks
   or sound. */
static void collide(void)
{
    for(int a = 0; a < R.n; a++)
        for(int b = a + 1; b < R.n; b++) {
            struct s2car_state *A = &R.car[a], *B = &R.car[b];
            const float dx = F(B, 0x4d8) - F(A, 0x4d8), dz = F(B, 0x4e0) - F(A, 0x4e0);
            const float reach = (float)(I(A, 0x4d0) + I(B, 0x4d0)) * 0.7f;
            if(dx * dx + dz * dz >= reach * reach * 2.0f || I(A, 0x4dc) - I(B, 0x4dc) > 500 || I(B, 0x4dc) - I(A, 0x4dc) > 500) continue;
            const float ya = F(A, 0x4e8), yb = F(B, 0x4e8);
            const float ax[4][2] = { { sinf(ya), -cosf(ya) }, { cosf(ya), sinf(ya) }, { sinf(yb), -cosf(yb) }, { cosf(yb), sinf(yb) } };
            const float hl[2] = { (float)I(A, 0x4d0) * 0.7f, (float)I(B, 0x4d0) * 0.7f }, hw[2] = { (float)I(A, 0x4c8) * 0.7f, (float)I(B, 0x4c8) * 0.7f };
            float pen = 1e30f, n[2] = { 0.0f, 0.0f };
            int apart = 0;
            for(int k = 0; k < 4 && !apart; k++) {
                const float *u = ax[k];
                const float ea = hl[0] * fabsf(ax[0][0] * u[0] + ax[0][1] * u[1]) + hw[0] * fabsf(ax[1][0] * u[0] + ax[1][1] * u[1]);
                const float eb = hl[1] * fabsf(ax[2][0] * u[0] + ax[2][1] * u[1]) + hw[1] * fabsf(ax[3][0] * u[0] + ax[3][1] * u[1]);
                const float d = dx * u[0] + dz * u[1], p = ea + eb - fabsf(d);
                if(p <= 0.0f) apart = 1;
                else if(p < pen) { pen = p; n[0] = d < 0.0f ? -u[0] : u[0]; n[1] = d < 0.0f ? -u[1] : u[1]; }
            }
            if(apart) continue;
            for(int w = 0; w < 2; w++) {                /* apart, half each: the middle and the corners, x and z */
                struct s2car_state *C = w ? B : A;
                const float sx = (w ? 0.5f : -0.5f) * pen * n[0], sz = (w ? 0.5f : -0.5f) * pen * n[1];
                F(C, 0x4d8) += sx; F(C, 0x4e0) += sz;
                for(int k = 0; k < 4; k++) { F(C, 0x514 + 0xc * k) += sx; F(C, 0x51c + 0xc * k) += sz; }
            }
            float va[2] = { 0, 0 }, vb[2] = { 0, 0 };   /* the mean speed of the four corners */
            for(int k = 0; k < 4; k++) {
                va[0] += F(A, 0x544 + 0xc * k) * 0.25f; va[1] += F(A, 0x54c + 0xc * k) * 0.25f;
                vb[0] += F(B, 0x544 + 0xc * k) * 0.25f; vb[1] += F(B, 0x54c + 0xc * k) * 0.25f;
            }
            const float vn = (va[0] - vb[0]) * n[0] + (va[1] - vb[1]) * n[1];
            if(vn <= 0.0f) continue;                    /* already parting */
            for(int k = 0; k < 4; k++) {
                F(A, 0x544 + 0xc * k) -= vn * n[0]; F(A, 0x54c + 0xc * k) -= vn * n[1];
                F(B, 0x544 + 0xc * k) += vn * n[0]; F(B, 0x54c + 0xc * k) += vn * n[1];
            }
        }
}

/* ------------------------------------------------------------------------------------------------ the race */

/* the race fields of a car's record that a new record must keep */
static const uint16_t kept[] = { 0x4, 0x48, 0x4a8, 0x4ac, 0x4b8, 0x4bc, 0x4c0, 0xc30, 0xc38, 0xc3c, 0xc40 };

static void put_on_line(int i)
{
    struct s2car_state *c = &R.car[i];
    uint32_t keep[sizeof kept / sizeof kept[0]];
    for(unsigned k = 0; k < sizeof kept / sizeof kept[0]; k++) keep[k] = (uint32_t)I(c, kept[k]);
    const int manual = c->manual, hold = c->hold, flags = I(c, 0x4c) & 3;
    if(!s2car_init_at(c, R.team[i], R.model[i], R.track, R.near[i][0], R.near[i][1], c->ai)) return;
    c->manual = manual;
    c->hold = hold;
    I(c, 0x4c) |= flags;
    I(c, 0) = i;
    for(unsigned k = 0; k < sizeof kept / sizeof kept[0]; k++) I(c, kept[k]) = (int32_t)keep[k];
    R.off[i] = R.still[i] = 0;
    respawns++;
}

int race_init(int track, float floor_z)
{
    if(track < 1 || track > 7) return 0;
    R.n = 0;
    R.track = track;
    R.floor_z = floor_z;
    R.running = 0;
    R.clock = 0.0f;
    R.steps = 0;
    respawns = 0;
    memset(R.off, 0, sizeof R.off);
    memset(R.still, 0, sizeof R.still);
    for(int i = 0; i < RACE_CARS; i++) {
        R.team[i] = i;                                  /* the player's team and car, then the next ones in turn */
        R.model[i] = i;
        const int grid = RACE_CARS - 1 - i;             /* the player starts last */
        if(!s2car_init_grid(&R.car[i], R.team[i], R.model[i], track, grid, i > 0)) return 0;
        R.car[i].hold = 1;
    }
    if(!load_line(track)) return 0;
    load_skills();
    for(int i = 0; i < RACE_CARS; i++) {
        struct s2car_state *c = &R.car[i];
        I(c, 0) = i;
        I(c, 0xc40) = (int32_t)0x4560fffcu;             /* no best lap yet (3599.999) */
        I(c, 0xc30) = RACE_CARS - i;
        memset(&R.st[i], 0, sizeof R.st[i]);
        point(&R.st[i]);
        locate(i);                                      /* the place on the line from the start: laps -1 until the line is crossed */
    }
    R.n = RACE_CARS;
    return 1;
}

/* the first half of a step: the race's clock, places and pace, the lights, the player's car */
void race_step_player(const struct s2car_input *in)
{
    R.clock += DT;
    places_and_pace();
    if(!R.running && R.clock > 3.0f) {                  /* the lights are out: the cars are let go */
        R.running = 1;
        for(int i = 0; i < R.n; i++) R.car[i].hold = 0;
    }
    struct s2car_state *c = &R.car[0];
    if(R.running && !(I(c, 0x4c) & 2)) { F(c, 0xc38) += DT; F(c, 0xc3c) += DT; }          /* 0x1f3ce */
    if(!(I(c, 0x4c) & 1)) {
        s2car_step(c, in);
        locate(0);
    } else
        s2car_step_driven(c, driver, 0);                /* he has finished: the game's driver brings the car to a stop */
    R.steps++;
}

/* the second half of `count` steps: the three computer cars (each step's physics and driver), the cars touching, the cars
   off the road. The viewer runs it while the rasterizer finishes the picture: the computers' cars are drawn as the step
   before left them, a 20 ms lag nobody sees, and their physics costs the frame no time where the rasterizer is the bound. */
void race_step_others(uint32_t count)
{
    for(uint32_t step = 0; step < count; step++) {
        for(int i = 1; i < R.n; i++) {
            struct s2car_state *c = &R.car[i];
            if(R.running && !(I(c, 0x4c) & 2)) { F(c, 0xc38) += DT; F(c, 0xc3c) += DT; }
            s2car_step_driven(c, driver, (void *)(intptr_t)i);
        }
        collide();
        for(int i = 0; i < R.n; i++) {                  /* a car off the road for good goes back onto the line */
            struct s2car_state *c = &R.car[i];
            R.off[i] = c->off_track ? R.off[i] + 1 : 0;
            R.still[i] = c->ai && R.running && !(I(c, 0x4c) & 1) && F(c, 0x500) < 3.0f ? R.still[i] + 1 : 0;
            if(R.off[i] >= OFF_STEPS || R.still[i] >= STILL_STEPS || c->pos[2] < R.floor_z) put_on_line(i);
        }
    }
}

void race_step(const struct s2car_input *in)
{
    race_step_player(in);
    race_step_others(1);
}

void race_get_hud(struct race_hud *h)
{
    const struct s2car_state *c = &R.car[0];
    int lap = I(c, 0x4ac) + 1;
    h->count = R.clock < 4.0f ? 3 - (int)R.clock : -1;
    h->laps = RACE_LAPS;
    h->lap = lap > RACE_LAPS ? RACE_LAPS : lap;
    h->place = I(c, 0xc30);
    h->cars = R.n;
    h->finished = (I(c, 0x4c) & 2) != 0;
    h->time = F(c, 0xc38);
    h->lap_time = F(c, 0xc3c);
    h->best_lap = F(c, 0xc40) < 3599.0f ? F(c, 0xc40) : -1.0f;
}
