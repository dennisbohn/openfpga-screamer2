/*
 * The 3D-o pages' toolkit (s2scene.h)
 */
#include "s2scene.h"
#include "frame.h"
#include "hw.h"
#include "dat.h"
#include "s2gfx.h"
#include <math.h>
#include <string.h>

#define MEM         (SDRAM_BASE + S3D_MEM)
#define STAGE       ((uint8_t *)(uintptr_t)MEM)
#define WHOLE       ((uint8_t *)(uintptr_t)(MEM + S3D_STAGE_MAX))
#define TAIL_MAX    4096u

static uint32_t generation;

uint8_t *s2scene_models(void)
{
    generation++;
    return (uint8_t *)(uintptr_t)(MEM + S3D_MODELS_OFF);
}

uint32_t s2scene_generation(void)
{
    return generation;
}

/* the SDRAM holds what the CPU wrote (the geometry unit reads the meshes there) */
static void settle(void)
{
    while (!(MEM_STATUS & MEM_WRITES_IDLE))
        ;
}

/* ------------------------------------------------------------------------------------------------ the files */

int s2scene_open(struct zip *z, struct s2_cont *c, const char *path)
{
    c->zip = z;
    c->e = zip_find(z, path);
    c->dir = 0;
    c->dir_len = 0;
    if (!c->e) return 0;
    if (c->e->method != 0) {                                /* deflated: the whole container, which the entries are then unpacked from */
        if (c->e->size + ZIP_SLACK > S3D_WHOLE_MAX || zip_read(z, c->e, WHOLE, S3D_WHOLE_MAX)) return 0;
        c->dir = WHOLE;
        c->dir_len = c->e->size;
        return 1;
    }
    uint32_t tail = c->e->size < TAIL_MAX ? c->e->size : TAIL_MAX;
    if (zip_read_range(z, c->e, c->e->size - tail, tail, (uint8_t *)c->tail, sizeof c->tail)) return 0;
    c->dir = (const uint8_t *)c->tail;
    c->dir_len = tail;
    return 1;
}

uint32_t s2scene_entry(struct s2_cont *c, const char *name, uint8_t *out, uint32_t max)
{
    struct dat_entry d;
    if (!c->dir || !dat_find(c->dir, c->dir_len, name, &d)) return 0;
    uint32_t n;
    if (c->e->method != 0) {
        n = dat_extract(c->dir, c->dir_len, &d, out, max);
    } else {                                                /* the entry's stored bytes by range, then unpacked */
        if (d.stored + ZIP_SLACK > S3D_STAGE_MAX || zip_read_range(c->zip, c->e, d.offset, d.stored, STAGE, S3D_STAGE_MAX)) return 0;
        d.offset = 0;                                       /* the bytes begin at the stage now */
        n = dat_extract(STAGE, d.stored, &d, out, max);
    }
    settle();
    return n;
}

/* ------------------------------------------------------------------------------------------------ the frame */

static struct su_view view = { S3D_CX * 16, S3D_CY * 16, 128 * 16, (int32_t)((43808.0f + 128.0f) * 256.0f) };

void s2scene_lens(int cx16, int cy16, int focal16)
{
    view.cx = cx16;
    view.cy = cy16;
    view.scale = (uint32_t)focal16;
}

void s2scene_begin(const uint8_t *mlb, int buffer, int fresh)
{
    s2gfx_flush();                                          /* the picture (the list holds it): the blitter is done before the first piece */
    back_wait = 0;
    if (fresh) {
        GU_INVAL = 1;
        RS_CTRL = 1;
    }
    GU_MLB = (uint32_t)((uintptr_t)mlb - SDRAM_BASE);
    GU_MODE = (uint32_t)buffer << 8;
    RS_CLIP0 = RS_XY(0, 0);
    RS_CLIP1 = RS_XY(FB_W, FB_H);
    su_view_regs(&view);
}

void s2scene_mlb(const uint8_t *mlb)
{
    while (RS_DRAW & RS_BUSY)
        ;
    GU_MLB = (uint32_t)((uintptr_t)mlb - SDRAM_BASE);
}

void s2scene_blocks(const struct track_cam *cam, const struct track_block *b, int n)
{
    for (int i = 0; i < n; i++) {
        struct gu_piece g;
        track_block_piece(cam, &b[i], &g);
        hw_piece(&g);
    }
}

void s2scene_end(void)
{
}

static int32_t rd32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

void s2scene_object(const uint8_t *mesh, float x, float y, float z, int a0, int a1, uint32_t phase, struct track_block *out)
{
    const float c = cosf(s2scene_rad(a0)), s = sinf(s2scene_rad(a0)), ch = cosf(s2scene_rad(a1)), sh = sinf(s2scene_rad(a1));
    /* the matrix: rows the track's world x, y, height = the engine's x, z, y; columns the block's x, y up, z. Turned about x, then about the vertical. */
    const uint8_t *blk = mesh + rd32(mesh + 0x18);
    const float m[9] = { ch, -sh * s, -sh * c, sh, ch * s, ch * c, 0.0f, c, -s };
    /* a block with a position record (block +0x0C, its +0x10 .. +0x18 the block's place in the mesh's frame: a few units) is moved by it, turned with the object */
    const int32_t so = rd32(blk + 0x0C);
    const float t[3] = { so > 0 ? (float)rd32(mesh + so + 0x10) : 0.0f, so > 0 ? (float)rd32(mesh + so + 0x14) : 0.0f,
                         so > 0 ? (float)rd32(mesh + so + 0x18) : 0.0f };
    *out = (struct track_block){ mesh, blk, { m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8] },
                                 { x + m[0] * t[0] + m[1] * t[1] + m[2] * t[2], z + m[3] * t[0] + m[4] * t[1] + m[5] * t[2],
                                   y + m[6] * t[0] + m[7] * t[1] + m[8] * t[2] }, phase };
}

struct track_cam s2scene_camera(float x, float y, float z, float pitch)
{
    return (struct track_cam){ x, z, y, 0.0f, pitch, 0.0f };
}

#define PI      3.1415927f
#define TWO_PI  6.2831853f

float s2scene_easing(int row)
{
    return 0.5f + (float)row * 0.005f;
}

void s2scene_ease_angle(float *v, float target, int row)
{
    float e = s2scene_easing(row), a = *v;
    if (a != target) {
        if (a < target) {
            if (target - a >= PI) *v = -((target - a) + -TWO_PI) * e + target;
            else *v = (a - target) * e + target;
        } else {
            if (a - target >= PI) *v = -((target - a) + TWO_PI) * e + target;
            else *v = (a - target) * e + target;
        }
    }
    if (*v < 0.0f) *v += TWO_PI;
    else if (*v >= TWO_PI) *v += -TWO_PI;
}

float s2scene_dist2(const struct track_cam *cam, float x, float y, float z)
{
    const float dx = x - cam->x, dy = z - cam->y, dh = y - cam->h;
    return dx * dx + dy * dy + dh * dh;
}
