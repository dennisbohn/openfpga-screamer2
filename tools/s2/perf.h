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
    X(fade_night) X(fade_fog) X(tex_miss32) X(tex_miss2w) \
    X(lod_s1) X(lod_s2) X(lod_sd) X(lod_c1) X(lod_c2) X(lod_cd) \
    X(cell_out) X(vis_hash)

struct perf_counts {
#define X(n) uint32_t n;
    PERF_FIELDS(X)
#undef X
};

extern struct perf_counts perf_c;
/* Screamer 2's level of detail per block (docs/screamer2/performance.md, "Detail levels and LOD at HIGH"),
   only in the Screamer 2 copies of track.c and car.c (perf_patch.py s2): perf_lod bit 0 = placed meshes
   (static rule), bit 1 = cars (moving rule), 0 = off (LOD=n in the environment of framestats);
   perf_lod_min = the lowest level used (1 in the rear-view mirror's pass). perf_lod_level: the level
   of a block whose middle is dist units from the camera. lod_s1/s2/sd: placed blocks drawn at level 1,
   2, dropped (no block at the level); lod_c1/c2/cd: the same for cars */
extern int perf_lod, perf_lod_min;
int perf_lod_level(float dist, int moving, int min_level);
void perf_reset(void);                      /* counters and both caches (a cold frame) */
void perf_gu_rd(const void *p, uint32_t len);   /* the geometry unit reads len bytes at p */
/* Screamer 2's fade by depth (NOTES2 S2-5d): a row of the DEPTH table per face from the depth of
   its first corners; counts the faces whose row differs from the face drawn before (a row of 256
   bytes would have to be fetched): night (near 6000, far 35000), fog (0, 25000). z: camera depth
   in units, 8 fraction bits. */
void perf_fade(int32_t f0, int32_t f1, int32_t f2);
/* every piece handed on (track.c PF_PIECE, firmware/prof.h): its kind and the counters it added, for the pipeline model
   of docs/screamer2/performance.md section 7 (perf_pieces is cleared by perf_reset) */
struct perf_piece_rec { uint32_t kind, faces_row, gu_miss, refl_verts, verts, px_span, tex_miss, walk_tris, rows; };
#define PERF_MAX_PIECES 4800
extern struct perf_piece_rec perf_pieces[PERF_MAX_PIECES];
extern int perf_npieces;
void perf_piece(int kind);
#define PF_PIECE(kind)  perf_piece(kind)
void perf_header(FILE *f);                  /* ",groups,grp_skipped,..." */
void perf_row(FILE *f);                     /* ",12,34,..." */
