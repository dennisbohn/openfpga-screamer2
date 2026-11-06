/*
 * WELCOME's flying plates (s2plates.h)
 *
 * Read from STARTL.EXE (tools/dis.sh -f), held against the running original (tools/s2/dosbox_desk.py welcome records the plates' records 0x131290 + 0x4c i and the engine's object
 * records every frame; tools/plates.sh). The page 0x39569 makes one plate for each race of the league (0x35b9e(i, record)) and every frame, in this order: the picture, then for
 * each plate the object put into the engine's list (0x4c18a) and the plate's frame (0x35cc8), the keys, the end test (0x36168), the camera (0, 100, 1300, all angles 0) and the
 * engine's list drawn alone (0x75570), then the band. A plate's record:
 *     u        its time (float), 1 + 0.6 i at the start, down by dt x 0.55 each frame
 *     the way in (phase 0), the object's place from u (cos and sin of the radians u pi / 2 and u 2 pi), x the aim X = -2000 (-1)^i, y the aim Y = -2500 (-1)^i, z the aim 2000, start 0:
 *         x = (1 - cos(u pi / 2)) X + k u cos(2 pi u),  y = (1 - cos(u pi / 2)) Y + k u sin(2 pi u),  z = (1 - cos(u pi / 2)) 2000,  k = -2200, all cut to whole numbers,
 *         the angles a0 = a1 = 0, a2 = 512 u (the roll: 90 degrees at u = 1, the plate turned on its edge, flat to the viewer at u = 0)
 *     at u < 0.05 (0 at the least) the plate is at the middle (about (0, 0, 0)) and starts its way out: phase 1, u = 1 - u, the place it has is its start (x0, y0, z0), the aim is
 *         x 7000 (even plates) or -6500 (odd), y -1000, z -2000, the swing k = 1500; the angles go on from where they are (a2 about 28, a0 0):
 *         x = x0 - (x0 - X') (1 - sin(u pi / 2)) + k u (1 - cos(2 pi u)),  y likewise with Y' and the same swing,  z = z0 - (z0 - Z') (1 - sin(u pi / 2)),
 *         a2 += 2048 x 0.15 (1 - sin(u pi / 2)),  a0 += 2048 x 0.05 (1 - sin(u pi / 2)) every frame (& 0x7ff); u down by dt x 0.55 as before
 *     at u < 0.05 again the plate is finished (its +0x40 is -1) and stays where it is, off the screen
 * The end (0x36168): every plate but the last finished, and the last on its way out (phase 1) with u < 0.4 (and the page fades to the TRACK page as ENTER does).
 * The plate meshes are `a%cplate` with the track's number: the league's race i is the track s2l_track(i).
 */
#include "s2plates.h"
#include "s2league.h"
#include "dat.h"
#include "dlog.h"
#include "hw.h"
#include <math.h>
#include <string.h>

#define DT          0.02f
#define RATE        0.55                        /* a plate's u goes down by dt x 0.55 a frame (0x1110b2) */
#define SPACING     0.6                         /* plate i starts at u = 1 + 0.6 i (0x11106a) */
#define SETTLED     0.05                        /* at this u a phase is over (0x1110ba) */
#define LAST_OUT    0.4                         /* the last plate's u at which the page ends (0x1110c2) */
#define HALF_PI     1.5707963705062866          /* the program's floats (0x11108a, 0x11108e) */
#define TWO_PI      6.2831854820251465
#define PLATES_SIZE 0x80000u                    /* PLATES.MLB unpacked: 426,280 bytes */
#define TRACKS      7
#ifndef PLATE_ROLL_SIGN
#define PLATE_ROLL_SIGN 1.0f                    /* which way the engine's a2 turns (tested against the running original) */
#endif

static struct s2plate plate[S2PLATES_MAX];
static int n_plates, fresh;
static struct { int32_t aim[3], exit[3]; } route[S2PLATES_MAX];
static const uint8_t *mlb, *mesh[TRACKS];
static uint32_t generation;

static int32_t rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

int s2plates_load(struct zip *z)
{
    static struct s2_cont c;
    const uint32_t t0 = rdcycle();
    uint8_t *to = s2scene_models();
    if (!s2scene_open(z, &c, "GFX/PLATES1.DAT")) {
        dlog_str("plates: GFX/PLATES1.DAT is not in the ZIP\n");
        return 0;
    }
    mlb = to;
    uint32_t n = s2scene_entry(&c, "PLATES.MLB", to, PLATES_SIZE);
    if (!n) {
        dlog_str("plates: PLATES.MLB could not be read\n");
        return 0;
    }
    to += (n + 255) & ~255u;
    for (int k = 0; k < TRACKS; k++) {
        char name[16] = "A1PLATE.3DX";
        name[1] = (char)('1' + k);
        mesh[k] = to;
        if (!(n = s2scene_entry(&c, name, to, 0x2000u)) || n < 0x100) {
            dlog_str("plates: ");
            dlog_str(name);
            dlog_str(" could not be read\n");
            return 0;
        }
        to += (n + 255) & ~255u;
    }
    generation = s2scene_generation();
    dlog_str("plates: PLATES1.DAT in ");
    dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
    dlog_str(" ms\n");
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the flight */

void s2plates_begin(int races)
{
    n_plates = races < 0 ? 0 : races > S2PLATES_MAX ? S2PLATES_MAX : races;
    fresh = 1;
    for (int i = 0; i < n_plates; i++) {
        const int sign = (i & 1) ? 1 : -1;                              /* (-1)^(i + 2) x -2000: 0x35c3b, pow(-1, i + 2) */
        route[i].aim[0] = sign * 2000;
        route[i].aim[1] = sign * 2500;
        route[i].aim[2] = 2000;
        plate[i] = (struct s2plate){ (float)((double)i * SPACING + 1.0), 0, 0, { 0, 0, 0 }, { 0, 0, 0 } };
    }
}

int s2plates_count(void)
{
    return n_plates;
}

const struct s2plate *s2plates_get(int i)
{
    return i >= 0 && i < n_plates ? &plate[i] : 0;
}

/* 0x35cc8 */
static void plate_step(int i)
{
    struct s2plate *p = &plate[i];
    if (p->done) return;
    const double u = (double)p->u;
    if (p->phase == 0) {
        const double w = 1.0 - cos(u * HALF_PI);
        p->pos[0] = (int32_t)(w * (double)route[i].aim[0] + -2200.0 * u * cos(u * TWO_PI));
        p->pos[1] = (int32_t)(w * (double)route[i].aim[1] + -2200.0 * u * sin(u * TWO_PI));
        p->pos[2] = (int32_t)(w * (double)route[i].aim[2]);
        p->a[0] = p->a[1] = 0;
        p->a[2] = (int)(512.0 * u);
    } else {
        const double w = 1.0 - sin(u * HALF_PI), sw = 1500.0 * u * (1.0 - cos(u * TWO_PI));
        p->pos[0] = (int32_t)((double)route[i].aim[0] - ((double)route[i].aim[0] - (double)route[i].exit[0]) * w + sw);
        p->pos[1] = (int32_t)((double)route[i].aim[1] - ((double)route[i].aim[1] - (double)route[i].exit[1]) * w + sw);
        p->pos[2] = (int32_t)((double)route[i].aim[2] - ((double)route[i].aim[2] - (double)route[i].exit[2]) * w);
        p->a[2] = (int)((double)p->a[2] + 2048.0 * 0.15 * w) & 0x7FF;
        p->a[0] = (int)((double)p->a[0] + 2048.0 * 0.05 * w) & 0x7FF;
    }
    p->u = (float)(u - (double)DT * RATE);
    if ((double)p->u >= SETTLED) return;
    if (p->u < 0.0f) p->u = 0.0f;
    if (p->phase != 0) {
        p->done = 1;
        return;
    }
    p->phase = 1;
    p->u = (float)(1.0 - (double)p->u);
    for (int k = 0; k < 3; k++) route[i].aim[k] = p->pos[k];            /* (the place it has becomes the start of the way out: the record's first three words) */
    route[i].exit[0] = (i & 1) ? -6500 : 7000;
    route[i].exit[1] = -1000;
    route[i].exit[2] = -2000;
}

int s2plates_step(void)
{
    for (int i = 0; i < n_plates; i++) plate_step(i);
    for (int i = 0; i + 1 < n_plates; i++)
        if (!plate[i].done) return 0;
    return n_plates > 0 && (double)plate[n_plates - 1].u < LAST_OUT && plate[n_plates - 1].phase != 0;
}

/* ------------------------------------------------------------------------------------------------ the picture */

void s2plates_block(const uint8_t *m, const int32_t pos[3], const int32_t a[3], struct track_block *out)
{
    const float c0 = cosf(s2scene_rad(a[0])), s0 = sinf(s2scene_rad(a[0])), c1 = cosf(s2scene_rad(a[1])), s1 = sinf(s2scene_rad(a[1]));
    const float c2 = cosf(s2scene_rad(a[2])), s2 = PLATE_ROLL_SIGN * sinf(s2scene_rad(a[2]));
    /* the engine's turn of a local vector (x, y up, z): about z by a2, about x by a0, about the vertical by a1; the matrix's rows are the world's x, y (the engine's z) and
       height (the engine's y), its columns the block's x, y up and z */
    float r[3][3];                                      /* r[world axis x, y up, z][local axis] */
    for (int k = 0; k < 3; k++) {
        float v[3] = { k == 0, k == 1, k == 2 };
        float x1 = c2 * v[0] - s2 * v[1], y1 = s2 * v[0] + c2 * v[1], z1 = v[2];
        float y2 = c0 * y1 - s0 * z1, z2 = s0 * y1 + c0 * z1;
        r[0][k] = c1 * x1 - s1 * z2;
        r[1][k] = y2;
        r[2][k] = s1 * x1 + c1 * z2;
    }
    const uint8_t *blk = m + rd32(m + 0x18);
    const int32_t so = rd32(blk + 0x0C);                /* the block's own place in the mesh (a few units) */
    float t[3];
    for (int k = 0; k < 3; k++) t[k] = so > 0 ? (float)rd32(m + so + 0x10 + 4 * k) : 0.0f;
    const float mm[9] = { r[0][0], r[0][1], r[0][2], r[2][0], r[2][1], r[2][2], r[1][0], r[1][1], r[1][2] };
    float o[3] = { (float)pos[0], (float)pos[2], (float)pos[1] };
    for (int k = 0; k < 3; k++)
        for (int j = 0; j < 3; j++) o[k] += mm[3 * k + j] * t[j];
    *out = (struct track_block){ m, blk, { mm[0], mm[1], mm[2], mm[3], mm[4], mm[5], mm[6], mm[7], mm[8] }, { o[0], o[1], o[2] }, 0 };
}

void s2plates_draw(struct blit_list *l)
{
    const int buffer = l->buffer;
    const struct track_cam cam = s2scene_camera(0.0f, 100.0f, 1300.0f, 0.0f);      /* 0x397a3: (0, 100, 1300), the angles 0 */
    int order[S2PLATES_MAX], m = 0;
    float key[S2PLATES_MAX];
    struct track_block b[S2PLATES_MAX];
    for (int i = 0; i < n_plates; i++) {
        if (plate[i].done) continue;                    /* (off the screen: 6,000 units to a side) */
        const float d = s2scene_dist2(&cam, (float)plate[i].pos[0], (float)plate[i].pos[1], (float)plate[i].pos[2]);
        int j = m++;
        for (; j > 0 && key[j - 1] < d; j--) {          /* far first */
            order[j] = order[j - 1];
            key[j] = key[j - 1];
        }
        order[j] = i;
        key[j] = d;
    }
    s2scene_begin(mlb, buffer, fresh || generation != s2scene_generation());
    fresh = 0;
    for (int j = 0; j < m; j++) {
        const struct s2plate *p = &plate[order[j]];
        s2plates_block(mesh[s2l_track(order[j]) - 1], p->pos, p->a, &b[j]);
    }
    s2scene_blocks(&cam, b, m);
    s2scene_end();
    while (RS_DRAW & RS_BUSY)
        ;
    blit_start(l, buffer);
}
