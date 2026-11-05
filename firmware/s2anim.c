/*
 * The tracks' animated scenery (s2anim.h)
 */
#include "s2anim.h"
#include "s2params.h"
#include "car.h"
#include "dat.h"
#include "track_build.h"
#include <math.h>
#include <string.h>

#define PI          3.14159265f
#define TWO_PI      6.28318531f
#define REACH       52000.0f            /* a row within this of the player (both ways) is alive (0xcb20) */
#define POOL        512                 /* keys of all the paths (the most any track has is 240) */
#define MESH_BYTES  32768               /* the meshes of a track, unpacked (the most is 20 KB) */
#ifndef PITCH_SIGN
#define PITCH_SIGN  -1.0f               /* the object's pitch against the climb (the game's pitch is nose up, car.h's the other way); -DPITCH_SIGN=1.0f only for the picture tests */
#endif

struct key { float t, v, out, in; };
struct chan { uint16_t first, n; };
struct path { struct chan c[3]; uint8_t ok; };
struct mesh { const uint8_t *file; int radius, key; char name[12]; };
struct row {
    char name[12];
    int8_t mesh;                        /* its mesh, -1: a sound */
    int8_t path;
    int flags, lap, sample;
    float time, start, range;
    struct s2anim_row st;
};

static struct key pool[POOL];
static struct path paths[S2ANIM_PATHS];
static struct mesh meshes[S2ANIM_MESHES];
static struct row rows[S2ANIM_ROWS];
static uint8_t mesh_mem[MESH_BYTES] __attribute__((aligned(4)));
static int nrows, nmeshes;

static int32_t rd32(const uint8_t *p)
{
    return (int32_t)(p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

static float rdf(const uint8_t *p)
{
    int32_t v = rd32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static int is_space(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* MAPn.PTH (0x30ec2, 0x11c0c): a count, then per path 100 bytes (its name at +8, "MV_<c>...": c is its number, '1' .. '9' are 0 .. 8, 'a' on 9 on) and three channels of
   0x30 bytes (the number of keys at +0x1c) with their keys of 0x2c bytes (the time +8, the value +0xc, the slopes out and in +0x10, +0x14); 1 if all of it was read */
static int load_paths(const uint8_t *p, uint32_t size)
{
    uint32_t at = 4, used = 0;
    memset(paths, 0, sizeof paths);
    if(size < 4) return 0;
    int32_t count = rd32(p);
    for(int32_t i = 0; i < count && at + 100 <= size; i++) {
        int c = p[at + 11], number = c >= '0' && c <= '9' ? c - '1' : c >= 'a' && c <= 'z' ? c - 0x58 : c - 0x38;
        at += 100;
        struct path q = { { { 0, 0 }, { 0, 0 }, { 0, 0 } }, 1 };
        for(int k = 0; k < 3; k++) {
            if(at + 0x30 > size) return 0;
            int32_t n = rd32(p + at + 0x1C);
            at += 0x30;
            if(n < 0 || n > S2ANIM_KEYS || at + 0x2C * (uint32_t)n > size) return 0;
            if(used + (uint32_t)n > POOL) q.ok = 0;
            q.c[k].first = (uint16_t)used;
            q.c[k].n = (uint16_t)n;
            for(int32_t j = 0; j < n && used < POOL; j++, at += 0x2C)
                pool[used++] = (struct key){ rdf(p + at + 8), rdf(p + at + 0xC), rdf(p + at + 0x10), rdf(p + at + 0x14) };
        }
        if(number >= 0 && number < S2ANIM_PATHS) paths[number] = q;
    }
    return 1;
}

/* 0x126ad: a channel's value, and its rate of change if rate is set, at s (0 .. 1). Between keys p and q the cubic with the controls v_p, v_p + out_p h, v_q - in_q h, v_q, h = (t_q - t_p) / 3;
   before the first key the first one's place, from the last key on 0.001 before it */
static float chan_at(const struct chan *c, float s, float *rate)
{
    const struct key *k = pool + c->first;
    if(rate) *rate = 0.0f;
    if(c->n < 1) return 0.0f;
    if(c->n < 2) return k[0].v;
    if(k[0].t <= s) {
        if(k[c->n - 1].t <= s) s = k[c->n - 1].t - 0.001f;
    } else {
        s = k[0].t;
    }
    int q = 1;
    while(q < c->n - 1 && k[q].t <= s) q++;
    const struct key *a = &k[q - 1], *b = &k[q];
    float dt = b->t - a->t, u = (s - a->t) / dt, h = dt * 0.33333334f;
    float b1 = a->out * h + a->v, b2 = b->v - b->in * h;
    float a3 = (b1 * 3.0f + (b->v - b2 * 3.0f)) - a->v, a2 = a->v * 3.0f + (b2 * 3.0f - b1 * 6.0f), a1 = b1 * 3.0f - a->v * 3.0f;
    if(rate) *rate = ((a2 * 2.0f + a3 * 3.0f * u) * u + a1) / dt;
    return ((a3 * u + a2) * u + a1) * u + a->v;
}

/* 0x129d5: a path's place (x, y, height) at part (0 .. 1) and, if way is set, which way it is going: the rates of the three channels made a unit vector */
static void path_at(const struct path *p, float part, float place[3], float way[3])
{
    part = part < 0.0f ? 0.0f : part > 1.0f ? 1.0f : part;
    float l = 0.0f;
    for(int k = 0; k < 3; k++) {
        place[k] = chan_at(&p->c[k], part, way ? &way[k] : 0);
        if(way) l += way[k] * way[k];
    }
    if(way && (l = sqrtf(l)) != 0.0f)
        for(int k = 0; k < 3; k++) way[k] *= 1.0f / l;
}

void s2anim_angles(int i, float part, int *heading, int *climb)
{
    float place[3], way[3];
    path_at(&paths[rows[i].path], part, place, way);
    /* 0x2cee4: the heading is the angle of (x, -y) from the quadrant (atan of the shares, then pi less if y > 0 and 2 pi less if x < 0); the climb that of the height against what is
       left along the ground when the way is turned back by the heading */
    float turn = way[1] == 0.0f ? PI / 2.0f : atanf(fabsf(way[0] / way[1]));
    if(0.0f < way[1]) turn = PI - turn;
    if(way[0] < 0.0f) turn = TWO_PI - turn;
    float along = way[1] * cosf(-turn) + way[0] * sinf(-turn);
    float up = along == 0.0f ? PI / 2.0f : atanf(fabsf(way[2] / along));
    if(0.0f < along) up = PI - up;
    if(way[2] < 0.0f) up = TWO_PI - up;
    *heading = (int)(turn * 1024.0f / PI) & 0x7FF;
    *climb = (int)(up * 1024.0f / PI) & 0x7FF;
}

/* the ANIMATIONS rows' names: the words that begin with s between the section's name and its END_LIST (the numbers come from s2_dtr_words) */
static int row_names(const uint8_t *t, uint32_t size, char names[][12])
{
    uint32_t p = 0;
    int n = 0, in = 0;
    while(p < size) {
        uint32_t e = p, a;
        while(e < size && t[e] != '\n') e++;
        a = p;
        p = e < size ? e + 1 : e;
        while(a < e && is_space(t[a])) a++;
        if(!in) {
            uint32_t z = e;
            while(z > a && is_space(t[z - 1])) z--;
            in = z - a == 10 && !memcmp(t + a, "ANIMATIONS", 10);
            continue;
        }
        if(a >= e || t[a] == '*') continue;
        uint32_t w = a;
        while(a < e && !is_space(t[a])) a++;
        if(a - w == 8 && !memcmp(t + w, "END_LIST", 8)) break;
        if(t[w] == 's' && a - w >= 2 && n < S2ANIM_ROWS) {
            uint32_t len = a - w - 1;
            if(len > 11) len = 11;
            memcpy(names[n], t + w + 1, len);
            names[n++][len] = 0;
        }
    }
    return n;
}

/* a row's mesh <NAME>.3DX out of the track's graphics container into the meshes' memory: its index, -1 if it is not there */
static int mesh_of(const uint8_t *gfx, uint32_t gfx_size, const char *name, int32_t misc_delta, uint32_t *used)
{
    for(int k = 0; k < nmeshes; k++)
        if(!strcmp(meshes[k].name, name)) return k;
    char file[20];
    uint32_t len = (uint32_t)strlen(name);
    struct dat_entry e;
    if(nmeshes >= S2ANIM_MESHES || len > 11) return -1;
    memcpy(file, name, len);
    memcpy(file + len, ".3DX", 5);
    for(uint32_t i = 0; i < len; i++)
        if(file[i] >= 'a' && file[i] <= 'z') file[i] = (char)(file[i] - 32);
    if(!dat_find(gfx, gfx_size, file, &e) || e.size < 0x100 || *used + ((e.size + 3) & ~3u) > MESH_BYTES) return -1;
    if(dat_extract(gfx, gfx_size, &e, mesh_mem + *used, MESH_BYTES - *used) != e.size) return -1;
    if(!misc_delta || !s2_faces_onto(mesh_mem + *used, e.size, misc_delta)) return -1;     /* its faces onto the image's copy of MISC.MLB's records */
    struct mesh *m = &meshes[nmeshes];
    m->file = mesh_mem + *used;
    m->radius = rd32(m->file);
    m->key = track_class_key(m->file);
    memcpy(m->name, name, len + 1);
    *used += (e.size + 3) & ~3u;
    return nmeshes++;
}

int s2anim_load(int track, const uint8_t *gfx, uint32_t gfx_size, const uint8_t *data, uint32_t data_size, int32_t misc_delta, uint8_t *scratch, uint32_t cap)
{
    char file[16] = "MAP0DAT.DTR";
    struct dat_entry e;
    uint32_t w[7 * S2ANIM_ROWS], used = 0;
    char names[S2ANIM_ROWS][12];
    nrows = nmeshes = 0;
    memset(paths, 0, sizeof paths);
    if(!gfx || !data) return 0;                 /* (the simulation takes a finished image: no containers) */
    file[3] = (char)('0' + track);
    if(!dat_find(data, data_size, file, &e) || e.size > cap) return 0;
    uint32_t n = dat_extract(data, data_size, &e, scratch, cap);
    if(!n) return 0;
    int count = row_names(scratch, n, names);
    int words = s2_dtr_words(scratch, n, "ANIMATIONS", w, 7 * S2ANIM_ROWS);
    if(count < 1 || words != 7 * count) return 0;
    memcpy(file + 4, ".PTH", 5);
    if(!dat_find(data, data_size, file, &e) || e.size > cap) return 0;
    n = dat_extract(data, data_size, &e, scratch, cap);
    if(!n || !load_paths(scratch, n)) return 0;
    for(int i = 0; i < count; i++) {
        struct row *r = &rows[i];
        union { uint32_t u; float f; } v;
        memset(r, 0, sizeof *r);
        memcpy(r->name, names[i], 12);
        v.u = w[7 * i];
        r->time = v.f;
        v.u = w[7 * i + 1];
        r->start = v.f;
        v.u = w[7 * i + 2];
        r->range = v.f;
        r->flags = (int)w[7 * i + 3];
        r->path = (int8_t)(w[7 * i + 4] < S2ANIM_PATHS && paths[w[7 * i + 4]].ok ? (int)w[7 * i + 4] : -1);
        r->lap = (int)w[7 * i + 5];
        r->sample = (int)w[7 * i + 6];
        r->mesh = -1;
        if(r->name[0] != '@') {
            r->mesh = (int8_t)mesh_of(gfx, gfx_size, r->name, misc_delta, &used);
            if(r->mesh < 0) r->path = -1;           /* an object that cannot be drawn is not run */
        }
    }
    nrows = count;
    return nrows;
}

void s2anim_begin(void)
{
    for(int i = 0; i < nrows; i++) {
        struct row *r = &rows[i];
        float place[3] = { 0.0f, 0.0f, 0.0f };
        memset(&r->st, 0, sizeof r->st);
        r->st.part = -1.0f;
        r->st.started = (r->flags & 1) ? 0.0f : r->start;       /* 0x31060: a row that starts by the player's coming near is not started */
        if(r->path < 0) continue;
        if(r->name[0] == '@')
            for(int k = 0; k < 3; k++) place[k] = paths[r->path].c[k].n ? pool[paths[r->path].c[k].first].v : 0.0f;
        else
            path_at(&paths[r->path], 0.0f, place, 0);
        for(int k = 0; k < 3; k++) r->st.place[k] = (int32_t)place[k];
    }
}

static inline int iabs(int v) { return v < 0 ? -v : v; }

void s2anim_frame(float clock, int px, int py, int lap)
{
    for(int i = 0; i < nrows; i++) {
        struct row *r = &rows[i];
        struct s2anim_row *s = &r->st;
        if(r->path < 0) continue;
        const int own = r->name[0] != '@', dx = iabs(px - s->place[0]), dy = iabs(py - s->place[1]);
        int shown = 0;
        /* 0x2cee4: alive within 52,000 of the player both ways, or an object with neither of the flags 1 and 2; and in the lap it belongs to */
        if(((dx < 52000 && dy < 52000) || (!(r->flags & 3) && own)) && (lap + 1 == r->lap || r->lap == 0)) {
            if(!own) {
                shown = 1;
            } else {
                float range = r->range == -1.0f ? REACH : r->range, f, part;
                if((float)dx < range && (float)dy < range && (r->flags & 1) && s->started == 0.0f) s->started = clock;
                f = (clock - s->started) / r->time;
                if(f < 0.0f || ((r->flags & 1) && s->started == 0.0f)) {
                    part = -1.0f;
                } else if(f > 1.0f && (r->flags & 2)) {
                    part = -1.0f;                                       /* once only, and it is over */
                } else if(f > 1.0f && (r->flags & 1)) {
                    s->started = 0.0f;                                  /* it waits to be set off again */
                    part = 0.0f;
                } else {
                    part = f - (float)(int)f;                           /* round and round */
                }
                if(part != -1.0f) {
                    float place[3];
                    path_at(&paths[r->path], part, place, 0);
                    for(int k = 0; k < 3; k++) s->place[k] = (int32_t)place[k];
                    shown = 1;
                }
                s->part = part;
            }
        } else if(own && (r->flags & 1) && !(r->flags & 2) && s->started != 0.0f) {
            s->started = 0.0f;
        }
        s->shown = shown && own;
        s->heard = shown;
    }
}

/* the blocks of a row's object, made when the drawing comes to it: its place, its heading and climb from the path's way (0x2cee4: roll 0), or, a row that faces the screen,
   turned to the camera (the engine's billboard, 0x4bca0: the camera's heading less a half turn, its pitch, its roll the other way) */
static int make(int id, const struct track_cam *cam, struct track_object *ob)
{
    static struct track_block blocks[CAR_BLOCKS];
    const struct row *r = &rows[id];
    struct car_look lk = { ob->x, ob->y, ob->h, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, track_phase() };
    if(r->flags & 4) {
        lk.yaw = cam->yaw - PI;
        lk.pitch = cam->pitch;
        lk.roll = -cam->roll;
    } else {
        int heading, climb;
        s2anim_angles(id, r->st.part, &heading, &climb);
        lk.yaw = (float)heading * (TWO_PI / 2048.0f);
        lk.pitch = PITCH_SIGN * (float)climb * (TWO_PI / 2048.0f);      /* (the object's pitch is the game's negated, car.h) */
    }
    ob->n = car_mesh_draw(meshes[r->mesh].file, &lk, cam, blocks);
    ob->b = blocks;
    return ob->n;
}

int s2anim_objects(struct track_object *objs, int max)
{
    int n = 0;
    for(int i = 0; i < nrows && n < max; i++) {
        const struct row *r = &rows[i];
        if(!r->st.shown) continue;
        objs[n++] = (struct track_object){ (float)r->st.place[0], (float)r->st.place[1], (float)r->st.place[2], (float)meshes[r->mesh].radius, 0, 0, make, i,
                                           meshes[r->mesh].key, 0, 0 };
    }
    return n;
}

int s2anim_rows(void)
{
    return nrows;
}

int s2anim_heard(int i, int *x, int *y, int *sample)
{
    if(i < 0 || i >= nrows || !rows[i].st.heard || !rows[i].sample) return 0;
    *x = rows[i].st.place[0];
    *y = rows[i].st.place[1];
    *sample = rows[i].sample;
    return 1;
}

const struct s2anim_row *s2anim_row(int i)
{
    return &rows[i].st;
}
