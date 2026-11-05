/*
 * The track maps of the TRACK pages (docs/screamer2/menus.md, row 7): GFX/MAPS1.DAT of the ZIP holds AMAP1..7-VS.3DX, one 3D model of each track (two to four blocks,
 * 313 to 1,207 vertices a block-set, flat textured faces) and MAPS.MLB, their materials (solid or colour 0 see-through textures). The page turns one of them about its
 * vertical axis in front of its picture (TKG_nL for a league's race, LEAG_0L for time attack's choice of the track).
 *
 * A map is drawn like any object of the engine: its blocks far to near by the distance of the block's centre (the header's, +4: 0 in every block of the maps, so the block's
 * place; the original's key, 0x8b04c), each block as a piece for the geometry unit. Six blocks have more vertices than the unit takes (525 to 882 against 512): the geometry
 * driver cuts any such block into runs (geom.h, gu_split), the maps need nothing of their own for it.
 *
 * SDRAM: the models area of the 3D pages (s2scene.h), 2.75 MB: MAPS.MLB (406 KB) and the seven meshes (479 KB).
 */
#pragma once
#include <stdint.h>
#include "s2scene.h"

#define S2MAP_TRACKS    7
#define S2MAP_BLOCKS    4                       /* the most blocks of a map */

struct s2map_block {
    const uint8_t *file, *blk;                  /* the mesh (offsets of the block are from it) and the block */
    float t[3];                                 /* the block's place in the mesh's frame: its position record, the engine's x, y up, z */
    float c[3];                                 /* the block's centre from its header (+4), added to t: the point whose distance orders the blocks */
};
struct s2map {
    int n;
    struct s2map_block b[S2MAP_BLOCKS];
};

/* GFX/MAPS1.DAT read from the ZIP and prepared: 1 if the seven maps and their materials are there, else 0 (the log says what was not) */
int s2map_load(struct zip *z);
const struct s2map *s2map_get(int track);       /* track 1 .. 7 as the files number them; 0 if not loaded */
const uint8_t *s2map_mlb(void);                 /* MAPS.MLB, the materials */

/* The map of `track` at (x, y, z) (y up), turned a1 (2048ths of a turn) about the vertical, as pieces for s2scene_blocks() in the order to draw them (far first); key[] is the
   squared distance of each block's centre from the camera. Returns how many (at most S2MAP_BLOCKS). */
int s2map_blocks(int track, const struct track_cam *cam, float x, float y, float z, int a1, struct track_block out[S2MAP_BLOCKS], float key[S2MAP_BLOCKS]);
