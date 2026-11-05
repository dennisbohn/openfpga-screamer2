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

static int32_t rd32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

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
    for (int i = 0; i < S2MAP_TRACKS; i++) {
        char name[16] = "AMAP1-VS.3DX";
        name[4] = (char)('1' + i);
        const uint8_t *mesh = to;
        if (!(n = s2scene_entry(&c, name, to, room)) || n < 0x100) {
            dlog_str("map: ");
            dlog_str(name);
            dlog_str(" could not be read\n");
            return 0;
        }
        to += MODELS_PAD(n);
        room -= MODELS_PAD(n);
        const int blocks = rd32(mesh + 0x14);
        struct s2map *m = &M.map[i];
        if (blocks < 1 || blocks > S2MAP_BLOCKS) return 0;
        for (int k = 0; k < blocks; k++) {
            struct s2map_block *b = &m->b[m->n++];
            b->file = mesh;
            b->blk = mesh + rd32(mesh + 0x18 + 4 * k);
            const int32_t so = rd32(b->blk + 0x0C);             /* the block's place in the mesh's frame: its position record (+0x10 .. +0x18 of it) */
            for (int q = 0; q < 3; q++) {
                b->t[q] = so > 0 ? (float)rd32(mesh + so + 0x10 + 4 * q) : 0.0f;
                b->c[q] = (float)rd16(b->blk + 4 + 2 * q);      /* the header's centre (0 in every block of the maps) */
            }
        }
    }
    M.ok = 1;
    dlog_str("map: MAPS1.DAT in ");
    dlog_dec((int32_t)((rdcycle() - t0) / (CPU_HZ / 1000)));
    dlog_str(" ms\n");
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

int s2map_blocks(int track, const struct track_cam *cam, float x, float y, float z, int a1, struct track_block out[S2MAP_BLOCKS], float key[S2MAP_BLOCKS])
{
    const struct s2map *m = s2map_get(track);
    if (!m) return 0;
    const float ch = cosf(s2scene_rad(a1)), sh = sinf(s2scene_rad(a1));
    const float r[9] = { ch, 0.0f, -sh, sh, 0.0f, ch, 0.0f, 1.0f, 0.0f };      /* the matrix of s2scene_object() for no turn about x */
    int n = 0;
    for (int i = 0; i < m->n && n < S2MAP_BLOCKS; i++) {
        const struct s2map_block *p = &m->b[i];
        const float *t = p->t;
        const float o[3] = { x + r[0] * t[0] + r[1] * t[1] + r[2] * t[2], z + r[3] * t[0] + r[4] * t[1] + r[5] * t[2], y + r[6] * t[0] + r[7] * t[1] + r[8] * t[2] };
        const float cx = t[0] + p->c[0], cy = t[1] + p->c[1], cz = t[2] + p->c[2];
        const float d = s2scene_dist2(cam, x + r[0] * cx + r[1] * cy + r[2] * cz, y + r[6] * cx + r[7] * cy + r[8] * cz, z + r[3] * cx + r[4] * cy + r[5] * cz);
        const struct track_block b = { p->file, p->blk, { r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8] }, { o[0], o[1], o[2] }, 0 };
        int j = n++;
        for (; j > 0 && key[j - 1] < d; j--) {                  /* far first: the original's key is the distance of the block's centre (0x8b04c) */
            out[j] = out[j - 1];
            key[j] = key[j - 1];
        }
        out[j] = b;
        key[j] = d;
    }
    return n;
}
