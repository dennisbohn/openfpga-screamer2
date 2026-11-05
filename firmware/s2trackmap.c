/*
 * The track maps (s2trackmap.h)
 */
#include "s2trackmap.h"
#include "s2scene.h"
#include "geom.h"
#include "dlog.h"
#include "hw.h"
#include <math.h>
#include <string.h>

#define FACE_MAX    1024                        /* faces of a block (GU_MAX_FACES) */
#define VERT_MAX    2048
#define ROWS        16
#define HEAD        0x80u                       /* the block header copied into a piece */
#define FACE_SIZE   36u

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static uint16_t ru16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int16_t rd16(const uint8_t *p) { return (int16_t)ru16(p); }
static void wr32(uint8_t *p, int32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)((uint32_t)v >> (8 * i)); }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* the block's place in the mesh's frame: its position record (block +0x0C: offsets from the mesh, +0x10 .. +0x18 the place) */
static void place_of(const uint8_t *mesh, const uint8_t *blk, float t[3])
{
    const int32_t so = rd32(blk + 0x0C);
    for (int i = 0; i < 3; i++) t[i] = so > 0 ? (float)rd32(mesh + so + 0x10 + 4 * i) : 0.0f;
}

/* the middle of vertices (indices list, or all of them when list is 0) */
static void middle(const uint8_t *verts, const int *list, int n, float c[3])
{
    double s[3] = { 0, 0, 0 };
    for (int i = 0; i < n; i++) {
        const uint8_t *v = verts + 6 * (list ? list[i] : i);
        for (int k = 0; k < 3; k++) s[k] += rd16(v + 2 * k);
    }
    for (int k = 0; k < 3; k++) c[k] = n ? (float)(s[k] / n) : 0.0f;
}

int s2map_cut(const uint8_t *mesh, int k, int max_vert, uint8_t *dst, uint32_t room, uint32_t *used, struct s2map_piece out[], int cap)
{
    const uint8_t *blk = mesh + rd32(mesh + 0x18 + 4 * k);
    const int nv = rd32(blk + 0x10);
    const uint8_t *verts = mesh + rd32(blk + 0x1C), *uvs = mesh + rd32(blk + 0x24);
    if (nv < 1 || nv > VERT_MAX || cap < 1) return 0;
    struct s2map_piece whole = { mesh, blk, { 0 }, { 0 } };
    place_of(mesh, blk, whole.t);
    if (nv <= max_vert) {
        middle(verts, 0, nv, whole.c);
        out[0] = whole;
        return 1;
    }
    if (rd32(blk + 0x18) & 0x2000) return 0;                    /* (a block with normals of its own, the reflecting kind: not cut; no map has one) */

    /* the faces, in the order they are stored: the first row lists them all */
    static uint16_t fo[FACE_MAX];
    static uint8_t fgroup[FACE_MAX];
    static int16_t vnew[VERT_MAX];
    int nf = 0;
    const int32_t r0 = rd32(blk + 0x28);
    if (r0 <= 0) return 0;
    for (const uint8_t *e = blk + r0; ru16(e) != 0xFFFF; e += 2) {
        if (nf >= FACE_MAX) return 0;
        fo[nf++] = ru16(e);
    }
    for (int i = 1; i < nf; i++) {                              /* (insertion sort by offset) */
        const uint16_t x = fo[i];
        int j = i;
        for (; j > 0 && fo[j - 1] > x; j--) fo[j] = fo[j - 1];
        fo[j] = x;
    }
    /* the groups: faces in turn while the vertices they use stay within max_vert */
    int groups = 1, count = 0;
    memset(vnew, -1, sizeof vnew);
    for (int i = 0; i < nf; i++) {
        const uint8_t *fc = blk + fo[i];
        const int nc = rd16(fc + 6);
        if (nc < 3 || nc > 4) { fgroup[i] = (uint8_t)(groups - 1); continue; }
        int fresh = 0, seen[4];
        for (int j = 0; j < nc; j++) {
            seen[j] = ru16(fc + 8 + 2 * j) / 6;
            if (seen[j] >= nv) return 0;
            int again = 0;
            for (int q = 0; q < j; q++) again |= seen[q] == seen[j];
            fresh += vnew[seen[j]] < 0 && !again;
        }
        if (count + fresh > max_vert) {
            if (groups >= cap) return 0;
            groups++;
            count = 0;
            memset(vnew, -1, sizeof vnew);
            fresh = 0;
            for (int j = 0; j < nc; j++) {
                int again = 0;
                for (int q = 0; q < j; q++) again |= seen[q] == seen[j];
                fresh += !again;
            }
        }
        for (int j = 0; j < nc; j++)
            if (vnew[seen[j]] < 0) vnew[seen[j]] = (int16_t)count++;
        fgroup[i] = (uint8_t)(groups - 1);
    }

    /* each group a block of its own */
    uint32_t at = (uint32_t)(used ? *used : 0);
    for (int g = 0; g < groups; g++) {
        int faces = 0, nvg = 0;
        static int list[VERT_MAX];
        memset(vnew, -1, sizeof vnew);
        for (int i = 0; i < nf; i++) {
            if (fgroup[i] != g) continue;
            faces++;
            const uint8_t *fc = blk + fo[i];
            const int nc = rd16(fc + 6);
            if (nc < 3 || nc > 4) continue;
            for (int j = 0; j < nc; j++) {
                const int v = ru16(fc + 8 + 2 * j) / 6;
                if (vnew[v] < 0) {
                    vnew[v] = (int16_t)nvg;
                    list[nvg++] = v;
                }
            }
        }
        const uint32_t rows_at = HEAD, faces_at = rows_at + (uint32_t)ROWS * 2u * (uint32_t)(faces + 1);
        const uint32_t verts_at = (faces_at + FACE_SIZE * (uint32_t)faces + 3u) & ~3u, uvs_at = verts_at + 6u * (uint32_t)nvg, end = (uvs_at + 6u * (uint32_t)nvg + 3u) & ~3u;
        if (faces_at + FACE_SIZE * (uint32_t)faces > 0xFFFF || at + end > room) return 0;
        uint8_t *b = dst + at;
        memset(b, 0, end);
        memcpy(b, blk, HEAD);
        wr32(b + 0x0C, 0);                                      /* (the place is the caller's) */
        wr32(b + 0x10, nvg);
        wr32(b + 0x14, faces);
        wr32(b + 0x1C, (int32_t)verts_at);
        wr32(b + 0x20, -1);
        wr32(b + 0x24, (int32_t)uvs_at);
        for (int i = 0; i < nvg; i++) {
            memcpy(b + verts_at + 6 * i, verts + 6 * list[i], 6);
            memcpy(b + uvs_at + 6 * i, uvs + 6 * list[i], 6);
        }
        /* the faces in the order of the file, with the corners renumbered; where each went (by the old offset) for the rows */
        static uint16_t newfo[FACE_MAX];
        int nth = 0;
        for (int i = 0; i < nf; i++) {
            newfo[i] = 0;
            if (fgroup[i] != g) continue;
            uint8_t *dfc = b + faces_at + FACE_SIZE * (uint32_t)nth;
            memcpy(dfc, blk + fo[i], FACE_SIZE);
            const int nc = rd16(dfc + 6);
            if (nc >= 3 && nc <= 4)
                for (int j = 0; j < nc; j++) wr16(dfc + 8 + 2 * j, (uint32_t)vnew[ru16(blk + fo[i] + 8 + 2 * j) / 6] * 6u);
            newfo[i] = (uint16_t)(faces_at + FACE_SIZE * (uint32_t)nth);
            nth++;
        }
        /* the rows: the original's order, the faces of this group only */
        uint32_t ra = rows_at;
        for (int r = 0; r < ROWS; r++) {
            const int32_t ro = rd32(blk + 0x28 + 4 * r);
            if (ro <= 0) {
                wr32(b + 0x28 + 4 * r, 0);
                continue;
            }
            wr32(b + 0x28 + 4 * r, (int32_t)ra);
            for (const uint8_t *e = blk + ro; ru16(e) != 0xFFFF; e += 2) {
                int lo = 0, hi = nf - 1;                        /* (binary search of the old offset) */
                while (lo < hi) {
                    const int mid = (lo + hi) / 2;
                    if (fo[mid] < ru16(e)) lo = mid + 1;
                    else hi = mid;
                }
                if (fo[lo] == ru16(e) && fgroup[lo] == g) {
                    wr16(b + ra, newfo[lo]);
                    ra += 2;
                }
            }
            wr16(b + ra, 0xFFFF);
            ra += 2;
        }
        if (ra > faces_at) return 0;
        out[g] = (struct s2map_piece){ b, b, { whole.t[0], whole.t[1], whole.t[2] }, { 0 } };
        middle(b + verts_at, 0, nvg, out[g].c);
        at += end;
    }
    if (used) *used = at;
    return groups;
}

/* ------------------------------------------------------------------------------------------------ the maps */

static struct {
    int ok;
    const uint8_t *mlb;
    struct s2map map[S2MAP_TRACKS];
} M;

#define MODELS_PAD(n)   (((n) + 255u) & ~255u)

int s2map_load(struct zip *z)
{
    static struct s2_cont c;
    uint8_t *to = s2scene_models();
    uint32_t room = S3D_MODELS_MAX, n;
    const uint32_t t0 = rdcycle();
    M.ok = 0;
    memset(M.map, 0, sizeof M.map);
    if (!s2scene_open(z, &c, "GFX/MAPS1.DAT")) {
        dlog_str("map: GFX/MAPS1.DAT is not in the ZIP\n");
        return 0;
    }
    M.mlb = to;
    if (!(n = s2scene_entry(&c, "MAPS.MLB", to, room))) {
        dlog_str("map: MAPS.MLB could not be read\n");
        return 0;
    }
    to += MODELS_PAD(n);
    room -= MODELS_PAD(n);
    const uint8_t *mesh[S2MAP_TRACKS];
    for (int i = 0; i < S2MAP_TRACKS; i++) {
        char name[16] = "AMAP1-VS.3DX";
        name[4] = (char)('1' + i);
        mesh[i] = to;
        if (!(n = s2scene_entry(&c, name, to, room)) || n < 0x100) {
            dlog_str("map: ");
            dlog_str(name);
            dlog_str(" could not be read\n");
            return 0;
        }
        to += MODELS_PAD(n);
        room -= MODELS_PAD(n);
    }
    uint32_t used = 0;
    int cut = 0;
    for (int i = 0; i < S2MAP_TRACKS; i++) {
        const int blocks = rd32(mesh[i] + 0x14);
        struct s2map *m = &M.map[i];
        if (blocks < 1 || blocks > 4) return 0;
        for (int k = 0; k < blocks; k++) {
            const int got = s2map_cut(mesh[i], k, GU_MAX_VERT, to, room, &used, m->p + m->n, S2MAP_PIECES - m->n);
            if (!got) {
                dlog_str("map: a block could not be cut\n");
                return 0;
            }
            cut += got > 1;
            m->n += got;
        }
        used = (used + 255u) & ~255u;
    }
    M.ok = 1;
    dlog_str("map: MAPS1.DAT in ");
    dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
    dlog_str(" ms, ");
    dlog_dec(cut);
    dlog_str(" blocks cut\n");
    return 1;
}

const struct s2map *s2map_get(int track)
{
    return M.ok && track >= 1 && track <= S2MAP_TRACKS ? &M.map[track - 1] : 0;
}

const uint8_t *s2map_mlb(void)
{
    return M.ok ? M.mlb : 0;
}

int s2map_blocks(int track, const struct track_cam *cam, float x, float y, float z, int a1, struct track_block out[S2MAP_PIECES], float key[S2MAP_PIECES])
{
    const struct s2map *m = s2map_get(track);
    if (!m) return 0;
    const float ch = cosf(s2scene_rad(a1)), sh = sinf(s2scene_rad(a1));
    const float r[9] = { ch, 0.0f, -sh, sh, 0.0f, ch, 0.0f, 1.0f, 0.0f };      /* the matrix of s2scene_object() for no turn about x */
    int n = 0;
    for (int i = 0; i < m->n && n < S2MAP_PIECES; i++) {
        const struct s2map_piece *p = &m->p[i];
        const float *t = p->t;
        const float o[3] = { x + r[0] * t[0] + r[1] * t[1] + r[2] * t[2], z + r[3] * t[0] + r[4] * t[1] + r[5] * t[2], y + r[6] * t[0] + r[7] * t[1] + r[8] * t[2] };
        const float cx = t[0] + p->c[0], cy = t[1] + p->c[1], cz = t[2] + p->c[2];
        const float d = s2scene_dist2(cam, x + r[0] * cx + r[1] * cy + r[2] * cz, y + r[6] * cx + r[7] * cy + r[8] * cz, z + r[3] * cx + r[4] * cy + r[5] * cz);
        struct track_block b = { p->file, p->blk, { r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8] }, { o[0], o[1], o[2] }, 0 };
        int j = n++;
        for (; j > 0 && key[j - 1] < d; j--) {                  /* far first */
            out[j] = out[j - 1];
            key[j] = key[j - 1];
        }
        out[j] = b;
        key[j] = d;
    }
    return n;
}
