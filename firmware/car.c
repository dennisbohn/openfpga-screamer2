/*
 * A car's model as an object for the drawing (car.h): its blocks made in the game's order for the
 * camera, the wheels turned and steered, the body leant. After OpenScreamer's port/src/main.c
 * (GPL-3.0: wheels_step, draw_object), which took it from the game.
 */
#include "car.h"
#include <math.h>
#include "trigmemo.h"                   /* sinf and cosf remembered: the cars' headings are the physics' (s2car.c), and the mirror's pass asks again */

/* a car model's blocks (mesh.c): place, the middle the drawing order goes by, how much further
   off it counts, whether it is a wheel and what it does; made the first time a car needs it */
#define MAX_BLOCKS  CAR_BLOCKS
#define MAX_MODELS  8
struct cblock { const uint8_t *blk; float t[3], mid[3]; int back, wheel, spins, steers; };
static struct cmodel { const uint8_t *mesh; struct cblock cb[MAX_BLOCKS]; int ncb, ready; } cmodels[MAX_MODELS];

/* the wheels' blocks of a Screamer 2 car are found by name (0x2f519, the table at 0x112f00): the hub that turns and steers
   (x) and the tyre that only steers (i) of each of the four wheels */
static int wheel_name(const uint8_t *n)
{
    static const char names[8][4] = { "flx", "fli", "blx", "rli", "frx", "fri", "brx", "rri" };
    for(int k = 0; k < 8; k++)
        if(n[0] == (uint8_t)names[k][0] && n[1] == (uint8_t)names[k][1] && n[2] == (uint8_t)names[k][2] && n[3] == 0) return 1;
    return 0;
}

/* a car's mesh as car_draw takes it */
static void cmodel_build(struct cmodel *m, const uint8_t *mesh)
{
    /* Which blocks are wheels: by name where the mesh has the eight of Screamer 2's (a body block there can sit a few units off
       the origin and would be turned like a wheel: AUTO02 and AUTO03's), else the blocks that sit off-centre (Screamer 1's) */
    int named_wheels = 0;
    for(int b = 0; b < *(const int32_t *)(mesh + 0x14) && b < MAX_BLOCKS; b++)
        named_wheels += wheel_name(mesh + *(const int32_t *)(mesh + 0x18 + 4 * b) + 0x68);
    /* the blocks: a name of three letters ending in i (a tyre, the arches' insides) counts
       4000 further off, one ending in x (a hub) 1000, unless it starts with A */
    m->mesh = mesh;
    m->ncb = 0;
    for(int b = 0; b < *(const int32_t *)(m->mesh + 0x14) && m->ncb < MAX_BLOCKS; b++) {
        struct cblock *q = &m->cb[m->ncb++];
        q->blk = m->mesh + *(const int32_t *)(m->mesh + 0x18 + 4 * b);
        int32_t so = *(const int32_t *)(q->blk + 0x0C);
        for(int j = 0; j < 3; j++) q->t[j] = so > 0 ? (float)*(const int32_t *)(m->mesh + so + 0x10 + 4 * j) : 0.0f;
        for(int j = 0; j < 3; j++) q->mid[j] = (float)*(const int16_t *)(q->blk + 4 + 2 * j) + q->t[j];
        const uint8_t *nmz = q->blk + 0x68;
        int named = nmz[0] != 'A' && nmz[0] != 'a' && nmz[3] == 0;
        q->back = named && nmz[2] == 'i' ? 4000 : named && nmz[2] == 'x' ? 1000 : 0;
        /* a wheel sits off-centre; only its hub disc (flags 0x800) turns, the tyre block is
           the lower half of the tread and stays; the front ones (local -z) steer */
        q->wheel = named_wheels >= 8 ? wheel_name(nmz) : q->t[0] != 0.0f;
        q->spins = q->wheel && (*(const int32_t *)(q->blk + 0x18) & 0x800);
        q->steers = q->wheel && q->t[2] < 0.0f;
    }
    m->ready = 1;
}

/* model k (AUTOk) */
static const struct cmodel *cmodel(int k)
{
    int nm;
    track_models(&nm);
    if(k < 0 || k >= nm || k >= MAX_MODELS) k = 0;
    struct cmodel *m = &cmodels[k];
    if(!m->ready) cmodel_build(m, track_model_mesh(k));
    return m;
}

void car_models_reset(void)
{
    for(int k = 0; k < MAX_MODELS; k++)         /* each track's image has its own car models */
        cmodels[k].ready = 0;
}

/* A block's point p (as stored) in the world (block_to_view, tilt, place): a wheel turned
   about its axle and steered, moved to its place on the car, a body block leant on its own,
   the object's pitch and roll, its heading, its place */
static void block_point(const struct cblock *q, const struct car_trig *ps, const float p[3], float w[3])
{
    const float *sc = ps->sc, *car = ps->car;
    float x = p[0], y = p[1], z = p[2];
    if(q->wheel) {
        float wcs = q->spins ? sc[0] : 1.0f, wsn = q->spins ? sc[1] : 0.0f;
        float scs = q->steers ? sc[2] : 1.0f, ssn = q->steers ? sc[3] : 0.0f;
        float y2 = y * wcs - z * wsn, z2 = z * wcs + y * wsn;
        float x2 = x * scs + z2 * ssn;
        z = z2 * scs - x * ssn;
        x = x2;
        y = y2;
    }
    x += q->t[0]; y += q->t[1]; z += q->t[2];
    if(ps->lean && !q->wheel) {     /* the body's lean (the suspension, an opponent in a bend) */
        float yb = y * ps->body[0] - z * ps->body[1], zb = z * ps->body[0] + y * ps->body[1];
        float xb = x * ps->body[2] - yb * ps->body[3];
        y = yb * ps->body[2] + x * ps->body[3] - ps->drop;
        x = xb;
        z = zb;
    }
    /* tilt: the object's pitch and roll are the car's, negated (player_sync) */
    float y3 = y * car[0] + z * car[1], z3 = z * car[0] - y * car[1];
    float x3 = x * car[2] - y3 * car[3], y4 = y3 * car[2] + x * car[3];
    w[0] = ps->x + x3 * car[4] - z3 * car[5];
    w[1] = ps->y + x3 * car[5] + z3 * car[4];
    w[2] = ps->z + y4;
}

/* the block's place in the world: its origin and the images of its three axes */
static void place_block(const struct cblock *q, const struct car_trig *ps, struct track_block *t)
{
    static const float zero[3] = { 0.0f, 0.0f, 0.0f }, ex[3] = { 1.0f, 0.0f, 0.0f };
    static const float ey[3] = { 0.0f, 1.0f, 0.0f }, ez[3] = { 0.0f, 0.0f, 1.0f };
    float o[3], px[3], py[3], pz[3];
    block_point(q, ps, zero, o);
    block_point(q, ps, ex, px);
    block_point(q, ps, ey, py);
    block_point(q, ps, ez, pz);
    for(int j = 0; j < 3; j++) {
        t->o[j] = o[j];
        t->m[3 * j] = px[j] - o[j];
        t->m[3 * j + 1] = py[j] - o[j];
        t->m[3 * j + 2] = pz[j] - o[j];
    }
}

/* m's blocks placed as lk, the body dropped by drop, into out in the game's order for the
   camera, each at its level of detail (STARTL.EXE 0x7f010, track_lod_level(): the table entry b + 13 L of
   the mesh, by the distance of the block's middle; a block without one at that level is not drawn, so
   a car at level 2 is its body alone): how many. What does not depend on the camera is worked out once for
   a look and kept in pose (car.h): the trigonometry (14 calls of the library), each block's middle and, the
   first time a pass draws the block, its place. The picture's pass and the mirror's then differ only in the
   order and the levels. */
static int draw_model(const struct cmodel *m, const struct car_look *lk, float drop, const struct track_cam *cam,
                      struct car_pose *pose, struct track_block *out)
{
    struct car_trig *ps = &pose->ps;
    if(!pose->made) {
        *ps = (struct car_trig){ { cosf(lk->spin), sinf(lk->spin), cosf(lk->steer), sinf(lk->steer) },
                                 { cosf(lk->pitch), sinf(lk->pitch), cosf(lk->roll), sinf(lk->roll), cosf(lk->yaw), sinf(lk->yaw) },
                                 { cosf(lk->body_pitch), sinf(lk->body_pitch), cosf(lk->body_roll), sinf(lk->body_roll) },
                                 lk->body_pitch != 0.0f || lk->body_roll != 0.0f || drop != 0.0f, lk->x, lk->y, lk->z, drop };
        for(int b = 0; b < m->ncb; b++) {
            const struct cblock *q = &m->cb[b];
            float mid[3];
            for(int j = 0; j < 3; j++) mid[j] = q->mid[j] - q->t[j];
            block_point(q, ps, mid, pose->mid[b]);
        }
        pose->placed = 0;
        pose->made = 1;
    }
    int ord[MAX_BLOCKS], nl = 0;
    float key[MAX_BLOCKS];
    const uint8_t *blk[MAX_BLOCKS];
    for(int b = 0; b < m->ncb; b++) {
        const struct cblock *q = &m->cb[b];
        /* the drawing order (0x8ac58, 0x8af24): the furthest middle first, the tyres and
           hubs counted further off; on a tie the lower block first */
        const float *mw = pose->mid[b];
        float dx = mw[0] - cam->x, dy = mw[1] - cam->y, dh = mw[2] - cam->h;
        float dist = rintf(sqrtf(dx * dx + dy * dy + dh * dh));
        key[b] = -dist - (float)q->back;
        blk[b] = q->blk;
        int lvl = track_lod_level(dist, 1);
        if(lvl) {
            int32_t off = *(const int32_t *)(m->mesh + 0x18 + 4 * (b + 13 * lvl));
            if(!off) continue;
            blk[b] = m->mesh + off;
        }
        if(!(pose->placed >> b & 1)) {          /* (only a block that is drawn is placed: a car far off is its body alone) */
            place_block(q, ps, &pose->b[b]);
            pose->b[b].file = m->mesh;
            pose->placed |= 1u << b;
        }
        int i = nl;
        while(i > 0 && key[ord[i - 1]] > key[b]) { ord[i] = ord[i - 1]; i--; }
        ord[i] = b;
        nl++;
    }
    for(int b = 0; b < nl; b++) {
        out[b] = pose->b[ord[b]];
        out[b].blk = blk[ord[b]];
        out[b].phase = lk->phase;
    }
    return nl;
}

static int draw_car(int model, const struct car_look *lk, float drop, const struct track_cam *cam,
                    struct car_pose *pose, struct track_block *out, struct track_object *obj)
{
    const struct cmodel *m = cmodel(model);
    if(!m->ncb) return 0;
    int nl = draw_model(m, lk, drop, cam, pose, out);
    obj->x = lk->x;
    obj->y = lk->y;
    obj->h = lk->z;
    obj->radius = 1200.0f;
    obj->n = nl;
    obj->b = out;
    return 1;
}

static struct car_pose once;            /* the pose of a car that is drawn by one pass alone (not on the stack: 1.3 KB) */

int car_draw(int model, const struct car_look *lk, const struct track_cam *cam, struct track_block *out,
             struct track_object *obj)
{
    once.made = 0;
    return draw_car(model, lk, 0.0f, cam, &once, out, obj);
}

int car_draw_pose(int model, const struct car_look *lk, const struct track_cam *cam, struct car_pose *pose,
                  struct track_block *out, struct track_object *obj)
{
    return draw_car(model, lk, 0.0f, cam, pose, out, obj);
}

int car_mesh_draw(const uint8_t *mesh, const struct car_look *lk, const struct track_cam *cam,
                  struct track_block *out)
{
    static struct cmodel m;
    cmodel_build(&m, mesh);
    once.made = 0;
    return draw_model(&m, lk, 0.0f, cam, &once, out);
}

/* sine and cosine; a tilt (under 0.5 rad, nearly always) by their series, to float's precision,
   without the library's calls */
static void sin_cos(float a, float *s, float *c)
{
    if(fabsf(a) >= 0.5f) {
        *s = sinf(a);
        *c = cosf(a);
        return;
    }
    float a2 = a * a;
    *s = a * (1.0f - a2 * (1.0f / 6.0f - a2 * (1.0f / 120.0f - a2 * (1.0f / 5040.0f))));
    *c = 1.0f - a2 * (0.5f - a2 * (1.0f / 24.0f - a2 * (1.0f / 720.0f - a2 * (1.0f / 40320.0f))));
}

void car_flat_block(const uint8_t *file, float x, float y, float z, float yaw, float pitch, float roll,
                    struct track_block *out)
{
    const struct cblock q = { 0 };
    struct car_trig ps = { { 1.0f, 0.0f, 1.0f, 0.0f }, { 0 }, { 1.0f, 0.0f, 1.0f, 0.0f }, 0, x, y, z, 0.0f };
    sin_cos(pitch, &ps.car[1], &ps.car[0]);
    sin_cos(roll, &ps.car[3], &ps.car[2]);
    ps.car[4] = cosf(yaw);
    ps.car[5] = sinf(yaw);
    place_block(&q, &ps, out);
    out->file = file;
    out->blk = file + *(const int32_t *)(file + 0x18);
    out->phase = 0;
}

int car_mesh_blocks(const uint8_t *file, float x, float y, float z, float yaw, float pitch, float roll,
                    struct track_block *out, int max)
{
    struct car_trig ps = { { 1.0f, 0.0f, 1.0f, 0.0f }, { 0 }, { 1.0f, 0.0f, 1.0f, 0.0f }, 0, x, y, z, 0.0f };
    sin_cos(pitch, &ps.car[1], &ps.car[0]);
    sin_cos(roll, &ps.car[3], &ps.car[2]);
    ps.car[4] = cosf(yaw);
    ps.car[5] = sinf(yaw);
    int nb = *(const int32_t *)(file + 0x14), n = 0;
    for(int b = 0; b < nb && n < max; b++, n++) {
        struct cblock q = { 0 };
        q.blk = file + *(const int32_t *)(file + 0x18 + 4 * b);
        int32_t so = *(const int32_t *)(q.blk + 0x0C);         /* its place in the mesh, as cmodel() */
        for(int j = 0; j < 3; j++) q.t[j] = so > 0 ? (float)*(const int32_t *)(file + so + 0x10 + 4 * j) : 0.0f;
        place_block(&q, &ps, &out[n]);
        out[n].file = file;
        out[n].blk = q.blk;
        out[n].phase = 0;
    }
    return n;
}

