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
#include "s2ghost.h"
#include "s2params.h"
#include "dat.h"
#include "prof.h"
#include <math.h>
#include <string.h>

#define PI_F        3.14159274f
#define TWO_PI_F    6.28318548f
#define HALF_PI_F   1.57079637f
#define DT          0.02f
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
    float damages[3];                                   /* and its DAMAGES: what a car's touch hurts, by level (pro 5, normal 2.5, rookie 1) */
    int kind, slot;                                     /* the kind of race (race_kind), the side the player took (time attack) */
    int replay, stopped;                                /* the race is the replay of the recorded one (race_replay); it was ended before its end (race_replay_stop) */
    int attract, first;                                 /* the attract race (race_attract): every car a computer's; the first car the computers' steps take: 0 there, else 1 (the player's is car 0), 2 in combat mode */
    int combat;                                         /* combat mode (race_combat): two cars, both with a player's controls */
    const struct s2gh_head *ghost;                      /* time attack: the run being raced, 0 for none */
    uint32_t gsteps;                                    /* the ghost's steps taken: its own count, so that a frame of several steps does not move it */
    float gclock;                                       /* ... and its own clock (the grid's hold ends at 3 s as the player's does) */
    int grunning;
    float cb[5];                                        /* the free run: this car's best lap's splits (+0xc58) */
    struct s2car_state car[RACE_CARS];
    struct pstate st[RACE_CARS];                        /* each car's place on the line (car + 0xc4) */
    float near[RACE_CARS][2];                           /* the nearest point of the line, whole numbers */
    uint32_t off[RACE_CARS], still[RACE_CARS];
    int team[RACE_CARS], model[RACE_CARS];
} R;

struct s2_pick race_grid[RACE_CARS] = { { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 4 } };
int race_replay;
int race_attract;
int race_combat;
static int rec_manual0;                                 /* the gearbox the player's car was set up with in the race being recorded (its first gear depends on it) */
int race_custom;
struct s2car_setup race_setup;
int race_tuned;
static int rec_custom;                                  /* ... and the set-up it was made of: a race is a function of the player's controls AND his set-up */
static struct s2car_setup rec_setup;
int race_laps = RACE_LAPS;
int race_level = 1;

int race_damage_key(void)
{
    return s2car_damages ? 3 - (race_level < 0 ? 0 : race_level > 2 ? 2 : race_level) : 0;
}
int race_kind = RACE_NORMAL;
const struct s2gh_head *race_ghost;
int race_slot(void) { return R.slot; }

/* the free run's lap list and the session's quickest lap (the original keeps them in the data segment, 0x130d1c, 0x130d18, 0x132fc8, 0x133000) */
static const union word NONE = { .u = 0x4560fffcu };    /* 3599.999: no time */
static struct race_free FR;
static int fr_ready;
const struct race_free *race_free_state(void) { return &FR; }
void race_free_reset(void)
{
    for(int k = 0; k < 5; k++) { FR.best[k] = NONE.f; FR.lt[k] = FR.rt[k] = NONE.f; FR.sign[k] = ' '; }
    FR.best_lap = NONE.f;
    FR.list_left = 0.0f;
    FR.msg_left = 0.0f;
    fr_ready = 1;
}

struct s2car_state *race_car(int i) { return &R.car[i]; }
int race_cars(void) { return R.n; }
uint32_t race_steps(void) { return R.steps; }
float race_get_clock(void) { return R.clock; }
int race_line_length(void) { return L.n ? L.nd[L.n - 1].dist : 0; }
int race_line_nodes(void) { return L.n; }
static uint32_t respawns;
uint32_t race_respawns(void) { return respawns; }

static void load_skills(void)
{
    uint32_t size;
    const uint8_t *game = s2car_game_dat(&size);
    struct dat_entry e;
    uint32_t w[21];
    R.skills_ok = 0;
    R.damages[0] = R.damages[1] = R.damages[2] = 1.0f;
    if(!game || !dat_find(game, size, "MISC.DTR", &e)) return;
    uint32_t n = dat_extract(game, size, &e, scratch, sizeof scratch);
    if(!n) return;
    uint32_t d[3];
    if(s2_dtr_words(scratch, n, "DAMAGES", d, 3) == 3) memcpy(R.damages, d, sizeof d);
    if(s2_dtr_words(scratch, n, "SKILLS", w, 21) != 21) return;
    memcpy(R.skills, w, sizeof w);
    R.skills_ok = 1;
}

/* A lap done (0x201ee): a lap not done before counts; the car's best lap; after the last the car has finished (it is
   the driver's, and stops when its time is up), given a fifth of a second for each car behind it */
static void lap_done(int i, int by)
{
    struct s2car_state *c = &R.car[i];
    I(c, 0x4a8) += by;
    if(I(c, 0x4a8) <= I(c, 0x4ac)) return;
    I(c, 0x4ac) = I(c, 0x4a8);
    if(!(F(c, 0xc3c) >= F(c, 0xc40))) F(c, 0xc40) = F(c, 0xc3c);
    if(R.kind == RACE_FREE && i == 0) {                 /* the free run (0x201ee): the session's quickest lap, its splits, the list stays for 4 s */
        if(F(c, 0xc40) < FR.best_lap) FR.best_lap = F(c, 0xc40);
        if(F(c, 0xc54) < R.cb[4]) {
            for(int k = 0; k < 5; k++) R.cb[k] = F(c, 0xc44 + 4 * k);
            if(F(c, 0xc54) < FR.best[4])
                for(int k = 0; k < 5; k++) FR.best[k] = F(c, 0xc44 + 4 * k);
        }
        FR.list_left = 4.0f;
    }
    if(I(c, 0x4a8) == race_laps && R.kind != RACE_FREE && !R.attract) {       /* (the free run and the attract race do not end on their laps) */
        I(c, 0x4c) |= 3;
        F(c, 4) = (float)(R.n - I(c, 0xc30)) * 0.2f;
    }
    F(c, 0xc3c) = 0.0f;
}

/* A marker (a fifth of a lap) passed, by checkpoints (0x1ffcb): the free run's list. The difference against the quickest lap's split is taken against what the
   row held (the running time the list shows in the row of the next marker, kept by free_frame() every step), the split itself is the lap's time now. */
static void free_marker(struct s2car_state *c, int by)
{
    I(c, 0x4b0) += by;
    if(!(I(c, 0x4b0) > I(c, 0x4b4))) return;
    I(c, 0x4b4) = I(c, 0x4b0);
    const int idx = (I(c, 0x4b0) - by) % 5;
    if(idx < 0 || idx > 4) return;
    if(FR.best[idx] != NONE.f) {
        const float d = F(c, 0xc44 + 4 * idx) - FR.best[idx];
        FR.rt[idx] = fabsf(FR.best[idx] - F(c, 0xc44 + 4 * idx));
        FR.sign[idx] = 0.0f <= d ? '+' : '-';
        FR.msg_sign = FR.sign[idx];
        FR.msg_val = FR.rt[idx];
        FR.msg_left = 4.0f;
        if(idx + 1 < 5) {                               /* the next row shows the quickest lap's own time there */
            FR.sign[idx + 1] = ' ';
            FR.rt[idx + 1] = FR.best[idx + 1];
        }
    }
    F(c, 0xc44 + 4 * idx) = F(c, 0xc3c);
    FR.lt[idx] = F(c, 0xc3c);
}

/* Once a step in the free run (the original's per-picture part of the race screen, 0x4612d): while a lap's list is held it counts down, and when it is over the
   columns clear (the first row on the right keeps the quickest lap's first split); otherwise the row of the next marker shows the running time */
static void free_frame(void)
{
    struct s2car_state *c = &R.car[0];
    FR.msg_left = FR.msg_left > DT ? FR.msg_left - DT : 0.0f;
    if(FR.list_left > 0.0f) {
        FR.list_left -= DT;
        if(FR.list_left >= 0.0f) return;
        for(int k = 0; k < 5; k++) { FR.lt[k] = FR.rt[k] = NONE.f; FR.sign[k] = ' '; }
        FR.rt[0] = FR.best[0];
    }
    const int idx = I(c, 0x4b4) % 5;
    if(idx < 0 || idx > 4) return;
    F(c, 0xc44 + 4 * idx) = F(c, 0xc3c);
    FR.lt[idx] = F(c, 0xc3c);
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
    if(R.kind == RACE_FREE && i == 0) {                 /* a marker passed (0x16789: the progress times 5 over the line's nodes) */
        const int chk = (int)(F(c, 0x4b8) * 5.0f / (float)nodes);
        if(chk != I(c, 0x4b0)) free_marker(c, chk - I(c, 0x4b0));
    }
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
            if(!(s >= -0.2f) || s > 1.0f || s >= best) continue;       /* (a car up to a fifth of the way back from where it aims from counts too) */
            const float om = 1.0f - s, r = (float)((I(o, 0x4c8) + I(c, 0x4c8)) * 12) / 10.0f;
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
    if(R.kind != RACE_NORMAL || !R.skills_ok || (int)R.clock % 8 != 0) return;      /* (time attack has no computers, the ghost is no computer) */
    if(R.attract) {                                     /* no car has controls to pace the others by (0x2db69): every car gets the level's middle value (0x2dd26) */
        for(int i = 0; i < n; i++) F(&R.car[i], 0x48) = R.skills[race_level][3];
        return;
    }
    const float mean = F(&R.car[0], 0x4bc);             /* the player's: the only car with controls (combat mode has no computers: R.first is n) */
    for(int i = R.first; i < n; i++) {
        struct s2car_state *a = &R.car[i];
        if(R.clock < 6.0f) { F(a, 0x48) = 0.0f; continue; }
        float x = (F(a, 0x4bc) - mean) * 3.0f / 200000.0f + 3.0f;
        x = !(x > 0.01f) ? 0.01f : x >= 5.99f ? 5.99f : x;
        const int k = (int)x;
        const float f = x - (float)k, v = (1.0f - f) * R.skills[race_level][k] + R.skills[race_level][k + 1] * f;
        F(a, 0x48) = !(0.0f > v) ? v : 0.0f;
    }
}

void (*race_step_hook)(struct s2car_state *cars, int n, int attack);

/* ------------------------------------------------------------------------------------------------ cars touching */

static float knock_max;                                 /* the hardest touch of the player's car since race_knock_take() */

/* The game's solver of touching cars, in its place in the frame (0x2baa4): after every car's passes over tyres, walls and ground (s2car_move),
   0x245a3 sweeps the cars' outlines through the step (the wheel corners 1.4 times as far from their mean, last step's against this step's, each corner of one
   against each edge of the other: 0x2348b) for the first touch of the step. If there is one (a share of the step below 2), the two cars are flagged (+0x4c
   bit 8), put back to where they were at that share of the step, knocked (0x19aaa), and the touch gives each car's four corners a push: the corner's
   speed against the edge's at the point, along the line from the car's middle to the point in full and across it weighted by the corner's angle from that
   line. The rest of the step is then run for the flagged cars alone with what is left of its length (not at all when that is 0.005 s or less), after the
   walls and ground of every car have been looked at again (0x16824), and 0x22d35 pushes apart the cars that still overlap (0x20a18). What the cars do with
   the step's controls (s2car_finish) comes after all of that. The game does this for every car; nothing here is for the computers alone. */

/* a car's outline in the solver's terms: the four wheel corners 1.4 times as far from their mean, last step's (+0x770) and this step's (+0x514) */
struct outline {
    float old[4][3], now[4][3];
    float cx, cz, r;                                    /* a circle round all eight points: two cars whose circles are apart cannot touch in this step */
};

/* the mean of the four corners at `base`, summed as the solver sums them: corners 0, 2, 1, 3 */
static void corner_mean(const struct s2car_state *c, int base, float m[3])
{
    for(int k = 0; k < 3; k++) {
        const int p = base + 4 * k;
        m[k] = (((F(c, p) + F(c, p + 0x18)) + F(c, p + 0xc)) + F(c, p + 0x24)) * 0.25f;
    }
}

static void make_outline(const struct s2car_state *c, struct outline *o)
{
    float mo[3], mn[3];
    corner_mean(c, 0x770, mo);
    corner_mean(c, 0x514, mn);
    float sx = 0.0f, sz = 0.0f;
    for(int i = 0; i < 4; i++)
        for(int k = 0; k < 3; k++) {
            o->old[i][k] = (F(c, 0x770 + 0xc * i + 4 * k) - mo[k]) * 1.4f + mo[k];
            o->now[i][k] = (F(c, 0x514 + 0xc * i + 4 * k) - mn[k]) * 1.4f + mn[k];
        }
    for(int i = 0; i < 4; i++) { sx += o->old[i][0] + o->now[i][0]; sz += o->old[i][2] + o->now[i][2]; }
    o->cx = sx * 0.125f;
    o->cz = sz * 0.125f;
    float r2 = 0.0f;
    for(int i = 0; i < 4; i++) {
        const float *p[2] = { o->old[i], o->now[i] };
        for(int q = 0; q < 2; q++) {
            const float dx = p[q][0] - o->cx, dz = p[q][2] - o->cz, d2 = dx * dx + dz * dz;
            if(d2 > r2) r2 = d2;
        }
    }
    o->r = sqrtf(r2) + 1.0f;
}

static const uint8_t edge_a[4] = { 3, 2, 0, 1 }, edge_b[4] = { 2, 0, 1, 3 };        /* the outline's four edges as corner pairs (the table at 0x112e64) */

/* Where a step crosses a gate (0x2348b): the corner moves p1 to p2; the edge runs from p3 to p5 at the start of the step and from p4 to p6 at its end.
   The edge's line is its middle (the mean of the four points) and its two ends' mean direction, and it moves as the ends do: by the longer of their two
   moves along the mean of the two. Gives the share of the step at the crossing, or 2 when the step does not cross the edge. */
static float gate(const float p1[3], const float p2[3], const float p3[3], const float p4[3], const float p5[3], const float p6[3])
{
    float A[3], D1[3], D2[3], M[3], E[3], Fv[3], G[3], N[3], P[3], Q[3], S[3], Rv[3];
    for(int k = 0; k < 3; k++) {
        A[k] = p2[k] - p1[k];
        D1[k] = p6[k] - p4[k];
        D2[k] = p5[k] - p3[k];
        M[k] = (D2[k] + D1[k]) * 0.5f;
        E[k] = p4[k] - p3[k];
        Fv[k] = p6[k] - p5[k];
        G[k] = E[k] + Fv[k];
    }
    const float le = sqrtf(E[1] * E[1] + E[0] * E[0] + E[2] * E[2]);
    const float lf = sqrtf(Fv[1] * Fv[1] + Fv[0] * Fv[0] + Fv[2] * Fv[2]);
    const float len = lf >= le ? lf : le;
    const float lg = sqrtf(G[1] * G[1] + G[0] * G[0] + G[2] * G[2]);
    if(lg == 0.0f) N[0] = N[1] = N[2] = 0.0f;
    else { const float inv = 1.0f / lg; for(int k = 0; k < 3; k++) N[k] = G[k] * inv; }
    for(int k = 0; k < 3; k++) {
        P[k] = N[k] * len;
        Q[k] = (M[k] + P[k]) * 0.5f;
        S[k] = (((p3[k] + p5[k]) + p4[k]) + p6[k]) * 0.25f;
        Rv[k] = S[k] - Q[k];
    }
    const float x1 = p1[0], z1 = p1[2];
    const float den = (A[2] - P[2]) * M[0] + (P[0] - A[0]) * M[2];
    if(den == 0.0f) return 2.0f;                        /* (the game's other way round is this one with the sign turned: also nought) */
    const float u = ((Rv[2] - z1) * M[0] + (x1 - Rv[0]) * M[2]) / den;
    if(!(u >= 0.0f) || u > 1.0f) return 2.0f;
    float v;
    if(M[0] != 0.0f) v = (((A[0] - P[0]) * u + x1) - Rv[0]) / M[0];
    else if(M[2] != 0.0f) v = (((A[2] - P[2]) * u + z1) - Rv[2]) / M[2];
    else return 2.0f;
    if(!(v >= 0.0f) || v > 1.0f) return 2.0f;
    return u;
}

/* a car put back to where it was at the share `best` of the step (0x245a3): place, turns, corners, wheels' places, springs */
static void put_back(struct s2car_state *c, float best)
{
    const float back = 1.0f - best;
    F(c, 0x4d8) = s2car_placed(F(c, 0x734) * back + F(c, 0x4d8) * best);
    F(c, 0x4e0) = s2car_placed(F(c, 0x73c) * back + F(c, 0x4e0) * best);
    I(c, 0x4dc) = (int32_t)((float)I(c, 0x738) * back + (float)I(c, 0x4dc) * best);
    static const uint16_t turn[3] = { 0x4e4, 0x4ec, 0x4e8 };
    for(int k = 0; k < 3; k++) F(c, turn[k]) = ang_diff(F(c, turn[k]) * best, -F(c, turn[k] + 0x25c) * back);
    for(int k = 0; k < 4; k++) {
        const int now = 0x514 + 0xc * k, was = 0x770 + 0xc * k, w = 0x5a0 + 0x70 * k;
        for(int q = 0; q < 3; q++) {
            const float o = F(c, was + 4 * q) * back, m = F(c, now + 4 * q) * best;
            F(c, now + 4 * q) = m + o;
        }
        static const uint8_t xyz[3] = { 0, 8, 4 };
        for(int q = 0; q < 3; q++) I(c, w + xyz[q]) = (int32_t)((float)I(c, w + 0x25c + xyz[q]) * back + (float)I(c, w + xyz[q]) * best);
        F(c, 0x588 + 0x70 * k) = back * F(c, 0x7e4 + 0x70 * k) + F(c, 0x588 + 0x70 * k) * best;
    }
}

/* the push a touch gives the corners of one car (0x245a3's end): `r` is the speed the corner meets the edge at, turned for this car's side */
static void push_corners(struct s2car_state *c, const float point[3], const float r[3])
{
    float centre[3];
    corner_mean(c, 0x514, centre);
    const float vx = point[0] - centre[0], vy = point[1] - centre[1], vz = point[2] - centre[2];
    const float len = sqrtf(vy * vy + vx * vx + vz * vz);
    float u[3] = { 0.0f, 0.0f, 0.0f };
    if(len != 0.0f) { const float inv = 1.0f / len; u[0] = vx * inv; u[1] = vy * inv; u[2] = vz * inv; }
    const float facing = ground_angle(u[0], u[2]);
    const float dot = r[1] * u[1] + r[0] * u[0] + r[2] * u[2];
    float along[3], across[3];
    for(int k = 0; k < 3; k++) along[k] = u[k] * dot;
    for(int k = 0; k < 3; k++) across[k] = r[k] - along[k];
    for(int k = 0; k < 4; k++) {
        const int corner = 0x514 + 0xc * k, push = 0x544 + 0xc * k;
        const float cx = F(c, corner) - centre[0], cz = F(c, corner + 8) - centre[2];
        const int32_t keep = I(c, push + 4);
        const float ang = ground_angle(cx, cz);
        const float w = (1.0f - fabsf(ang_diff(ang, facing)) / PI_F) * 0.3f;
        const float rot = ang - facing;
        const float sn = sinf(rot), cs = cosf(rot);
        const float px = across[0] * cs - across[2] * sn, pz = across[2] * cs + across[0] * sn;
        const float add[3] = { along[0] + px * w, along[1] + across[1] * w, along[2] + pz * w };
        for(int q = 0; q < 3; q++) F(c, push + 4 * q) = add[q] + F(c, push + 4 * q);
        I(c, push + 4) = keep;
    }
}

/* the middles of two cars are nearer than `reach`, in all three ways (0x245a3's measure) or, with `game_push`, in the way 0x22d35 measures: along the ground in x and in
   height, not in z (which would be the ground plane) -- and also in the ground plane, where the pair could not overlap otherwise */
static int closer(const struct s2car_state *A, const struct s2car_state *B, float reach, int game_push)
{
    const float dx = F(B, 0x4d8) - F(A, 0x4d8), dz = F(B, 0x4e0) - F(A, 0x4e0), dh = (float)(I(B, 0x4dc) - I(A, 0x4dc));
    if(game_push) return dx * dx + dh * dh < reach * reach && dx * dx + dz * dz < reach * reach;
    return dx * dx + dh * dh + dz * dz < reach * reach;
}

/* 0x245a3, the sweep and, for the first touch, everything the game does at once; gives the share of the step at which it was, 2 for none. Only
   the pairs of cars nearer than four of their lengths are swept (the game's own reach: a step is at most 280 units, a car 1,165 long with its outline). */
static float touch_solver(void)
{
    struct outline ol[RACE_CARS];
    const int n = R.n;
    unsigned mask = 0;
    for(int a = 0; a < n; a++)
        for(int b = a + 1; b < n; b++)
            if(closer(&R.car[a], &R.car[b], (float)(I(&R.car[a], 0x4d0) + I(&R.car[b], 0x4d0)) * 2.0f, 0)) mask |= (1u << a) | (1u << b);
    if(!mask) return 2.0f;
    for(int i = 0; i < n; i++) if(mask & (1u << i)) make_outline(&R.car[i], &ol[i]);
    float best = 2.0f;
    int ca = 0, cb = 0, corner = 0, edge = 0;
    for(int a = 0; a < n; a++)
        for(int b = 0; b < n; b++) {
            if(a == b || !(mask & (1u << a)) || !(mask & (1u << b))) continue;
            const float dx = ol[b].cx - ol[a].cx, dz = ol[b].cz - ol[a].cz, reach = ol[a].r + ol[b].r;
            if(dx * dx + dz * dz >= reach * reach) continue;     /* (outlines whose circles are apart cannot touch in this step) */
            for(int c = 0; c < 4; c++)
                for(int e = 0; e < 4; e++) {
                    const float t = gate(ol[a].old[c], ol[a].now[c], ol[b].old[edge_a[e]], ol[b].now[edge_a[e]], ol[b].old[edge_b[e]], ol[b].now[edge_b[e]]);
                    if(!(t >= best)) { ca = a; cb = b; corner = c; edge = e; best = t; }
                }
        }
    if(!(best < 2.0f)) return 2.0f;
    struct s2car_state *A = &R.car[ca], *B = &R.car[cb];
    I(A, 0x4c) |= 8; F(A, 0x30) = 1.0f;
    I(B, 0x4c) |= 8; F(B, 0x30) = 1.0f;
    for(int i = 0; i < n; i++) if(I(&R.car[i], 0x4c) & 8) put_back(&R.car[i], best);
    const int ei = edge_a[edge], ej = edge_b[edge];
    float point[3], d0, d1;
    for(int k = 0; k < 3; k++) point[k] = F(A, 0x514 + 0xc * corner + 4 * k);
    {
        float x[3], y[3];
        for(int k = 0; k < 3; k++) { x[k] = F(B, 0x514 + 0xc * ei + 4 * k) - point[k]; y[k] = F(B, 0x514 + 0xc * ej + 4 * k) - point[k]; }
        d0 = sqrtf(x[1] * x[1] + x[0] * x[0] + x[2] * x[2]);
        d1 = sqrtf(y[1] * y[1] + y[0] * y[0] + y[2] * y[2]);
    }
    const float s = d0 / (d0 + d1), om = 1.0f - s;
    float rel[3];
    for(int k = 0; k < 3; k++) {
        const float bj = F(B, 0x544 + 0xc * ej + 4 * k) * s, bi = F(B, 0x544 + 0xc * ei + 4 * k) * om;
        rel[k] = (F(A, 0x544 + 0xc * corner + 4 * k) - bi) - bj;
    }
    const float knock = sqrtf(rel[1] * rel[1] + rel[0] * rel[0] + rel[2] * rel[2]);
    if(s2car_sparks) {                                  /* 0x2aeb9, the cars' thirty: at the touch's point, thrown along the mean of the two points' pushes */
        float mid[3];
        const float si = d1 / (d0 + d1);
        for(int k = 0; k < 3; k++) {
            const float bj = F(B, 0x544 + 0xc * ej + 4 * k) * s, bi = F(B, 0x544 + 0xc * ei + 4 * k) * si;
            mid[k] = ((F(A, 0x544 + 0xc * corner + 4 * k) + bi) + bj) * 0.5f;
        }
        s2car_sparks(point, mid, knock, 1);
    }
    if((ca == 0 || cb == 0) && knock > knock_max) knock_max = knock;     /* heard (s2sound.c) */
    s2car_knock(A, corner == 1 || corner == 3 ? 3 : 1, corner, 1.0f, knock);
    s2car_knock(B, edge, s >= 0.5f ? ej : ei, fabsf(s + -0.5f) * 2.0f, knock);
    const float ra[3] = { -rel[0], -rel[1], -rel[2] };
    push_corners(A, point, ra);
    push_corners(B, point, rel);
    return best;
}

/* Two cars pushed apart (0x20a18): a corner of one inside the other's outline (the corners 1.4 times as far from their mean) is taken back along the line
   between the two middles to that outline's edge; the deepest of these moves one car back and the other on by that share of the line, the corners' places
   (+0x514 and +0x770) in x and z. Two cars both under speed 5 and not already turned (+0x58) that are nearer each other nose to tail than three of the
   first one's width are turned away from each other (+0x54, +0x58). */
static void push_apart(struct s2car_state *A, struct s2car_state *B)
{
    float ca[3], cb[3], dir[3], C[4][2][3];
    for(int k = 0; k < 3; k++) {
        const int pa = 0x514 + 4 * k;
        ca[k] = (((F(A, pa + 0x18) + F(A, pa)) + F(A, pa + 0x24)) + F(A, pa + 0xc)) * 0.25f;
        cb[k] = (((F(B, pa + 0x18) + F(B, pa)) + F(B, pa + 0x24)) + F(B, pa + 0xc)) * 0.25f;
        dir[k] = cb[k] - ca[k];
    }
    for(int c = 0; c < 4; c++)
        for(int k = 0; k < 3; k++) {
            C[c][0][k] = (F(A, 0x514 + 0xc * c + 4 * k) - ca[k]) * 1.4f + ca[k];
            C[c][1][k] = (F(B, 0x514 + 0xc * c + 4 * k) - cb[k]) * 1.4f + cb[k];
        }
    const float sn = sinf(HALF_PI_F), cs = cosf(HALF_PI_F);
    float deepest = 0.0f, T = 0.0f, U = 0.0f;
    for(int pass = 0; pass < 2; pass++) {
        const int mine = pass, theirs = !pass;          /* A's corners in B's outline, then B's in A's */
        for(int c = 0; c < 4; c++) {
            const float *P = C[c][mine];
            int inside = 1;
            float first = 0.0f;
            for(int e = 0; e < 4; e++) {
                const float *Q = C[edge_a[e]][theirs], *Rp = C[edge_b[e]][theirs];
                const float ex = Rp[0] - Q[0], ey = Rp[1] - Q[1], ez = Rp[2] - Q[2];
                const float nx = ex * cs - ez * sn, nz = ez * cs + ex * sn;
                const float vx = P[0] - Q[0], vy = P[1] - Q[1], vz = P[2] - Q[2];
                const float side = vy * ey + vx * nx + vz * nz;
                if(e == 0) first = side;
                else if(0.0f > side * first) inside = 0;
            }
            if(!inside) continue;
            for(int e = 0; e < 4; e++) {
                const float *Q = C[edge_a[e]][theirs], *Rp = C[edge_b[e]][theirs];
                const float E[3] = { Rp[0] - Q[0], Rp[1] - Q[1], Rp[2] - Q[2] };
                s2car_lines_meet(P, dir, Q, E, &T, &U);
                if(!pass) T = -T;
                if(!(U >= 0.0f) || U > 1.0f) continue;
                if(T > deepest) deepest = T;
            }
        }
    }
    if(!(deepest > 0.0f)) return;
    const float px = dir[0] * deepest, pz = dir[2] * deepest;
    for(int c = 0; c < 4; c++) {
        const int at = 0x514 + 0xc * c, was = 0x770 + 0xc * c;
        F(A, at) -= px; F(A, at + 8) -= pz;
        F(A, was) -= px; F(A, was + 8) -= pz;
        F(B, at) += px; F(B, at + 8) += pz;
        F(B, was) += px; F(B, was + 8) += pz;
    }
    if(F(A, 0x500) < 5.0f && F(B, 0x500) < 5.0f && I(A, 0x58) == 0 && I(B, 0x58) == 0) {
        const float x1 = F(A, 0x52c) - F(B, 0x514), z1 = F(A, 0x534) - F(B, 0x51c);
        const float x2 = F(A, 0x514) - F(B, 0x52c), z2 = F(A, 0x51c) - F(B, 0x534);
        const float l1 = sqrtf(0.0f * 0.0f + x1 * x1 + z1 * z1), l2 = sqrtf(0.0f * 0.0f + x2 * x2 + z2 * z2);
        if((float)(I(A, 0x4c8) * 3) > l2 + l1) {
            struct s2car_state *both[2] = { A, B };
            for(int i = 0; i < 2; i++) {
                F(both[i], 0x54) = ang_diff(F(both[i], 0x4e8), 0.628318548f);
                I(both[i], 0x58) = 0.0f > ang_diff(F(both[i], 0x54), F(both[i], 0x4e8)) ? -1 : 1;
            }
        }
    }
}

/* Cars touching, in the game's order (0x2baa4 after every car's passes up to the step on the ground): the solver and, if it found a touch, the walls and ground
   of every car again and the rest of the step for the two cars; then every pair that still overlaps pushed apart (0x22d35). In pieces, one a call, the way the
   viewer wants its slices (a touch costs about as much as three cars' steps, which are the biggest piece it fits into the gaps of the drawing): each call does the next
   piece and says whether another follows. A car that is neither one of the two nor against a wall has nothing for the walls and ground to find the second time, they are not run
   for it. */
static struct { int phase, car; float rest; } touching;

static int collide_slice(void)
{
    if(touching.phase == 0) {                           /* the solver */
        const float best = touch_solver();
        touching.rest = (1.0f - best) * DT;
        touching.car = 0;
        if(best < 2.0f && touching.rest > 0.005f) {
            touching.phase = 1;
            return 1;
        }
        touching.phase = 3;                             /* (no touch, or one with nothing left of the step to run: on to the pairs) */
    }
    if(touching.phase == 1) {                           /* the walls and ground of the cars again (0x16824) */
        for(int i = 0; i < R.n; i++) if((I(&R.car[i], 0x4c) & 8) || I(&R.car[i], 0x150)) s2car_walls(&R.car[i]);
        touching.phase = 2;
        return 1;
    }
    if(touching.phase == 2) {                           /* the rest of the step, a car a call */
        while(touching.car < R.n && !(I(&R.car[touching.car], 0x4c) & 8)) touching.car++;
        if(touching.car < R.n) {
            s2car_move_again(&R.car[touching.car++], touching.rest);
            return 1;
        }
        touching.phase = 3;
    }
    for(int a = 0; a < R.n; a++)                        /* the pairs that overlap */
        for(int b = a + 1; b < R.n; b++) {
            struct s2car_state *A = &R.car[a], *B = &R.car[b];
            if(closer(A, B, (float)(I(A, 0x4d0) + I(B, 0x4d0)), 1)) push_apart(A, B);
        }
    touching.phase = 0;
    return 0;
}

/* ------------------------------------------------------------------------------------------------ the race */

/* the race fields of a car's record that a new record must keep */
static const uint16_t kept[] = { 0x4, 0x48, 0x4a8, 0x4ac, 0x4b8, 0x4bc, 0x4c0, 0xc30, 0xc38, 0xc3c, 0xc40 };

static void put_on_line(int i)
{
    struct s2car_state *c = &R.car[i];
    uint32_t keep[sizeof kept / sizeof kept[0]];
    for(unsigned k = 0; k < sizeof kept / sizeof kept[0]; k++) keep[k] = (uint32_t)I(c, kept[k]);
    const int manual = c->manual, stick = c->stick, hold = c->hold, flags = I(c, 0x4c) & 3;
    if(!s2car_init_at(c, R.team[i], R.model[i], R.track, R.near[i][0], R.near[i][1], c->ai)) return;
    c->manual = manual;
    c->stick = stick;
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
    if(race_level < 0 || race_level > 2) race_level = 1;
    if(race_laps < 1) race_laps = RACE_LAPS;
    R.n = 0;
    R.track = track;
    R.floor_z = floor_z;
    R.running = 0;
    R.clock = 0.0f;
    R.steps = 0;
    R.kind = race_kind;
    R.replay = race_replay && R.kind == RACE_NORMAL && !race_combat && s2gh_rec_steps() > 0 && !s2gh_rec_full();     /* (the recording of the race just run is the one to run again) */
    race_replay = 0;
    R.attract = race_attract && R.kind == RACE_NORMAL && !R.replay;
    R.combat = race_combat && R.kind == RACE_NORMAL && !R.attract;
    R.first = R.attract ? 0 : R.combat ? 2 : 1;
    R.stopped = 0;
    R.ghost = race_kind == RACE_ATTACK ? race_ghost : 0;
    R.gsteps = 0;
    R.gclock = 0.0f;
    R.grunning = 0;
    respawns = 0;
    touching.phase = 0;
    memset(R.off, 0, sizeof R.off);
    memset(R.still, 0, sizeof R.still);
    /* four cars on the grid (combat mode, 0x34d4a: the two players' on the first row), or in time attack the player's and the ghost's side by side, or the free run's one */
    const int ncars = R.kind == RACE_NORMAL ? (R.combat ? 2 : RACE_CARS) : R.ghost ? 2 : 1;
    R.slot = R.kind == RACE_ATTACK ? (R.ghost ? 1 - R.ghost->slot : 1) : 0;     /* (0x339e7: car i of n takes place n - 1 - i; the player the other side than the kept run) */
    if(R.replay) R.car[0].manual = rec_manual0;
    else if(R.kind == RACE_NORMAL && !R.attract && !R.combat) rec_manual0 = R.car[0].manual;
    const int custom = R.replay ? rec_custom : race_custom;                     /* the player's set-up (a replay: the one the recorded race was made with) */
    const struct s2car_setup setup = R.replay ? rec_setup : race_setup;
    if(!R.replay && R.kind == RACE_NORMAL && !R.attract && !R.combat) rec_custom = race_custom, rec_setup = race_setup;
    for(int i = 0; i < ncars; i++) {
        struct s2car_state *c = &R.car[i];
        const int ghost = i == 1 && R.kind == RACE_ATTACK && R.ghost && R.ghost->custom;
        c->custom = i == 0 ? custom : ghost;                                    /* the player's, the ghost's (the kept run's), the computers' standard one (`tuned` makes it over) */
        c->setup = i == 0 ? setup : ghost ? R.ghost->setup : (struct s2car_setup){ 0 };
        c->tuned = R.kind == RACE_NORMAL && (i > 0 || R.attract) && race_tuned;    /* (the attract race has no player: car 0 is a computer's too) */
        R.team[i] = race_grid[i].team - 1;              /* the player's team and car, then the computers' (the physics counts from 0) */
        R.model[i] = race_grid[i].car - 1;
        /* the controls a car is made for (s2car.h `stick`): the player's as the viewer set them before this (a replay: the recorded race's), the second player's in combat mode
           likewise, the ghost's the kept run's, every other car's the keys' (a computer's car has none) */
        if(i == 0 && R.replay) c->stick = s2gh_rec_stick();
        else if(i == 1 && R.kind == RACE_ATTACK && R.ghost) c->stick = s2gh_stick(R.ghost);
        else if(R.attract || i >= (R.combat ? 2 : 1)) c->stick = 0;
        if(R.kind != RACE_NORMAL || i >= R.first || R.attract) R.car[i].manual = 0;     /* (the first gear of a car depends on it: the ghost must start as the car it was recorded from, and a
                                                                       computer's car has the automatic gearbox, not what the ghost of an earlier race left in the record;
                                                                       the player's in the arcade is the viewer's, set before this) */
        if(R.kind == RACE_NORMAL) {
            const int grid = ncars - 1 - i;             /* the player starts last (combat mode: the first player behind the second) */
            if(!s2car_init_grid(&R.car[i], R.team[i], R.model[i], track, grid, i >= R.first || R.attract)) return 0;
        } else if(!s2car_init_row(&R.car[i], R.team[i], R.model[i], track, i == 0 ? R.slot : R.ghost->slot, 0)) {
            return 0;
        }
        R.car[i].hold = 1;
    }
    if(!load_line(track)) return 0;
    load_skills();
    s2car_damage_scale = R.damages[race_level];         /* every knock of the race goes by the level's row (0x19b7b), the player's walls included */
    if(R.kind == RACE_FREE && !fr_ready) race_free_reset();
    for(int i = 0; i < ncars; i++) {
        struct s2car_state *c = &R.car[i];
        I(c, 0) = i;
        I(c, 0xc40) = (int32_t)0x4560fffcu;             /* no best lap yet (3599.999) */
        I(c, 0xc30) = ncars - i;
        for(int k = 0; k < 5; k++) F(c, 0xc44 + 4 * k) = NONE.f;                    /* the splits of the lap (0x2f519) */
        memset(&R.st[i], 0, sizeof R.st[i]);
        point(&R.st[i]);
        locate(i);                                      /* the place on the line from the start: laps -1 until the line is crossed */
    }
    R.n = ncars;
    if(R.kind == RACE_FREE) {                           /* the list starts clear, the first row on the right showing the quickest lap's first split (0x2e6dc ..) */
        for(int k = 0; k < 5; k++) { FR.lt[k] = FR.rt[k] = NONE.f; FR.sign[k] = ' '; R.cb[k] = FR.best[k]; }
        FR.rt[0] = FR.best[0];
        FR.list_left = 0.0f;
        FR.msg_left = 0.0f;
    }
    if(R.kind == RACE_ATTACK || (R.kind == RACE_NORMAL && !R.replay && !R.attract && !R.combat)) {
        s2gh_rec_begin(track, race_grid[0].team, race_grid[0].car, R.slot, race_damage_key(), custom, &setup);
        s2gh_rec_mode(R.car[0].stick);
    }
    return 1;
}

/* One step of a car with a human's controls: the player's, and in time attack the ghost's. Its clocks (0x1f3ce), the physics, where it is on the line; he has
   finished: the game's driver brings the car to a stop. Where the race has no computers (time attack, the free run) the check that puts a car that has left the
   road for good back on the line is made here, every step, and not after the frame's steps as race_others_slice() makes it for the arcade's cars: a run
   then does not depend on how many steps a frame took, which the ghost needs. */
static void human_step(int i, const struct s2car_input *in, int running)
{
    PF_VAR(pf);
    struct s2car_state *c = &R.car[i];
    if(running && !(I(c, 0x4c) & 2)) { F(c, 0xc38) += DT; F(c, 0xc3c) += DT; }
    if(!(I(c, 0x4c) & 1)) {
        PF_SET(pf);
        s2car_step(c, in);
        PF_ADD(PF_PLAYER_STEP, pf); PF_SET(pf);
        locate(i);
        PF_ADD(PF_LINE, pf);
    } else
        s2car_step_driven(c, driver, (void *)(intptr_t)i);
    if(R.kind != RACE_NORMAL) {
        R.off[i] = c->off_track ? R.off[i] + 1 : 0;
        if(R.off[i] >= OFF_STEPS || c->pos[2] < R.floor_z) put_on_line(i);
    }
}

/* the first half of a step: the race's clock, places and pace, the lights, the player's car (recorded, in time attack) */
void race_step_player(const struct s2car_input *in)
{
    PF_VAR(pf);
    if(R.stopped) return;                               /* (a replay ended by the player: the race's frames are no longer run) */
    struct s2car_input replayed;
    if(R.replay) {                                      /* the recorded race's controls, by the step: what the player does now is not asked */
        int manual = R.car[0].manual;
        replayed = R.steps < s2gh_rec_steps() ? s2gh_rec_input(R.steps, &manual) : (struct s2car_input){ 0 };
        R.car[0].manual = manual;
        in = &replayed;
    } else if(R.car[0].stick && !R.attract) {          /* a stick's controls in the steps the recording keeps (s2ghost.h): the race is the same race when it is run again */
        replayed = s2gh_round(in);
        in = &replayed;
    }
    R.clock += DT;
    places_and_pace();
    PF_ADD(PF_PLACES, pf);
    if(!R.running && R.clock > 3.0f) {                  /* the lights are out: the cars are let go */
        R.running = 1;
        for(int i = 0; i < R.n; i++) if(R.kind == RACE_NORMAL || i == 0) R.car[i].hold = 0;
    }
    struct s2car_state *c = &R.car[0];
    if((R.kind == RACE_ATTACK || (R.kind == RACE_NORMAL && !R.replay && !R.attract && !R.combat)) && !(I(c, 0x4c) & 1)) s2gh_rec_step(in, c->manual);        /* up to the step in which he has done his laps */
    if(!R.attract) human_step(0, in, R.running);        /* (the attract race has no player: car 0 is stepped with the computers, race_others_slice()) */
    if(R.kind == RACE_FREE) free_frame();
    R.steps++;
}

/* combat mode: the second player's car, after the first's (race_step_player) and before the cars touching (race_others_slice) */
void race_step_player2(const struct s2car_input *in)
{
    if(!R.combat || R.stopped) return;
    const struct s2car_input q = R.car[1].stick ? s2gh_round(in) : *in;     /* (the same steps as the first player's stick) */
    human_step(1, &q, R.running);
}

/* the ghost's step: the kept run's byte for it, its own clock and hold */
static void ghost_step(void)
{
    struct s2car_state *c = &R.car[1];
    R.gclock += DT;
    if(!R.grunning && R.gclock > 3.0f) {
        R.grunning = 1;
        c->hold = 0;
    }
    struct s2car_input in = { 0 };
    int manual = c->manual;
    if(R.gsteps < R.ghost->steps) in = s2gh_input(R.ghost, R.gsteps, &manual);
    R.gsteps++;
    c->manual = manual;
    human_step(1, &in, R.grunning);
}

/* the second half of `count` steps, in the game's order (0x2baa4): the three computer cars' passes over tyres, walls and ground (s2car_move), the cars
   touching (the solver and what it sets off: collide_slice()), then what each computer does with the step's controls (s2car_finish_driven: the engine, the
   driver) and the cars off the road. The viewer runs it while the pieces are handed to the hardware and the rasterizer finishes the picture: the
   computers' cars are drawn from the step before (road_race_scene copies the poses before the first slice), a 20 ms lag nobody sees, and their
   physics costs the frame no time where the hardware is the bound. In slices (one car's part of a step, or the cars touching, or the rest of a step)
   so that the viewer can run them while the geometry unit is behind (race_others_slice); the order is the same as in one go. The player's car has
   done all of its step before the first slice (race_step_player): where a touch puts it back and runs the rest of the step again, its controls are those
   of the whole step and its engine, wheels' picture and outputs are not worked out again. */
static struct { uint32_t left; int stage; } others;

void race_others_begin(uint32_t count)
{
    others.left = R.stopped ? 0 : count;
    others.stage = R.first;
}

int race_others_slice(void)
{
    if(!others.left) return 0;
    const int n = R.n;
    if(R.kind != RACE_NORMAL) {                         /* time attack: the ghost (car 1), stepped by its controls; nothing collides with it */
        if(others.stage < n) {
            others.stage++;
            ghost_step();
            return 1;
        }
        if(race_step_hook) race_step_hook(R.car, n, 1);     /* the end of the step: the obstacles and the sparks (s2props.h) */
        others.stage = R.first;
        return --others.left != 0;
    }
    if(others.stage < n) {                              /* a computer's car: the tyres, walls and ground, the step on the ground */
        PF_VAR(pf);
        s2car_move(&R.car[others.stage++], 1);
        PF_ADD(PF_AI_STEP, pf);
        return 1;
    }
    if(others.stage == n) {                             /* the cars touching */
        PF_VAR(pc);
        if(!collide_slice()) others.stage++;
        PF_ADD(PF_COLLIDE, pc);
        return 1;
    }
    if(others.stage < 2 * n + 1 - R.first) {            /* its clocks, the engine, the driver (the stages after the touching count on from n + 1: car R.first first) */
        const int i = others.stage++ - n - 1 + R.first;
        struct s2car_state *c = &R.car[i];
        if(R.running && !(I(c, 0x4c) & 2)) { F(c, 0xc38) += DT; F(c, 0xc3c) += DT; }
        PF_VAR(pf);
        s2car_finish_driven(c, driver, (void *)(intptr_t)i);
        PF_ADD(PF_AI_STEP, pf);
        return 1;
    }
    if(race_step_hook) race_step_hook(R.car, n, 0);
    for(int i = 0; i < R.n; i++) {                      /* a car off the road for good goes back onto the line */
        struct s2car_state *c = &R.car[i];
        R.off[i] = c->off_track ? R.off[i] + 1 : 0;
        R.still[i] = c->ai && R.running && !(I(c, 0x4c) & 1) && F(c, 0x500) < 3.0f ? R.still[i] + 1 : 0;
        if(R.off[i] >= OFF_STEPS || R.still[i] >= STILL_STEPS || c->pos[2] < R.floor_z) put_on_line(i);
    }
    others.stage = R.first;
    return --others.left != 0;
}

void race_step_others(uint32_t count)
{
    race_others_begin(count);
    while(race_others_slice())
        ;
}

float race_knock_take(void)
{
    float k = knock_max;
    knock_max = 0.0f;
    return k;
}

void race_step(const struct s2car_input *in)
{
    race_step_player(in);
    race_step_others(1);
}

int race_replay_ok(void)
{
    return R.n > 0 && R.kind == RACE_NORMAL && !R.attract && !R.combat && s2gh_rec_steps() > 0 && !s2gh_rec_full() && (I(&R.car[0], 0x4c) & 1);
}

int race_replaying(void)
{
    return R.replay;
}

/* ESC in a replay (0x2d7c0): the rest of it is not run and the player's car is done; unless it already is (the end screen is up) */
void race_replay_stop(void)
{
    struct s2car_state *c = &R.car[0];
    if(!R.replay || R.stopped || (I(c, 0x4c) & 1)) return;
    R.stopped = 1;
    I(c, 0x4c) |= 3;
    F(c, 4) = 0.0f;
}

void race_get_results(struct race_result r[RACE_CARS])
{
    for(int i = 0; i < RACE_CARS; i++) {
        const struct s2car_state *c = &R.car[i];
        r[i] = (struct race_result){ I(c, 0xc30), I(c, 0x4a8) >= race_laps, (I(c, 0x4c) & 2) != 0, F(c, 0xc38) };
    }
}

void race_get_hud(struct race_hud *h)
{
    race_get_hud_car(0, h);
}

void race_get_hud_car(int i, struct race_hud *h)
{
    const struct s2car_state *c = &R.car[i];
    int lap = I(c, 0x4ac) + 1;
    h->count = R.clock < 4.0f ? 3 - (int)R.clock : -1;
    h->laps = race_laps;
    h->lap = lap > race_laps ? race_laps : lap;
    h->place = I(c, 0xc30);
    h->cars = R.n;
    h->finished = (I(c, 0x4c) & 2) != 0;
    h->time = F(c, 0xc38);
    h->lap_time = F(c, 0xc3c);
    h->best_lap = F(c, 0xc40) < 3599.0f ? F(c, 0xc40) : -1.0f;
}
