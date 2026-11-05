/*
 * The track maps (s2trackmap.h)
 */
#include "s2trackmap.h"
#include "s2scene.h"
#include "track.h"
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

/* the middle of the n vertices at verts */
static void middle(const uint8_t *verts, int n, float c[3])
{
    double s[3] = { 0, 0, 0 };
    for (int i = 0; i < n; i++) {
        const uint8_t *v = verts + 6 * i;
        for (int k = 0; k < 3; k++) s[k] += rd16(v + 2 * k);
    }
    for (int k = 0; k < 3; k++) c[k] = n ? (float)(s[k] / n) : 0.0f;
}

/* The pieces of one face row of a block with more than max_vert vertices: the row's faces in its order, taken in turn while the vertices they use stay within max_vert; each run
   is a block of its own (header, the faces, their vertices and texture coordinates compacted, all 16 rows the run's one list). Drawn one after the other they paint the faces in
   the order of the block's row. Returns how many pieces (0: the block has no such row, nothing is drawn; -1: they do not fit), their blocks in out[]; *at is the next free byte of dst. */
static int cut_row(const uint8_t *mesh, const uint8_t *blk, int row, int max_vert, uint8_t *dst, uint32_t room, uint32_t *at, const uint8_t *out[S2MAP_CHUNKS])
{
    const int nv = rd32(blk + 0x10);
    const uint8_t *verts = mesh + rd32(blk + 0x1C), *uvs = mesh + rd32(blk + 0x24);
    const int32_t ro = rd32(blk + 0x28 + 4 * row);
    if (ro <= 0) return 0;
    static uint16_t fo[FACE_MAX];
    static uint8_t fgroup[FACE_MAX];
    static int16_t vnew[VERT_MAX];
    static int list[VERT_MAX];
    int nf = 0;
    for (const uint8_t *e = blk + ro; ru16(e) != 0xFFFF; e += 2) {
        if (nf >= FACE_MAX) return -1;
        fo[nf++] = ru16(e);
    }
    if (nf == 0) return 0;

    /* the runs: faces in the row's order while the vertices they use stay within max_vert */
    int groups = 1, count = 0;
    memset(vnew, -1, sizeof vnew);
    for (int i = 0; i < nf; i++) {
        const uint8_t *fc = blk + fo[i];
        const int nc = rd16(fc + 6);
        if (nc < 3 || nc > 4) {
            fgroup[i] = (uint8_t)(groups - 1);
            continue;
        }
        int fresh = 0, seen[4];
        for (int j = 0; j < nc; j++) {
            seen[j] = ru16(fc + 8 + 2 * j) / 6;
            if (seen[j] >= nv) return -1;
            int again = 0;
            for (int q = 0; q < j; q++) again |= seen[q] == seen[j];
            fresh += vnew[seen[j]] < 0 && !again;
        }
        if (count + fresh > max_vert) {
            if (groups >= S2MAP_CHUNKS) return -1;
            groups++;
            count = 0;
            memset(vnew, -1, sizeof vnew);
        }
        for (int j = 0; j < nc; j++)
            if (vnew[seen[j]] < 0) vnew[seen[j]] = (int16_t)count++;
        fgroup[i] = (uint8_t)(groups - 1);
    }

    /* each run a block of its own */
    for (int g = 0; g < groups; g++) {
        int faces = 0, nvg = 0;
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
        const uint32_t list_at = HEAD, faces_at = (list_at + 2u * (uint32_t)(faces + 1) + 3u) & ~3u;
        const uint32_t verts_at = (faces_at + FACE_SIZE * (uint32_t)faces + 3u) & ~3u, uvs_at = verts_at + 6u * (uint32_t)nvg, end = (uvs_at + 6u * (uint32_t)nvg + 3u) & ~3u;
        if (faces_at + FACE_SIZE * (uint32_t)faces > 0xFFFF || *at + end > room) return -1;
        uint8_t *b = dst + *at;
        memset(b, 0, end);
        memcpy(b, blk, HEAD);
        wr32(b + 0x0C, 0);                                      /* (the place is the caller's) */
        wr32(b + 0x10, nvg);
        wr32(b + 0x14, faces);
        wr32(b + 0x1C, (int32_t)verts_at);
        wr32(b + 0x20, -1);
        wr32(b + 0x24, (int32_t)uvs_at);
        for (int q = 0; q < ROWS; q++) wr32(b + 0x28 + 4 * q, (int32_t)list_at);        /* (whichever row the eye picks, this is the run) */
        for (int i = 0; i < nvg; i++) {
            memcpy(b + verts_at + 6 * i, verts + 6 * list[i], 6);
            memcpy(b + uvs_at + 6 * i, uvs + 6 * list[i], 6);
        }
        int nth = 0;
        for (int i = 0; i < nf; i++) {
            if (fgroup[i] != g) continue;
            uint8_t *dfc = b + faces_at + FACE_SIZE * (uint32_t)nth;
            memcpy(dfc, blk + fo[i], FACE_SIZE);
            const int nc = rd16(dfc + 6);
            if (nc >= 3 && nc <= 4)
                for (int j = 0; j < nc; j++) wr16(dfc + 8 + 2 * j, (uint32_t)vnew[ru16(blk + fo[i] + 8 + 2 * j) / 6] * 6u);
            wr16(b + list_at + 2 * nth, faces_at + FACE_SIZE * (uint32_t)nth);
            nth++;
        }
        wr16(b + list_at + 2 * nth, 0xFFFF);
        out[g] = b;
        *at += end;
    }
    return groups;
}

int s2map_cut(const uint8_t *mesh, int k, int max_vert, uint8_t *dst, uint32_t room, uint32_t *used, struct s2map_block *out)
{
    const uint8_t *blk = mesh + rd32(mesh + 0x18 + 4 * k);
    const int nv = rd32(blk + 0x10);
    if (nv < 1 || nv > VERT_MAX) return 0;
    memset(out, 0, sizeof *out);
    out->file = mesh;
    out->blk = blk;
    place_of(mesh, blk, out->t);
    middle(mesh + rd32(blk + 0x1C), nv, out->c);
    if (nv <= max_vert) return 1;
    if (rd32(blk + 0x18) & 0x2000) return 0;                    /* (a block with normals of its own, the reflecting kind: not cut; no map has one) */
    out->cut = 1;
    uint32_t at = used ? *used : 0;
    for (int r = 0; r < S2MAP_ROWS; r++) {
        const int n = cut_row(mesh, blk, r, max_vert, dst, room, &at, out->chunk[r]);
        if (n < 0) return 0;
        out->nchunks[r] = (uint8_t)n;
    }
    if (used) *used = at;
    return 1;
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
        if (blocks < 1 || blocks > S2MAP_BLOCKS) return 0;
        for (int k = 0; k < blocks; k++) {
            if (!s2map_cut(mesh[i], k, GU_MAX_VERT, to, room, &used, &m->b[m->n])) {
                dlog_str("map: a block could not be cut\n");
                return 0;
            }
            cut += m->b[m->n].cut;
            m->n++;
        }
        used = (used + 255u) & ~255u;
    }
    M.ok = 1;
    dlog_str("map: MAPS1.DAT in ");
    dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
    dlog_str(" ms, ");
    dlog_dec(cut);
    dlog_str(" blocks cut, ");
    dlog_dec((int32_t)(used >> 10));
    dlog_str(" KB of pieces\n");
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
    for (int i = 0; i < m->n; i++) {
        const struct s2map_block *p = &m->b[i];
        const float *t = p->t;
        const float o[3] = { x + r[0] * t[0] + r[1] * t[1] + r[2] * t[2], z + r[3] * t[0] + r[4] * t[1] + r[5] * t[2], y + r[6] * t[0] + r[7] * t[1] + r[8] * t[2] };
        const float cx = t[0] + p->c[0], cy = t[1] + p->c[1], cz = t[2] + p->c[2];
        const float d = s2scene_dist2(cam, x + r[0] * cx + r[1] * cy + r[2] * cz, y + r[6] * cx + r[7] * cy + r[8] * cz, z + r[3] * cx + r[4] * cy + r[5] * cz);
        const struct track_block b = { p->file, p->blk, { r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8] }, { o[0], o[1], o[2] }, 0 };
        int row = 0, count = 1;
        if (p->cut) {                                           /* the pieces of the face row the eye picks for the block, in the row's order */
            struct gu_piece g;
            track_block_piece(cam, &b, &g);
            row = g.row & (S2MAP_ROWS - 1);                     /* (a row of an eye below the block's plane, 8 .. 15, is not cut: the maps are seen from above) */
            count = p->nchunks[row];
        }
        for (int q = 0; q < count && n < S2MAP_PIECES; q++) {
            struct track_block c = b;
            if (p->cut) c.file = c.blk = p->chunk[row][q];
            int j = n++;
            for (; j > 0 && key[j - 1] < d; j--) {              /* far first; the pieces of a block (one key) stay in their order */
                out[j] = out[j - 1];
                key[j] = key[j - 1];
            }
            out[j] = c;
            key[j] = d;
        }
    }
    return n;
}
