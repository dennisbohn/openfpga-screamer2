/*
 * WELCOME TO LEAGUE n's flying plates (docs/screamer2/menus.md, "WELCOME's plates"): a 3D card for each race of the league (A<track>PLATE.3DX of GFX/PLATES<res>.DAT, a flat
 * model with the track's poster, PLATES.MLB the materials) flies in from behind the camera, settles at the middle of the screen and flies away to a side, one after the other
 * at 1.1 s apart; the page ends by itself when the last has flown most of its way out (STARTL.EXE 0x35b9e the set-up, 0x35cc8 a plate's frame, 0x36168 the end).
 * The page (s2leaguepages.c) draws the league's picture, the plates over it and the dark band with its two lines over them.
 */
#pragma once
#include <stdint.h>
#include "blit.h"
#include "s2scene.h"

#define S2PLATES_MAX    6                       /* the most races of a league */

/* GFX/PLATES1.DAT's PLATES.MLB and the seven plates' meshes into the toolkit's models (s2scene.h), read by range from the ZIP (0.14 MB of the 0.14 MB file stored, 0.43 MB unpacked,
   about a quarter of a second): 1, or 0 and the log says what is missing */
int s2plates_load(struct zip *z);

/* The flight of a league's races (2 .. 6 plates: A<s2l_track(i)>PLATE for race i): every plate where the original's set-up puts it, before its first frame */
void s2plates_begin(int races);

/* One frame (0x35cc8 for each plate, then the end test 0x36168): the plates' places and turns, then 1 if the page ends (the last plate is on its way out and the others are gone).
   Call it once a frame, in order, before s2plates_draw(). */
int s2plates_step(void);

/* The plates as the frame has them, over what the blitter list l holds (the picture): the blitter is done, the geometry unit draws the plates, the rasterizer is done and the list
   is the page's again. */
void s2plates_draw(struct blit_list *l);

/* ------------------------------------------------------------------------------------------------ the flight's rules (for the tests)
   A plate's state as its record 0x4c bytes at 0x131290 + 0x4c i has it and the engine's object record it makes (x, y up, z whole numbers, the angles in 2048ths). */
struct s2plate {
    float u;                                    /* the plate's time: 1 + 0.6 i down to 0.05 (phase 0, the way in), then 1 down to 0.05 (phase 1, the way out) */
    int phase, done;                            /* 0 in, 1 out; finished (the record's +0x40 is -1) */
    int32_t pos[3], a[3];                       /* the object's record: place, a0 (pitch), a1, a2 (roll) */
};
int s2plates_count(void);
const struct s2plate *s2plates_get(int i);
/* the matrix of a plate's block for the geometry unit: the object turned about z by a2, about x by a0, about the vertical by a1 (the engine's turn), placed at pos */
void s2plates_block(const uint8_t *mesh, const int32_t pos[3], const int32_t a[3], struct track_block *out);
