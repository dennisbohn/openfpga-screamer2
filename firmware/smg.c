/*
 * The see-through table in 64 rows (smg.h)
 */
#include "smg.h"
#include <string.h>

static uint32_t ld32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void st32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t pad256(uint32_t n)
{
    return (n + 255) & ~255u;
}

/* the original's five ramps: the first texel, the rows and the bit of each */
static const uint8_t first_texel[5] = { 0, 32, 48, 80, 96 };
static const uint8_t ramp_rows[5] = { 32, 16, 32, 16, 16 };

static int ramp_of(int t)
{
    int k = 4;
    while(k > 0 && t < first_texel[k]) k--;
    return t >= 1 && t <= 111 ? k : -1;
}

int smg_ramps(const int32_t asmoke[3], int race)
{
    if(race && (asmoke[0] >= 0 || asmoke[1] >= 0 || asmoke[2] >= 0)) return SMG_SMOKE | SMG_SHADOW | SMG_SMOKE2 | SMG_BRIGHT | SMG_WHITE;
    return SMG_SHADOW;
}

/* a see-through material: its texture record, the size and the frames' offsets, if all of it lies in the section (size bytes) */
struct mat {
    uint32_t ro, w, h, n;
    uint32_t off[64];
};

static int mat_of(const uint8_t *rec, const uint8_t *mlb, uint32_t size, struct mat *m)
{
    uint32_t routine = ld32(rec), w = ld32(rec + 8), h = ld32(rec + 12);
    int32_t ro = (int32_t)ld32(rec + 4);
    if((routine != 208 && routine != 224) || ro <= 0 || (uint32_t)ro + 12 > size || w < 1 || w > 256 || h < 1 || h > 256) return 0;
    int32_t cnt = (int32_t)ld32(mlb + ro);
    uint32_t n = cnt > 0 ? (uint32_t)cnt & 0xFFFF : 1;
    if(n < 1 || n > 64 || (uint64_t)ro + 8 + 4 * (uint64_t)n > size) return 0;
    for(uint32_t f = 0; f < n; f++) {
        int32_t o = (int32_t)ld32(mlb + ro + 8 + 4 * f);
        if(o < 0 || (uint64_t)o + 256 * (uint64_t)(h - 1) + w > size) return 0;
        m->off[f] = (uint32_t)o;
    }
    m->ro = (uint32_t)ro;
    m->w = w;
    m->h = h;
    m->n = n;
    return 1;
}

/* the rows: every texel the table has, in order, and the merging of the closest pair of a ramp while there are too many */
static void plan(struct smg *s)
{
    uint8_t alive[112], into[112];
    int n = 0;
    for(int t = 1; t < 112; t++) {
        into[t] = (uint8_t)t;
        if((s->seen[t >> 3] >> (t & 7) & 1) && (s->ramps >> ramp_of(t) & 1)) alive[n++] = (uint8_t)t;
    }
    while(n > SMG_FREE) {
        int gap = 1000, at = -1;
        for(int i = 0; i + 1 < n; i++)
            if(ramp_of(alive[i]) == ramp_of(alive[i + 1]) && alive[i + 1] - alive[i] < gap) {
                gap = alive[i + 1] - alive[i];
                at = i;
            }
        if(at < 0) break;                               /* (112 texels in five ramps cannot all be apart) */
        into[alive[at]] = alive[at + 1];
        memmove(alive + at, alive + at + 1, (size_t)(n - at - 1));
        n--;
    }
    memset(s->lut, 0, sizeof s->lut);
    s->rows = n;
    for(int i = 0; i < n; i++) {
        s->alive[i] = alive[i];
        s->lut[alive[i]] = (uint8_t)(1 + i);
    }
    for(int t = 1; t < 112; t++) {                      /* the merged ones take the row their chain ends in */
        int e = t;
        if(!(s->seen[t >> 3] >> (t & 7) & 1) || !(s->ramps >> ramp_of(t) & 1)) continue;
        while(into[e] != e) e = into[e];
        s->lut[t] = s->lut[e];
    }
}

uint32_t smg_copies(struct smg *s, int ramps, uint8_t *front, uint32_t nfront, uint8_t *mlb, uint32_t ntrack, uint32_t size, uint32_t cap)
{
    static struct mat m;
    memset(s, 0, sizeof *s);
    s->ramps = ramps;
    uint8_t *set[2] = { front, mlb };
    const uint32_t count[2] = { nfront, ntrack };
    for(int g = 0; g < 2; g++)                          /* the texels the textures have */
        for(uint32_t k = 0; k < count[g]; k++) {
            if(!mat_of(set[g] + 16 * k, mlb, size, &m)) continue;
            for(uint32_t f = 0; f < m.n; f++)
                for(uint32_t y = 0; y < m.h; y++) {
                    const uint8_t *row = mlb + m.off[f] + 256 * y;
                    for(uint32_t x = 0; x < m.w; x++) s->seen[row[x] >> 3] |= (uint8_t)(1 << (row[x] & 7));
                }
        }
    for(int t = 1; t < 256; t++) s->found += s->seen[t >> 3] >> (t & 7) & 1;
    plan(s);
    uint32_t at = size;
    uint32_t done_ro[64], done_at[64];
    int ndone = 0;
    for(int g = 0; g < 2; g++)
        for(uint32_t k = 0; k < count[g]; k++) {
            uint8_t *rec = set[g] + 16 * k;
            if(!mat_of(rec, mlb, size, &m)) continue;
            int d = 0;
            while(d < ndone && done_ro[d] != m.ro) d++;
            if(d == ndone) {                            /* this texture record's copy: the record (padded), then the frames' bytes from the first to the last */
                uint32_t lo = m.off[0], hi = 0, head = pad256(8 + 4 * m.n);
                for(uint32_t f = 0; f < m.n; f++) {
                    if(m.off[f] < lo) lo = m.off[f];
                    if(m.off[f] + 256 * (m.h - 1) + m.w > hi) hi = m.off[f] + 256 * (m.h - 1) + m.w;
                }
                uint32_t block = head + pad256(hi - lo);
                if(ndone == 64 || at > cap || block > cap - at) return 0;
                uint8_t *to = mlb + at;
                memset(to, 0, block);
                memcpy(to, mlb + m.ro, 8);              /* the frame word and the other word, as they were */
                for(uint32_t f = 0; f < m.n; f++) st32(to + 8 + 4 * f, at + head + m.off[f] - lo);
                const uint8_t *from = mlb + lo;
                uint8_t *dst = to + head;
                for(uint32_t i = 0; i < hi - lo; i++) dst[i] = s->lut[from[i]];
                done_ro[ndone] = m.ro;
                done_at[ndone] = at;
                d = ndone++;
                at += block;
            }
            st32(rec, 224);                             /* (the copy of a see-through texture may follow in the record table it is drawn from: the frames were read above) */
            st32(rec + 4, done_at[d]);
        }
    return at;
}

/* ------------------------------------------------------------------------------------------------ the table */

/* the original's row of texel t: for each of the 256 colours the palette's nearest to what it became, the first of equals (the squared distances of the three components,
   whole numbers: the target scaled by the ramp's rows - 1). The palette sorted by its red value, the nearest looked for outwards from where the colour's red would lie:
   an entry whose red alone is further than the best so far cannot win */
static void make_row(const uint8_t *pal6, int t, const int32_t asmoke[3], const int32_t a2smoke[3], uint8_t *out)
{
    int k = ramp_of(t), l = t - first_texel[k], n = ramp_rows[k] - 1;
    int32_t to[3] = { 0, 0, 0 };
    for(int j = 0; j < 3; j++) {
        if(k == 0) to[j] = asmoke[j] >> 2;
        else if(k == 2) to[j] = a2smoke[j] >> 2;
        else if(k == 4) to[j] = 63;
    }
    uint8_t order[256];
    int start[65] = { 0 };
    for(int i = 0; i < 256; i++) start[pal6[3 * i] + 1]++;
    for(int r = 0; r < 64; r++) start[r + 1] += start[r];
    int fill[64];
    for(int r = 0; r < 64; r++) fill[r] = start[r];
    for(int i = 0; i < 256; i++) order[fill[pal6[3 * i]]++] = (uint8_t)i;
    for(int c = 0; c < 256; c++) {
        int32_t tg[3];
        for(int j = 0; j < 3; j++) {
            int32_t v = pal6[3 * c + j];
            tg[j] = k == 3 ? (v * (15 + l) > 945 ? 945 : v * (15 + l)) : v * n + (to[j] - v) * l;
        }
        int r = tg[0] / n;                              /* (negative targets: the search begins at either end) */
        if(r < 0) r = 0;
        if(r > 63) r = 63;
        int32_t best = 0x7FFFFFFF;
        int bi = 0;
        for(int dir = 0; dir < 2; dir++) {
            for(int p = dir ? start[r] - 1 : start[r]; p >= 0 && p < 256; p += dir ? -1 : 1) {
                int i = order[p];
                int32_t d0 = pal6[3 * i] * n - tg[0];
                if(d0 * d0 > best) break;
                int32_t d1 = pal6[3 * i + 1] * n - tg[1], d2 = pal6[3 * i + 2] * n - tg[2], d = d0 * d0 + d1 * d1 + d2 * d2;
                if(d < best || (d == best && i < bi)) {
                    best = d;
                    bi = i;
                }
            }
        }
        out[c] = (uint8_t)bi;
    }
}

void smg_table(const struct smg *s, const uint8_t *col, const int32_t asmoke[3], const int32_t a2smoke[3], uint8_t *out)
{
    uint8_t pal6[768];
    for(int i = 0; i < 768; i++) pal6[i] = col[i] & 63;      /* (MAPn.COL's values are six bits) */
    memset(out, 0, SMG_TABLE);
    for(int r = 1; r <= s->rows; r++) make_row(pal6, s->alive[r - 1], asmoke, a2smoke, out + 256 * r);
}
