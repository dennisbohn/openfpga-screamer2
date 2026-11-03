/*
 * Frame statistics for the PC renderer (docs/screamer2/performance.md). Included into the
 * firmware's track.c, geom.c, setup.c and raster.c copies by tools/s2/perf_patch.py (gcc
 * -include); the firmware itself is not changed. Counters of what one frame asks of the CPU
 * and the units, plus two cache models fed with the addresses the C models read:
 *   the rasterizer's texture cache (16 KB, direct mapped, lines of 32 bytes: raster_tex.v)
 *   the geometry unit's read cache (4 KB, direct mapped, lines of 32 bytes: geom.v)
 */
#pragma once
#include <stdint.h>
#include <stdio.h>

#define PERF_FIELDS(X) \
    X(groups) X(grp_skipped) X(tested) X(tested_mesh) X(visible) X(obj_visible) \
    X(obj_blocks) X(multi_blocks) \
    X(gu_pieces) X(verts) X(refl_verts) X(faces_row) X(back) \
    X(polys) X(clip_out) X(clip_any) X(clip_near) X(clip_far) \
    X(tris_flat) X(tris_tex) X(tris_blend) X(walk_tris) X(rows) X(spans) X(px_span) X(px_drawn) \
    X(texel_reads) X(tex_miss) X(tex_lines) X(gu_reads) X(gu_miss) \
    X(fade_night) X(fade_fog) X(tex_miss32) X(tex_miss2w)

struct perf_counts {
#define X(n) uint32_t n;
    PERF_FIELDS(X)
#undef X
};

extern struct perf_counts perf_c;
void perf_reset(void);                      /* counters and both caches (a cold frame) */
void perf_gu_rd(const void *p, uint32_t len);   /* the geometry unit reads len bytes at p */
/* Screamer 2's fade by depth (NOTES2 S2-5d): a row of the DEPTH table per face from the depth of
   its first corners; counts the faces whose row differs from the face drawn before (a row of 256
   bytes would have to be fetched): night (near 6000, far 35000), fog (0, 25000). z: camera depth
   in units, 8 fraction bits. */
void perf_fade(int32_t f0, int32_t f1, int32_t f2);
void perf_header(FILE *f);                  /* ",groups,grp_skipped,..." */
void perf_row(FILE *f);                     /* ",12,34,..." */
