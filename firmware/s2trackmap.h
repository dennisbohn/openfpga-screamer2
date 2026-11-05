/*
 * The track maps of the TRACK pages (docs/screamer2/menus.md, row 7): GFX/MAPS1.DAT of the ZIP holds AMAP1..7-VS.3DX, one 3D model of each track (two to four blocks,
 * 313 to 1,207 vertices a block-set, flat textured faces) and MAPS.MLB, their materials (solid or colour 0 see-through textures). The page turns one of them about its
 * vertical axis in front of its picture (TKG_nL for a league's race, LEAG_0L for time attack's choice of the track).
 *
 * The geometry unit takes at most GU_MAX_VERT (512) vertices of a piece (geom.h; the hardware is not changed): six blocks of the maps have more (AMAP1's second 525,
 * AMAP4's first three 630, 710 and 882, AMAP5's first 530, AMAP6's second 767). s2map_load() therefore cuts such a block into pieces of at most 512 vertices, along the
 * order the faces are stored in, each a block of its own (header, the faces it keeps, their vertices and texture coordinates compacted, the original's face rows
 * (the 16 eye sides' orders) cut down to the faces it keeps, in their order); the pieces of a block are drawn far to near by their middle. A map that was cut is not
 * the picture the original draws to the pixel where two pieces cover one another (the faces of one piece come all before the other's); tools/s2/check_map_s2.sh counts
 * how often against a render with a unit that takes any number of vertices.
 *
 * SDRAM: the models area of the 3D pages (s2scene.h), 0.9 MB: MAPS.MLB (406 KB), the seven meshes (479 KB) and the pieces cut.
 */
#pragma once
#include <stdint.h>
#include "s2scene.h"

#define S2MAP_TRACKS    7
#define S2MAP_PIECES    10                      /* the most blocks and pieces cut of one map */

struct s2map_piece {
    const uint8_t *file, *blk;                  /* the mesh (offsets of the block are from it) and the block of the piece */
    float t[3];                                 /* the block's place in the mesh's frame: its position record (a few units), the engine's x, y up, z */
    float c[3];                                 /* the middle of the vertices of the piece in the same frame (for ordering the pieces) */
};
struct s2map {
    int n;
    struct s2map_piece p[S2MAP_PIECES];
};

/* GFX/MAPS1.DAT read from the ZIP and prepared (the pieces cut): 1 if the seven maps and their materials are there, else 0 (the log says what was not) */
int s2map_load(struct zip *z);
const struct s2map *s2map_get(int track);       /* track 1 .. 7 as the files number them; 0 if not loaded */
const uint8_t *s2map_mlb(void);                 /* MAPS.MLB, the materials */

/* The map of `track` at (x, y, z) (y up), turned a1 (2048ths of a turn) about the vertical, as pieces for s2scene_blocks(); key[] is the squared distance of each
   piece's middle from the camera. Returns how many (at most S2MAP_PIECES). */
int s2map_blocks(int track, const struct track_cam *cam, float x, float y, float z, int a1, struct track_block out[S2MAP_PIECES], float key[S2MAP_PIECES]);

/* Cutting a block (the part of s2map_load that tools/s2/check_map_s2.c tests): block k of `mesh` into pieces of at most `max_vert` vertices in dst (room bytes, 4-aligned).
   Fills out[] (at most `cap`) with the pieces' blocks (inside dst); returns how many, 0 if it does not fit. A block that has no more than max_vert vertices is a piece of
   its own, not copied (out[0].blk is the original). */
int s2map_cut(const uint8_t *mesh, int k, int max_vert, uint8_t *dst, uint32_t room, uint32_t *used, struct s2map_piece out[], int cap);
