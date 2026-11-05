/*
 * The track maps of the TRACK pages (docs/screamer2/menus.md, row 7): GFX/MAPS1.DAT of the ZIP holds AMAP1..7-VS.3DX, one 3D model of each track (two to four blocks,
 * 313 to 1,207 vertices a block-set, flat textured faces) and MAPS.MLB, their materials (solid or colour 0 see-through textures). The page turns one of them about its
 * vertical axis in front of its picture (TKG_nL for a league's race, LEAG_0L for time attack's choice of the track).
 *
 * The geometry unit takes at most GU_MAX_VERT (512) vertices of a piece (geom.h; the hardware is not changed): six blocks of the maps have more (AMAP1's second 525,
 * AMAP4's first three 630, 710 and 882, AMAP5's first 530, AMAP6's second 767). The original draws a block's faces in the order of one of its 16 face rows (the eye's side),
 * far to near, and a cut that draws one part of the faces before the other covers what is behind it wrong (the tall buildings of California lost their walls). s2map_load()
 * therefore cuts such a block once for each of the rows 0 .. 7 (the eye above the block's plane): the row's faces in their order, taken in turn while their vertices stay
 * within 512, each run a block of its own (header, the faces it keeps, their vertices and texture coordinates compacted); drawn one after the other, at the block's place in
 * the far-to-near order of the blocks, they paint the faces in exactly the whole block's order. s2map_blocks() picks the row the eye of the frame gives (track_block_piece).
 *
 * SDRAM: the models area of the 3D pages (s2scene.h), 2.75 MB: MAPS.MLB (406 KB), the seven meshes (479 KB) and the pieces cut (the log says how many KB).
 */
#pragma once
#include <stdint.h>
#include "s2scene.h"

#define S2MAP_TRACKS    7
#define S2MAP_BLOCKS    4                       /* the most blocks of a map */
#define S2MAP_CHUNKS    4                       /* the most pieces a cut block gives for one face row */
#define S2MAP_ROWS      8                       /* the face rows cut: 0 .. 7, the eye above the block's plane */
#define S2MAP_PIECES    16                      /* the most pieces drawn of one map */

struct s2map_block {
    const uint8_t *file, *blk;                  /* the mesh (offsets of the block are from it) and the block */
    float t[3];                                 /* the block's place in the mesh's frame: its position record (a few units), the engine's x, y up, z */
    float c[3];                                 /* the block's centre from its header (+4; 0 in every block of the maps), added to t: the point whose distance orders the blocks far to near, as the original's 0x8b04c does */
    int cut;                                    /* 1: the block has more vertices than the unit takes, chunk[row][0 .. nchunks[row]) are drawn in its place */
    uint8_t nchunks[S2MAP_ROWS];
    const uint8_t *chunk[S2MAP_ROWS][S2MAP_CHUNKS];     /* blocks of their own (their own file), in drawing order */
};
struct s2map {
    int n;
    struct s2map_block b[S2MAP_BLOCKS];
};

/* GFX/MAPS1.DAT read from the ZIP and prepared (the pieces cut): 1 if the seven maps and their materials are there, else 0 (the log says what was not) */
int s2map_load(struct zip *z);
const struct s2map *s2map_get(int track);       /* track 1 .. 7 as the files number them; 0 if not loaded */
const uint8_t *s2map_mlb(void);                 /* MAPS.MLB, the materials */

/* The map of `track` at (x, y, z) (y up), turned a1 (2048ths of a turn) about the vertical, as pieces for s2scene_blocks() in the order to draw them (far first; a cut block's
   pieces in the face row's order); key[] is the squared distance of each block's middle from the camera. Returns how many (at most S2MAP_PIECES). */
int s2map_blocks(int track, const struct track_cam *cam, float x, float y, float z, int a1, struct track_block out[S2MAP_PIECES], float key[S2MAP_PIECES]);

/* Cutting block k of `mesh` (the part of s2map_load that tools/s2/check_map_s2.c tests) into out, using dst (room bytes, 4-aligned, from *used on) for the pieces: a block with
   no more than max_vert vertices is not cut (out->cut 0, nothing copied), a larger one is cut for each of the rows 0 .. S2MAP_ROWS - 1. Returns 1, 0 if it does not fit. */
int s2map_cut(const uint8_t *mesh, int k, int max_vert, uint8_t *dst, uint32_t room, uint32_t *used, struct s2map_block *out);
