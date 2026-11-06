/*
 * The obstacles of the tracks and the sparks of the race (s2props.h)
 */
#include "s2props.h"
#include "s2params.h"
#include "race.h"
#include "car.h"
#include "dat.h"
#include "track_build.h"
#include <math.h>
#include <string.h>

#define PI_F        3.14159274f
#define HALF_PI_F   1.57079637f
#define DT          0.02f               /* the step; the game's raw frame time (the sparks' life goes by it) is the same */
#define KINDS       12                  /* meshes: the obstacles' rows (ten at most) and the sparks' two */
#define ROWS        10                  /* rows of OSTACLES (the game's table 0x126be4 has room for ten) */
#define MESH_BYTES  8192                /* the meshes unpacked (the obstacles' 1.1 to 1.6 KB each, the sparks' 664 bytes) */

union word { int32_t i; uint32_t u; float f; };
#define RF(c, o)    (((union word *)(c)->rec)[(o) >> 2].f)
#define RI(c, o)    (((union word *)(c)->rec)[(o) >> 2].i)

/* the game's record of 0x3c bytes (an obstacle's and a spark's alike): the place (+0 x, +4 height, +8 z), the angles (+0xc, +0x10, +0x14), the speeds (+0x18 x, +0x1c up, +0x20
   z), a spark's life (+0x24); on: +0x34 (an obstacle flying, a spark alive), hit: +0x38 (an obstacle knocked once) */
struct prop {
    float p[3], a[3], v[3], life;
    uint8_t kind, on, hit, dirty;       /* the mesh; dirty: its picture is to be made again */
};

struct kind { const uint8_t *file; int radius, key; char name[12]; };

static struct kind kinds[KINDS];
static uint8_t mesh_mem[MESH_BYTES] __attribute__((aligned(4)));
static int nkinds, spark_kind[2];       /* the sparks' sets' meshes: [1] AZTRI (set 1, the touching cars'), [0] AZ1TRI (the walls'); -1 if the track has none */
static struct prop obst[S2PROPS_OBSTACLES], spark[S2PROPS_SPARKS];
static struct track_block block[S2PROPS_OBSTACLES];   /* an obstacle's picture as it lies (made when it is drawn, again after it moved) */
static int nobst;
static float lo[3], hi[3];              /* the obstacles' extent in x, height and z: a car further than 2,000 / 200 outside it knocks none */
static uint32_t steps;                  /* the steps the race has run: the game's 100 Hz counter is two of them */
static uint32_t seed;                   /* the game's rand (0xb3dd9) */
static int knocked;                     /* the player's car knocked an obstacle (s2props_hit_take) */
static float step_dt = DT, raw_dt = DT; /* the game's step (0x133350: everything flies by it) and raw frame time (0x133354: the sparks' life); the firmware's are both 20 ms */

static uint32_t rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int rand_(void)
{
    seed = seed * 0x41c64e6du + 0x3039u;
    return (int)(seed >> 16 & 0x7fff);
}

/* ------------------------------------------------------------------------------------------------ the data */

/* the mesh <name>.3DX (the one already there if it was asked for before): its kind, -1 if the container has none */
static int kind_of(const uint8_t *gfx, uint32_t gfx_size, const char *name, int32_t misc_delta, uint32_t *used)
{
    for(int k = 0; k < nkinds; k++)
        if(!strcmp(kinds[k].name, name)) return k;
    const uint32_t len = (uint32_t)strlen(name);
    if(nkinds >= KINDS || len > 11) return -1;
    const uint32_t size = s2_mesh_load(gfx, gfx_size, name, misc_delta, mesh_mem + *used, MESH_BYTES - *used);
    if(!size) return -1;
    struct kind *k = &kinds[nkinds];
    k->file = mesh_mem + *used;
    k->radius = (int)rd32(k->file);
    k->key = track_class_key(k->file);
    memcpy(k->name, name, len + 1);
    *used += (size + 3) & ~3u;
    return nkinds++;
}

int s2props_load(int track, const uint8_t *gfx, uint32_t gfx_size, const uint8_t *data, uint32_t data_size, int32_t misc_delta, uint8_t *scratch, uint32_t cap)
{
    char file[16] = "MAP0DAT.DTR";
    static char names[3 * ROWS][12];
    int rowkind[ROWS];
    struct dat_entry e;
    uint32_t used = 0;
    nkinds = nobst = 0;
    spark_kind[0] = spark_kind[1] = -1;
    memset(spark, 0, sizeof spark);
    if(!gfx || !data || !misc_delta) return 0;          /* (the simulation takes a finished image: no containers) */
    spark_kind[1] = kind_of(gfx, gfx_size, "AZTRI", misc_delta, &used);
    spark_kind[0] = kind_of(gfx, gfx_size, "AZ1TRI", misc_delta, &used);
    file[3] = (char)('0' + track);
    if(!dat_find(data, data_size, file, &e) || e.size > cap) return 0;
    uint32_t n = dat_extract(data, data_size, &e, scratch, cap);
    const int rows = n ? s2_dtr_names(scratch, n, "OSTACLES", names, 3 * ROWS) / 3 : 0;      /* (three names a row, the same three: the first is the mesh) */
    if(!rows) return 0;
    for(int r = 0; r < rows; r++) rowkind[r] = kind_of(gfx, gfx_size, names[3 * r], misc_delta, &used);
    memcpy(file + 4, "OST.SPL", 8);
    if(!dat_find(data, data_size, file, &e) || e.size > cap || !(n = dat_extract(data, data_size, &e, scratch, cap)) || n < 8) return 0;
    int32_t count = (int32_t)rd32(scratch);
    if(count < 0 || (uint32_t)count > (n - 8) / 0x38) return 0;
    for(int32_t i = 0; i < count && nobst < S2PROPS_OBSTACLES; i++) {        /* nodes of 0x38 bytes: +4 x, +8 z, +0xc the row */
        const uint8_t *q = scratch + 8 + 0x38 * (uint32_t)i;
        const int32_t row = (int32_t)rd32(q + 0xc);
        if(row < 0 || row >= rows || rowkind[row] < 0) continue;
        obst[nobst] = (struct prop){ { (float)(int32_t)rd32(q + 4), 0.0f, (float)(int32_t)rd32(q + 8) }, { 0 }, { 0 }, 0.0f, (uint8_t)rowkind[row], 0, 0, 1 };
        nobst++;
    }
    return nobst;
}

/* ------------------------------------------------------------------------------------------------ the obstacles */

/* the angles that lay an obstacle of heading a[1] on a ground face with the normal n (x, up, z; 0x30b29, and 0x2c475 when one comes to rest): the normal turned about the up axis
   by the heading less a quarter turn, each level part an angle */
static void lean(struct prop *o, const int n[3])
{
    const int32_t in[3] = { n[0], n[1], n[2] };
    int32_t out[3];
    s2car_turn_up(out, in, o->a[1] - HALF_PI_F);
    o->a[0] = s2car_ground_angle((float)out[1], (float)out[2]) + HALF_PI_F;
    o->a[2] = s2car_ground_angle((float)out[1], (float)out[0]) + HALF_PI_F;
}

static void widen(const float p[3])
{
    if(p[0] < lo[0]) lo[0] = p[0];
    if(p[0] > hi[0]) hi[0] = p[0];
    if(p[1] < lo[1]) lo[1] = p[1];
    if(p[1] > hi[1]) hi[1] = p[1];
    if(p[2] < lo[2]) lo[2] = p[2];
    if(p[2] > hi[2]) hi[2] = p[2];
}

/* 0x30b29: on the ground at its place (the height there, 0 where there is none), the road's heading there, leant to the ground's slope; not flying, not knocked */
static void stand(struct prop *o)
{
    static float heading;                               /* (the game keeps the last heading found: 0x11cddc) */
    int held, h = 0, n[3] = { 0, 0, 0 }, kind;
    const int found = track_ground_s2((int)o->p[0], (int)o->p[2], &held, &h, n, &kind);
    o->p[1] = found ? (float)h : 0.0f;
    s2car_road_near((int)o->p[0], (int)o->p[2], &heading);
    o->a[1] = heading;
    lean(o, n);
    o->v[0] = o->v[1] = o->v[2] = 0.0f;
    o->on = o->hit = 0;
    o->dirty = 1;
}

void s2props_begin(void)
{
    steps = 0;
    knocked = 0;
    memset(spark, 0, sizeof spark);
    s2car_sparks = s2props_spark;
    race_step_hook = s2props_step;
    for(int k = 0; k < 3; k++) { lo[k] = 1e30f; hi[k] = -1e30f; }
    for(int i = 0; i < nobst; i++) {
        stand(&obst[i]);
        widen(obst[i].p);
    }
}

/* the state of a flying thing a step on: it falls (7,848 a second squared), moves by its speed and turns against it (0x2c475, 0x2cc70) */
static void move(struct prop *o)
{
    o->v[1] -= step_dt * 7848.0f;
    o->p[0] += o->v[0] * step_dt;
    o->p[2] += o->v[2] * step_dt;
    o->p[1] += o->v[1] * step_dt;
    o->a[0] -= o->v[0] * step_dt * 0.0008f;
    o->a[1] -= o->v[1] * step_dt * 0.0004f;
    o->a[2] -= o->v[2] * step_dt * 0.00015f;
}

/* 0x2c475: a flying obstacle's step. Off the ground (no face under it) it meets the nearest wall if there is one: put back out of it (into the wall's frame, its part across
   nought), its speed turned about the wall by twice the angle between the wall and its way (that angle kept within a quarter turn), slowed to half its cosine across, half up;
   with no wall the ground is at its own height. At or below the ground it stands on it with 0.7 of its speed (the up speed turned upwards); with under 300 left it stops and,
   on the ground, leans with it as it did when it was stood up */
static void fly(struct prop *o)
{
    move(o);
    int x = (int)o->p[0], z = (int)o->p[2], held, g = 0, n[3] = { 0, 0, 0 }, kind;
    int off = !track_ground_s2(x, z, &held, &g, n, &kind);
    if(off) {
        int wx, wz;
        float wa;
        if(!s2car_wall_near(x, z, &wx, &wz, &wa)) {
            g = (int)o->p[1];
        } else {
            float a = s2car_ang_diff(PI_F, wa);
            const int32_t v[3] = { x - wx, 0, z - wz };
            int32_t o1[3], o2[3];
            s2car_turn_up(o1, v, a);
            o1[0] = 0;
            s2car_turn_up(o2, o1, -a);
            o->p[0] += (float)(o2[0] - v[0]);
            o->p[2] += (float)(o2[2] - v[2]);
            x = (int)o->p[0];
            z = (int)o->p[2];
            g = 0;
            off = !track_ground_s2(x, z, &held, &g, n, &kind);
            if(off) g = (int)o->p[1];
            a = s2car_ang_diff(a, s2car_ground_angle(o->v[0], o->v[2]));
            if(fabsf(a) > 1.570796370506286f) a = s2car_ang_diff(a, PI_F);
            const float b = a * 2.0f, s = sinf(b), c = cosf(b);
            const float vx = o->v[0] * c - o->v[2] * s, vz = o->v[2] * c + o->v[0] * s;
            const float k = (float)((double)cosf(a) * 0.5);
            o->v[0] = vx * k;
            o->v[2] = vz * k;
            o->v[1] = (float)((double)o->v[1] * 0.5);
        }
    }
    if((float)g >= o->p[1]) {
        o->p[1] = (float)g;
        o->v[0] = (float)((double)o->v[0] * 0.7);
        o->v[2] = (float)((double)o->v[2] * 0.7);
        o->v[1] = fabsf((float)((double)o->v[1] * 0.7));
        if(sqrtf(o->v[1] * o->v[1] + o->v[0] * o->v[0] + o->v[2] * o->v[2]) < 300.0f) {
            o->on = 0;
            if(!off) lean(o, n);
        }
    }
    widen(o->p);
    o->dirty = 1;
}

/* 0x2a639: the car c against the obstacles. One within 200 of the car's height and 2,000 of its middle is knocked if the car's nose point, half the car's length on along the
   way it just went and swept from last step's place to this step's, passes within the car's reach (its width and length by the wheels' angle) of it */
static void knock(struct s2car_state *c, int player)
{
    const float cy = (float)RI(c, 0x4dc), cx = (float)(int32_t)RF(c, 0x4d8), cz = (float)(int32_t)RF(c, 0x4e0);
    if(cx < lo[0] - 2000.0f || cx > hi[0] + 2000.0f || cz < lo[2] - 2000.0f || cz > hi[2] + 2000.0f || cy < lo[1] - 200.0f || cy > hi[1] + 200.0f) return;
    const float px = (float)(int32_t)RF(c, 0x734), pz = (float)(int32_t)RF(c, 0x73c);
    const float dx = cx - px, dz = cz - pz, len = sqrtf(dx * dx + dz * dz), half = (float)RI(c, 0x4d0) * 0.5f;
    const float wheels = RF(c, 0x4d4), reach = fabsf(cosf(wheels) * (float)RI(c, 0x4c8)) + fabsf(sinf(wheels) * (float)RI(c, 0x4d0));
    const float hx = len != 0.0f ? dx * (1.0f / len) * half : 0.0f, hz = len != 0.0f ? dz * (1.0f / len) * half : 0.0f;
    const float ax = px + hx, az = pz + hz, bx = cx + hx, bz = cz + hz;     /* the nose point, last step's and this step's */
    for(int i = 0; i < nobst; i++) {
        struct prop *o = &obst[i];
        if(!(fabsf(cy - o->p[1]) < 200.0f)) continue;
        const float ex = cz - o->p[2], ey = cx - o->p[0];
        if(!(sqrtf(ex * ex + ey * ey) < 2000.0f)) continue;
        /* how far along the nose's way the obstacle falls (0x1766c: ((a - o) . (a - b)) / |a - b|^2, a the last place, b this) and how near that point of the way is */
        const float wx = ax - bx, wz = az - bz, l2 = wx * wx + wz * wz;
        const float s = l2 != 0.0f ? ((ax - o->p[0]) * wx + (az - o->p[2]) * wz) / l2 : 0.0f;
        if(0.0f > s || s > 1.0f) continue;
        const float qx = bx * s + ax * (1.0f - s) - o->p[0], qz = bz * s + az * (1.0f - s) - o->p[2];
        if(!(sqrtf(qx * qx + qz * qz) < reach)) continue;
        const int32_t r = (int32_t)(o->p[0] + o->p[2]);
        const float way = (float)((r & 15) - 7) * 0.0314159281f + RF(c, 0x508);      /* the car's way, turned by the obstacle's place: a chance */
        const float sp = (float)((double)RF(c, 0x500) * 1.4 * 111.0);
        o->v[0] = sinf(way) * sp;
        o->v[2] = -cosf(way) * sp;
        o->v[1] = sinf(RF(c, 0x50c) + 0.261799395f) * sp;
        if(!o->hit) RI(c, 0xc88)++;                     /* (the game counts them in the car's record and nothing reads the count) */
        o->hit = o->on = 1;
        o->dirty = 1;
        if(player) knocked = 1;
    }
}

/* ------------------------------------------------------------------------------------------------ the sparks */

void s2props_spark(const float at[3], const float dir[3], float hit, int set)
{
    seed = steps * 2;                                   /* srand(the 100 Hz counter): every call starts the chance over (0x2aeb9) */
    int n = (int)(hit * 3600.0f / 400000.0f * 30.0f / 60.0f);
    if(n > 30) n = 30;
    for(int i = 0; i < n; i++) {
        struct prop *s = &spark[set == 1 ? i : i + 30];
        s->life = 2.0f;
        s->p[0] = at[0];
        s->p[1] = at[1] + 100.0f;
        s->p[2] = at[2];
        s->v[0] = (float)((double)(rand_() % 3000) + (double)dir[0] * 0.9 + -1500.0);
        s->v[2] = (float)((double)(rand_() % 3000) + (double)dir[2] * 0.9 + -1500.0);
        s->v[1] = (float)(rand_() % 2000 + 1000);
        s->a[0] = (float)(rand_() % 500) * 0.012566371f;
        s->a[2] = (float)(rand_() % 500) * 0.012566371f;
        s->a[1] = (float)(rand_() % 500) * 0.012566371f;
        s->on = 1;
        s->dirty = 1;
    }
}

/* ------------------------------------------------------------------------------------------------ the step */

void s2props_step(struct s2car_state *cars, int n, int attack)
{
    steps++;
    for(int i = 0; i < nobst; i++)
        if(obst[i].on) fly(&obst[i]);
    for(int i = 0; i < S2PROPS_SPARKS; i++) {
        struct prop *s = &spark[i];
        if(!s->on) continue;
        move(s);
        s->life -= raw_dt;
        if(!(0.0f < s->life)) s->on = 0;
        s->dirty = 1;
    }
    if(!nobst) return;
    /* 0x2b9b1: every car whose height and x are within 20,000 of the player's (the game adds the two differences and leaves the z out) */
    const float px = (float)(int32_t)RF(&cars[0], 0x4d8), ph = (float)RI(&cars[0], 0x4dc);
    for(int i = 0; i < n; i++) {
        if(attack && i != 0) continue;                  /* (time attack: only the player's car) */
        struct s2car_state *c = &cars[i];
        if(fabsf((float)(int32_t)RF(c, 0x4d8) - px) + fabsf((float)RI(c, 0x4dc) - ph) < 20000.0f) knock(c, i == 0);
    }
}

int s2props_hit_take(void)
{
    const int k = knocked;
    knocked = 0;
    return k;
}

/* ------------------------------------------------------------------------------------------------ the picture */

/* a thing's angles as the game's object record keeps them (2,048ths of a turn, cut towards zero) and as the drawing takes them: the heading, the pitch the game's negated, the roll */
static float turns(float a)
{
    return (float)((int32_t)(a * 1024.0f / PI_F) & 0x7ff) * (PI_F / 1024.0f);
}

static void pose(const struct prop *o, const struct kind *k, struct track_block *b)
{
    car_mesh_blocks(k->file, o->p[0], o->p[2], o->p[1], turns(o->a[1]), -turns(o->a[0]), turns(o->a[2]), b, 1);
    b->phase = track_phase();
}

static int make_obst(int id, const struct track_cam *cam, struct track_object *ob)
{
    static struct track_block tb;
    struct prop *o = &obst[id];
    const struct kind *k = &kinds[o->kind];
    if(o->dirty) {
        pose(o, k, &block[id]);
        o->dirty = 0;
    }
    tb = block[id];
    const float dx = ob->x - cam->x, dy = ob->y - cam->y, dh = ob->h - cam->h;
    const int lvl = track_lod_level(sqrtf(dx * dx + dy * dy + dh * dh), 1);       /* the level of detail by the object's distance: the moving objects' rule */
    if(lvl) {
        const int32_t at = (int32_t)rd32(k->file + 0x18 + 4 * 13 * lvl);
        if(!at) return ob->n = 0;
        tb.blk = k->file + at;
    }
    ob->b = &tb;
    return ob->n = 1;
}

static int make_spark(int id, const struct track_cam *cam, struct track_object *ob)
{
    static struct track_block tb;
    (void)cam;
    pose(&spark[id], &kinds[spark_kind[id < 30]], &tb);
    ob->b = &tb;
    return ob->n = 1;
}

int s2props_objects(const struct track_cam *cam, struct track_object *objs, int max)
{
    int n = 0;
    float f[3], r[3], u[3];
    track_cam_axes(cam, f, r, u);
    const float far = track_far() + 1000.0f;            /* (an object further than the far plane and its own reach cannot be seen: the drawing would leave it out) */
    for(int i = 0; i < nobst && n < max; i++) {
        const struct prop *o = &obst[i];
        const float dx = o->p[0] - cam->x, dy = o->p[2] - cam->y;
        if(dx * dx + dy * dy > far * far) continue;
        objs[n++] = (struct track_object){ o->p[0], o->p[2], o->p[1], (float)kinds[o->kind].radius, 0, 0, make_obst, i, kinds[o->kind].key, 0, 0 };
    }
    for(int i = 0; i < S2PROPS_SPARKS && n < max; i++) {
        const struct prop *s = &spark[i];
        const int k = spark_kind[i < 30];
        if(!s->on || k < 0) continue;
        objs[n++] = (struct track_object){ s->p[0], s->p[2], s->p[1], (float)kinds[k].radius, 0, 0, make_spark, i, kinds[k].key, 0, 0 };
    }
    return n;
}

/* ------------------------------------------------------------------------------------------------ for the tests */

int s2props_obstacles(void)
{
    return nobst;
}

void s2props_time(float step, float raw)
{
    step_dt = step;
    raw_dt = raw;
}

void s2props_get(int is_spark, int i, struct s2props_rec *r)
{
    const struct prop *o = is_spark ? &spark[i] : &obst[i];
    memcpy(r->p, o->p, sizeof r->p);
    memcpy(r->a, o->a, sizeof r->a);
    memcpy(r->v, o->v, sizeof r->v);
    r->life = o->life;
    r->on = o->on;
    r->hit = o->hit;
}

void s2props_set(int is_spark, int i, const struct s2props_rec *r)
{
    struct prop *o = is_spark ? &spark[i] : &obst[i];
    memcpy(o->p, r->p, sizeof o->p);
    memcpy(o->a, r->a, sizeof o->a);
    memcpy(o->v, r->v, sizeof o->v);
    o->life = r->life;
    o->on = (uint8_t)r->on;
    o->hit = (uint8_t)r->hit;
    o->dirty = 1;
}
