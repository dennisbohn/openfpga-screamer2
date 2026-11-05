/*
 * The geometry unit (core/src/fpga/core/geom.v) as a C model: bit for bit what the hardware
 * does with one piece of the track (a placed mesh). The CPU picks the pieces, sorts them far
 * to near and hands each over with its place in view space; the unit reads the mesh itself
 * (UNR block, vertices, face row, faces, texture coordinates, MLB materials, all as in the
 * game files, track.c has the layout), takes the vertices into view space, leaves out the
 * faces turned away and hands the others to the setup unit (setup.h), in fixed point:
 *   - vertex: r = t_r + ((x m_rx + y m_ry + z m_rz) >> 8), u and f the same (x, y, z the
 *     mesh's int16, m with 16 fraction bits, t and the result with 8)
 *   - a face is seen when ((c0 << 8) - e) . n >= 0 (c0 its first corner, e the eye in the
 *     piece's frame with 8 fraction bits, n the face's int16 normal)
 *   - texel position with 8 fraction bits: raw u times the material's width, raw v times
 *     its height (texel = raw * size / 256)
 *   - routine 160 (a car's windows, the game's 0xdb310): the texel is the reflection's u, v
 *     of the corner's vertex (no material size), and it wraps at 256 (raster.h). A block with
 *     flag 0x2000 (blk +0x18) has a normal per vertex (blk +0x20: count, then float x, y, z);
 *     for each vertex i < count the unit works out, as the original does on the x87
 *     (0xc2230), the view's reflection r = 2 (e.n) n - e (e the direction from the camera,
 *     EYE_BACK in front of the eye, to the vertex, n its normal in view space) and keeps
 *     u = round(127 r.x) + 127 - offx, v = round(127 r.z) + 127 - offz, x and z the
 *     original's view axes (left, back: -r, -f here). In integers, Q = 2^-bits:
 *       q    each normal component from its float bits, rounded to Q14
 *       n    (m q) >> 14, the normal in view space, Q16
 *       p    the vertex less EYE_BACK in f (8 fraction bits) shifted left or right until the
 *            largest |component| (one's complement) has 16 bits: 17 bits signed
 *       S    (p.n) >> 16; Q = p.p; B = ((p << 16) - 2 S n) >> 16 for r and f (B = -A)
 *       y    (127/128) 2^32 / sqrt(Q') by a table of 128 (Q' = Q, or 4 Q below 2^32: h = 1)
 *            and one Newton step: t1 = (x y0) >> 16, t2 = (t1 y0) >> 15,
 *            t3 = (3 2^16 - t2) >> 1, y = (y0 (t3 - (t3 >> 7))) >> 16 (x = Q' >> 17)
 *       u    ((B_r y << h) + 2^24) >> 25 + 127 - offx; v the same with B_f and offz
 *     Against the float form (GU_REFL_FLOAT, as the original computes it) about 1 % of the
 *     values differ by one.
 */
#pragma once
#include <stdint.h>
#include "raster.h"
#include "setup.h"

#ifndef GU_MAX_VERT
#define GU_MAX_VERT     512             /* vertices of a block the unit takes (a build of the tests may take more: tools/trackmap.sh); a block over it is cut, gu_split() */
#endif
#define GU_MAX_FACES    1024            /* faces of a row the unit takes; a block with more is cut as well */

/* A block over the unit's limits (gu_oversize: more than GU_MAX_VERT vertices or GU_MAX_FACES faces) is drawn as runs, one after the other: the faces of its row in the row's
   order, taken in turn while their vertices and faces stay within the limits, each run a block of its own (the header, the faces, their vertices and texture coordinates
   compacted, all 16 rows its one list). Drawn in turn they paint the faces in exactly the order the whole block's row has; the picture is the whole block's. Every piece goes
   through this: hw_piece() (the hardware) and gu_draw() (the model) call gu_split(); nothing else needs to know the limit. The runs of a (block, row) are cut on the first use and
   kept in GU_CUT_SLOTS slots of GU_CUT_BYTES, the least recently used given up. A block that cannot be cut (its normals are its own, it needs more than a slot or GU_CUT_RUNS
   runs) is not drawn, as before, and counted in gu_cut_failures. */
#define GU_CUT_RUNS     6               /* the most runs of a block's row */
#define GU_CUT_SLOTS    8               /* (block, row) pairs kept cut */
#define GU_CUT_BYTES    0x10000u        /* the memory of one: all runs of the pair */

struct gu_piece {
    int32_t m[9];                       /* local to view: rows r, u, f of columns x, y, z; 16 fraction bits */
    int32_t t[3];                       /* the piece's origin in view space, 8 fraction bits */
    int32_t e[3];                       /* the eye in the piece's frame, 8 fraction bits */
    int row;                            /* the face row for the side the eye is on, 0..15 (track.c) */
    int mirror;                         /* a moving part's block: its frames turn the model's z up
                                           into y up, a mirror, so the faces turned away are the
                                           others (the game's 0x8acf0: jge for jl) */
    uint32_t phase;                     /* texture frame of n: n x phase >> 14 (14 bits) */
    uint32_t refl;                      /* routine 160: [15:0] the reflection picture's phase
                                           (frame n x phase >> 16; the object's record +0x3c,
                                           the game's 0x84288), [23:16] and [31:24] the
                                           reflection's offsets of u and v (0xc1fc0, track.c) */
    const uint8_t *obj, *blk;           /* the mesh: object and its block */
};

struct gu_stats { uint32_t faces, triangles, see_through, vertices; };

extern uint32_t gu_cut_failures;        /* blocks over the limits that could not be cut (once for each (block, row)) */
int gu_oversize(const struct gu_piece *g);      /* 1 if the piece's block is over the unit's limits */
/* The runs a piece is drawn as: copies of *g whose obj and blk are the runs' (self-contained) blocks, in drawing order; returns how many (0 if the row has no faces or the block
   could not be cut). Only for a piece gu_oversize() says is over the limits. */
int gu_split(const struct gu_piece *g, struct gu_piece out[GU_CUT_RUNS]);
/* The cutting itself (gu_split's, with the limits as arguments for the tests): the row `row` of the block at `blk` (of the object `mesh`) as runs written to dst (room bytes,
   4-aligned), their blocks in out[] (at most cap). Returns how many runs (0: the block has no such row), -1 if it cannot be cut or does not fit. */
int gu_cut(const uint8_t *mesh, const uint8_t *blk, int row, int max_vert, int max_faces, uint8_t *dst, uint32_t room, const uint8_t *out[], int cap);

/* One piece through the models of the geometry unit, the setup unit and the rasterizer
   into fb; mlb is the track's material file. Adds to *st, returns the pixels drawn. */
uint32_t gu_draw(volatile uint8_t *fb, const struct rs_clip *clip, const struct su_view *v,
                 const uint8_t *mlb, const struct gu_piece *p, struct gu_stats *st);
