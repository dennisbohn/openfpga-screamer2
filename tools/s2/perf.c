/*
 * Counters and cache models for tools/s2/perf.h. SDRAM byte address = pointer - SDRAM_BASE (the
 * PC maps the SDRAM at the firmware's address, as tools/s2/render_s2.c does).
 */
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "hw.h"
#include "perf.h"

struct perf_counts perf_c;
int perf_lod, perf_lod_min;
__attribute__((constructor)) static void perf_lod_init(void)    /* LOD=n for any program with these counters */
{
    if (getenv("LOD")) perf_lod = atoi(getenv("LOD"));
}

/* The original's level of a block (STARTL.EXE 0x7f148 placed meshes, 0x7f010 cars): the distance of
   the block's middle from the camera, rounded to nearest (even). Placed: key = -(dist >> 1), level 0
   above -3000 (dist < 6000), 1 above -4500 (dist < 9000), else 2; cars: key = -dist, -2000 and -6000.
   The rear-view mirror's pass (0xae6b4 = 1) takes at least level 1. */
int perf_lod_level(float dist, int moving, int min_level)
{
    int32_t d = (int32_t)nearbyintf(dist);
    int32_t key = moving ? -d : -(d >> 1);
    int lvl = key > (moving ? -2000 : -3000) ? 0 : key > (moving ? -6000 : -4500) ? 1 : 2;
    return lvl < min_level ? min_level : lvl;
}

#define TEX_LINES   512                     /* 16 KB / 32 */
#define GU_LINES    128                     /* 4 KB / 32 */
#define MAX_LINES   (1u << 21)              /* 64 MB of SDRAM in lines of 32 bytes */
static int32_t tex_tag[TEX_LINES], gu_tag[GU_LINES];
/* what other texture caches would have missed (levers): 32 KB direct mapped; 16 KB two-way (LRU) */
static int32_t tex_tag32[2 * TEX_LINES], tex_tag2[TEX_LINES][2];
static uint8_t seen[MAX_LINES / 8];
static int fade_last[2] = { -1, -1 };

struct perf_piece_rec perf_pieces[PERF_MAX_PIECES];
int perf_npieces;
static struct perf_piece_rec perf_last;

void perf_piece(int kind)
{
    struct perf_piece_rec now = { (uint32_t)kind, perf_c.faces_row, perf_c.gu_miss, perf_c.refl_verts, perf_c.verts, perf_c.px_span,
                                  perf_c.tex_miss, perf_c.walk_tris, perf_c.rows };
    if (perf_npieces < PERF_MAX_PIECES) {
        struct perf_piece_rec d = { (uint32_t)kind, now.faces_row - perf_last.faces_row, now.gu_miss - perf_last.gu_miss,
                                    now.refl_verts - perf_last.refl_verts, now.verts - perf_last.verts, now.px_span - perf_last.px_span,
                                    now.tex_miss - perf_last.tex_miss, now.walk_tris - perf_last.walk_tris, now.rows - perf_last.rows };
        perf_pieces[perf_npieces++] = d;
    }
    perf_last = now;
}

void perf_reset(void)
{
    perf_npieces = 0;
    memset(&perf_last, 0, sizeof perf_last);
    memset(&perf_c, 0, sizeof perf_c);
    for (int i = 0; i < TEX_LINES; i++) tex_tag[i] = -1;
    for (int i = 0; i < 2 * TEX_LINES; i++) tex_tag32[i] = -1;
    for (int i = 0; i < TEX_LINES; i++) tex_tag2[i][0] = tex_tag2[i][1] = -1;
    for (int i = 0; i < GU_LINES; i++) gu_tag[i] = -1;
    memset(seen, 0, sizeof seen);
    fade_last[0] = fade_last[1] = -1;
}

/* the rasterizer's texel read (raster.c, RS_TRACE) */
void rs_trace(const volatile uint8_t *p)
{
    uint32_t a = (uint32_t)((uintptr_t)p - SDRAM_BASE), line = a >> 5;
    perf_c.texel_reads++;
    uint32_t i = line & (TEX_LINES - 1);
    if (tex_tag[i] != (int32_t)(line >> 9)) {
        tex_tag[i] = (int32_t)(line >> 9);
        perf_c.tex_miss++;
    }
    uint32_t j = line & (2 * TEX_LINES - 1);
    if (tex_tag32[j] != (int32_t)(line >> 10)) {
        tex_tag32[j] = (int32_t)(line >> 10);
        perf_c.tex_miss32++;
    }
    uint32_t s = line & (TEX_LINES / 2 - 1);          /* 256 sets of two lines; way 0 the most recent */
    int32_t tg = (int32_t)(line >> 8);
    if (tex_tag2[s][0] == tg) {
    } else if (tex_tag2[s][1] == tg) {
        tex_tag2[s][1] = tex_tag2[s][0];
        tex_tag2[s][0] = tg;
    } else {
        tex_tag2[s][1] = tex_tag2[s][0];
        tex_tag2[s][0] = tg;
        perf_c.tex_miss2w++;
    }
    if (line < MAX_LINES && !(seen[line >> 3] >> (line & 7) & 1)) {
        seen[line >> 3] |= (uint8_t)(1u << (line & 7));
        perf_c.tex_lines++;
    }
}

void perf_gu_rd(const void *p, uint32_t len)
{
    if (len == 0) return;
    uint32_t a = (uint32_t)((uintptr_t)p - SDRAM_BASE);
    for (uint32_t line = a >> 5; line <= (a + len - 1) >> 5; line++) {
        uint32_t i = line & (GU_LINES - 1);
        perf_c.gu_reads++;
        if (gu_tag[i] != (int32_t)(line >> 7)) {
            gu_tag[i] = (int32_t)(line >> 7);
            perf_c.gu_miss++;
        }
    }
}

void perf_fade(int32_t f0, int32_t f1, int32_t f2)
{
    /* ((z0 + z1) / 2 + z2) / 2 of the first three corners, less the eye's 128 behind the camera */
    float z = (float)(((int64_t)f0 + f1) / 2 + f2) / 2.0f / 256.0f - 128.0f;
    const float near_[2] = { 6000.0f, 0.0f }, far_[2] = { 35000.0f, 25000.0f };
    uint32_t *count[2] = { &perf_c.fade_night, &perf_c.fade_fog };
    for (int k = 0; k < 2; k++) {
        float d = z - near_[k];
        int row = d <= 0.0f ? 0 : d >= far_[k] - near_[k] ? 127 : (int)(d * 128.0f / (far_[k] - near_[k]));
        if (row != fade_last[k]) (*count[k])++;
        fade_last[k] = row;
    }
}

void perf_header(FILE *f)
{
#define X(n) fprintf(f, ",%s", #n);
    PERF_FIELDS(X)
#undef X
}

void perf_row(FILE *f)
{
#define X(n) fprintf(f, ",%u", perf_c.n);
    PERF_FIELDS(X)
#undef X
}

/* a tool that draws exactly one frame: PERF_OUT=file dumps its counters at exit */
__attribute__((destructor)) static void perf_dump(void)
{
    const char *path = getenv("PERF_OUT");
    if (!path) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "label");
    perf_header(f);
    fprintf(f, "\n%s", getenv("PERF_LABEL") ? getenv("PERF_LABEL") : "frame");
    perf_row(f);
    fprintf(f, "\n");
    fclose(f);
}
