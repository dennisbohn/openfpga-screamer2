/*
 * Geometry unit C model (geom.h). Every >> of a signed value rounds down, as in the
 * hardware. Also the cutting of the blocks that are over the unit's limits (gu_split):
 * the hardware's driver (hw_piece) and the model (gu_draw) both go through it.
 */
#include "geom.h"
#include <string.h>

static int32_t rd32(const uint8_t *p) { return *(const int32_t *)p; }
static int16_t rd16(const uint8_t *p) { return *(const int16_t *)p; }
static uint16_t ru16(const uint8_t *p) { return *(const uint16_t *)p; }
static void wr32(uint8_t *p, int32_t v) { *(int32_t *)p = v; }
static void wr16(uint8_t *p, uint32_t v) { *(uint16_t *)p = (uint16_t)v; }

/* ---------------------------------------------------------------------------------------- blocks over the unit's limits */

#define CUT_ROW_MAX     4096u           /* the longest row, the most vertices of a block the cut takes */
#define CUT_VERT_MAX    4096u
#define CUT_HEAD        0x80u           /* the block header copied into a run */
#define CUT_FACE        36u             /* a face record */

uint32_t gu_cut_failures;

int gu_cut(const uint8_t *mesh, const uint8_t *blk, int row, int max_vert, int max_faces, uint8_t *dst, uint32_t room, const uint8_t *out[], int cap)
{
    const int nv = rd32(blk + 0x10);
    const int32_t ro = rd32(blk + 0x28 + 4 * row);
    if(nv < 1 || nv > (int)CUT_VERT_MAX) return -1;
    if(ro <= 0) return 0;
    if(rd32(blk + 0x18) & 0x2000) return -1;                    /* (normals of its own, the reflecting kind: its reflections are of the whole block; no such block is over the limits) */
    if(cap > 255) cap = 255;                                    /* (a run's number is a byte) */
    const uint8_t *verts = mesh + rd32(blk + 0x1C), *uvs = mesh + rd32(blk + 0x24);
    static uint16_t fo[CUT_ROW_MAX], list[CUT_VERT_MAX];
    static uint8_t fgroup[CUT_ROW_MAX];
    static int16_t vnew[CUT_VERT_MAX];
    unsigned nf = 0;
    for(const uint8_t *e = blk + ro; ru16(e) != 0xFFFF; e += 2) {
        if(nf >= CUT_ROW_MAX) return -1;
        fo[nf++] = ru16(e);
    }
    if(nf == 0) return 0;

    /* the runs: the row's faces in order while their vertices and faces stay within the limits */
    int groups = 1, count = 0, faces_in = 0;
    memset(vnew, -1, sizeof(int16_t) * (size_t)nv);
    for(unsigned i = 0; i < nf; i++) {
        const uint8_t *fc = blk + fo[i];
        const int nc = rd16(fc + 6);
        if(nc < 3 || nc > 4) {                                  /* (the unit skips it, but it takes a place in the row) */
            fgroup[i] = (uint8_t)(groups - 1);
            faces_in++;
            continue;
        }
        int fresh = 0, seen[4];
        for(int j = 0; j < nc; j++) {
            seen[j] = ru16(fc + 8 + 2 * j) / 6;
            if(seen[j] >= nv) return -1;
            int again = 0;
            for(int q = 0; q < j; q++) again |= seen[q] == seen[j];
            fresh += vnew[seen[j]] < 0 && !again;
        }
        if(count + fresh > max_vert || faces_in >= max_faces) {
            if(groups >= cap) return -1;
            groups++;
            count = faces_in = 0;
            memset(vnew, -1, sizeof(int16_t) * (size_t)nv);
        }
        for(int j = 0; j < nc; j++)
            if(vnew[seen[j]] < 0) vnew[seen[j]] = (int16_t)count++;
        fgroup[i] = (uint8_t)(groups - 1);
        faces_in++;
    }

    /* each run a block of its own */
    uint32_t at = 0;
    for(int g = 0; g < groups; g++) {
        unsigned faces = 0, nvg = 0;
        memset(vnew, -1, sizeof(int16_t) * (size_t)nv);
        for(unsigned i = 0; i < nf; i++) {
            if(fgroup[i] != g) continue;
            faces++;
            const uint8_t *fc = blk + fo[i];
            const int nc = rd16(fc + 6);
            if(nc < 3 || nc > 4) continue;
            for(int j = 0; j < nc; j++) {
                const unsigned v = ru16(fc + 8 + 2 * j) / 6u;
                if(vnew[v] < 0) {
                    vnew[v] = (int16_t)nvg;
                    list[nvg++] = (uint16_t)v;
                }
            }
        }
        const uint32_t list_at = CUT_HEAD, faces_at = (list_at + 2u * (faces + 1u) + 3u) & ~3u;
        const uint32_t verts_at = (faces_at + CUT_FACE * faces + 3u) & ~3u, uvs_at = verts_at + 6u * nvg, end = (uvs_at + 6u * nvg + 3u) & ~3u;
        if(faces_at + CUT_FACE * faces > 0xFFFFu || at + end > room) return -1;
        uint8_t *b = dst + at;
        memset(b, 0, end);
        memcpy(b, blk, CUT_HEAD);
        wr32(b + 0x0C, 0);                                      /* (no position record: the caller places the piece) */
        wr32(b + 0x10, (int32_t)nvg);
        wr32(b + 0x14, (int32_t)faces);
        wr32(b + 0x1C, (int32_t)verts_at);
        wr32(b + 0x20, -1);
        wr32(b + 0x24, (int32_t)uvs_at);
        for(int q = 0; q < 16; q++) wr32(b + 0x28 + 4 * q, (int32_t)list_at);   /* (whichever row the eye picks, this is the run) */
        for(unsigned i = 0; i < nvg; i++) {
            memcpy(b + verts_at + 6 * i, verts + 6 * list[i], 6);
            memcpy(b + uvs_at + 6 * i, uvs + 6 * list[i], 6);
        }
        unsigned nth = 0;
        for(unsigned i = 0; i < nf; i++) {
            if(fgroup[i] != g) continue;
            uint8_t *dfc = b + faces_at + CUT_FACE * nth;
            memcpy(dfc, blk + fo[i], CUT_FACE);
            const int nc = rd16(dfc + 6);
            if(nc >= 3 && nc <= 4)
                for(int j = 0; j < nc; j++) wr16(dfc + 8 + 2 * j, (uint32_t)vnew[ru16(blk + fo[i] + 8 + 2 * j) / 6] * 6u);
            wr16(b + list_at + 2 * nth, faces_at + CUT_FACE * nth);
            nth++;
        }
        wr16(b + list_at + 2 * nth, 0xFFFF);
        out[g] = b;
        at += end;
    }
    return groups;
}

static struct {
    const uint8_t *blk;                                         /* 0: free */
    int row, n;                                                 /* the runs of (blk, row): how many, */
    const uint8_t *run[GU_CUT_RUNS];                            /* and where */
    uint32_t age;
} cut_slot[GU_CUT_SLOTS];
static uint8_t cut_pool[GU_CUT_SLOTS][GU_CUT_BYTES] __attribute__((aligned(4)));

int gu_split(const struct gu_piece *g, struct gu_piece out[GU_CUT_RUNS])
{
    static uint32_t tick;
    int s = -1, victim = 0;
    for(int i = 0; i < GU_CUT_SLOTS; i++) {
        if(cut_slot[i].blk == g->blk && cut_slot[i].row == g->row) {
            s = i;
            break;
        }
        if(cut_slot[i].age < cut_slot[victim].age) victim = i;
    }
    if(s < 0) {                                                 /* the first time: cut it into the least recently used slot */
        s = victim;
        cut_slot[s].blk = 0;
        const int n = gu_cut(g->obj, g->blk, g->row & 15, GU_MAX_VERT, GU_MAX_FACES, cut_pool[s], GU_CUT_BYTES, cut_slot[s].run, GU_CUT_RUNS);
        if(n < 0) gu_cut_failures++;
        cut_slot[s].blk = g->blk;
        cut_slot[s].row = g->row;
        cut_slot[s].n = n < 0 ? 0 : n;                          /* (a failure is kept too: it is not tried again every frame) */
    }
    cut_slot[s].age = ++tick;
    for(int i = 0; i < cut_slot[s].n; i++) {
        out[i] = *g;
        out[i].obj = out[i].blk = cut_slot[s].run[i];
    }
    return cut_slot[s].n;
}

static int32_t clamp_size(int32_t v)
{
    return v < 1 ? 1 : v > 256 ? 256 : v;
}

/* routine 160: the reflection's u, v per vertex number (kept from block to block, as the
   original's table at 0xc3bd4 and the hardware's RAM) */
static int16_t refl_u[GU_MAX_VERT], refl_v[GU_MAX_VERT];

#ifdef GU_REFL_FLOAT
/* the original's form (0xc2230: floats, fistp to nearest), to measure the integer one */
#include <math.h>
static void reflection(const struct gu_piece *p, const struct su_fixed *vt, const uint8_t *nrm, int i)
{
    float n[3], e[3], q[3], d = 0.0f, l;
    for(int k = 0; k < 3; k++) q[k] = *(const float *)(nrm + 12 * i + 4 * k);
    for(int k = 0; k < 3; k++)
        n[k] = ((float)p->m[3 * k] * q[0] + (float)p->m[3 * k + 1] * q[1] + (float)p->m[3 * k + 2] * q[2]) / 65536.0f;
    e[0] = (float)vt[i].r; e[1] = (float)vt[i].u; e[2] = (float)vt[i].f - 32768.0f;
    l = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
    for(int k = 0; k < 3; k++) e[k] /= l;
    for(int k = 0; k < 3; k++) d += 2.0f * e[k] * n[k];
    refl_u[i] = (int16_t)(lrintf(127.0f * (e[0] - d * n[0])) + 127 - (int32_t)(p->refl >> 16 & 255));
    refl_v[i] = (int16_t)(lrintf(127.0f * (e[2] - d * n[2])) + 127 - (int32_t)(p->refl >> 24));
}
#else
/* 2^16 / sqrt(x) at x = (i + 0.5) / 32, i = 32..127 (below 32: as 32) */
static const uint16_t rsq[96] = {
    65030, 64052, 63117, 62222, 61363, 60540, 59748, 58987, 58254, 57548, 56867, 56210,
    55574, 54960, 54366, 53791, 53233, 52693, 52169, 51660, 51165, 50685, 50218, 49763,
    49321, 48890, 48470, 48061, 47663, 47273, 46894, 46523, 46161, 45807, 45462, 45124,
    44793, 44470, 44153, 43843, 43540, 43243, 42951, 42666, 42386, 42112, 41843, 41579,
    41320, 41065, 40816, 40571, 40330, 40093, 39861, 39632, 39408, 39187, 38970, 38756,
    38546, 38340, 38136, 37936, 37739, 37545, 37354, 37166, 36980, 36798, 36618, 36441,
    36266, 36093, 35924, 35756, 35591, 35428, 35267, 35109, 34953, 34798, 34646, 34496,
    34347, 34201, 34056, 33913, 33772, 33633, 33496, 33360, 33225, 33093, 32962, 32832,
};

/* a normal component (float bits) rounded to Q14: 0 below 2^-15, |x| < 2 */
static int32_t q14(uint32_t w)
{
    uint32_t e = w >> 23 & 255;
    int32_t q = e >> 4 == 7 ? (int32_t)((((w & 0x7FFFFFu) | 0x800000u) >> (8 + (15 - (e & 15))) & 0xFFFF) + 1) >> 1 : 0;
    return (int16_t)(w >> 31 ? -q : q);         /* (16 bits, as the unit's: never reached by a normal) */
}

/* v's low bits as a signed number of that many bits: a register of the unit */
static int64_t sx(int64_t v, int bits)
{
    uint64_t m = 1ull << (bits - 1);
    return (int64_t)((((uint64_t)v & ((m << 1) - 1)) ^ m) - m);
}

/* vertex i's reflection in integers (geom.h), bit for bit the hardware (its registers' widths) */
static void reflection(const struct gu_piece *p, const struct su_fixed *vt, const uint8_t *nrm, int i)
{
    int64_t q[3], n[3], pv[3], pn[3];
    for(int k = 0; k < 3; k++) q[k] = q14((uint32_t)rd32(nrm + 12 * i + 4 * k));
    for(int k = 0; k < 3; k++)                  /* the normal into view space: Q16, 18 bits */
        n[k] = sx((sx(p->m[3 * k], 18) * q[0] + sx(p->m[3 * k + 1], 18) * q[1] + sx(p->m[3 * k + 2], 18) * q[2]) >> 14, 18);
    pv[0] = vt[i].r; pv[1] = vt[i].u; pv[2] = (int32_t)((uint32_t)vt[i].f - 32768u);
    uint32_t o = 0;
    for(int k = 0; k < 3; k++) o |= (uint32_t)(pv[k] ^ (pv[k] >> 31));
    int l = 0;
    while(o >> l) l++;
    for(int k = 0; k < 3; k++) pn[k] = sx((pv[k] * 65536) >> l, 17);
    int64_t S = sx(pn[0] * n[0], 34) + sx(pn[1] * n[1], 34) + sx(pn[2] * n[2], 34);
    int64_t sh = sx(S >> 16, 18), Q = pn[0] * pn[0] + pn[1] * pn[1] + pn[2] * pn[2];
    int64_t br = sx(pn[0] - (sx(sh * n[0], 34) >> 15), 18), bf = sx(pn[2] - (sx(sh * n[2], 34) >> 15), 18);
    int h = Q < (1ll << 32);
    int64_t qn = h ? Q << 2 : Q;
    int ix = (int)(qn >> 27);
    int64_t x = qn >> 17, y0 = rsq[ix < 32 ? 0 : ix - 32];
    int64_t t1 = (x * y0) >> 16, t2 = (t1 * y0) >> 15;
    int64_t t3 = (3 * 65536 - t2) >> 1;
    int64_t y = sx((y0 * sx(t3 - (t3 >> 7), 18)) >> 16, 18);
    int64_t u = ((sx(br * y, 34) * (1 << h)) + (1 << 24)) >> 25, v = ((sx(bf * y, 34) * (1 << h)) + (1 << 24)) >> 25;
    refl_u[i] = (int16_t)sx(u + 127 - (int64_t)(p->refl >> 16 & 255), 10);
    refl_v[i] = (int16_t)sx(v + 127 - (int64_t)(p->refl >> 24), 10);
}
#endif

uint32_t gu_draw(volatile uint8_t *fb, const struct rs_clip *clip, const struct su_view *v,
                 const uint8_t *mlb, const struct gu_piece *p, struct gu_stats *st)
{
    uint32_t pixels = 0;
    if(gu_oversize(p)) {                        /* over the unit's limits: drawn as the runs of its row, in order (gu_split) */
        struct gu_piece run[GU_CUT_RUNS];
        const int n = gu_split(p, run);
        for(int i = 0; i < n; i++) pixels += gu_draw(fb, clip, v, mlb, &run[i], st);
        return pixels;
    }
    static struct su_fixed vt[GU_MAX_VERT];     /* the vertices in view space */
    const uint8_t *verts = p->obj + rd32(p->blk + 0x1C), *uvs = p->obj + rd32(p->blk + 0x24);
    int32_t nvert = rd32(p->blk + 0x10), ro = rd32(p->blk + 0x28 + 4 * p->row);
    if(nvert < 0 || nvert > GU_MAX_VERT || ro <= 0) return 0;

    for(int i = 0; i < nvert; i++) {
        int64_t x = rd16(verts + 6 * i), y = rd16(verts + 6 * i + 2), z = rd16(verts + 6 * i + 4);
        for(int k = 0; k < 3; k++) {
            int64_t s = x * p->m[3 * k] + y * p->m[3 * k + 1] + z * p->m[3 * k + 2];
            int32_t w = (int32_t)(p->t[k] + (s >> 8));
            if(k == 0) vt[i].r = w;
            else if(k == 1) vt[i].u = w;
            else vt[i].f = w;
        }
    }
    st->vertices += (uint32_t)nvert;
    if(rd32(p->blk + 0x18) & 0x2000) {          /* the reflections, for the vertices with a normal */
        const uint8_t *nrm = p->obj + rd32(p->blk + 0x20);
        int32_t cnt = rd32(nrm);
        for(int i = 0; i < nvert && i < cnt; i++) reflection(p, vt, nrm + 4, i);
    }

    const uint8_t *e = p->blk + ro;
    for(int nf = 0; nf < GU_MAX_FACES; nf++, e += 2) {
        uint16_t fo = ru16(e);
        if(fo == 0xFFFF) break;
        const uint8_t *fc = p->blk + fo;
        int nc = rd16(fc + 6);
        if(nc < 3 || nc > 4) continue;

        /* turned away? */
        const uint8_t *c0 = verts + ru16(fc + 8);
        int64_t bx = ((int64_t)rd16(c0) << 8) - p->e[0], by = ((int64_t)rd16(c0 + 2) << 8) - p->e[1];
        int64_t bz = ((int64_t)rd16(c0 + 4) << 8) - p->e[2];
        if((bx * rd16(fc + 0x14) + by * rd16(fc + 0x18) + bz * rd16(fc + 0x1C) < 0) != (p->mirror != 0)) continue;

        /* the material */
        const uint8_t *mat = mlb + rd16(fc + 4);
        int32_t routine = rd32(mat), tw = 0, th = 0;
        /* 192 (0xcae84, track 5's bridge) takes the face's colour and corners like 128: one
           colour, no texture record */
        int flat = routine == 128 || routine == 192;
        if(routine == 224) st->see_through++;   /* through the see-through table (raster.h) */
        struct rs_texture tex;
        if(!flat) {
            tw = clamp_size(rd32(mat + 8));
            th = clamp_size(rd32(mat + 12));
            /* the frame: n x phase >> 14 of the texture record's n frames (the game's 0xd5980),
               n taken as its low half when it is above 0 */
            const uint8_t *trec = mlb + rd32(mat + 4);
            int32_t n = rd32(trec);
            /* routine 160 (a car's windows) takes its frame from the object's reflection
               picture ([0x84288], record +0x3c), n x phase >> 16, not from the paint job's phase
               with its brake lights ([0x84284]) */
            uint32_t ph = routine == 160 ? p->refl & 0xFFFF : (p->phase & 0x3FFF) << 2;
            uint32_t frame = n > 0 ? ((uint32_t)n & 0xFFFF) * ph >> 16 : 0;
            tex.data = mlb + rd32(trec + 8 + 4 * frame);
            tex.width = tw;
            tex.height = th;
            tex.routine = routine == 64 || routine == 48 ? 64 : routine == 224 ? 224 : routine == 160 ? 160 : 16;  /* 48: as 64 */
        }

        /* the corners */
        struct su_fixed c[4];
        int ok = 1;
        for(int j = 0; j < nc; j++) {
            uint16_t vo = ru16(fc + 8 + 2 * j);
            int vi = vo / 6;
            if(vi >= nvert) ok = 0;
            c[j].r = vt[vi % GU_MAX_VERT].r;
            c[j].u = vt[vi % GU_MAX_VERT].u;
            c[j].f = vt[vi % GU_MAX_VERT].f;
            int32_t s = rd16(uvs + vo) * tw, t = rd16(uvs + vo + 2) * th;
            if(routine == 160) {                /* the reflection, whole texels (0xdb310: << 3) */
                s = refl_u[vi % GU_MAX_VERT] * 256;
                t = refl_v[vi % GU_MAX_VERT] * 256;
            }
            c[j].s = s < -(1 << 23) ? -(1 << 23) : s > (1 << 23) - 1 ? (1 << 23) - 1 : s;
            c[j].t = t < -(1 << 23) ? -(1 << 23) : t > (1 << 23) - 1 ? (1 << 23) - 1 : t;
        }
        if(!ok) continue;
        st->faces++;
        st->triangles += (uint32_t)(nc - 2);
        pixels += su_polygon8(fb, clip, v, c, nc, flat ? 0 : &tex, (uint8_t)(rd16(fc + 2) & 255));
    }
    return pixels;
}
